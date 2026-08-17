# Renz-Fi Session Synchronization Forensic — 2026-08-15

**Mode:** SOURCE + supplied hardware evidence. **No firmware flash. No portal upload. No RouterOS config changes. No code edits in this pass.**

**Customer evidence:** MAC `06:36:E3:2C:C4:E8`, IP `10.20.0.251`

**Priority:** STABILITY > RECOVERY > DATA CONSISTENCY > SESSION CORRECTNESS > PERFORMANCE

---

## 1. Executive summary

This is **not one bug**. The supplied traces show at least four independent defects that combine into one customer-visible failure: coin accepted → 05:00 shown → Internet late or missing → portal stale / wrong error → `/login` or `/status` saying “You are logged in” then becoming unreachable.

| # | Issue | Verdict |
|---|--------|---------|
| 1 | Idle `/ip/hotspot/host` with empty Active | **Not a bug.** Host ≠ paid session. |
| 2 | `donePaying` writes `secondsLeft=300` and `activating` **before** RouterOS `active/login` | **Confirmed.** Entitlement is reserved locally; Internet is not. |
| 3 | Health FSM `PROBING → RECOVERING` (15s dwell) **blocks Activate** but **allows Deauth** | **Confirmed.** Customer activation waits; leftover cleanup can run first. |
| 4 | Activate vs Expire/Deauth have **no per-MAC generation** | **Confirmed in source.** A stale cleanup job can deauthorize a newly purchased session; a late Deauth outcome can overwrite `activating`/`active`. |
| 5 | Frontend shows `secondsLeft` immediately; `waitForActivation(35s)` can time out and paint an error while firmware is still activating | **Confirmed.** |
| 6 | UI “unknown host IP `10.20.0.254`” vs serial `10.20.0.251` | **`.254` is not hardcoded in Renz-Fi source.** Authoritative ROS trap is `.251`. `.254` is a stale/other-surface IP, not a firmware fallback constant. |
| 7 | Direct `10.20.0.1/login` / `/status` “You are logged in” | **MikroTik servlet / leftover `alogin.html`**, not the ESP32. After API login the walled garden ends; stock alogin redirects and the page can look “gone.” |

**Architectural correction (do not implement by polling):**

```
ESP32 session intent
  → RouterWorker (one job, one API session)
  → RouterOS authorization result
  → ESP32 commits ACTIVE + CONNECTED
  → portal reads that session
```

Idle must remain **zero unnecessary RouterOS API polling**.

---

## 2. Confirmed root causes

### 2.1 Local entitlement is committed before RouterOS authorization

`PortalSessionManager::donePaying()` under lock sets:

- `secondsLeft = 300` (₱1 × 5 min)
- `sessionState = activating`
- `connected = false`
- `routerAuthPending = true`
- `credits = 0`

Then it tries `onSessionActivated()` → `tryEnqueueActivateHotspotUser()`.

Hardware:

```
[portal] done-paying begin
[router-worker] activate deferred reason=router_unavailable
[portal-session] mac=06:36:E3:2C:C4:E8 state=activating credits=0 remaining=300
```

`GET /api/portal/done-paying` then returns that snapshot (`ApiServer.cpp`). The portal applies it with `trustFully` and paints **05:00** while `timerRunning` is false. The number is reserved entitlement, not granted Internet.

Firmware already documents the intended display rule: countdown advances only when `active && connected && !paused` (`enrichSessionCapabilities`). The **value** is still published early, so the UI looks like a live 5-minute session.

### 2.2 Health recovery dwell blocks the customer job

`RouterApiTransportGate::allowsHotspotActivate()` is **HEALTHY or UNKNOWN only**.

`RECOVERING`, `PROBING`, `COOLDOWN`, `UNAVAILABLE`, `DEGRADED` all defer Activate.

`allowsHotspotDeauth()` **includes RECOVERING and DEGRADED**.

Hardware:

```
[ros-health] state=PROBING reason=readiness_check
[router-worker] dispatch type=health-probe
[ros-health] probe ok
[ros-health] state=RECOVERING reason=job_ok dwell_ms=15000
… activate deferred reason=router_unavailable …
```

`ROUTER_HEALTH_RECOVERY_DWELL_MS = 15000` (`Config.h`). After a successful probe the worker will not accept Activate for 15 seconds. Deauth/cleanup **can** run in that window.

Probe is only enqueued when `wantsHealthProbe() && needsRouterOsWork()` (`PortalSessionManager::loop`). Idle with no pending ROS work still does not poll. The dwell is a **recovery** cost, not an idle poll. It is still a customer-visible delay when a purchase happens while health is not HEALTHY.

### 2.3 Activate and Expire are not generation-scoped (race)

There is **no** `sessionGeneration` on Hotspot jobs. Outcomes are `{kind, ok, mac[18], reason[128]}` only (`RouterProvisioningWorker.h`).

Proven race paths:

1. **Leftover cleanup wins the drain.** `retryPendingRouterWork()` prefers `cleanupRetryPending` over `activationRetryPending`. During RECOVERING, cleanup is allowed and activate is not.

2. **`donePaying` / `startCoinWindow` do not cancel cleanup flags or queued `ExpireSession` items.** A prior 0-credit coin-window timeout (`tickSessions` → `ExpireSession` / `coin_window_expired`) or a previous terminate/expire can still be in the portal work queue.

3. **`onSessionExpired()` does not re-check that the MAC is still supposed to be cleaned up.** It always `tryEnqueueDeauthorizeHotspotUser(mac)`.

4. **Deauth outcome blindly mutates the live record** (`drainHotspotOutcomes`, `Kind::Deauthorize`):

   - `connected = false`
   - `sessionState = Expired` (if ok)
   - does **not** require `routerCleanupQueued`
   - does **not** compare `sessionId`
   - if `secondsLeft > 0`, `recycleExpiredSessionUnlocked()` **refuses** to recycle

   Result: a new purchase can be forced to `expired` while still holding 300s.

5. **Activate outcome is stricter** — it ignores results unless state is `activating|paused|activation_error` **and** `routerAuthPending`. A successful login after a stale Deauth painted `expired` is **dropped**. RouterOS can be Active while ESP32 is Expired (or the reverse if Deauth runs after a successful Activate).

Hardware order matches this:

```
[portal-expire] mac=06:36:E3:2C:C4:E8 router=queued
[router-worker] dispatch type=deauthorize-hotspot-user
… user/remove, cookie/print …
[INFO] router: Hotspot user disconnected: 06:36:E3:2C:C4:E8
… later …
[router-worker] dispatch type=activate-hotspot-user
```

### 2.4 `unknown host IP` is a real RouterOS trap

`MikroTikDriver::loginHotspotActive()` sends `/ip/hotspot/active/login` with `user`, `password`, `mac-address`, and **`ip=` from `session["ipAddress"]`**.

Hardware:

```
TRAP: unknown host IP 10.20.0.251
[activate] operation=active_login … result=fail reason=unknown host IP 10.20.0.251
[portal-activate] … ok=no reason=unknown host IP 10.20.0.251
```

This means: at login time, `/ip/hotspot/host` had **no row for 10.20.0.251**. It is not a frontend-only string.

Typical causes (see §12): stale stored IP, host not bound yet, client roam/DHCP change during the 15s+ queue, or login after a cleanup that raced the association.

### 2.5 Frontend can declare failure while firmware is still activating

`handleDonePaying()` → `waitForActivation(35000)` requires `sessionState===active && connected`. Otherwise after 35s it throws `"Activation timed out — purchased time preserved"`, calls `noteApplianceFailure()`, and `showPortalError(...)`.

Lower bound from this remediation + traces:

| Step | Time |
|------|------|
| Health dwell | 15000 ms |
| ROS login (success path) | ~4000 ms |
| Activate job | ~8904 ms |
| Preceding deauth job | several seconds |

