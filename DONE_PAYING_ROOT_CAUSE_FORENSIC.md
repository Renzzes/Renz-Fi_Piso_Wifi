# DONE_PAYING Root Cause Forensic

Date: 2026-08-10  
Scope: Investigation only — **no code changes**  
Symptom: Coins accumulate (PHP2 / 10 min) → Done Paying → `done-paying begin` → internet never granted → UI stays Waiting for Payment  
Verdict: **Backend exits early on wall-clock gate; RouterWorker / MikroTik never reached**

---

## 1. Executive conclusion (proven)

| Question | Answer |
|---|---|
| Exact stopping function | `PortalSessionManager::donePaying()` |
| Exact stopping lines | `ESP32_S3_Firmware/src/PortalSessionManager.cpp` **452–459** |
| Condition | `salesRecordedAtNow()` returns empty `String` |
| Log evidence | `[ERROR] portal: donePaying: wall clock not ready — sale not recorded` |
| Does this only skip sales? | **No.** It `return false` and aborts the **entire** activation path |
| RouterWorker reached? | **No** |
| RouterOS authorize commands executed? | **No** |
| HTTP response | **400** `NO_CREDITS` / “No credits to convert…” (misleading) |
| Frontend | Receives failure / never gets success → remains Waiting for Payment |
| Confidence | **Very high (≈95%)** |

Primary forensic hypothesis (**wall clock aborts activation, not merely sales**) is **confirmed by source control flow**.

---

## 2. Complete execution timeline (observed + source)

```
Customer inserts coins
  → credits / purchasedMinutes updated (working)
  → Portal shows Waiting for Payment + credits

Customer presses Done Paying
  → Frontend POST /api/portal/done-paying { mac }

ApiServer handler
  → log portal.done-paying
  → heap before done-paying
  → PortalSessionManager::donePaying(mac)

donePaying:
  → Serial "[portal] done-paying begin"          ← OBSERVED
  → lock; find session; credits>0; minutes>=1    ← must pass (credits shown)
  → unlock
  → _storage present                             ← must pass (salesSaved=false would log differently)
  → recordedAt = salesRecordedAtNow()
  → recordedAt.isEmpty() == true
  → Serial "[portal] salesSaved=false"
  → Logger ERROR "wall clock not ready — sale not recorded"  ← OBSERVED
  → return false                                 ← STOPS HERE

ApiServer:
  → sendError(400, "No credits to convert…", "NO_CREDITS")

Never executed:
  → sessionState = activating
  → credits = 0
  → enqueueRecordSale
  → enqueueActivateSession
  → processDeferredWork ActivateSession
  → onSessionActivated → RouterWorker tryEnqueueActivateHotspotUser
  → MikroTik hotspot user/active authorize
  → "[portal] done-paying complete"
  → HTTP 200 "Session activating"
```

---

## 3. Complete function call graph

### Intended success path (not reached after clock fail)

```
Captive Portal / Final_Build_Portal JS
  finishCoinPayment / handleDonePaying
    → POST /api/portal/done-paying
        ApiServer.cpp (~2946–2980)
          → PortalSessionManager::donePaying(mac)   [PortalSessionManager.cpp:373]
               → (credits/minutes OK)
               → salesRecordedAtNow()               [SalesTime.cpp:71]
               → ★ FAIL empty → return false
               ✗ enqueueRecordSale
               ✗ enqueueSaveSessions
               ✗ enqueueActivateSession
               ✗ enqueueEmitBus
          ✗ getSession + sendOk
          → sendError 400 NO_CREDITS

Would-be continuation (only if donePaying returns true):
  PortalSessionManager::processDeferredWork
    → ActivateSession → onSessionActivated
         → _routerWorker->tryEnqueueActivateHotspotUser(...)
              → RouterWorker → MikroTikDriver authorize path
                 (/ip/hotspot/user …, /ip/hotspot/active …)
```

### Exact abort site (source)

```452:459:ESP32_S3_Firmware/src/PortalSessionManager.cpp
  recordedAt = salesRecordedAtNow();
  if (recordedAt.isEmpty()) {
    Serial.println("[portal] salesSaved=false");
    if (_logger) {
      _logger->error("portal",
                     "donePaying: wall clock not ready — sale not recorded");
    }
    return false;
  }
```

