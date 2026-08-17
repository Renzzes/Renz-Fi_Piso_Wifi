# Activation & Session State Stabilization — Implementation Report

Scope: fix the proven production defects in the customer session lifecycle.
No architecture was redesigned. RouterWorker, PortalSessionManager,
StorageManager and the router drivers keep their existing structure, and the
number of RouterOS API commands per customer action is unchanged.

---

## 1. Root causes

### 1.1 Internet not granted after "Done Paying"

`MikroTikDriver::openRouterSession()` was reaching the RouterOS API with an
empty username, so every hotspot authorization aborted before its first
command with *"RouterOS API username is not configured"*.

The credentials themselves were valid — the setup wizard had verified a live
login and stored them in `/config/router-connection.json`. Production
activation, however, reads only `/config/router.json`, which
`StorageManager::kDefaultRouter` seeds with empty `username`/`password` on
first boot. The one function that copies setup credentials into the production
file, `RouterProvisioningEngine::syncProductionRouterCredentials()`, runs
inside the Finish pipeline. On an appliance whose wizard never completed
Finish — the state the failing unit was in, reporting
`Installation = Factory` — the production file stayed at its empty seed.

The failure was also invisible: the driver returned a bare `false`, the worker
published a generic outcome, and the portal displayed only "Activation failed".

### 1.2 Countdown jumping, freezing and reverting

The countdown had two owners. The firmware decremented `secondsLeft` once per
second in `tickSessions()`, and the browser independently decremented its own
copy once per second in `startMainTimer()`. Whenever a poll response landed,
the browser reconciled the two with heuristics that could accept a value
higher than what was on screen, so the display visibly jumped backwards and
forwards. The Insert Coin modal had the same problem with its own local
`coinCountdown` decrement.

The countdown also appeared to "freeze" during activation, which was correct
firmware behaviour (the clock only advances in `active`) but the browser kept
counting down anyway, so the two disagreed and then snapped.

### 1.3 Pause / Resume

Pause worked on the firmware side but had no per-session budget, and the API
returned an undifferentiated 404 for every refusal, so the portal could not
distinguish "no session" from "not allowed right now". A refused pause was
rendered as a service outage.

### 1.4 Session never returned to Waiting Payment

When time expired, cleanup ran and the session settled in `expired` — and
nothing ever moved it out. The record stayed terminal for the life of the
device entry, leaving the customer on a dead screen. The lifecycle described
in the requirements (`Expired → Disconnected → WaitingPayment`) had no final
edge.

### 1.5 Coin credit latency

Coin credit was only visible to the customer on the portal's next poll. The
insert modal polls every 2 s and the main heartbeat every 10 s, so an inserted
coin took up to 2 s (or 10 s outside the modal) to appear even though the
ESP32 had already booked it within the ~450 ms coin settle window.

### 1.6 Terminate Session

The confirmation dialog stated only "Remaining time will be lost" with no
figure. On failure the handler showed a generic status-line message, closed
the dialog regardless, and left the customer unsure whether their session had
ended.

---

## 2. Changes

### Firmware

