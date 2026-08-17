# Renz-Fi Session Clock Forensic — 2026-08-15

**Mode:** SOURCE + supplied hardware evidence. **No firmware flash. No portal upload. No RouterOS config changes in this pass.**

**Customer evidence:** MAC `06:36:E3:2C:C4:E8`, IP `10.20.0.251`

**Question this document must answer:**

> WHY can MikroTik show ~1 minute remaining while the captive portal shows ~4 minutes?

**Priority:** STABILITY > RECOVERY > DATA CONSISTENCY > SESSION CORRECTNESS > PERFORMANCE

**Related (already implemented, must not regress):**

- `docs/RENZFI_SESSION_SYNC_FORENSIC_2026-08-15.md` / remediation
- `docs/RENZFI_UPTIME_LIMIT_FORENSIC.md` (Model B user `limit-uptime`)
- `docs/RENZFI_SESSION_DESYNC_EXPIRY_FORENSIC.md`
- `docs/RENZFI_COIN_PROMO_PAUSE_FORENSIC_FIX.md`
- `docs/RENZFI_VOUCHER_EXPIRY_REMEDIATION_2026-08-15.md`

---

## 1. Executive answer

MikroTik Session Time Left and the portal countdown are **two independent clocks that are not started from the same authorization event and are not kept on the same remaining-time formula**.

| Clock | Authority | When it starts | What “remaining” means |
|-------|-----------|----------------|------------------------|
| **A — ESP32 / portal** | `PortalSessionManager.secondsLeft` + browser `sessionExpiryAt` | Entitlement is **reserved at `donePaying`**. The number is published immediately. Decrement starts only after `connected=true`. The browser may **re-anchor** `Date.now() + secondsLeft` on `trustFully` GET/`/status` re-entry. | “Purchased seconds still stored in ESP32 RAM” |
| **B — RouterOS Active** | `/ip/hotspot/active` `uptime` + `limit-uptime` → `session-time-left` | **`/ip/hotspot/active/login` success**, or earlier if a **leftover Active row** already existed | Proven hardware arithmetic: `session-time-left ≈ limit-uptime − uptime` |

The observed multi-minute offset (portal ~4 min, MikroTik tens of seconds) is **not** Android captive-portal lag. Android “Login or authentication required” is irrelevant.

It is **not** explained by the typical 10–30 s Activate job alone.

It **is** explained by the proven RouterOS identity:

```text
session-time-left ≈ active.limit-uptime − active.uptime
```

combined with this firmware write (`MikroTikDriver::loginHotspotActive`):

```text
if Active already exists:
    active/set limit-uptime = ESP32 timeoutSeconds   // “from now”
```

If leftover (or still-running) Active `uptime` is already ~4 minutes and the new write is `limit-uptime=00:05:00`:

```text
session-time-left ≈ 300 − 240 = 60s     // MikroTik ~1 minute
ESP32 secondsLeft               = 300s  // portal ~4–5 minutes
```

That is the exact customer-visible discrepancy.

A second, smaller but real offset always exists even on a clean first login: Clock B starts at `active/login` (T8); Clock A only begins decrementing when the worker outcome is committed (`connected=true`, T10). `secondsLeft` is **not** reduced by T8→T10. The portal then builds a **new** browser deadline from that still-full reservation.

---

## 2. Classification

| Label | Meaning |
|-------|---------|
| **PROVEN** | Directly supported by current source **and** the observed direction of the hardware offset |
| **STRONGLY INDICATED** | Supported by source; the specific leftover-Active uptime for this capture was not printed |
| **UNPROVEN** | Possible, not demonstrated |
| **RULED OUT** | Contradicted by source or by the observed state |

---

## 3. Hardware evidence (authoritative)

| Observation | Meaning |
|-------------|---------|
| Coin inserted, then MikroTik HotSpot Active appears | Clock B is running. Internet authorization exists. |
| Speedtest / Internet already works | Active is the Internet authority. |
| Android may still show “Login or authentication required” | **Not** Renz-Fi session state. Do not poll or delay for it. |
| Portal eventually paints Connected | Clock A `connected` is committed later than Active creation. |
| MikroTik Session Time Left ≈ tens of seconds | Clock B has already consumed most of a ₱1 / 5-minute cap **or** leftover Active uptime was subtracted from a 5-minute `limit-uptime`. |
| Portal still shows ≈ 4 minutes | Clock A still holds nearly the reserved purchase. |

Customer identity matches prior forensics: `06:36:E3:2C:C4:E8` / `10.20.0.251`.

---

## 4. Trace: complete activation path

