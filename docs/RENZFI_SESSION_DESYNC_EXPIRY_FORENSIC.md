# Renz-Fi Session Desync / Expiry Forensic

**Date:** 2026-08-13  
**Mode:** SOURCE-LEVEL + LIFECYCLE FORENSICS ONLY — **NO CODE CHANGES**  
**Scope:** Prove why MikroTik Hotspot **ACTIVE is empty** while the ESP32 captive portal still shows **Connected** with remaining time, and why the countdown can jump **01:xx → 02:xx**.

**Files inspected (read-only):**

| Layer | File |
|---|---|
| Browser | `portal/renzfi-app.js` |
| HTTP API | `ESP32_S3_Firmware/src/ApiServer.cpp` |
| Local session | `ESP32_S3_Firmware/src/PortalSessionManager.cpp` / `.h` |
| Router worker | `ESP32_S3_Firmware/src/RouterProvisioningWorker.cpp` |
| Router platform | `ESP32_S3_Firmware/src/router/RouterPlatform.cpp` |
| MikroTik driver | `ESP32_S3_Firmware/src/router/drivers/MikroTikDriver.cpp` |
| Config | `ESP32_S3_Firmware/src/Config.h` |

**Not modified:** portal JS/HTML/CSS, `PortalSessionManager`, `ApiServer`, `MikroTikDriver`, `RouterPlatform`, `RouterProvisioningWorker`, `RouterOsClient`, RouterOS configuration, firmware flash, captive-portal rebuild.

---

## Classification standard

| Label | Meaning |
|---|---|
| **PROVEN** | Directly supported by current source **and** the observed hardware evidence |
| **STRONGLY INDICATED** | Supported by current source; hardware correlation still needed for the last boundary |
| **UNPROVEN** | Possible but not demonstrated |
| **RULED OUT** | Contradicted by current source **or** by the observed hardware state |

Every conclusion cites file, function, line range, and the state transition it implies.

---

## 1. Executive summary

There are **two independent authorities** for “this customer still has Internet”:

1. **ESP32 RAM** — `PortalSessionManager` `sessionState=active`, `connected=true`, `secondsLeft>0`, decremented by `tickSessions()` once per `loop()` second.
2. **RouterOS Hotspot Active** — `/ip/hotspot/active` row created by `/ip/hotspot/active/login`, removed by `/ip/hotspot/active/remove` **or by RouterOS itself** when `limit-uptime` (and possibly server `idle-timeout`) expires.

They are **not wired together** on the portal read path.

**PROVEN:** `GET /api/portal/session` and `POST /api/portal/heartbeat` never query `/ip/hotspot/active`. They trust local RAM. The portal API **can and will** return `connected=true` while WinBox Active is empty.

**PROVEN:** The documented ESP32 expiry order is:

```text
secondsLeft hits 0
  → sessionState=expiring, connected=false
  → then queue RouterOS deauthorize (active/remove + user/remove + cookie/remove)
```

The hardware observation is the **opposite order**:

```text
RouterOS Active = empty
Android = “Login and authentication required”
ESP32 / browser = still Connected, secondsLeft apparently > 0
```

That cannot be the ESP32 `tickSessions()` expiry path. If ESP32 had expired first, `connected` would already be `false` and the UI would not paint **Connected**.

**PROVEN architectural root cause:** Internet authorization (RouterOS Active) and portal “Connected” (ESP32 RAM + browser) are allowed to diverge, and the portal has no read-path to notice.

**STRONGLY INDICATED hardware trigger:** RouterOS removed the Active row on its own (most likely user/active `limit-uptime`, possibly server idle-timeout), while `tickSessions()` still had `secondsLeft > 0`.

**RULED OUT as the disconnect trigger:** the 30-second warning in `renzfi-app.js` (UI-only). Pause, terminate, and ESP32-driven expire/deauth (they flip local `connected=false` **before or as** Active is removed).

**Countdown 01:xx → 02:xx** is a **separate** browser/timer defect: the display is a `Date.now()` deadline that can be moved later by `trustFully` (re-entry `/status` init) or by a server `secondsLeft` more than 10 seconds later than the current deadline (stale/out-of-order GET, or ESP32 tick lag vs wall clock). It is **not** a monotonic `localSeconds--`.

---

## 2. Exact reproduction

From the production report:

1. Customer is Connected; remaining time counts down.
2. Remaining time approaches expiration.
3. At approximately 30 seconds remaining, the portal warning appears.
4. A disconnect is observed.
5. Android immediately shows **“Login and authentication required.”**
6. WinBox **HOTSPOT > ACTIVE** is **empty**.
7. WinBox **HOTSPOT > HOSTS** still shows the customer MAC/IP on `hotspot-renzfi`.
8. Customer opens the captive portal again.
9. Portal still shows **Status: Connected** and remaining time.
10. Countdown may jump backward (**01:xx → 02:xx**).
11. Browser continues to reach ESP32:

    - `GET /api/portal/session`
    - `POST /api/portal/heartbeat`
    - `GET /api/portal/branding`

12. Serial shows those HTTP hits from `10.20.0.251` to ESP32 `10.10.10.2`. There are **no** corresponding logs that the ESP32 queried `/ip/hotspot/active` for those heartbeats.

Customer identity:

| Field | Value |
|---|---|
| MAC | `06:36:E3:2C:C4:E8` |
| IP | `10.20.0.251` |
| Hotspot server | `hotspot-renzfi` |
| ESP32 | ProductionReady, Ethernet UP, IP `10.10.10.2` |

---

## 3. Hardware evidence