| File | Function | Change |
| --- | --- | --- |
| `RouterProvisioningEngine.cpp/.h` | `ensureProductionRouterCredentials()` | Self-heals `/config/router.json` from the setup-verified connection when the production file is still empty. Storage-only, cached per boot, zero RouterOS commands. |
| `FirmwareApp.cpp` | `begin()` | Runs the credential check once at boot and logs the outcome. |
| `RouterProvisioningWorker.cpp/.h` | `ensureRouterCredentialsForHotspotJob()`, `hotspotFailureReason()` | Lazy credential check before each activate/pause/deauthorize job; combines credential and driver errors into one exact reason. |
| `RouterProvisioningWorker.h` | `HotspotOutcome` | Added a `reason[72]` field so a failure is never published bare. |
| `IRouterDriver.h`, `RouterPlatform.cpp/.h`, `MikroTikDriver.cpp/.h` | `lastHotspotError()`, `failHotspot()` | Every hotspot failure records why (credentials missing, session open failed, user add/update failed, active login failed). |
| `PortalSessionManager.cpp` | `drainHotspotOutcomes()` | Stores `activationErrorReason`; logs the reason on both serial and the event log. |
| `PortalSessionManager.cpp/.h` | `tickSessions()` | Bounded automatic recovery: a paid session stuck in `activation_error` retries up to `kActivationRetryLimit` (3) times, `kActivationRetryDelaySec` (20 s) apart. |
| `PortalSessionManager.cpp/.h` | `pause()` | Added `enforceLimit` and `errorCode`; customer pauses capped at `kMaxCustomerPauses` (3), owner pause uncapped. |
| `PortalSessionManager.cpp` | `resume()`, `donePaying()` | Reset the retry and pause counters at the right lifecycle points. |
| `PortalSessionManager.cpp/.h` | `enrichSessionCapabilities()` | New. Derives `timerRunning`, `canPause`, `canResume`, `canTerminate`, `canInsertCoin`, `pausesRemaining`, `pauseLimit` so the portal renders rules it never has to infer. |
| `PortalSessionManager.cpp/.h` | `recycleExpiredSessionUnlocked()` | New. Returns a fully cleaned-up portal session to Waiting Payment, closing the lifecycle loop. |
| `PortalSessionManager.cpp` | `emitSessionEvent()` | SSE payloads now carry the same enriched shape as `GET /api/portal/session`, so a push can be applied without a follow-up request. |
| `PortalSessionManager.cpp` | `onCoinInserted()` | Emits the session event before the SD save so credit reaches the customer immediately. |
| `PortalSessionManager.cpp` | `drainHotspotOutcomes()` (Deauthorize) | Emits `portal.session.expired` and recycles the session to Waiting Payment. |
| `ApiServer.cpp` | `/api/portal/pause`, `/api/portal/resume` | Distinct status codes and stable error codes (`PAUSE_LIMIT_REACHED`, `ACTIVATION_QUEUE_FULL`, `SESSION_NOT_FOUND`). |
| `ApiServer.cpp` | `/api/users/pause` | Owner pause passes `enforceLimit=false`. |

### Captive portal (`portal/` → `Final_Build_Portal/`)

| File | Change |
| --- | --- |
| `renzfi-app.js` | Replaced the local countdown with a deadline model (below). |
| `renzfi-app.js` | Subscribes to eleven `portal.*` SSE events on the existing `/api/events` stream; applies payloads addressed to this MAC directly. |
| `renzfi-app.js` | `waitForActivation()` short-circuits on the SSE result and polls at 1 s instead of 500 ms. |
| `renzfi-app.js` | Terminate rewritten: busy state, failure kept on screen, dialog not dismissible mid-request, post-terminate re-sync. |
| `renzfi-app.js` | Pause shows the remaining budget; a refused pause explains the rule instead of raising the outage banner. |
| `renzfi-app.js` | API errors carry the firmware's stable `code`. |
| `login.html` | Terminate dialog shows the exact time and credits at stake, a status line, and accessible dialog semantics. |
| `renzfi-style.css` | Styles for the new dialog elements and a single-column layout below 380 px. |

---

## 3. State machine

```
                 insert coin
  waiting_coin ──────────────┐
       │  coin window expires │
       ▼                      ▼
     idle ◀───────────── (credits kept)
       │  done paying
       ▼
   activating ──── worker fails ──▶ activation_error
       │                                │
       │ worker authorizes              │ auto-retry ×3 @20 s
       ▼                                │ or customer Resume
     active ◀───────────────────────────┘
       │  ▲
 pause │  │ resume (router re-authorizes)
       ▼  │
     paused
       │
       │  time reaches zero / terminate
       ▼
    expiring ── deauthorize ok ──▶ expired ──▶ idle (Waiting Payment)
```

Every state now has a defined exit:

- **activating** — succeeds to `active`, or fails to `activation_error` with an
  exact reason. Never silent.
- **activation_error** — bounded auto-retry, then waits for Resume. Purchased
  time is preserved throughout.