**15 + 4 + 9 + cleanup > 35s** is easy. The UI then shows an error / Disconnected / service notice while the worker is still activating. A later SSE `portal.session.connected` may or may not repair it depending on poll/SSE health.

### 2.6 Direct `/login` and `/status` are MikroTik servlets

Production captive portal is **hosted on MikroTik** (`portal/` → `deployment/mikrotik-hotspot/`). ESP32 is `http://10.10.10.2` API only.

`Captive Portal/alogin.html` (legacy, still what stock RouterOS uses if not replaced) contains **“You are logged in”** and redirects to `$(link-redirect)` / opens `$(link-status)`.

After a successful `/ip/hotspot/active/login`:

- The client **is** logged in on MikroTik.
- Hitting `10.20.0.1/login` often serves logged-in servlet behavior (status/alogin), not the unpaid `login.html` flow.
- Walled-garden DNS interception stops. The captive-portal tab can close, redirect to a WAN URL, or fail HTTPS. That is “page becomes unreachable,” not ESP32 crash.

Renz-Fi `portal/status.html` is the same app as `login.html` and does **not** say “You are logged in.” That phrase is MikroTik-native.

---

## 3. Likely root causes (strong source + evidence, not fully timestamp-correlated)

1. **Coin-window timeout with 0 credits queues ExpireSession** (`tickSessions`). A later purchase on the same MAC does not dequeue it.
2. **`POST /done-paying` ignores the body `ip`.** Only `mac` is read. `getSession(mac, "")` after success does not refresh IP. Activation uses last stored `ipAddress` (start-coin / GET session / heartbeat).
3. **Deauth before Activate can remove the HotSpot user and cookies**, then `active/login` runs against a host that moved or is briefly absent → `unknown host IP`.
4. **Outcome mailbox is one-deep and “newest wins.”** A Deauth result can replace an unread Activate result (`publishHotspotOutcome`).
5. First failed login with a **stale `.254`** stored in `activationErrorReason`, later retry with `.251` in serial — UI still showing the first reason if the second payload did not apply.

---

## 4. Root causes not yet proven

- That `/ip/hotspot/host` was empty for `.251` at the exact login millisecond (no host/print in the fail trace).
- That `.254` was ever written by this firmware build (no `10.20.0.254` in the repository).
- That Winbox/Profile CPU caused the original UNAVAILABLE (already unproven in the 2026-08-15 stability forensic).
- That the customer’s phone used a second DHCP lease `.254` concurrently with `.251`.
- Loss of purchased seconds on this specific MAC (recycle refuses when `secondsLeft > 0`; financial wipe is possible only if Deauth+recycle ran while remaining was 0).

---

## 5. Exact source files / functions

| Area | File | Function / site |
|------|------|-----------------|
| States | `PortalSessionManager.h` | `PortalState::*` |
| Coin window | `PortalSessionManager.cpp` | `startCoinWindow`, `onCoinInserted` |
| Coin pulse | `CoinManager.cpp` | insert path → `onCoinInserted` |
| Done paying | `PortalSessionManager.cpp` | `donePaying` |
| HTTP | `ApiServer.cpp` | `POST /api/portal/done-paying`, `GET /session`, `POST /heartbeat` |
| Activate enqueue | `PortalSessionManager.cpp` | `onSessionActivated` |
| Expire enqueue | `PortalSessionManager.cpp` | `onSessionExpired`, `terminateSession`, `tickSessions` |
| Drain / race | `PortalSessionManager.cpp` | `retryPendingRouterWork`, `drainHotspotOutcomes` |
| Timer tick | `PortalSessionManager.cpp` | `tickSessions` (`isActive && !paused` decrements) |
| Capabilities | `PortalSessionManager.cpp` | `enrichSessionCapabilities` (`timerRunning`) |
| Worker | `RouterProvisioningWorker.cpp` | `tryEnqueueActivate*`, `tryEnqueueDeauthorize*`, `tryEnqueueHealthProbe`, `runOp`, `publishHotspotOutcome` |
| Health | `RouterApiTransportGate.cpp` | `allowsHotspotActivate/Verify/Deauth`, `tickHealth`, `noteJobSuccess/Failure` |
| ROS login | `MikroTikDriver.cpp` | `createHotspotUser`, `loginHotspotActive`, `deauthorizeUser`, `macToHotspotUsername` |
| Portal UI | `portal/renzfi-app.js` | `normalizeSession`, `applyNormalizedSession`, `handleDonePaying`, `waitForActivation`, `renderStatus` |
| Portal HTML | `portal/login.html`, `portal/status.html` | `$(ip)`, `$(mac)` |
| Legacy alogin | `Captive Portal/alogin.html` | “You are logged in” |