| Observation | Meaning |
|---|---|
| Active empty | No authenticated Hotspot session. Internet authorization is gone. |
| Host present (`06:36:E3:2C:C4:E8` / `10.20.0.251` / `hotspot-renzfi`) | Device is still a Hotspot LAN host. Host ≠ authenticated. |
| Android “Login and authentication required.” | Android captive-portal helper detected unauthorized / no Internet. |
| Portal Connected + remaining time | Browser is rendering ESP32 local session, not RouterOS Active. |
| Heartbeat + GET `/session` still succeed | Walled-garden / appliance path to `10.10.10.2:80` still works **without** Active. That is expected: portal API is not Internet. |
| No `/ip/hotspot/active/print` on heartbeat | Matches source: heartbeat does not talk to RouterOS. |

Serial pattern (as reported):

```text
[HTTP] Incoming Request  Interface: Ethernet  Client IP: 10.20.0.251  URL: /api/portal/heartbeat
[portal-api] API path: /api/portal/session  Destination: Ethernet
             Server IP: 10.10.10.2  Socket remote: 10.20.0.251:xxxxx
[HTTP] Incoming Request  Interface: Ethernet  Client IP: 10.20.0.251  URL: /api/portal/session
```

**Not reported** in that stream (and would be expected if ESP32 had just expired/deauthed/paused):

```text
[portal-expire] mac=... router=queued
[router-worker] deauthorize-hotspot-user mac=...
[router-budget] operation=terminate/expire
[portal-pause] mac=... router=queued
[router-budget] operation=pause
```

Absence of those lines in the described capture is **STRONGLY INDICATED** evidence that the Active row was not removed by a contemporaneous ESP32 pause/expire/terminate job. It is not a hard proof that those jobs never ran earlier in the session.

---

## 4. RouterOS Active vs Host interpretation

| Table | What it is | Authenticated Internet? |
|---|---|---|
| `/ip/hotspot/host` | Device seen on the Hotspot server (MAC, IP, server) | **No** |
| `/ip/hotspot/active` | Logged-in Hotspot session | **Yes** |
| `/ip/hotspot/user` | Credential / limit-uptime account | Not by itself |
| `/ip/hotspot/cookie` | Auto-login cookie | Can restore Active |

**PROVEN interpretation of the screenshot:** the phone is on the Hotspot LAN and still has a Host row; it is **not** authenticated. Android’s login prompt is consistent with that.

Current firmware **does not query** `/ip/hotspot/host` for portal session state. Host is never used as a proxy for Active.

Grep of `ESP32_S3_Firmware/src` for `/ip/hotspot/host`: **no runtime callers**. Mentions exist only in older firmware docs.

---

## 5. Exact call graph

### 5.1 Payment → Internet

```text
POST /api/portal/done-paying
  ApiServer.cpp ~done-paying handler
  → PortalSessionManager::donePaying()
       secondsLeft = purchasedSeconds (or existingRemaining + purchased)
       sessionState = activating
       connected = false
       routerAuthPending = true
  → onSessionActivated(mac)
       timeoutSeconds = session["secondsLeft"]          [PortalSessionManager.cpp 2348-2350]
       tryEnqueueActivateHotspotUser(user)
  → RouterProvisioningWorker OpType::ActivateHotspotUser  [RouterProvisioningWorker.cpp 1148-1171]
  → RouterPlatform::provisionHotspotUser()
  → MikroTikDriver::authorizeUser() = createHotspotUser()
       /ip/hotspot/user/print
       /ip/hotspot/user/add | /set   limit-uptime=formatLimitUptime(timeoutSeconds)
       loginHotspotActive():
         /ip/hotspot/active/print ?mac-address=
         if active exists: /ip/hotspot/active/set limit-uptime=...   (best-effort)
         else: /ip/hotspot/active/login user,password,mac,ip         (NO limit-uptime attr)
  → HotspotOutcome Activate
  → drainHotspotOutcomes()
       if expected && ok:
         connected = true
         sessionState = active
         emit portal.session.connected
```

`connected=true` is set **only after** `outcome.ok` in `drainHotspotOutcomes()` (`PortalSessionManager.cpp` 2537-2558). That is authorization success, not a later Active poll.

### 5.2 ESP32-driven expiration (documented path — verified in current source)

```text
PortalSessionManager::loop()
  if (now - _lastTickMs >= 1000) tickSessions()          [134-147]

tickSessions()                                           [1996-2036]
  if Active && !paused && secondsLeft == 0
       && !routerCleanupQueued && !routerCleanupComplete:
    sessionState = expiring
    connected = false
    secondsLeft = 0
    routerCleanupQueued = true
    routerCleanupPending = true
    terminationReason = time_expired
    ExpireSession + portal.session.expired

processDeferredWork ExpireSession
  → onSessionExpired(mac)                                [2419-2454]
  → tryEnqueueDeauthorizeHotspotUser(mac)

RouterProvisioningWorker DeauthorizeHotspotUser          [1176-1194]
  → RouterPlatform::disconnectHotspotUser()
  → MikroTikDriver::deauthorizeUser()                    [2673-2732]
       /ip/hotspot/active/print ?mac-address=
       /ip/hotspot/active/remove =.id=
       /ip/hotspot/user/print ?name=
       /ip/hotspot/user/remove
       /ip/hotspot/cookie/print + /remove

drainHotspotOutcomes Kind::Deauthorize                   [2663-2721]
  connected = false
  sessionState = expired (ok) or expiring (!ok)
  routerCleanupComplete = outcome.ok
  if ok: recycleExpiredSessionUnlocked() → idle, zeros
  emit portal.session.expired
```

**Critical order:** local `connected=false` happens in `tickSessions()` **before** Active/remove. The observed hardware (Active empty, portal Connected) is **not** this path.

### 5.3 Portal read path (the path that is firing in the serial log)