- **paused** — resume re-queues authorization; if that authorization fails the
  session stays `paused` with time intact rather than claiming connectivity.
- **expiring** — cleanup retries up to 3 times; if the budget is spent the
  customer can still start a new session (`canInsertCoin` stays true) so the
  device is never a dead end.
- **expired** — recycled to `idle` once cleanup completes and nothing is owed.

The countdown decrements only in `active && !paused`, which is exactly the
condition published as `timerRunning`.

---

## 4. Countdown synchronization

The browser no longer counts. On every payload it converts the firmware's
`secondsLeft` into a deadline:

```
expiryAt = now + secondsLeft * 1000
display  = ceil((expiryAt - now) / 1000)
```

- The clock advances only when the firmware sets `timerRunning`. Paused,
  activating and expired sessions show the firmware's frozen value.
- A payload that moves the deadline **later** by ≤ `SYNC_JUMP_THRESHOLD` (10 s)
  is treated as transport jitter and ignored — the earlier deadline is kept, so
  the display keeps falling smoothly instead of rewinding or stalling.
- A larger move, or any explicit user action, is adopted immediately: added
  time appears at once.
- The display is floored at zero and can never go negative.
- The Insert Coin modal countdown uses the same anchoring against
  `coinWindowRemaining`.

The 1 s interval is a repaint loop only; it holds no value of its own.

---

## 5. RouterWorker flow verification

A successful activation now traces end to end:

```
[coin] mac=… peso=1 coinMinutes=5 sessionCredits=2 purchasedMinutes=10
[portal] done-paying begin
[portal] done-paying complete
[portal-activate] mac=… job=queued profile=… remaining=600
[router-worker] activate-hotspot-user mac=… ok=yes
[portal-activate] mac=… ok=yes
```

A failure now names the step instead of failing silently:

```
[router-worker] activate-hotspot-user mac=… ok=no reason=RouterOS API username is not configured
[portal-activate] mac=… ok=no reason=RouterOS API username is not configured
```

Reasons currently reported: router settings unavailable, API username/password
not configured, API session could not be opened, hotspot user lookup failed,
hotspot user add/update failed, active login failed, worker queue full.

---

## 6. Load impact

**RouterOS API commands — unchanged per customer action.**

| Action | Before | After |
| --- | --- | --- |
| Done paying (success) | 1 authorize job | 1 authorize job |
| Pause | 1 job | 1 job |
| Resume | 1 job | 1 job |
| Expire / terminate | 1 deauthorize job | 1 deauthorize job |
| Credential self-heal | — | 0 (storage only) |

The only new RouterOS traffic is the bounded retry for a session that already
failed: at most 3 additional attempts, 20 s apart, replacing an unbounded
sequence of customer-initiated Resume taps. No new polling loop, no background
scan, no periodic RouterOS read was added.

**ESP32 HTTP load decreased.** Heartbeat (10 s) and coin poll (2 s) are
unchanged, but activation waiting dropped from up to 70 requests (500 ms for
35 s) to typically one, because the SSE push resolves it. SSE reuses the
EventSource the portal already opened for branding — no additional connection.

**Build size** (`freenove_esp32_s3_wroom`):

| | Start of this session | After these changes | Delta |
| --- | --- | --- | --- |
| RAM (static) | 106,308 B (32.4 %) | 106,308 B (32.4 %) | 0 |
| Flash | 2,392,451 B (91.3 %) | 2,393,771 B (91.3 %) | +1,320 B |

**Heap / DMA.** No new tasks, queues, buffers or allocations in any hot path.
`emitSessionEvent()` now builds one `JSON_DOC_SMALL` document on the heap per
event, freed on return; events are user-paced, not periodic. The retry and
recycle logic operate on the existing session document under the existing
mutex. No change to DMA-capable memory usage.

---

## 7. Automated test

`npm run test:portal` now runs the existing resolver test plus a new headless
lifecycle test, `scripts/test-portal-session-lifecycle.mjs`. It loads
`Final_Build_Portal/renzfi-app.js` — the exact file uploaded to the hotspot —
in a stubbed browser with a controllable clock, and drives it against a fake
ESP32 that mirrors the `/api/portal` contract.