**Note:** Message says “sale not recorded”, but the `return false` occurs **before** any session mutation to `Activating` and **before** `enqueueActivateSession` (lines 537–586). Activation is aborted, not deferred.

---

## 4. Activation state machine

Firmware portal states (`PortalState` in `PortalSessionManager.h`):

| State | Meaning |
|---|---|
| `idle` | No active entitlement |
| `waiting_coin` | Coin window / accumulating credits |
| `activating` | Entitlement reserved; router auth pending |
| `active` | Connected / timer running |
| `paused` | Timer frozen |
| `activation_error` | Router auth failed; may retry |
| `expiring` / `expired` | Teardown |

(UI labels like “Waiting for Payment” map to waiting_coin / idle with credits, not a separate firmware enum.)

| | |
|---|---|
| **Current state (at Done Paying)** | `waiting_coin` (or idle-like with credits>0) — credits=2, purchasedMinutes=10 |
| **Expected next** | `activating` → (RouterWorker) → `active` / connected |
| **Actual next** | **Unchanged** (still waiting / credits retained) |
| **Why** | Early `return false` before `session["sessionState"] = PortalState::Activating` |

---

## 5. Investigation C — Early returns in `donePaying` (ordered)

| Line region | Condition | Log | Reached in this failure? |
|---|---|---|---|
| ~385–388 | session null | aborted (session not found) | No (coins worked) |
| ~392–395 | voucher source | rejected for voucher | No |
| ~401–416 | credits≤0 (non-idempotent) | no credits / idempotent | No (credits=2) |
| ~419–423 | pending activation/sale | idempotent | No (no prior enqueue) |
| ~436–439 | minutes&lt;1 | no purchased minutes | No (10 min) |
| ~444–449 | `!_storage` | storage unavailable — sale not recorded | No (different log) |
| **~452–459** | **`salesRecordedAtNow()` empty** | **wall clock not ready** | **YES** |
| ~467–469 | session gone after re-lock | silent false | Unreachable |
| ~475–490 | credits raced away | aborted/idempotent | Unreachable |
| ~503–506 | minutes&lt;1 under lock | aborted | Unreachable |
| ~558–579 | enqueueRecordSale fail | failed to queue sale | Unreachable |
| ~583–585 | enqueueActivateSession fail | markActivationEnqueueFailed | Unreachable |

---

## 6. Investigation D — RouterWorker queue

| Check | Result |
|---|---|
| `enqueueActivateSession` called? | **No** (only after clock OK + sale enqueue) |
| Job created? | **No** |
| Worker awakened for hotspot activate? | **No** |
| Cancelled/rejected/timeout? | N/A — never enqueued |

Proven: activation enqueue is at lines 583–586, after the clock gate.

---

## 7. Investigation E — RouterOS commands

No authorize path runs unless `onSessionActivated` → `tryEnqueueActivateHotspotUser`. That is never scheduled.

Therefore **zero** Done-Paying-triggered RouterOS hotspot authorize commands for this attempt. Idle/background RouterWorker behavior is unchanged and unrelated.

---

## 8. Investigation F — HTTP response

```2966:2979:ESP32_S3_Firmware/src/ApiServer.cpp
        if (_portalSessions->donePaying(mac)) {
          ...
          sendOk(req, heapOut.doc(), "Session activating");
        } else {
          sendError(req, 400,
                    "No credits to convert — insert coins first", "NO_CREDITS");
        }
```

| | |
|---|---|
| Status | **400** |
| Code | **NO_CREDITS** |
| Message | Misleading — credits exist; clock failed |
| Contrast | Voucher redeem maps same clock miss to **503** `CLOCK_NOT_READY` |

---

## 9. Investigation G — Frontend

### Captive Portal (`Captive Portal/renzfi-app.js`)

`finishCoinPayment` → POST `/done-paying` → `.then` applies session / success sound / hotspot auth; `.catch` only `markApiFailure()`.

On 400: promise rejects → **no success path** → coin modal flow does not advance to Connected; status remains payment-waiting UI.

### Final_Build_Portal (`Final_Build_Portal/renzfi-app.js`)

`handleDonePaying` → `donePayingAPI()` → on success may `waitForActivation(35000)`; on catch shows service/error notice.