There are **no** firmware states named `credited`, `paying`, or `terminating`. Closest: credits on `waiting_coin` / `idle`; `expiring` is terminate-in-flight.

---

## 6. Current state machine

```
idle
  ├─ startCoinWindow ──────────────────────────► waiting_coin
  │     coin insert ── credits++, purchasedMinutes++
  │     window timeout + credits>0 ────────────► idle (credits kept)
  │     window timeout + credits==0 ───────────► expiring + ExpireSession  ◄── DANGER
  └─ donePaying (credits>0)
        secondsLeft := purchasedMinutes*60 (+ add-time)
        credits := 0
        connected := false
        ───────────────────────────────────────► activating   ◄── remaining already 300
              Activate deferred (health) ── stay activating, activationRetryPending
              Activate fail ───────────────────► activation_error (secondsLeft kept)
              Activate ok ─────────────────────► active + connected=true
              Verify not_active ───────────────► activation_error
              tick: only Active+!paused decrements secondsLeft
              secondsLeft→0 ───────────────────► expiring + ExpireSession
              terminate/reset ─────────────────► expiring + ExpireSession
              Deauth outcome ok ───────────────► expired → recycle → idle
              Deauth outcome (NO generation) ──► expired EVEN IF now activating  ◄── BUG
```

`paused` / voucher paths omitted; they are out of this incident’s critical path except that resume also uses Activate.

---

## 7. Proposed synchronized state machine

Keep existing state names. Change **commit points** and **job identity**.

```
idle
  → waiting_coin          (local only; Host may exist; no User required; no Active)
  → activating            (entitlement reserved; connected=false; timerRunning=false)
        display: "Activating…" and frozen remaining (or hide countdown — product choice)
        RouterOS: user add/set + active/login
  → active + connected    ONLY after HotspotOutcome Activate ok for THIS sessionGeneration
        timer starts here
  → activation_error      Activate fail / verify lost; entitlement kept; Retry Internet
  → expiring              local sessionGeneration invalidated FIRST, then cleanup job tagged with that generation
  → idle                  after cleanup of THAT generation only
```

**Invariant:** `connected==true` iff last matching Activate outcome succeeded and no matching Deauth has completed.

**Transient only:** `activating`, `expiring`.

Do **not** add Host delete. Do **not** add idle ROS polling.

---

## 8. MikroTik object lifecycle

| Object | Meaning | Idle unpaid | Activating | Active | Expired/terminated |
|--------|---------|-------------|------------|--------|--------------------|
| `/ip/hotspot/host` | Associated client (MAC/IP). **Not payment.** | MAY exist | MAY exist | exists | **Do not delete** |
| `/ip/hotspot/user` | Credential `0636E32CC4E8` | should not be needed | may be added | exists | remove if session-owned |
| `/ip/hotspot/active` | Authorized Internet | MUST NOT | not authoritative until login ok | MUST exist | remove |
| `/ip/hotspot/cookie` | Remembered login | usually none | — | may exist | remove if session-owned |

Username mapping: `macToHotspotUsername` strips colons and uppercases. `06:36:E3:2C:C4:E8` → `0636E32CC4E8`. Unique per MAC, **not** per purchase. An old user row is reused (Model B uptime). That is correct for add-time; it is **unsafe** if a stale Deauth `user/remove` runs against a new purchase of the same MAC.