```
[test:portal-resolver] OK
  PASS  boots into Waiting Payment with a zeroed countdown
  PASS  insert coin opens the firmware coin window
  PASS  coin credit renders from the SSE push without an extra request
  PASS  a second coin accumulates credit and time
  PASS  done paying enters Activating and does not start the clock
  PASS  the countdown stays frozen while activation is in flight
  PASS  router authorization flips the portal to Connected
  PASS  the countdown advances in real time once connected
  PASS  a stale payload never rewinds the visible countdown
  PASS  the countdown keeps falling after a resync
  PASS  added time is applied immediately
  PASS  pause freezes the countdown
  PASS  pause is reported as used against the session budget
  PASS  resume continues from the remaining time, without a reset
  PASS  the pause budget is shown on the button
  PASS  a refused pause explains the limit instead of an outage banner
  PASS  the terminate dialog states exactly what is forfeited
  PASS  a failed terminate keeps the session and reports the reason
  PASS  a successful terminate clears the session and closes the dialog
  PASS  terminating returns the portal to Waiting Payment
  PASS  the countdown floors at zero instead of going negative
  PASS  expiry restores Waiting Payment and the Insert Coin button
[test:portal-lifecycle] 22/22 checks passed
```

The test caught three defects during development that would otherwise have
shipped: the display could stall for several seconds after a stale payload
(fixed by the deadline model replacing the earlier hold-the-value guard); the
terminate dialog's own dismissal guard blocked it from closing on success; and
the pause-limit branch matched on prose instead of the firmware's error code.

---

## 8. Hardware validation checklist

Firmware: `ESP32_S3_Firmware/.pio/build/freenove_esp32_s3_wroom/firmware.bin`
Portal: upload the eight files in `Final_Build_Portal/` to MikroTik Hotspot
storage (`login.html`, `renzfi-app.js`, `renzfi-style.css`, `md5.js`,
`Default-Banner.png`, `bg_music.mp3`, `coin.mp3`, `success.mp3`).

Watch the serial console during the run; every step below has a matching log
line.

Activation
- [ ] Boot logs `[boot] Production RouterOS credentials ready`
- [ ] Insert ₱1 — credit appears in under a second, no page refresh
- [ ] Insert ₱1 again — ₱2.00 / 10 minutes shown
- [ ] Insert ₱1 + ₱5 — minutes accumulate per the configured promos
- [ ] Done Paying — status goes Activating, then Connected
- [ ] Internet actually works on the client device
- [ ] `[router-worker] activate-hotspot-user … ok=yes` in the log

Countdown
- [ ] Counts down one second per second, no jumps in either direction
- [ ] Insert Coin modal countdown behaves the same
- [ ] Adding time raises the countdown immediately
- [ ] Reloading the page shows the same remaining time

Pause / Resume
- [ ] Pause freezes the countdown and Internet stops
- [ ] Resume restores Internet and continues from the same value
- [ ] No time or credit is lost across pause/resume
- [ ] Button reads `PAUSE (2 left)`, then `(1 left)`, then `(0 left)`
- [ ] A fourth pause is refused with the limit message, not an outage banner

Expiration
- [ ] Countdown reaches 00:00:00 and stops — never negative
- [ ] Client is disconnected from the Internet
- [ ] `[portal-expire] mac=… ok=yes -> waiting_payment`
- [ ] Portal returns to Waiting Payment with Insert Coin available
- [ ] No leftover entry in `/ip hotspot active` on the router

Terminate
- [ ] Dialog shows the exact remaining time and credits
- [ ] Cancel leaves the session untouched
- [ ] Terminate ends Internet immediately and returns to Waiting Payment
- [ ] With the router unplugged, terminate reports the failure and keeps the
      session rather than silently clearing it

Stability
- [ ] Free heap stable across ten full cycles
- [ ] `[dma]` monitor reports no regression
- [ ] MikroTik CPU unchanged versus the previous firmware
- [ ] No repeated RouterOS commands in `/log print` while a session is idle