```text
GET /api/portal/session?mac=&ip=
  ApiServer.cpp 2910-2926
  → PortalSessionManager::getSession()                   [233-244]
       findOrCreateUnlocked(mac, ip)
       lastSeen = millis()/1000
       enrichSessionPurchasedMinutes   // 0 RouterOS
       enrichSessionCapabilities       // 0 RouterOS, comment line 249
  → sendOk(session JSON)

POST /api/portal/heartbeat
  ApiServer.cpp 3230-3243
  → PortalSessionManager::heartbeat()                    [1064-1073]
       findOrCreateUnlocked(mac, ip)
       lastSeen = nowSec
       optional ipAddress update
  → sendOk { success:true, data:{ok:true}, message:"ok" }
  // JS then GET /session again (see §10)
```

---

## 6. Local ESP32 session lifecycle

### 6.1 Who decrements `secondsLeft`

`tickSessions()` (`PortalSessionManager.cpp` 1996-2017):

- Only if `sessionState == "active"` **and** `paused == false`.
- Portal (non-voucher): `secondsLeft = secs - 1` once per tick.
- Voucher only: may clamp to `serviceExpiresEpoch - time(nullptr)` if wall clock is valid (`>= 1704067200`).
- Tick cadence: `loop()` sets `_lastTickMs = now` and calls `tickSessions()` when `now - _lastTickMs >= 1000` (`134-147`). If `loop()` stalls 5 s, **one** decrement is applied, not five.

**PROVEN:** ESP32 paid-session expiry is **loop-second** based, not wall-clock based (except vouchers).

### 6.2 When `connected` flips false locally

| Cause | `connected` | `sessionState` | Then RouterOS |
|---|---|---|---|
| `tickSessions` time_expired | `false` **first** | `expiring` | deauth queued |
| `terminateSession` | `false` immediately | `expiring` | ExpireSession |
| `reset` / owner disconnect | `false` immediately | `expiring` | ExpireSession |
| `pause()` | stays until pause outcome; pause outcome sets `false` | `paused` immediately | active+cookie remove, **user kept** |
| `donePaying` | `false` until Activate outcome | `activating` | activate job |
| Activate outcome ok | `true` | `active` | already logged in |
| Deauthorize outcome | `false` | `expired` / `expiring` | already removed |

### 6.3 `findOrCreateUnlocked` (`2247-2283`)

- Existing MAC → return it (update IP if changed).
- Missing MAC → create **idle** session: `secondsLeft=0`, `connected=false`, `sessionState=idle`.

GET/heartbeat **can create** a blank idle record. They **cannot invent purchased time**. They **will return** an existing paid Active record forever until `tickSessions`/terminate/recycle changes it.

### 6.4 Recycle (`2289-2330`, called from deauth success `2711`)

Only after **successful** deauthorize outcome, and only if `secondsLeft==0`, no credits, no coin window, `routerCleanupComplete`, source ≠ voucher.

After recycle the MAC is Idle / Waiting Payment. GET cannot resurrect paid time **after that**. The production bug is that recycle **never runs** because ESP32 never reached `secondsLeft==0`.

---

## 7. RouterOS lifecycle

### 7.1 How `limit-uptime` is assigned

`onSessionActivated` copies `session["secondsLeft"]` into `HotspotUser.timeoutSeconds` (`2348-2350`) at enqueue time.

`MikroTikDriver::formatLimitUptime` (`342-349`) formats `HH:MM:SS` with integer hours/minutes/seconds. No extra rounding beyond integer seconds.

`createHotspotUser` (`2521`, `2553`, `2577`) writes `=limit-uptime=` on **user add/set**.

`loginHotspotActive` (`2427-2478`):

- If an Active row already exists (Add Time): `/ip/hotspot/active/set =limit-uptime=` — **best-effort**; trap is ignored (`2449-2453`). Activation still returns `true`.
- If not: `/ip/hotspot/active/login` with user, password, mac, ip — **no `limit-uptime` attribute**. Active inherits the user’s limit.

**PROVEN:** initial authorization and Add Time use different Active operations (`login` vs `set`). Add Time Active update is explicitly best-effort.

### 7.2 When the two clocks start

| Clock | Start | What it counts |
|---|---|---|
| ESP32 `secondsLeft` | Frozen during `activating`. Starts decrementing only after Activate outcome sets `sessionState=active` (`2537-2558` + `tickSessions` 1996). | One decrement per `loop()` second while Active and unpaused |
| RouterOS user `limit-uptime` | Written on user add/set, **before** `active/login` in the same API session | RouterOS remaining login time (vendor semantics: counted while logged in) |
| RouterOS Active | Created at `active/login` (or updated at `active/set`) | Session uptime vs limit |

They do **not** start at the exact same instruction. Delay between user set and login is milliseconds inside one RouterOS API session. Delay between login success and ESP32 `connected=true` is worker outcome drain (loop later). During `activating`, ESP32 does **not** decrement, so ESP32 should if anything be **behind** RouterOS by the activating duration, not ahead — **unless** `loop()` later misses ticks, in which case ESP32 `secondsLeft` **lags behind wall time** and stays **higher** than RouterOS remaining.

### 7.3 Who can empty Active

`/ip/hotspot/active/remove` exists in **one** firmware function: `MikroTikDriver::removeHotspotActiveByMac` (`2365-2378`).

Callers of that function:

| Caller | Function | Keeps user? | Local ESP32 state when it runs |
|---|---|---|---|
| `pauseHotspotUser` | `2633-2670` | Yes | `sessionState=paused` already (`pause()` 801-804) |
| `deauthorizeUser` | `2695` | No (user+cookie also removed) | `connected=false`, `expiring` already |

Worker wrappers:

- `RouterPlatform::disconnectHotspotUser` → `deauthorizeUser` (`606-608`)
- `RouterPlatform::pauseHotspotUser` → `pauseHotspotUser` (`611-613`)

ESP32 enqueue sources for those jobs:

| Local API | Work item | Router op |
|---|---|---|
| `tickSessions` time_expired | ExpireSession | deauthorize |
| `terminateSession` | ExpireSession | deauthorize |
| `reset` | ExpireSession | deauthorize |
| `administerVoucher` expire/disable | ExpireSession | deauthorize |
| coin window expired with no credits | ExpireSession | deauthorize |
| boot recovery expiring/expired | ExpireSession | deauthorize |
| `pause()` | PauseSession | pause (active+cookie) |
| `retryPendingRouterWork` | Expire/Activate/Pause | matching |

**Firmware does not set** `idle-timeout`, `keepalive-timeout`, or hotspot profile `session-timeout`. Hotspot profile ensure only sets `html-directory` (`RouterWirelessAdapter.cpp` 277-338). RouterOS **defaults** (commonly `idle-timeout=5m`) remain whatever is on the device.

**Therefore Active can become empty without a matching ESP32 `connected=false` if and only if RouterOS itself logs the user out** (limit-uptime, idle-timeout, keepalive, manual WinBox, or a failed Add Time `active/set` leaving a shorter Active limit).

---

## 8. Browser lifecycle

`portal/renzfi-app.js` is a renderer. Connected paint:

```text
renderStatus() 1307-1337
  Connected  iff  sessionState==="active"
              &&  connected
              &&  secondsLeft > 0
              &&  not paused
```

`state.connected` / `state.sessionState` / `state.secondsLeft` come from the last `applyNormalizedSession()` of GET `/session` or SSE — **never** from RouterOS.

Timers:

| Timer | Interval | Clears previous? |
|---|---|---|
| `mainTimer` | 1000 ms | `clearInterval` in `startMainTimer` (1258) |
| `heartbeatTimer` | `HEARTBEAT_MS=10000` | `clearInterval` in `startHeartbeat` (1288) |
| `coinPollTimer` | `COIN_POLL_MS=2000` | only while coin modal |
| `waitForActivation` | 250 ms local + 2000 ms HTTP GET | during activating only |
| EventSource `/api/events` | push | one instance; reconnect on error |

`startMainTimer()` always clears first. **Multiple main countdown intervals are RULED OUT** as long as all starts go through `startMainTimer` (init, done-paying success, resume success).

**Multiple GET `/session` in flight are PROVEN possible:** no request id, no abort, no mutex. Overlap sources: heartbeat (10 s) + `syncSessionFromServer`, coin poll (2 s), `waitForActivation` HTTP (2 s), init `fetchSession`, SSE does not GET but `applyNormalizedSession` can interleave with GET.

No `visibilitychange` / `pageshow` / `pagehide` handlers.

Init (`1479-1511`):

1. Restore `localStorage` **frozen** (`timerRunning=false`).
2. `fetchSession()` with **`trustFully=true`**.
3. `startHeartbeat()`.

Re-entry via `/status` is a **new document**. That forces the server snapshot onto the deadline.

---

## 9. Timer model

### 9.1 What the browser actually does

Comments at `44-49` and `283-289` state the contract: ESP32 owns `secondsLeft`; the browser stores a wall-clock deadline.

```text
anchorSession(seconds, running, force)     291-310
  candidate = Date.now() + secs * 1000
  jitter = already running
        && candidate > sessionExpiryAt
        && (candidate - sessionExpiryAt) <= 10s
  if (force || !jitter) sessionExpiryAt = candidate

displaySeconds()                           312-315
  if running: ceil((sessionExpiryAt - Date.now()) / 1000)
  else: sessionFrozenSeconds

applyNormalizedSession                     405-407
  state.secondsLeft = session.secondsLeft   // always
  anchorSession(..., trustFully)
```

**Not used:** `localSeconds--`. **Not used:** `serverSeconds - elapsedClientTime` except insofar as the deadline is `Date.now()+serverSeconds` at apply time.

### 9.2 When the displayed value can increase

| Condition | Effect |
|---|---|
| `trustFully=true` | Always adopts server seconds, even if larger |
| `trustFully=false` and later deadline ≤ +10 s | **Keeps earlier deadline** (monotonic) |
| `trustFully=false` and later deadline **> +10 s** | Adopts later deadline (**display jumps up**) — treated as Add Time |
| Later GET with **smaller** seconds | Adopts earlier deadline (jump down) |

`trustFully=true` callers:

- `init` first fetch (`1494`)
- `waitForActivation` HTTP fallback (`943`)
- coin start / done-paying / other user actions that call `applyNormalizedSession(session, true)`
- SSE `portal.coin.credit` only (`handleSessionPush` `581`)

Heartbeat path: `syncSessionFromServer` → `applyNormalizedSession(session, false)` (`626-629`).

**PROVEN:** 01:xx → 02:xx is possible if a payload with ~60 extra seconds is applied with `trustFully` **or** with `trustFully=false` and delta > 10 s.

Most natural hardware path for **re-entry**: previous page displayed ~01:xx (wall clock from an older snapshot). New `/status` `init()` `trustFully=true` applies current ESP32 `secondsLeft` (still ~02:xx if ESP32 ticks lagged or the previous tab had run ahead). That is **STRONGLY INDICATED** for the reported reopen, not yet packet-proven.

### 9.3 Out-of-order GET

No sequence numbers.

Example:

```text
Request A: server 120s
Request B: server 60s
B arrives first → display ~60
A arrives second, trustFully=false, candidate is ~60s later → >10s threshold
  → sessionExpiryAt replaced → display jumps toward 120
```