---

## 9. RouterWorker race analysis

Single worker + single API session: **still true**. The race is **job identity / order**, not parallelism.

```
donePaying → activating, remaining=300
     │
     ├─ Activate deferred (RECOVERING)
     ├─ leftover ExpireSession / cleanupRetryPending
     │
worker idle → retryPendingRouterWork
     │
     ├─ 1) Deauth (allowed in RECOVERING)
     └─ 2) Activate (blocked until HEALTHY)
```

Deauth outcome can mark `expired`. Later Activate outcome can be ignored. Or Activate succeeds, then a late Deauth removes Active/User.

Mailbox: one slot; newer outcome replaces unread older one.

---

## 10. Health-state delay analysis

| Question | Answer |
|----------|--------|
| Probe only when needed? | **Yes today**, if `needsRouterOsWork()`. Idle stays quiet. |
| Should Activate be its own readiness check? | **Yes.** A customer Activate should not wait for a separate probe + 15s dwell. First command failure already marks DEGRADED. |
| Is 15s dwell necessary before customer Activate? | **No for this product path.** Dwell was added to prevent reboot storms. It now **starves** Activate while **admitting** Deauth. |
| Should successful Activate establish HEALTHY? | **Yes.** `noteJobSuccess()` already promotes Unknown/Connecting → HEALTHY. From Probing it goes to RECOVERING (dwell). Customer success should be HEALTHY. |
| Failed Activate → DEGRADED forever? | No. Two failures → UNAVAILABLE; cooldown + probe only if work remains. Do not reintroduce idle login polls. |

**Recommendation for a later implementation pass:** if a Critical HotSpot Activate/Deauth is pending, skip HealthProbe and skip RECOVERING dwell for that job. Keep dwell only for admin/discovery flood control.

---

## 11. Frontend synchronization analysis

| Symptom | Cause |
|---------|--------|
| 05:00 while Internet down | `secondsLeft=300` in done-paying response; timer frozen but displayed |
| Disconnected after start | Default HTML “Disconnected”; error path; or `activation_error`; or timeout paint |
| unknown host IP | `renderStatus` shows `activationErrorReason` verbatim |
| Stale countdown | localStorage cache + `applyNormalizedSession`; timeout error does not clear `secondsLeft` |
| Optimistic overwrite | `handleDonePaying` sets “Activating…” locally; then server snapshot; then 35s timeout error **over** still-activating firmware |

Portal GET/heartbeat/branding **do not** call RouterOS. That contract is correct and must stay.

`timerRunning` is already false until `active && connected`. The missing piece is **not showing reserved time as if the session were live**, and **not timing out the UI before the worker**.

---

## 12. Unknown-host-IP source

**Repository search:** no `10.20.0.254`, no `unknown host IP` string, no `unknownHost` fallback.

Path of the **real** error:

1. `onSessionActivated` copies `session["ipAddress"]` → `HotspotUser.ip`
2. `loginHotspotActive` adds `=ip=` + that value
3. RouterOS traps `unknown host IP <that value>`
4. Worker publishes `reason`
5. `drainHotspotOutcomes` stores `activationErrorReason`
6. `renzfi-app.js` `renderStatus` displays that string

**Why UI can show `.254` while serial shows `.251`:**

- Not a hardcoded firmware IP.
- Most likely: an earlier trap (or MikroTik `$(ip)` / stock error page) used `.254`; the supplied serial is a later attempt with `.251`.
- `POST /done-paying` does not accept/update IP from the portal body, so the stored lease can lag `$(ip)` or the current Host row.
- Do **not** “fix” by writing `.251`. Use the **current** client IP at Activate time (portal body + last GET/heartbeat) and show the **exact** RouterOS trap. If host is missing, fail with that trap and retry when host exists — do not invent an IP.

---

## 13. Timer synchronization problem