**Conclusion:** Frontend is **not** ignoring a 200 success. It **never receives success**. Backend exited early.

---

## 10. Investigation H — Session persistence (variables)

| Field | Before Done Paying | After clock abort |
|---|---|---|
| `credits` | 2 (example) | **Unchanged** (mutation not reached) |
| `purchasedMinutes` | 10 | **Unchanged** |
| `secondsLeft` | typically 0 while waiting | **Unchanged** |
| `sessionState` | `waiting_coin` | **Unchanged** |
| `connected` | false | **Unchanged** |
| `routerAuthPending` | false | **Unchanged** |
| `activation` / Activating | not set | **not set** |

Unlock at line 442 occurs **before** the clock check; no Activating write occurs on this failure path.

---

## 11. Investigation I — Activation atomicity / sales vs activation

**Proven:** Wall-clock failure aborts **activation**, not sales-only.

Evidence:

1. Clock check is before any `sessionState = Activating` (537).
2. Clock check is before `enqueueRecordSale` (558) and `enqueueActivateSession` (583).
3. `return false` causes API 400; no `"done-paying complete"`.
4. Log text is **misleading**; control flow is hard-fail.

Why clock is empty (`SalesTime.cpp`):

```71:76:ESP32_S3_Firmware/src/SalesTime.cpp
String salesRecordedAtNow() {
  if (!installationAllowsNtp()) return "";
  salesTimeBegin();
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 500)) return "";
  if (timeinfo.tm_year + 1900 < 2024) return "";
```

Empty when:

1. Installation not ready (`!isReady()` → NTP deferred), **or**
2. NTP/`getLocalTime` not ready within 500 ms, **or**
3. Clock year still &lt; 2024.

`FirmwareApp` only calls `salesTimeBegin()` when `_installation.isReady()`. Until NTP sync completes, Done Paying hard-fails even though coin accumulation works (coins do not require wall clock for credit math).

---

## 12. Investigation J — CPU / RouterOS stability (analysis only)

This failure **does not** involve extra RouterOS polling. A future fix must **not**:

- add RouterOS heartbeats / polling,
- busy-wait on NTP inside async HTTP handlers,
- increase RouterWorker load on the failure path,

and should keep idle RouterOS command rate at **0**/min when idle. (No fix proposed here.)

---

## 13. Evidence matrix

| Evidence | Agrees with clock-abort root cause? |
|---|---|
| Log: `done-paying begin` then wall-clock ERROR | Yes |
| No `done-paying complete` | Yes |
| No RouterWorker / `[activate] router` logs | Yes |
| UI stuck Waiting for Payment | Yes (400 / catch) |
| Credits still visible after failure | Yes (session not cleared) |
| SD writable / portal / coins working | Compatible (orthogonal) |

No conflicting evidence found in source for an alternate stop **after** this return on the same attempt.

---

## 14. Answers to deliverable checklist

1. **Timeline** — §2  
2. **Call graph** — §3  
3. **State machine** — §4  
4. **Root cause** — `salesRecordedAtNow()` empty → `donePaying` returns false before activation enqueue  
5. **Log evidence** — wall clock ERROR immediately after begin  
6. **Stopping function** — `PortalSessionManager::donePaying`  
7. **Stopping lines** — **452–459** `PortalSessionManager.cpp`  
8. **Vars before** — credits/minutes present; waiting_coin  
9. **Vars after** — unchanged; not activating  
10. **RouterWorker reached?** — **No**  
11. **RouterOS commands?** — **No** (for this Done Paying)  
12. **Frontend waiting forever?** — Waiting on failed activation / payment UI; not ignoring 200  
13. **Backend exited early?** — **Yes**  
14. **Confidence** — **≈95%** (source-proven abort; remaining ~5% is which of the three SalesTime empty reasons applies on the specific appliance at that boot)

---

## 15. What is *not* the root cause (ruled out for this symptom)

- SD WRITE_PROBE / READ_ONLY (orthogonal; coins already worked)
- Missing credits / promo math (credits and minutes present)
- RouterWorker queue full (never enqueued)
- MikroTik API down (never called for authorize)
- Frontend ignoring HTTP 200 (no 200 issued)

---

## 16. Investigation status

**COMPLETE.** Exact proven root cause identified with source + log evidence.  

**NO IMPLEMENTATION** in this document.