**PROVEN in source** that this can happen. **UNPROVEN** that this specific production capture was an out-of-order pair (would need request ids / timestamps).

---

## 10. Heartbeat model

### Firmware (`ApiServer.cpp` 3230-3243, `heartbeat()` 1064-1073)

- Requires mac (if empty, still `sendOk` without calling heartbeat).
- `findOrCreateUnlocked` + `lastSeen` + optional IP.
- **Does not** mutate `secondsLeft`, `connected`, `sessionState`, `paused`.
- **Does not** query RouterOS.
- **Does not** keep Hotspot Active alive (RouterOS does not see this HTTP).
- Returns `{ success: true, data: { ok: true }, message: "ok" }` via `sendOk(req, "ok")` (`308-320`).

`lastSeen` is used for:

- owner Active Users **coin-window presence** (`isPortalSessionActive` 1406-1412)
- `cleanupExpired` unpaid/idle/waiting rows (`1515-1558`)

`cleanupExpired` **explicitly must not erase** active/activating/paused entitlement on stale heartbeat (`1554-1556`). Heartbeat TTL does **not** expire a paid session.

`PORTAL_HEARTBEAT_STALE_SEC = 120` (`Config.h` 304) is a dashboard/idle-record constant, not an Internet keep-alive.

### Browser (`heartbeat` 1278-1291)

```text
POST /heartbeat  → unwrap {ok:true}
THEN GET /session (syncSessionFromServer, trustFully=false)
every 10 seconds
```

Heartbeat does **not** itself `renderStatus()`. The follow-up GET does, via `applyNormalizedSession` → `render()`.

**Current envelope is compatible.** Older mismatch reports do not apply to this source: `unwrapPortalResponse` requires `success===true` and uses `data`; firmware provides `data.ok=true`.

**Answer to Q7:** heartbeat is **A + H** — touch `lastSeen` only, then the JS triggers GET `/session`. Not B/C/D as a heartbeat body. Not a RouterOS keepalive.

---

## 11. GET `/session` model

### Request

`GET /api/portal/session?mac=&ip=` — mac required (`ApiServer.cpp` 2910-2926).

### Response fields (RAM session + enrich)

From the stored object plus `enrichSessionCapabilities` (`246-284`):

| Field | Source |
|---|---|
| `sessionState` | RAM |
| `connected` | RAM |
| `secondsLeft` | RAM |
| `paused` | RAM |
| `credits` / `insertedAmount` / `purchasedMinutes` | RAM (+ enrich fallback) |
| `timerRunning` | derived: `active && !paused && secondsLeft>0` |
| `canInsertCoin` / `canPause` / `canResume` / `canTerminate` / `canReconnect` | derived, **0 RouterOS** |
| `pausesUsed` / `pausesRemaining` / `pauseLimit` | RAM + constant |
| `routerCleanupPending` / `routerCleanupComplete` | RAM (present on object; UI does not use them for Connected) |
| `lastSeen` | overwritten on every GET |

### Can GET return `connected=true` when Active is empty?

**YES. PROVEN.**

`getSession` never calls `active/print`. Comment at `enrichSessionCapabilities` line 249: **“ESP32-local only — 0 RouterOS commands.”**

That is a **critical state-model inconsistency** with Internet authorization.

### Does GET create a paid session?

| Existing RAM | GET behavior |
|---|---|
| None | Creates idle, `secondsLeft=0`, `connected=false` |
| Active paid | Returns it, bumps `lastSeen` |
| Paused | Returns paused |
| Expiring / expired | Returns that (until recycle after successful deauth) |
| Recycled idle | Returns idle zeros |

**GET cannot accidentally mint purchased minutes.** It **can** keep serving a paid-looking Active row after RouterOS has already revoked Internet. That is stale **persistence**, not recreation of a new sale.

A customer who lost authorization **can** reopen `/status`, GET `/session`, and see Connected for as long as ESP32 `secondsLeft>0` and `sessionState=active`.

---

## 12. 30-second warning path

**Only** in `portal/renzfi-app.js`.

Flags: `warnedAt30Seconds`, `warnedAt15Seconds` (`71-72`).

`updateSessionNotice` (`830-850`):

- Requires `sessionState==="active" && connected && !paused`.
- At 30 s: sets `warnedAt30Seconds`, `showSessionNotice("30 seconds remaining. Insert more coins...")`.
- At 15 s: similar 15-second copy.
- `showSessionNotice` only writes DOM + 5-tick hide (`816-821`).

Two call sites:

1. `applyNormalizedSession` (`421`) using **server** `state.secondsLeft`.
2. `startMainTimer` (`1267`) using **display** `displaySeconds()`.

Reset when session becomes active or `secondsLeft` increases (`409-416`).

**PROVEN: the warning does not** call terminate, pause, deauthorize, reset, RouterOS, or expire. It does not change `connected`.

The ~30 s timing in the reproduction is **coincidental with approaching expiry**, not a side-effect of the warning. Classification: **RULED OUT** as the disconnect cause.

---

## 13. State matrix

Legend: **Y** = present/true, **N** = absent/false, **?** = depends / can disagree.