```text
COIN
  → onCoinInserted (credits / purchasedMinutes)
  → DONE PAYING
      PortalSessionManager::donePaying()
        secondsLeft = purchasedSeconds (or existing + purchased)
        sessionState = activating
        connected = false
        routerAuthPending = true
        sessionGeneration bump (new purchase only)
        timer does NOT decrement (tick requires connected)
  → onSessionActivated()
        HotspotUser.timeoutSeconds = session["secondsLeft"]   // still full reservation
        tryEnqueueActivateHotspotUser()
  → RouterProvisioningWorker::ActivateHotspotUser
        RouterPlatform::provisionHotspotUser
        MikroTikDriver::createHotspotUser
          user/print
          user/set|add  limit-uptime = existing_uptime + timeoutSeconds   // Model B USER — correct
          loginHotspotActive
            active/print ?mac-address=
            IF active id exists:
              active/set limit-uptime = timeoutSeconds                 // Model B NOT applied to ACTIVE
            ELSE:
              active/login  (no limit-uptime attribute)
              active/print verify
  → publishHotspotOutcome(Activate, ok, mac, generation)
        Outcome has NO authorizedAt, NO grantedSeconds, NO Active snapshot
  → drainHotspotOutcomes()
        if expected activating + routerAuthPending + ok:
          connected = true
          sessionState = active
          secondsLeft UNCHANGED (still reserved purchase)
          emit portal.session.connected
  → GET /session or SSE
        enrichSessionCapabilities: timerRunning = active && connected && !paused && secondsLeft > 0
  → portal applyNormalizedSession(..., trustFully)
        anchorSession(secondsLeft, true, trustFully)
        sessionExpiryAt = Date.now() + secondsLeft * 1000
```

**PROVEN:** Connected is not optimistic on the ESP32 commit path. The portal can still **display** reserved `secondsLeft` during Activating (frozen). The **timer** is not supposed to run until Connected.

**PROVEN:** There is no `authorizedAt` / `expiresAt` for coin sessions. Voucher already has `serviceExpiresAt` (wall clock). Coin remaining is a RAM counter reserved at pay time.

---

## 5. Trace: when RouterOS Active starts

`MikroTikDriver::loginHotspotActive` (`MikroTikDriver.cpp`):

1. `active/print ?mac-address=`
2. If an `.id` exists → **`active/set limit-uptime=<timeoutSeconds>`** and return true **without** a new login.
3. Else → `active/login` with user/password/mac/ip. **No `limit-uptime` on the login.**
4. Verify print; fail if no Active row.

**PROVEN:** Clock B starts at leftover Active’s original login, or at this job’s `active/login`.

**PROVEN (RENZFI_UPTIME_LIMIT_FORENSIC.md §5 hardware sample):**

```text
uptime=18s
session-time-left=4m42s
18 + 282 = 300
```

So:

```text
session-time-left ≈ limit-uptime − uptime
```

This is a lifetime/session-start cap, **not** “seconds from this API write.”

---

## 6. Trace: when ESP32 `connected` becomes true

Only in `drainHotspotOutcomes()` Activate success, and only when:

- outcome generation matches
- `routerAuthPending`
- state is `activating` | `paused` | `activation_error`

Then: `connected=true`, `hadRouterAuth=true`, `sessionState=active`.

**PROVEN:** `secondsLeft` is not rewritten on coin Activate success (voucher may replace it from `serviceExpiresAt`).

**PROVEN:** `tickSessions` decrements only when `active && !paused && connected`.

Therefore during Activating the reserved 300 s is **frozen** while Clock B may already be burning.

---

## 7. Trace: when `secondsLeft` begins decrementing

`tickSessions()`:

```text
if (isActive && !isPaused && connected) {
    secondsLeft = secondsLeft - 1   // coin
    // voucher: may clamp to wall remaining first
}
```

Starts at the **loop() second after Connected commit**, from whatever value was reserved at `donePaying` (or last add-time).

It does **not** start at RouterOS `active/login`.

It does **not** subtract leftover Active uptime.

---

## 8. Trace: how the browser deadline is constructed

`portal/renzfi-app.js`:

```text
anchorSession(seconds, running, force):
  if !running: freeze seconds, sessionExpiryAt = 0
  else:
    candidate = Date.now() + seconds * 1000
    jitter absorb only if candidate is LATER by ≤ 10s
    if force || !jitter: sessionExpiryAt = candidate
```

`applyNormalizedSession` **always** writes `state.secondsLeft = session.secondsLeft` and calls `anchorSession(..., trustFully)`.

`trustFully=true` sites:

| Site | Effect |
|------|--------|
| `init()` first `GET /session` | Re-entry to `/status` rebuilds deadline from current `secondsLeft` |
| `waitForActivation()` every HTTP poll | **Force-rebases** when Connected arrives, from still-full reservation |
| `handleDonePaying` snapshot | Shows reserved time immediately (frozen while Activating) |

Background `syncSessionFromServer` uses `trustFully=false`, so a later GET can still move the deadline later if the jump is **> 10 s** (`force || !jitter`).

**PROVEN:** The browser is a second deadline authority (`Date.now() + secondsLeft`). A GET can increase displayed remaining. `/status` re-entry can reconstruct a large deadline from a frozen/stale `secondsLeft`.

**PROVEN:** The browser never reads RouterOS `session-time-left`.

---

## 9. Every place `secondsLeft` can be written (ESP32)

| Site | When | Can increase? |
|------|------|----------------|
| `donePaying` | Reserve purchase / add-time | **Yes** (purchase) |
| `donePaying` rollback | Enqueue fail | Restores previous |
| Voucher redeem / activate / wall clamp | Absolute `serviceExpiresAt` | Voucher rules only |
| `tickSessions` decrement | Connected + active + !paused | No (decrements) |
| `tickSessions` expire | Hits 0 | Sets 0 |
| Activate outcome (voucher only) | Wall remaining | Can clamp down |
| `terminateSession` / `reset` | Owner/customer stop | Sets 0 |
| Activation-error voucher expire | Wall 0 | Sets 0 |

**PROVEN:** Coin Activate success does **not** restamp `secondsLeft` from RouterOS or from an authorization timestamp.

---

## 10. Every place expiry / deadline can be regenerated

| Layer | Mechanism |
|-------|-----------|
| ESP32 coin | None. No `expiresAt`. Remaining is a counter. |
| ESP32 voucher | `serviceExpiresAt` / `serviceExpiresEpoch` (correct absolute model) |
| Browser | `sessionExpiryAt = Date.now() + secondsLeft` on apply, especially `trustFully` |
| RouterOS user | `limit-uptime = existing_uptime + timeoutSeconds` (Model B) |
| RouterOS Active | `limit-uptime = timeoutSeconds` on `active/set` (**not** Model B) |

---

## 11. GET / SSE overwrite

| Path | RouterOS? | Timer effect |
|------|-----------|--------------|
| `GET /api/portal/session` | No | Returns RAM `secondsLeft`. Portal may re-anchor. |
| `POST /api/portal/heartbeat` | No | Touch `lastSeen` only. |
| SSE `portal.session.*` | No | Full session snapshot via `enrichSessionCapabilities`. Same `secondsLeft`. |
| `waitForActivation` GET | No | `trustFully=true` — force deadline. |

Generation guard exists (`incomingGen < currentGen` ignored unless `trustFully`). `trustFully` on init/`waitForActivation` **bypasses** that for timer rebase.

---

## 12. RouterOS Active verification

`maybeEnqueueActiveVerify()`: at most one Active print / 60 s, one Connected MAC, RouterWorker only, skipped when unhealthy or zero Connected.

Verify outcome:

- query fail → leave Connected (entitlement preserved)
- `not_active` → `activation_error`, `connected=false`, `secondsLeft` preserved

**PROVEN:** No 1-second poll. No heartbeat → RouterOS. Idle with `needsRouterOsWork()==false` still produces zero API work.

This path does **not** reconcile remaining time. It only detects missing Active.

---

## 13. Every place RouterOS `limit-uptime` is changed

| Command | Formula | Verdict |
|---------|---------|---------|
| `user/add` | `limit-uptime = timeoutSeconds` (uptime 0) | Correct |
| `user/set` | `new_limit = existing_uptime + timeoutSeconds` (never shrink below a still-valid larger cap) | **Model B — keep** |
| `active/set` | `limit-uptime = timeoutSeconds` (**ESP32 remaining from now**) | **ROOT DEFECT for Clock B** |
| `active/login` | no `limit-uptime`; remaining comes from user cap − user uptime | Correct on a clean user |

`reset-counters` is not in production firmware. Do not add it.

---

## 14. WHY MikroTik can show 1 minute while the portal shows 4 minutes

### 14.1 Primary cause — Active `limit-uptime` is not Model B (PROVEN arithmetic)

User Model B (already fixed, must keep):

```text
user.limit-uptime = user.uptime + requested_seconds
usable remaining    = requested_seconds
```

Active write (current):

```text
active.limit-uptime = requested_seconds          // treated as “from now”
RouterOS remaining  = active.limit-uptime − active.uptime
```

