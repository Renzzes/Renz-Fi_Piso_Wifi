# Captive Portal Timer Stop / Admin Empty / MikroTik Still Active

**Date:** 2026-08-25  
**Mode:** SOURCE-LEVEL + HARDWARE EVIDENCE — **NO CODE CHANGES IN THIS PASS**  
**Firmware:** `0.5.0-w5500`  
**Scope:** Prove why the captive portal remaining time **stops**, why Status paints **Disconnected**, why Admin **Active Users is empty**, while MikroTik HotSpot **Active is still running**.

This is the **reverse** of `docs/RENZFI_SESSION_DESYNC_EXPIRY_FORENSIC.md` (that incident: portal Connected, WinBox Active empty). Same split of authorities; opposite painted side.

**Not modified:** portal JS/HTML, `PortalSessionManager`, `ApiServer`, Admin pages, MikroTik config, firmware flash.

---

## Classification standard

| Label | Meaning |
|---|---|
| **PROVEN** | Directly supported by current source **and** this capture |
| **STRONGLY INDICATED** | Supported by source; last boundary not printed on serial for this capture |
| **UNPROVEN** | Possible, not demonstrated |
| **RULED OUT** | Contradicted by source or by the capture |

---

## 1. Hardware capture (authoritative)

Same customer, same moment (~10:19–10:20 PH, 2026-08-25).

| Surface | Observation |
|---|---|
| **MikroTik HotSpot → Active** | User `123931C84C30`, MAC `12:39:31:C8:4C:30`, IP `10.20.0.181`, uptime `00:24:50`, **session time left `00:05:10`**, idle 2s, Rx 230 bps |
| **Captive portal** `10.20.0.1/status` | Status **Disconnected**, remaining **`00:18:50` (not moving)**, credits ₱0.00, same MAC/IP, Add Additional Time / Pause (3 left) / Terminate still shown |
| **Admin** `10.10.10.2/active-users` | **0 active sessions**, empty table |
| **Admin User History (Day)** | One sales row: id/code `1e2f56331e0ec`, MAC `12:39:31:C8:4C:30`, start `2026-08-25T21:54:27` |

Sale start 21:54 + MikroTik uptime 24:50 ≈ 22:19 (10:19 PM). The clocks of **session start** and **RouterOS Active uptime** agree. The 2026 date is the appliance calendar date, not an NTP “future” failure.

---

## 2. Executive answer

There are **three independent authorities**. None of them read the others on the status/list path.

| Authority | What “still going” means | This capture |
|---|---|---|
| **A — RouterOS HotSpot Active** | `/ip/hotspot/active` row exists; `session-time-left ≈ limit-uptime − uptime` | **Running** (5:10 left) |
| **B — ESP32 RAM** `PortalSessionManager` | `sessionState`, `connected`, `secondsLeft`, `timerRunning` | **Not listing as active** (Admin empty) |
| **C — Browser** `renzfi-app.js` | Paints Status / remaining from last payload or **localStorage cache** | **Disconnected**, remaining **frozen at 18:50** |

**PROVEN root cause:** Internet (Clock A) is not the source of truth for the portal countdown or for Admin Active Users. Both B and C stop the visible timer whenever `timerRunning` is false. Admin Active Users is **not** `/ip/hotspot/active`; it is `isPortalSessionActive()` over ESP32 RAM, which **excludes Idle and Expired**. User History is the **sales ledger**, not the live session table.

The customer still has Internet because nobody removed the HotSpot Active row. The portal timer stopped because the browser is not allowed to tick unless firmware says `timerRunning`. Admin is empty because ESP32 no longer classifies that MAC as an active portal session (or the browser never received a live Active snapshot).

This is **not** MikroTik “losing” the session. MikroTik is the only plane that is still correct.

---

## 3. What “Connected” and “the timer runs” actually require

### 3.1 Firmware (`enrichSessionCapabilities`)

`ESP32_S3_Firmware/src/PortalSessionManager.cpp`:

```text
timerRunning = sessionState == active
            && !paused
            && secondsLeft > 0
            && connected == true
```

`tickSessions()` only advances `secondsLeft` / `expiresAtMs` when:

```text
sessionState == active && !paused && connected == true
```

If `connected` is false **or** state leaves `active`, ESP32 **freezes** remaining time. RouterOS **does not**.

### 3.2 Browser (`renderStatus` / `displaySeconds`)

`portal/renzfi-app.js`:

```text
Connected  ⇔  sessionState === "active" && connected && secondsLeft > 0
otherwise Status falls through to "Disconnected"
  (unless activating / waiting_coin / paused / expiring / activation_error)
```

```text
displaySeconds():
  if !sessionTimerRunning → return sessionFrozenSeconds   // STOPPED
  else → ceil((sessionExpiryAt - Date.now()) / 1000)
```