| # | Phase | Local `sessionState` | `connected` | `secondsLeft` | ROS user | ROS Active | ROS cookie | Browser UI |
|---|---|---|---|---|---|---|---|---|
| 1 | Before payment | `idle` / `waiting_coin` | N | 0 | N (or leftover) | N | maybe leftover | Disconnected / Waiting |
| 2 | Activating | `activating` | N | frozen >0 | Y (just added/set) | pending login | — | Activating… |
| 3 | Connected | `active` | Y | ticking | Y | **Y expected** | often Y | Connected + countdown |
| 4 | Paused | `paused` | N after pause ok | frozen >0 | Y | **N** (removed) | N (cleared) | Paused |
| 5 | Resumed | `activating` then `active` | N then Y | frozen then tick | Y | Y after login | — | Activating then Connected |
| 6 | 30 s warning | `active` | Y | ~30 | Y | **should still be Y** | Y | Connected + notice |
| 7 | **ROS Active removed, ESP32 not expired** | **`active`** | **Y** | **>0** | **often still Y** | **N** | maybe | **Connected + time** |
| 8 | ESP32 expired | `expiring` then `expired`/`idle` | N | 0 | removed if deauth ok | N | N | Disconnecting / Disconnected |
| 9 | Terminated | `expiring` immediately | N | 0 | removed | N | N | Disconnecting |
| 10 | Add Time | `activating` then `active` | N then Y | increased | Y limit updated | set best-effort | — | Activating then Connected |
| 11 | Browser reload | unchanged RAM | unchanged | unchanged | unchanged | unchanged | — | init trustFully paints RAM |
| 12 | Re-entry `/status` | unchanged RAM | unchanged | unchanged | unchanged | **may already be N** | — | **Connected if RAM still active** |

**Row 7 is the observed production state.** It is a designed hole: five layers are allowed to disagree, and the portal read path only looks at layer 1.

Other disagreement rows:

| Disagreement | When |
|---|---|
| Active empty, UI Paused | Pause — **expected** |
| Active present, UI Activating | Login succeeded, outcome not drained yet — brief |
| User exists, Active empty, UI Connected | **Row 7 — defect** |
| Add Time: user limit new, Active limit old | `active/set` trap ignored |
| Browser display ≠ `state.secondsLeft` | jitter keep-earlier vs always-assign server field |

---

## 14. Race-condition analysis

### Q1 — Can RouterOS remove Active **before** ESP32 sets `connected=false`?

**YES. PROVEN as possible; STRONGLY INDICATED as what happened.**

ESP32 only removes Active **after** local `connected=false` (expire/terminate/pause). RouterOS can also remove Active **without asking ESP32**:

1. User/active `limit-uptime` exhausted (set at activate to the same integer seconds, then counted on RouterOS wall clock).
2. Hotspot server idle/keepalive timeouts (not written by this firmware; device default may apply).
3. Add Time `active/set` failed → Active keeps a shorter limit than ESP32 `secondsLeft`.
4. Manual WinBox.

ESP32 will not notice until `secondsLeft` hits 0.

### Loop-tick vs wall clock

If `loop()` misses ticks, ESP32 `secondsLeft` stays **higher** than real elapsed time. RouterOS `limit-uptime` still consumes wall time. RouterOS can hit 0 while ESP32 still shows 30–90 s. Browser display, being wall-clock from the last snapshot, can show ~30 s (warning) at the same moment RouterOS logs out.

That composite is **STRONGLY INDICATED** for “warning then Android login required.” It is not packet-proven without `[session-timer]` before/after logs and a RouterOS Active remaining print.

### Outcome races already guarded

Late Activate outcome will not revive expired sessions (`drainHotspotOutcomes` `2530-2534`). That guard does **not** help when Active dies and local state is still `active`.

---

## 15. Stale-session analysis

| Question | Answer | Class |
|---|---|---|
| Can GET create a blank session? | Yes, idle zeros | PROVEN |
| Can GET recreate paid minutes after recycle? | No | PROVEN |
| Can GET keep serving Connected after Active is gone? | Yes, until local expire | PROVEN |
| Can localStorage resurrect Connected without GET? | Cache is frozen; Connected paint needs `connected && active && secondsLeft>0` from a payload. Init then GET trustFully. Cache alone does not keep a running Connected clock. | PROVEN |
| After successful deauth+recycle, can GET show Connected? | No — idle/0 | PROVEN |
| After RouterOS-only logout, can GET show Connected? | **Yes — this bug** | PROVEN |

---

## 16. Countdown rollback analysis

Observed: **01:xx → 02:xx** (display increased by ~60 s). Impossible for a single decrementing counter.

Mechanisms that can do that in current JS:

| Mechanism | Class | Notes |
|---|---|---|
| `/status` init `trustFully=true` adopting larger server `secondsLeft` | STRONGLY INDICATED | Matches “customer opens portal again” |
| Heartbeat GET with server > display+10 s | PROVEN possible | ESP32 tick lag or stale in-flight GET |
| Out-of-order GET A overwriting B | PROVEN possible | No sequence numbers |
| SSE coin credit `trustFully` | RULED OUT unless a coin arrived | Would also raise credits |
| Add Time | UNPROVEN for this capture | Would be a real top-up |
| Multiple `mainTimer` intervals | RULED OUT | `clearInterval` first |
| 30 s warning | RULED OUT | Does not touch the deadline |
| `localSeconds--` bug | RULED OUT | No such counter |
| Heartbeat mutating `secondsLeft` | RULED OUT | Heartbeat does not |

`renderMainTimer` uses `displaySeconds()`, not `state.secondsLeft`. The jump is `sessionExpiryAt` moving later.

---

## 17. Root-cause candidates