Whenever Active already has uptime U:

```text
MikroTik remaining = requested − U
ESP32 remaining    = requested
offset             = U
```

If U ≈ 4 minutes and requested = 5 minutes:

```text
MikroTik ≈ 1 minute
portal   ≈ 4–5 minutes
```

**When Active already exists at Activate time (STRONGLY INDICATED on this MAC):**

1. **Leftover Active** from a previous authorization (ESP32 expired / disconnected / activating retry, Active not removed). New ₱1 purchase reserves 300 s, takes the `active/set` branch.
2. **Add Time** on a live session. `donePaying` sets `activating`, `connected=false`, `secondsLeft = old + 300`. Timer **stops**. Clock B **keeps burning**. Then `active/set limit-uptime = newRemaining` without adding Active uptime. Offset = Active uptime since that Active row started.
3. **Retry after a successful login** whose outcome was dropped (unexpected state / mailbox). Second job finds Active and `active/set`s 300 against already-elapsed uptime.
4. **Long Activating with leftover or early login.** Portal frozen at ~300; WinBox already consuming. Later Connected commits the full 300.

The same MAC has a persistent HotSpot username `0636E32CC4E8` (Model B reuse). Leftover Active on reuse is the expected production case, not an edge case.

### 14.2 Secondary cause — clocks start at different events (PROVEN)

```text
T2  donePaying:     secondsLeft = 300, connected = 0, timer frozen
T8  active/login:   Clock B starts
T10 outcome commit: connected = 1, secondsLeft still 300, Clock A starts
T12 browser:        deadline = now + 300
```

T8→T10 is usually seconds (not minutes). It **cannot** alone produce a multi-minute WinBox vs portal gap, but it **always** makes the portal start late from a full reservation.

If Activating lasts minutes (historical health dwell, `unknown host IP` retries, leftover Deauth-before-Activate), Clock B can consume minutes while the portal stays frozen at ~4–5:00, then Connected re-anchors to ~300.

### 14.3 Tertiary cause — browser re-anchor (PROVEN in source)

`/status` init and `waitForActivation` use `trustFully=true`.

If ESP32 still holds ~300 (timer not running, or just committed), the browser **creates a new 4–5 minute deadline** even if RouterOS has already consumed most of the Active cap.

A GET is allowed to move the deadline later when the jump exceeds 10 s.

### 14.4 Not the cause

| Hypothesis | Verdict |
|------------|---------|
| Android captive detection delay | **RULED OUT** as clock authority. Internet already works. |
| Need to display RouterOS `session-time-left` in the portal | **Rejected.** That hides the formula bug; it does not create one timeline. |
| Constant grace / fudge seconds | **Rejected.** Offset is leftover Active uptime, not a fixed 15 s. |
| User Model B missing | **RULED OUT** for user objects. `createHotspotUser` already adds `existing_uptime`. The bug is the **Active** write. |
| ESP32 decrementing during Activating | **RULED OUT** after session-sync remediation. Decrement requires `connected`. That would make the portal **lower**, not higher. |
| Idle RouterOS polling | **RULED OUT.** GET/heartbeat do not query Active. |

---

## 15. Pause / resume / terminate (one system)

Customer and owner already share `PortalSessionManager::pause` / `resume`.

| Actor | API | Behavior |
|-------|-----|----------|
| Customer | `POST /api/portal/pause` | `pause(mac, err, enforceLimit=true)` — max 3 pauses |
| Owner | `POST /api/users/pause` | `pause(mac, nullptr, false)` — no budget |
| Customer | `POST /api/portal/resume` | `resume(mac)` — Connected only after Activate success |
| Owner | `POST /api/users/resume` | same `resume(mac)` |
| Customer | `POST /api/portal/terminate` | `terminateSession` — zeros time, Deauth(**generation**) |
| Owner | `POST /api/users/disconnect` | `reset(mac)` — zeros time, ExpireSession **without generation** |

**Pause (PROVEN, keep):**

- ESP32 freezes immediately (`paused=true`, `sessionState=paused`). Timer stops. No `sleep()`.
- RouterOS pause is async: Active + cookies removed, **user kept**.
- Failure reverts to Active (Internet may still be up). Entitlement preserved.

**Resume (PROVEN, keep):**

- `paused` stays true until Activate outcome (no optimistic Connected).
- Reuses the Activate path → **same Active `limit-uptime` bug** if leftover Active exists (pause job not finished, or remove failed).
- Portal `handleTogglePause` then `syncSessionFromServer` + `waitForActivation` (`trustFully=true`) — same re-anchor risk.