`applySessionClock()` sets `sessionTimerRunning` only when firmware `timerRunning && connected && !paused && secondsLeft > 0`.

**PROVEN:** A remaining time of `00:18:50` that does not move is exactly `!sessionTimerRunning` with `sessionFrozenSeconds = 1130`. It is not a CSS glitch and not MikroTik idle-timeout.

---

## 4. Why Admin Active Users is empty while User History has the MAC

### 4.1 Active Users

`GET /api/users` → `fillActiveUsers()` → `PortalSessionManager::appendActiveUsers()`.

Filter `isPortalSessionActive()`:

| `sessionState` | Listed? |
|---|---|
| `active` with `secondsLeft > 0` | Yes (does **not** require `connected`) |
| `paused` / `activating` / `activation_error` with time | Yes |
| `waiting_coin` | Only if coin window **and** heartbeat fresh (≤ 120 s) |
| **`idle`** | **No** |
| **`expired`** | **No** |

**PROVEN:** Admin “0 active sessions” means ESP32 RAM did not pass that filter. It does **not** mean HotSpot Active is empty.

The empty-state copy *“There are currently no connected WiFi sessions”* is **wrong as a product sentence**. The API never asked MikroTik.

### 4.2 User History

User History is `GET /api/sales/records` filtered by day. The help text on the page says it is **sales persistence**, same ledger as Sales Reports.

Row `1e2f56331e0ec` matches `makeSessionId()` (`esp_random` hex + `millis` hex), i.e. a **coin session id**, not proof the portal session is still `active`.

**PROVEN:** History showing the MAC only proves **Done Paying recorded a sale at 21:54**. It does not keep the customer in Active Users.

---

## 5. Why MikroTik still shows 5:10 while the portal shows 18:50 frozen

Two proven, stacking effects.

### 5.1 Clock A frozen, Clock B still running

Once ESP32/`timerRunning` stops, portal remaining is a snapshot. RouterOS Active `uptime` keeps increasing:

```text
session-time-left ≈ limit-uptime − uptime
```

Arithmetic on this capture:

```text
Sale start          21:54:27
MikroTik uptime     00:24:50     → wall ~22:19
Portal remaining    00:18:50     frozen
MikroTik remaining  00:05:10     still counting
```

If entitlement was ~30 minutes from first login:

```text
30:00 − 24:50 = 05:10     // matches WinBox
18:50 frozen              // Clock A stopped ~11 minutes after start
24:50 − 11:10 ≈ 13:40     // Clock B kept going after the freeze
18:50 − 13:40 = 05:10     // matches the split
```

**PROVEN direction:** portal remaining **ahead** of MikroTik remaining is what you get when Clock A stops and Clock B does not. It is the same class as `docs/RENZFI_SESSION_CLOCK_FORENSIC_2026-08-15.md`, with an extra freeze (not only leftover-Active uptime).

### 5.2 Leaving Active does not log the customer out of HotSpot

These firmware paths set `sessionState = idle` and/or `connected = false` **without** `ExpireSession` / `/ip/hotspot/active/remove` when paid time still exists:

| Path | File | What it does to RouterOS |
|---|---|---|
| `cancelModal()` after Add Additional Time | `PortalSessionManager.cpp` | Coin window closed; **Idle**; **no deauth** |
| `tickSessions()` coin window expiry with `credits > 0` | same | **Idle** + `connected=false`; **no deauth** |
| `closeOtherCoinWindowsUnlocked()` | same | Other MAC’s waiting_coin → **Idle**; **no deauth** |
| `recoverSessionsAfterReboot()` open coin window | same | **Idle** + `connected=false`; **no deauth**; does not re-activate a previously Active paid session that only had a coin window open |

`cleanupExpired()` *does* restore `Active` when a waiting_coin heartbeat goes stale **and** `secondsLeft > 0`. `cancelModal()` and coin-window timeout with credits **do not**. That inconsistency is in source today.

**PROVEN:** Internet can remain granted after ESP32 no longer treats the MAC as an Active Users row.

---

## 6. Why `/status` paints Disconnected with a frozen timer even before firmware answers

`portal/renzfi-app.js` `init()`:

1. `state = loadCachedState()` from localStorage.
2. `anchorSession(state.secondsLeft, false)` — **always freeze**.
3. `render()` immediately.
4. Then `fetchSession()`; only a successful payload with `timerRunning` unfreezes the clock.

`loadCachedState()`:

- Sets `timerRunning: false` on purpose (*“Never resume ticking from cache”*).
- Restores `secondsLeft` and `sessionState`.
- **Does not restore `connected`** (property omitted → `undefined` → falsy).
- Defaults `canPause` / `canInsertCoin` / `canTerminate` to **true** unless explicitly `false`.

`renderStatus()` therefore paints **Disconnected** on every `/status` re-entry until a live payload has `connected: true` and `sessionState: "active"`.