| ID | Candidate | Class |
|---|---|---|
| C1 | Dual authority: ESP32 `tickSessions` vs RouterOS `limit-uptime` / server timeouts | **PROVEN (architecture)**; **STRONGLY INDICATED (this outage)** |
| C2 | GET `/session` + heartbeat never observe Active | **PROVEN** (why UI stays Connected) |
| C3 | Browser re-entry `trustFully` +/or >10 s upward sync | **PROVEN (mechanism)**; **STRONGLY INDICATED (01→02 on reopen)** |
| C4 | Add Time `active/set` best-effort failure leaving short Active limit | **UNPROVEN** (possible; not shown in this capture) |
| C5 | 30 s warning disconnects RouterOS | **RULED OUT** |
| C6 | ESP32 expire-then-deauth already ran | **RULED OUT** for the Connected observation (would set `connected=false` first) |
| C7 | Pause path | **RULED OUT** for Connected UI (`paused` paints Paused) |
| C8 | Terminate / owner reset | **RULED OUT** (`connected=false` immediately) |
| C9 | GET recreates a new paid session | **RULED OUT** (idle zeros only) |
| C10 | Host confused with Active in firmware | **RULED OUT** (no `/ip/hotspot/host` session logic) |
| C11 | Heartbeat API envelope mismatch | **RULED OUT** in current source |
| C12 | Android reconnect drops Active | **UNPROVEN** |
| C13 | Hotspot `idle-timeout` (device default, not set by firmware) | **UNPROVEN** without `/ip/hotspot/print` of the live server |
| C14 | Multiple browser countdown intervals | **RULED OUT** |

---

## 18. Proven root cause

### What is PROVEN

**The portal and RouterOS do not share one authoritative session lifecycle.**

1. **Internet authorization** is the RouterOS `/ip/hotspot/active` row. When it is gone, Android correctly demands login. Host remaining does not authorize.
2. **Portal Connected** is ESP32 RAM `sessionState=active && connected=true && secondsLeft>0`, painted by `renderStatus()`.
3. **Those two bits are updated on different paths.** `connected=true` is set once on Activate outcome. It is cleared by ESP32 expire/pause/terminate — **not** by RouterOS independently logging the client out.
4. **GET `/session` and heartbeat never read Active.** They will keep returning Connected for this MAC until `tickSessions` hits 0.
5. The documented order “ESP32 expires first, then deauth” is **true in source** and **false as an explanation of this hardware capture**. The capture is Active-gone / ESP32-still-connected.

That dual-authority hole **is** the root cause of the Connected-after-revoke symptom. It is not a CSS/label bug and must not be “fixed” by hiding Connected in JS alone.

### What is STRONGLY INDICATED but not packet-proven

**Why Active became empty while `secondsLeft` was still > 0:**

RouterOS independently ended the Active session (limit-uptime wall clock running ahead of `tickSessions`, and/or a shorter Active limit after a best-effort `active/set`, and/or server idle-timeout).

Supporting source:

- `limit-uptime` is always written to the user at activate (`createHotspotUser`).
- ESP32 decrement misses wall time when `loop()` stalls (`loop` 144-147, `tickSessions` 2013-2014).
- No continuous Active poll exists.

Supporting hardware:

- Active empty + Host present + Android login required + portal Connected + heartbeat still reaching ESP32.
- No described `[portal-expire]` / `[router-budget] operation=terminate` in the heartbeat storm.

### What is PROVEN as the countdown mechanism, STRONGLY INDICATED for 01→02

Re-entry `/status` → `init()` `trustFully=true` reapplies ESP32 `secondsLeft`, which can be larger than the previous tab’s wall-clock display. Independently, `SYNC_JUMP_THRESHOLD=10` will adopt any later server value >10 s as a “top-up”.

---

## 19. Ruled-out causes

| Cause | Why |
|---|---|
| 30-second warning triggers disconnect | `updateSessionNotice` only sets DOM text |
| Changing Hotspot profiles / NAT / firewall | Not in this path; Active empty with Host present is session logout, not L3 vanish |
| Portal UI “stuck Connected” as a local-only paint bug | GET `/session` is still succeeding and returning RAM connected; UI matches API |
| Heartbeat keep-alive failure causing RouterOS logout | Heartbeat is not a RouterOS keepalive; it only sets ESP32 `lastSeen` |
| GET recreating a sale after expire | `findOrCreateUnlocked` creates idle zeros; paid fields only from existing RAM |
| Firmware confusing Host with Active | No host query in session code |
| Pause showing as Connected | `renderStatus` paints Paused when `paused` or `sessionState==="paused"` |
| ESP32 `tickSessions` already at 0 | Then API would return `connected=false` / expiring; contradicts Connected |
| Multiple `setInterval` main timers | `startMainTimer` clears first |
| `visibilitychange` resurrecting state | No such listener |

---

## 20. Evidence required for remaining unproven boundaries

Do **not** add these logs in this pass. Minimum hardware capture to close C1’s last inch and C4/C13:

### ESP32 (next firmware pass, not now)

```text
[session-sync] mac= local_state= connected= secondsLeft= paused= routerCleanupPending=
[session-timer] mac= before= after= state= loop_gap_ms=
[portal-api-session] mac= state= connected= secondsLeft=  // already implied by getSession; stamp it
[portal-api-heartbeat] mac= result= lastSeen=
[router-deauth] mac= active_found= active_id= remove_ok=
[activate] limit-uptime= timeoutSeconds=  // already partially logged as remaining=
```

One correlated sample **at the moment Android shows login required**:

- ESP32 `secondsLeft` and `sessionState`
- Whether any `[portal-expire]` / `[portal-pause]` / `[router-budget]` fired in the prior 60 s

### RouterOS (WinBox / export — no config change)

At the same moment:

```text
/ip/hotspot/active/print where mac-address=06:36:E3:2C:C4:E8
/ip/hotspot/host/print where mac-address=06:36:E3:2C:C4:E8
/ip/hotspot/user/print where name=<stripped MAC>
/ip/hotspot/print
  (idle-timeout, keepalive-timeout, login-timeout on hotspot-renzfi)
```

Needed to split **limit-uptime expiry** vs **idle-timeout** vs **user gone**.

### Browser (devtools, no code)

```text
[portal-js-session] requestId= receivedAt= serverSeconds= localDisplay= sessionState= connected= trustFully=
```