| Clock | When it starts today | Desired |
|-------|----------------------|---------|
| ESP32 `secondsLeft` | At `donePaying` (reserved) | Keep reservation (financial safety) |
| ESP32 decrement | Only `active && !paused` | Keep; also require `connected` (already true for `timerRunning`, **not** for the decrement — `tickSessions` uses `isActive && !isPaused` only) |
| Portal display | Immediately shows 300 | Show frozen remaining **or** hide until Connected — product choice. Must not look “Connected.” |
| RouterOS `limit-uptime` | At user add/set, before login | Unchanged (Model B). Do not redesign. |

**Safest implementation:** keep `secondsLeft` as reserved entitlement; set `timerRunning` / decrement only after Activate ok; portal already respects `timerRunning`. Optionally do not decrement during `activating` even if a bug sets `active` without `connected` — add `connected` to the tick guard.

**Do not** start the live countdown at Done Paying.

---

## 14. Per-MAC generation / idempotency requirements

Today: `sessionId` exists and is rotated on recycle, but **is not placed on RouterWorker jobs or outcomes**.

Required:

```
sessionGeneration++  (or bind job to sessionId)
on terminate/expire: invalidate generation N, enqueue Deauth(N)
on purchase: generation N+1, enqueue Activate(N+1)
outcome for N must not mutate session if current generation != N
```

Username `0636E32CC4E8` may stay MAC-derived. Generation protects **asynchronous results**, not the RouterOS username.

---

## 15. Exact files that should be changed (later pass)

1. `ESP32_S3_Firmware/src/PortalSessionManager.h/.cpp`
2. `ESP32_S3_Firmware/src/RouterProvisioningWorker.h/.cpp`
3. `ESP32_S3_Firmware/src/RouterApiTransportGate.cpp` (Activate during RECOVERING / skip dwell for Critical)
4. `ESP32_S3_Firmware/src/ApiServer.cpp` (`done-paying` accept `ip`)
5. `ESP32_S3_Firmware/src/router/drivers/MikroTikDriver.cpp` only if login should omit stale `ip=` or refresh host — **minimal**, do not rewrite driver
6. `portal/renzfi-app.js` (timeout vs activating; do not paint Connected early; show exact reason)
7. Contract tests under `ESP32_S3_Firmware/tools/` + `scripts/test-portal-session-lifecycle.mjs`

**Do not change:** Model B uptime math, voucher absolute expiry, W5500/TWDT, wizard, RouterOS config, idle poll policy.

---

## 16. Exact functions that should be changed (later pass)

| Function | Change |
|----------|--------|
| `donePaying` | Bump generation; clear leftover cleanup flags **only for superseded generations**; keep `connected=false` |
| `startCoinWindow` | Cancel/supersede pending Expire for this MAC if a new unpaid window starts |
| `tickSessions` coin-window 0-credit expire | Do not queue Deauth unless a session-owned User/Active might exist (`hadRouterAuth`) |
| `onSessionExpired` | No-op if current generation is newer or state is `activating`/`active` with remaining time |
| `onSessionActivated` | Attach generation; refresh IP from latest session; refuse if generation stale |
| `drainHotspotOutcomes` | Match generation; Deauth must not expire a newer purchase |
| `retryPendingRouterWork` | Do not drain cleanup for a MAC that is `activating` with a newer generation |
| `tryEnqueueActivateHotspotUser` / health gates | Critical Activate may run in RECOVERING (or skip dwell when Activate is queued) |
| `publishHotspotOutcome` | Carry generation; do not replace Activate with unrelated Deauth for another generation |
| `loginHotspotActive` | Prefer current host IP; if `ip=` is set, it must be the live Host IP |
| `ApiServer` done-paying | Read `ip` from body; `getSession(mac, ip)` |
| `waitForActivation` | Stay on Activating until firmware `activation_error` or `active+connected`; do not treat 35s as Connected failure if state is still `activating` |
| `renderStatus` / timer | Never Connected unless `active && connected`; keep showing exact ROS reason |