If `fetchSession()` / heartbeat then **fails** (timeout, 503 `ETH_DMA_LOW`, walled-garden miss), `catch` only calls `startMainTimer()`. The frozen cache remains: **Disconnected**, remaining stuck, Pause still visible.

**PROVEN for the portal screenshot shape.** Concurrent Admin on `10.10.10.2` plus portal on `10.20.0.1` is the documented DMA pressure class (`ESP32_S3_Firmware/docs/ADMIN_PORTAL_MULTI_CLIENT_DMA_GURU_FORENSIC.md`). Failed or delayed `/api/portal/session` is **STRONGLY INDICATED** as the reason the live `connected=true` snapshot never replaced the cache.

HTML default on `status.html` is also `Disconnected` / `00:00:00`. A painted `00:18:50` is **not** the HTML default; it is cached or last firmware `secondsLeft`.

---

## 7. What this capture is not

| Hypothesis | Verdict |
|---|---|
| Customer pause | **RULED OUT** — UI would say **Paused**, Admin would list `paused` |
| Owner Disconnect (`suspendInternet`) | **RULED OUT** — same: listed as paused, time preserved in Active Users |
| Activation error / verify `not_active` | **RULED OUT** as the painted Status — that path sets `activation_error` and **RETRY INTERNET**, and Admin still lists the row |
| ESP32 `tickSessions` expiry first | **RULED OUT** — expiry zeros `secondsLeft` and enqueues deauth; WinBox would not still show 5:10 Active |
| Admin UI filter hiding the row | **RULED OUT** — `ActiveUsersPage` renders the full `/api/users` array |
| “2026 date means NTP is wrong” | **RULED OUT** — capture date is 2026-08-25 |
| Portal remaining and MikroTik remaining being the same clock | **RULED OUT** — source + 18:50 vs 5:10 |

---

## 8. Causal chain for this incident

```text
21:54  Coin Done Paying → sale 1e2f56331e0ec recorded
       MikroTik /ip/hotspot/active/login  (Clock B starts)
       ESP32 sessionState=active, connected=true (Clock A starts)

~22:05 Clock A stops (connected=false and/or leave Active,
       and/or /status re-entry cache with timerRunning forced false)
       Remaining snapshot 00:18:50
       Clock B keeps running

22:19  WinBox: uptime 24:50, session-time-left 05:10
       Portal /status: Disconnected + frozen 18:50
       Admin Active Users: isPortalSessionActive == false → 0 rows
       User History: sale row still present (expected)
```

**STRONGLY INDICATED last firmware transition for Admin empty:** `sessionState` became `idle` (Add Additional Time cancel / coin-window timeout with leftover entitlement / boot recovery of an open coin window) **without** HotSpot remove.

**PROVEN last browser transition for frozen Disconnected:** `timerRunning` false and `connected` falsy, which `/status` init **forces** from cache until a live Connected payload arrives.

---

## 9. Files (read-only)

| Layer | File |
|---|---|
| Browser | `portal/renzfi-app.js` (`loadCachedState`, `applySessionClock`, `displaySeconds`, `renderStatus`, `init`) |
| Portal HTML | `portal/status.html` (default Disconnected) |
| Session RAM | `ESP32_S3_Firmware/src/PortalSessionManager.cpp` (`enrichSessionCapabilities`, `tickSessions`, `isPortalSessionActive`, `cancelModal`, `startCoinWindow`, `findSessionUnlocked`) |
| Admin API | `ESP32_S3_Firmware/src/ApiServer.cpp` (`GET /api/users` → `fillActiveUsers`) |
| Admin UI | `src/pages/ActiveUsersPage.tsx` (empty state vs User History sales) |
| Related | `docs/RENZFI_SESSION_DESYNC_EXPIRY_FORENSIC.md`, `docs/RENZFI_SESSION_CLOCK_FORENSIC_2026-08-15.md` |

---

## 10. Remediation (implemented)

1. **Do not Idle a paid session** on coin-window cancel, insert timeout, other-window close, or boot recovery. `restorePaidSessionAfterCoinWindowUnlocked()` restores `active` + `connected` (when `hadRouterAuth`) and rebuilds `expiresAtMs` so `timerRunning` continues. ExpireSession on window timeout runs only when `secondsLeft == 0`.
2. **Admin list:** Idle with leftover `secondsLeft` remains visible. MAC lookup ignores colon/case so `/status` cannot create a second idle record.
3. **Portal `/status`:** cache no longer defaults Pause/Terminate on; `connected` is restored for display; status is **Connecting…** until firmware hydrates; the countdown still starts only from live `timerRunning`.
4. **User History** copy on Active Users clarifies sales vs live sessions.

Still not done (by design): portal GET does not poll `/ip/hotspot/active` (no RouterOS on the read path). Verify-login remains the correction if HotSpot was actually removed.