Waterfall of overlapping GET `/session` around the 01→02 jump. Confirms trustFully re-entry vs stale GET vs tick lag.

Until those exist:

- C4 Add Time `active/set` trap = **UNPROVEN**
- C13 idle-timeout = **UNPROVEN**
- Exact missed-tick count = **UNPROVEN**
- Exact 01→02 trigger (init vs heartbeat) = **UNPROVEN** (mechanism is proven)

---

## 21. Recommended fix boundary — NO IMPLEMENTATION

Do **not** hide Connected in `renzfi-app.js` as the fix. The browser is faithfully rendering a lying API.

### Authority rule (product)

**One lifecycle.** When Internet authorization is gone, portal state must become disconnected/expired (or paused, if that was the intent) **deterministically**. When the session is active, remaining time must be monotonic.

### Preferred boundary (do not implement here)

Pick **one** timer owner:

**Option A — ESP32 remains owner of expiry (matches existing `tickSessions` docs)**

- Stop giving RouterOS a competing deadline: do not set a finite user/active `limit-uptime` that can elapse before ESP32 hits 0 (or set it strictly longer / unused).
- ESP32 expire path already deauths; that path is correct **when it runs**.
- Still need a **read-path reconcile** so a RouterOS-side logout (idle-timeout, admin, radio roam) cannot leave RAM Connected: on heartbeat or a coalesced worker job, `active/print` for that MAC and if missing while local `active&&connected`, transition local state. Must obey single Router worker + no RouterOS from `async_tcp` + no aggressive polling.

**Option B — RouterOS Active is owner of Internet**

- Portal GET `/session` must not return `connected=true` unless Active exists (or a cached observation with a bounded TTL and a worker refresh).
- ESP32 `secondsLeft` becomes presentation remaining, derived from a **wall-clock expiry epoch** (voucher already has `serviceExpiresEpoch`) so tick lag cannot outlive RouterOS.

Either option, not both.

### Timer / GET rules (same pass later)

- GET `/session` must never recreate paid entitlement after expire/terminate (already true for minutes; keep it).
- GET `/session` must not keep `connected=true` after authorization is gone (today it does).
- Browser should sequence GET responses (ignore older `requestId`) and should not `trustFully` a Connected payload on re-entry if a reconcile says Active is gone.
- `SYNC_JUMP_THRESHOLD` upward adopt is why 01→02 can happen without Add Time; monotonic display needs an expiry **timestamp** from the server, not a fresh `secondsLeft` re-anchor.
- 30 s warning stays UI-only.
- Do not change pause/resume/terminate semantics, coin/promo, Hotspot profiles, or the setup wizard.

### Explicitly out of bounds for the eventual fix unless proven in §20

- Rewriting `RouterOsClient` login protocol
- MikroTik NAT/firewall/Hotspot server redesign
- Flashing / portal rebuild in the forensic pass (this pass)

---

## Concise boundary table

| Boundary | Finding | Classification | Evidence |
|---|---|---|---|
| RouterOS Active | Empty means unauthenticated. Host remaining is normal. Firmware Active/remove only on pause/deauth. RouterOS can empty Active on its own via `limit-uptime` (and possibly idle-timeout). | **PROVEN** (empty ≠ Host; firmware remove callers). **STRONGLY INDICATED** (independent ROS logout this capture) | WinBox Active empty + Host present; `MikroTikDriver.cpp` 2365-2378, 2443-2478, 2633-2732; no `/ip/hotspot/host` in src |
| ESP32 local session | Still `active`/`connected`/`secondsLeft>0` while Active is gone. `tickSessions` expires **local first** then deauths — opposite of observation. Decrements 1/loop-second, not wall clock. | **PROVEN** | `tickSessions` 1996-2036; `loop` 144-147; GET still Connected; no expire-first in capture |
| `/api/portal/session` | Trusts RAM via `findOrCreateUnlocked`. 0 RouterOS. Can return `connected=true` with no Active row. Creates idle zeros if missing; does not mint paid time. | **PROVEN** | `ApiServer.cpp` 2910-2926; `getSession` 233-244; `enrichSessionCapabilities` 246-249 |
| heartbeat | Sets `lastSeen` only; `{ok:true}`; JS then GET `/session`. Not a RouterOS keepalive. Does not refresh `secondsLeft`. | **PROVEN** | `ApiServer.cpp` 3230-3243, 308-320; `heartbeat` 1064-1073; `renzfi-app.js` 1278-1285 |
| `renzfi-app.js` timer | `Date.now()` deadline; `trustFully` or >10 s later server value jumps display up. One `mainTimer`. Overlapping GETs have no sequence id. Connected paint uses RAM flags, not Active. | **PROVEN** (mechanism). **STRONGLY INDICATED** (01→02 on `/status` re-entry) | `anchorSession` 291-310; `init` 1489-1494; `renderStatus` 1328-1330; `SYNC_JUMP_THRESHOLD=10` |
| 30-sec warning | UI notice only. No deauth/expire/pause/terminate. | **RULED OUT** as disconnect cause | `updateSessionNotice` 830-850 |
| expiration/deauth | Source order: `connected=false` then Active/remove. Observation is Active empty while Connected. This capture is **not** the ESP32 expire path. | **PROVEN** (order in source vs hardware) | `tickSessions` 2021-2032 then `onSessionExpired` 2419; contrast WinBox + UI |
| stale/recreation | GET will not invent a new paid session. GET **will** keep a still-paid RAM session after ROS logout. Recycle only after successful ESP32 deauth. | **PROVEN** | `findOrCreateUnlocked` 2247-2283; `recycleExpiredSessionUnlocked` 2289-2330, 2711 |

---

*End of forensic. No implementation in this pass.*