---

## 17. Minimal safe implementation plan

1. Add `sessionGeneration` (uint32) on the portal session and on Hotspot jobs/outcomes.  
2. Invalidate generation on terminate/expire **before** enqueueing Deauth.  
3. Ignore mismatched outcomes.  
4. Cancel or no-op Expire when the same MAC is `activating`/`active` with remaining entitlement.  
5. Let Critical Activate proceed in RECOVERING (successful Activate → HEALTHY). Keep probe-only-when-needed.  
6. Pass client IP through done-paying; never hardcode `.251`/`.254`.  
7. Portal: Activating stays Activating; 35s timeout must not override firmware `activating`.  
8. Tests for: idle 0 ROS; activate vs stale deauth; health dwell does not starve Activate; unknown-host reason is exact; portal lifecycle 30/30.  
9. Compile only. Hardware validation separate.

---

## 18. Acceptance criteria

1. Idle, no pending ROS work → zero periodic RouterOS API.  
2. Host may exist unpaid; Active must not.  
3. Done Paying → Activating, not Connected; timer does not consume until Activate ok.  
4. `connected==true` only after `active/login` ok for that generation.  
5. Stale Deauth cannot remove a newer purchase’s User/Active.  
6. Health UNAVAILABLE reduces work; recovery does not put cleanup ahead of a new paid Activate for the same MAC.  
7. UI error text equals the RouterOS trap (actual client IP).  
8. `/login`/`status` “You are logged in” understood as MikroTik servlet; Renz-Fi UI remains the payment surface.  
9. Coin, add-time, pause/resume, terminate, voucher expiry unchanged in business rules.  
10. No claim of hardware stability until bench validation.

---

## 19. Regression risks

- Skipping RECOVERING dwell could send Activate into a still-booting RouterOS (one failed login, then DEGRADED — acceptable vs 15s+deauth race).
- Cancelling 0-credit window Expire might leave a stale User if `hadRouterAuth` was wrong.
- Omitting `ip=` on `active/login` (login by MAC only) may be safer on some RouterOS builds — must be proven on hardware, not assumed.
- Longer UI wait without a 35s hard fail may look “stuck” unless Activating copy is clear.
- Generation mismatch bugs could drop a valid Activate (customer stays activating until retry) — better than applying a stale Deauth.

---

## 20. Timing breakdown (successful supplied activate)

From firmware logs + code (not a new hardware run):

| Stage | Evidence | Delay |
|-------|----------|--------|
| Coin detect → credit | CoinManager → `onCoinInserted` | local, typically <100 ms after pulse settle |
| Done Paying HTTP | `donePaying` + JSON | local; Activate may already be deferred |
| Health probe | identity/print | one login; then **15000 ms dwell** |
| Queue wait | Deauth ahead of Activate | **seconds** (full deauth session) |
| ROS TCP + login | prior forensics / ~4s class | **~4000 ms** |
| user/print, user/add, active/print, active/login | `commands=5 session=1 elapsed=8904` | **8904 ms** job |
| ESP32 `active+connected` | `drainHotspotOutcomes` | next `loop()` after outcome |
| Browser Connected | SSE or 2s GET fallback | 0–2000 ms after commit, or **35s timeout fail** |

Every step ≥100 ms that matters: **15s dwell, ~4s login, ~8.9s activate, cleanup-before-activate, 2s portal poll, 35s UI timeout.**

---

## 21. Bottom line

Synchronize through **one ESP32 session record** and **RouterOS success/failure**. Do not make portal, ESP32, and MikroTik poll each other.

Host-during-idle is normal. Showing 05:00 before `active/login` is a commit-point bug. The 15s RECOVERING dwell plus generation-less cleanup is the main RouterWorker defect. The `.254` string is not a hardcoded fallback; the real trap is `unknown host IP 10.20.0.251`.

**Do not treat hardware as fixed until a later implementation pass is flashed and validated.**