**No artificial delay on pause/resume.** The only wait is the existing one-worker Activate/Pause job. Do not add dwell.

**Owner terminate gap (PROVEN):** `reset()` does not capture `sessionGeneration` for the Deauth job (customer `terminateSession` does). A late owner disconnect can theoretically collide with a newer purchase. Fix: pass generation like terminate. Also clear any new expiry fields.

**Owner pause/resume are already the same lifecycle as the customer.** They must stay on the same expiry restamp (freeze on pause, `authorizedAt + remaining` on resume success).

---

## 16. Voucher (one system — do not merge clocks)

Voucher already uses the required absolute model:

```text
serviceExpiresAt = redeemedAt + minutes × 60
remaining = max(0, serviceExpiresAt − now)
```

Coin sessions must **not** adopt voucher NTP as the coin authority (coin must work without wall-clock). Coin should use **millis() monotonic** `authorizedAtMs + grantedSeconds = expiresAtMs`.

Portal should consume the same presentation fields (`secondsLeft`, `timerRunning`, `grantedSeconds`, `serverNowMs`) for both. Do not apply Model B user-uptime math to voucher remaining.

Enhancement (safe): publish the same expiry snapshot on GET/SSE so the browser never treats `secondsLeft` as a resettable deadline. Voucher wall remaining already clamps `secondsLeft` in `enrichSessionCapabilities`.

---

## 17. Instrumentation gaps (why the offset was hidden)

`ActivationLatencyTrace` has T0–T10 (pay → portal Connected) but:

- does not name `activeLoginSuccessAt` as the **authorization** instant for the clock
- is not copied into the HotspotOutcome
- does not print `existingUserUptime`, `activeUptime`, `activeSessionTimeLeft`, `newUserLimit`
- does not compute `routerAuthorizationToPortalCommitMs`

Without those, a 4-minute Active-uptime offset looks like “portal delay” or “Android.”

Required diagnostic line at commit (implementation):

```text
[session-clock] mac=… granted=… authorizedAtMs=…
  existingUserUptime=… existingUserLimit=… newUserLimit=…
  activeUptime=… activeSessionTimeLeft=… usedActiveSet=…
  routerAuthorizationToPortalCommitMs=…
```

If `usedActiveSet=1` and `activeUptime` is minutes while `activeSessionTimeLeft` ≈ `granted − activeUptime`, the primary cause is confirmed on hardware.

---

## 18. Required architecture (not yet implemented)

```text
RouterOS Active created/verified
  → capture authorizedAtMs (T8)
  → grantedSeconds = purchased entitlement from now
  → Active limit-uptime = active.uptime + grantedSeconds     // Model B on Active
  → User limit-uptime   = user.uptime + grantedSeconds       // already Model B
  → outcome carries generation + timestamps + ROS snapshot
  → ESP32: expiresAtMs = authorizedAtMs + grantedSeconds * 1000
  → secondsLeft = max(0, expiresAtMs − millis())
  → connected = true, timerRunning = true
  → portal presents remaining; GET/SSE cannot increase it
```

Connected must never precede that event.

GET must never increase remaining.

Add Time / new purchase / voucher redeem are the only legitimate increases.

Pause freezes remaining. Resume restamps `authorizedAt` from the new Active success.

Transport failure ≠ missing Active. Missing Active (confirmed) → authorization lost, entitlement preserved.

Idle: `needsRouterOsWork()==false` → 0 RouterOS API polls.

---

## 19. Stop-condition review

| Condition | Status |
|-----------|--------|
| RouterOS Active remaining semantics | **Clear.** Hardware + wiki-equivalent: `session-time-left ≈ limit − uptime`. |
| Authorization timestamp | **Capturable** at T8 (`loginHotspotActive` success) via `millis()`. |
| Browser deadline vs generation | **Conflicts today** (`trustFully` can rebase). Fixable without dropping generation. |
| Share one timeline | **Safe** if Active uses Model B and ESP32 expiry starts at T8. |
| HotSpot config change | **Not required.** |
| Aggressive polling | **Not required.** |
| W5500 / TWDT / extra task | **Not required.** |
| Pause / voucher regression | Avoided by restamping expiry on pause/resume and leaving voucher wall-clock intact. |

No stop. Proceed to the smallest remediation.

---

## 20. What must not change

- Coin denominations / promo rates
- HotSpot topology / NAT / firewall
- W5500 / TWDT / RouterWorker / one API session
- User Model B (`new_limit = existing_uptime + requested`)
- Voucher `serviceExpiresAt` business rules
- Setup wizard (frozen)
- Idle zero-poll contract
- `sessionGeneration` stale-job rules
