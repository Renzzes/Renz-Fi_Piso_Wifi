# Investigation B — Phase 1 Runtime Boundary Forensic

**Scope:** Done Paying → RouterWorker → MikroTik Internet activation  
**Mode:** Forensic only  
**Source changes:** None  
**Runtime trace supplied:** None

## 1. Final Verdict

**EXACT HARDWARE ROOT CAUSE NOT YET PROVEN.**

The repository contains enough source evidence to identify every possible
activation boundary, but no captured hardware log for a failing occurrence is
present in the available terminal output.

Therefore this investigation cannot truthfully answer:

- Whether RouterWorker received the failed activation.
- Which RouterOS command executed last.
- Whether `/ip/hotspot/active/login` was requested.
- Whether RouterOS accepted or rejected it.
- Whether the worker published an outcome.
- Whether the browser merely retained stale `Activating…`.

Declaring enqueue failure, worker admission, mailbox overwrite, RouterOS login,
or UI state as the actual hardware cause without that trace would be guessing.

### One previously supplied signature remains proven

If the failing occurrence contains:

```text
[portal] salesSaved=false
donePaying: wall clock not ready — sale not recorded
```

then execution stops at:

```text
ESP32_S3_Firmware/src/PortalSessionManager.cpp
PortalSessionManager::donePaying()
lines 316-323
```

That request never reaches session reservation, activation queue, RouterWorker,
or RouterOS.

This proven signature must not be generalized to failures where authoritative
backend state actually remains `activating`.

## 2. Current Evidence Availability

A search of the available terminal captures found no matching runtime lines for:

```text
done-paying
portal-activate
activate-hotspot-user
salesSaved
active/login
```

Source code was inspected, but source establishes possible paths—not which path
occurred on the physical device.

## 3. Activation Checkpoint Trace

| # | Boundary | Source | Existing evidence marker | Can current logs prove passage? |
|---:|---|---|---|---|
| 1 | Done Paying click | `portal/renzfi-app.js:648-692` | None persisted; browser UI only | Browser Network panel required |
| 2 | POST received | `ApiServer.cpp:2832-2869` | Request diagnostics `portal.done-paying` | Yes, if serial request diagnostics captured |
| 3 | Session function entered | `PortalSessionManager.cpp:242-243` | `[portal] done-paying begin` | Yes |
| 4 | Session validation | `PortalSessionManager.cpp:252-304` | Specific abort/idempotent messages | Failure only |
| 5 | Storage gate | `PortalSessionManager.cpp:308-314` | `salesSaved=false` plus logger message | Failure only |
| 6 | Clock gate | `PortalSessionManager.cpp:316-323` | `salesSaved=false` plus wall-clock logger message | Failure only |
| 7 | Reservation committed | `PortalSessionManager.cpp:357-414` | No immediate marker | Not directly |
| 8 | RecordSale admitted | `PortalSessionManager.cpp:418-440` | Failure marker only | Failure only |
| 9 | SaveSessions admitted | `PortalSessionManager.cpp:442` | Generic queue-full marker only | No positive proof |
| 10 | ActivateSession admitted | `PortalSessionManager.cpp:443` | Generic queue-full marker only | No positive proof |
| 11 | Done Paying returns local success | `PortalSessionManager.cpp:446-460` | `[portal-session] ... state=activating`; `done-paying complete` | Yes |
| 12 | HTTP success serialized/sent | `ApiServer.cpp:2853-2863` | Before/after JSON and send markers | Yes |
| 13 | Activation dequeued | `PortalSessionManager.cpp:1045-1067` | No dequeue marker | No |
| 14 | `onSessionActivated()` entered | `PortalSessionManager.cpp:1448-1450` | `[STACK] activation entry` | Yes, but lacks MAC |
| 15 | RouterWorker admission attempted | `PortalSessionManager.cpp:1478` | No attempt marker | No |
| 16 | RouterWorker admission accepted | `RouterProvisioningWorker.cpp:252-277` | Worker dispatch and portal queued markers | Yes |
| 17 | RouterWorker task dequeued | `RouterProvisioningWorker.cpp:1256-1280` | `[router-worker] started type=...` | Yes |
| 18 | RouterPlatform called | `RouterProvisioningWorker.cpp:1084-1096` | No dedicated platform marker | Driver entry proves passage |
| 19 | MikroTik credentials loaded | `MikroTikDriver.cpp:2492-2503` | Router step 1 | Yes |
| 20 | RouterOS session opened | `MikroTikDriver.cpp:2513-2521` | Router step 2 plus login diagnostics | Yes |
| 21 | User print executed | `MikroTikDriver.cpp:2523-2535` | Router step 3 + Router API START/END | Yes |
| 22 | User add/set executed | `MikroTikDriver.cpp:2540-2590` | Router step 4 + Router API START/END | Yes |
| 23 | Active print executed | `MikroTikDriver.cpp:2426-2438` | Router API START/END | Yes |
| 24 | Active login/set executed | `MikroTikDriver.cpp:2442-2477` | Router API START/END | Yes |
| 25 | Router authorization result | `MikroTikDriver.cpp:2596-2617` | Router budget `ok=yes/no` | Yes |
| 26 | Worker outcome attempted | `RouterProvisioningWorker.cpp:1084-1096` | Worker result log occurs after publication call | Attempt only |
| 27 | Outcome accepted by PortalSessionManager | `PortalSessionManager.cpp:1590-1649` | `[portal-activate] mac=... ok=...` | Yes |
| 28 | Browser receives state | `/api/portal/session` + JS polling | Browser Network response | Browser capture required |

## 4. Exact Current Activation Sequence

```text
Browser main thread
  handleDonePaying()
  status text becomes Activating...
  POST /api/portal/done-paying

AsyncTCP / AsyncWebServer callback
  ApiServer route
  PortalSessionManager::donePaying()
  validate session/credits/purchasedMinutes
  obtain sale timestamp
  reserve credits and purchased time
  state = activating
  enqueue RecordSale
  enqueue SaveSessions
  enqueue ActivateSession
  return HTTP 200 with local session

Arduino loopTask
  PortalSessionManager::loop()
  drainHotspotOutcomes()
  processDeferredWork()
  process RecordSale/SaveSessions before activation in FIFO order
  dequeue ActivateSession
  onSessionActivated()
  build HotspotUser from MAC/IP/secondsLeft/profile
  tryEnqueueActivateHotspotUser()

RouterWorker admission
  zero-wait dispatch mutex
  reject if running
  copy payload to shared worker slot
  send one wake byte to queue depth 1

FreeRTOS router_worker task
  receive wake byte
  set running/type
  runOp(ActivateHotspotUser)
  RouterPlatform::provisionHotspotUser()
  MikroTikDriver::authorizeUser()
  MikroTikDriver::createHotspotUser()

RouterOS API
  open one API session
  /ip/hotspot/user/print
  /ip/hotspot/user/add OR /ip/hotspot/user/set
  /ip/hotspot/active/print
  /ip/hotspot/active/login OR /ip/hotspot/active/set
  close API session

router_worker
  publish HotspotOutcome
  log operation result

Arduino loopTask
  drainHotspotOutcomes()
  success -> sessionState=active, connected=true
  failure -> sessionState=activation_error, connected=false

Browser
  existing activation polling reads authoritative session
  active + connected -> Connected
  activation_error -> Activation failed
```

## 5. State Ownership

### Browser state

File:

```text
portal/renzfi-app.js
```

`handleDonePaying()` sets visible `Activating…` before receiving the backend
result.

Consequences:

- Browser `Activating…` does not prove backend reservation.
- Browser `Activating…` does not prove RouterWorker admission.
- Browser `Activating…` does not prove RouterOS activity.

The backend session response must be captured separately.

### Backend session state

File:

```text
ESP32_S3_Firmware/src/PortalSessionManager.cpp
```

Possible authoritative paths:

```text
waiting_coin
  -> pre-reservation rejection
  -> credits preserved

waiting_coin
  -> activating
  -> active

waiting_coin
  -> activating
  -> activation_error

waiting_coin
  -> activating
  -> no activation completion observed
  -> activating indefinitely
```

### Internet state

Creating or updating `/ip/hotspot/user` does not grant access.

Initial Internet authorization is requested at:

```text
/ip/hotspot/active/login
```

File:

```text
ESP32_S3_Firmware/src/router/drivers/MikroTikDriver.cpp
MikroTikDriver::loginHotspotActive()
lines 2426-2477
```

For Add Time on an already active row:

```text
/ip/hotspot/active/set
```

The active-set result is currently best effort and ignored by the driver.

## 6. RouterWorker State Determination

### Did RouterWorker receive the reported failed activation?

```text
UNKNOWN — no matching runtime trace was supplied or found.
```

Required proof of admission:

```text
[router-worker] dispatch type=activate-hotspot-user priority=critical
[portal-activate] mac=<MAC> job=queued ...
```

Required proof that the worker task dequeued it:

```text
[router-worker] started type=activate-hotspot-user
```

Required proof that worker execution completed:

```text
[router-worker] activate-hotspot-user mac=<MAC> ok=yes|no
[router-worker] finished type=activate-hotspot-user ...
```

Required proof that PortalSessionManager consumed the result:

```text
[portal-activate] mac=<MAC> ok=yes|no
```

These are separate checkpoints. A worker result line does not prove the
one-slot outcome was later consumed.

## 7. RouterOS Command State Determination

### Did the commands execute on the reported failed activation?

```text
UNKNOWN — no matching runtime trace was supplied or found.
```

Expected source markers:

```text
[activate] router step 1 — load credentials
[activate] router step 2 — open RouterOS session
[activate] router step 3 — print existing hotspot user
[router-api] START
/ip/hotspot/user/print
[router-api] END ...

[activate] router step 4 — add new hotspot user
or
[activate] router step 4 — update existing hotspot user

[router-api] START
/ip/hotspot/user/add
or
/ip/hotspot/user/set
[router-api] END ...

[activate] router step 5 — authorize hotspot active
[router-api] START
/ip/hotspot/active/print
[router-api] END ...

[router-api] START
/ip/hotspot/active/login
or
/ip/hotspot/active/set
[router-api] END ...

[router-budget] operation=activate commands=4 session=1 ok=yes|no ...
```

Router API failure markers already distinguish:

- Not connected.
- Not logged in.
- Job deadline expired.
- DMA headroom rejection.
- Write failure.
- Read failure.
- `!trap`.
- `!fatal`.

Therefore no additional per-command logging is recommended.

## 8. Proven Source Risks That Remain Competing Hypotheses

These mechanisms are proven by source but are not proven as the observed
hardware cause.

### Activation deferred-queue admission failure

`donePaying()` checks RecordSale admission but ignores:

```cpp
enqueueSaveSessions();
if (activate) enqueueActivateSession(mac);
enqueueEmitBus("sessions.changed");
```

If activation admission fails after state was set to `activating`, no worker
operation exists.

### RouterWorker shared-slot window

The worker queue contains one wake byte; payload remains in shared `_slot`.
There is a scheduling window between wake enqueue and worker `_running=true`
where another producer can overwrite `_slot`.

### Outcome mailbox loss

Only one Hotspot outcome exists. Publication can time out on its mutex or
overwrite an undrained prior result.

### Lost worker-busy retry

The portal re-enqueue result at the end of `onSessionActivated()` is ignored.
A full portal queue can discard the retry and its timeout budget.

### Reboot recovery gap

Persisted `activating` sessions are not re-enqueued or converted to a recoverable
state during boot recovery.

### Storage blocking before activation

RecordSale and SaveSessions are FIFO predecessors of ActivateSession.
Their synchronous storage operations have no enforced operation timeout.

### UI optimistic/stale state

The browser sets Activating before HTTP success and performs an unawaited
recovery synchronization in its error path. This can show Activating when
backend activation did not start.

## 9. Minimum Instrumentation Recommendation

No instrumentation was implemented.

Existing RouterOS and RouterWorker command logs are already sufficient after
worker admission. The missing evidence is concentrated around portal queue
admission and outcome delivery.

### Recommended temporary event-level markers

#### Marker 1 — reservation and queue admission

File/function:

```text
ESP32_S3_Firmware/src/PortalSessionManager.cpp
PortalSessionManager::donePaying()
```

One line after all three enqueue attempts:

```text
[activation-trace] mac=<MAC> reserved=yes saleQ=<0|1> saveQ=<0|1> activateQ=<0|1>
```

Purpose:

- Proves backend entered activating.
- Proves whether activation entered the portal queue.

Requirement:

- Capture each enqueue return value.
- Do not add a loop or retry.

#### Marker 2 — activation dequeued

File/function:

```text
ESP32_S3_Firmware/src/PortalSessionManager.cpp
PortalSessionManager::processDeferredWork()
```

Only for `ActivateSession`:

```text
[activation-trace] mac=<MAC> portalQ=dequeued
```

Purpose:

- Separates queue admission from loopTask processing.

#### Marker 3 — first RouterWorker rejection reason

File/function:

```text
ESP32_S3_Firmware/src/PortalSessionManager.cpp
PortalSessionManager::onSessionActivated()
```

Only when `firstAttemptMs == 0`:

```text
[activation-trace] mac=<MAC> worker=busy-or-rejected firstAttempt=yes
```

Purpose:

- Confirms admission rejection without logging every retry.

Do not log each immediate retry; that would create serial pressure and alter
timing.

#### Marker 4 — outcome publication failure/overwrite

File/function:

```text
ESP32_S3_Firmware/src/RouterProvisioningWorker.cpp
RouterProvisioningWorker::publishHotspotOutcome()
```

Only on exceptional conditions:

```text
[activation-trace] outcome=dropped reason=mutex-timeout
[activation-trace] outcome=overwrite oldMac=<...> newMac=<...>
```

Purpose:

- Distinguishes RouterOS completion from outcome delivery loss.

### Browser capture

No frontend source instrumentation is required for Phase 1.

Capture from the browser Network panel:

1. `POST /api/portal/done-paying` status and JSON.
2. First `GET /api/portal/session` after the response.
3. Session JSON at 5, 20, and 35 seconds.

Required fields:

```text
sessionId
credits
purchasedMinutes
secondsLeft
sessionState
connected
routerAuthPending
activationError
```

### Instrumentation impact

- Four event-level marker locations.
- No RouterOS command.
- No polling.
- No timer.
- No task.
- No queue.
- No memory buffer.
- No flash/SD write.
- No reconnect.
- No repeated login/logout.

Serial output itself consumes CPU and UART time. Therefore markers must be
limited to one per boundary or exceptional failure and removed after diagnosis.

## 10. Add Time Verification

Current source path:

```text
accepted denomination
  -> coin-specific configured minutes
  -> session["purchasedMinutes"] accumulation
  -> Done Paying saleMinutes
  -> purchasedSeconds
  -> existingRemaining + purchasedSeconds
  -> session["secondsLeft"]
  -> HotspotUser.timeoutSeconds
  -> RouterOS limit-uptime
```

Example:

```text
PHP1 = 5 minutes
PHP1 = 5 minutes
PHP5 = 10 minutes

credits = PHP7
purchasedMinutes = 20
activation/add-time seconds = 1200
```

Preview and activation read the same stored `purchasedMinutes`.

Source verdict:

```text
CORRECT
```

Hardware grant remains subject to the same activation trace under investigation.
No Promo, coin, or frontend change is recommended.

## 11. Minimal Localized Fix Recommendation

No fix can be selected until the trace identifies the failed boundary.

### If queue admission is proven

Potential file/function:

```text
ESP32_S3_Firmware/src/PortalSessionManager.cpp
PortalSessionManager::donePaying()
```

Localized direction:

- Check the existing activation enqueue result.
- Do not add retries.
- Do not add RouterOS work.
- Preserve purchased seconds in a recoverable state.

Risk:

```text
Medium — accounting and session-state rollback
```

### If worker admission is proven

Potential files:

```text
ESP32_S3_Firmware/src/RouterProvisioningWorker.cpp
ESP32_S3_Firmware/src/RouterProvisioningWorker.h
```

Risk:

```text
High — shared RouterOS serialization path
```

Do not modify until a trace proves this boundary.

### If RouterOS command failure is proven

Potential file:

```text
ESP32_S3_Firmware/src/router/drivers/MikroTikDriver.cpp
```

Risk:

```text
High — Hotspot compatibility and RouterOS command budget
```

Do not add commands or retries. Use the existing command response to determine
the smallest compatibility correction.

### If backend rejects before activation

Potential files:

```text
ESP32_S3_Firmware/src/PortalSessionManager.cpp
ESP32_S3_Firmware/src/SalesTime.cpp
ESP32_S3_Firmware/src/ApiServer.cpp
```

Risk:

```text
Medium/high — sales durability and idempotency
```

### If frontend only is proven

Canonical:

```text
portal/renzfi-app.js
```

Generated/upload:

```text
deployment/mikrotik-hotspot/renzfi-app.js
```

Upload only:

```text
Files/hotspot/renzfi-app.js
```

## 12. Regression Analysis

The investigation changed no runtime code and therefore introduces no runtime
regression.

For any future localized fix:

| Subsystem | Required invariant |
|---|---|
| Coin ISR | No timing, debounce, or pulse changes |
| Coin sessions | Credits and purchased minutes remain exactly-once |
| Promo | Per-denomination accumulation remains unchanged |
| Voucher | No change |
| Portal queue | No added retries, loops, or capacity changes unless separately approved |
| RouterWorker | Preserve one RouterOS session and serialization |
| RouterOS commands | Activation remains four functional commands |
| Synchronize Router | No interaction |
| Router cache | No refresh/invalidation |
| WAN | No probes or NTP loops |
| Admin Dashboard | No change |
| Authentication/RBAC | No change |
| HTTP plane | Preserve portal production gate |
| InstallationState | Do not force state transitions |
| Heartbeat | Keep current frequency |
| Portal polling | Keep current frequency |
| Storage | No additional writes or retry loops |
| SPIFFS/SD | Preserve existing fallback and cadence |
| Add Time | Preview, sale, ESP32 seconds, and RouterOS limit remain identical |

## 13. CPU, RouterOS, and Memory Impact

### This investigation

```text
ESP32 CPU impact: none
RouterOS CPU impact: none
RouterOS commands added: zero
ESP32 heap impact: none
DMA impact: none
PSRAM impact: none
Flash-write impact: none
SD/SPIFFS impact: none
Worker impact: none
Queue impact: none
Polling impact: none
Idle RouterOS: 0 commands/minute
```

### Proposed temporary instrumentation

Expected:

- Zero RouterOS commands.
- Zero additional tasks/loops/timers.
- Zero dynamic allocation.
- Small event-only UART cost.
- No idle output.

Reject instrumentation that logs every 500 ms portal poll, every worker-busy
retry, every session tick, or every idle loop.

## 14. MikroTik File Impact

Current Phase 1 investigation:

```text
No MikroTik upload required.
```

Temporary backend serial instrumentation, if later approved:

```text
ESP32 firmware flash only.
No MikroTik upload required.
```

Frontend-only correction, only if proven:

```text
portal/renzfi-app.js
-> build:mikrotik-portal
-> deployment/mikrotik-hotspot/renzfi-app.js
-> upload only Files/hotspot/renzfi-app.js
```

## 15. Hardware Validation Plan

### Capture one failing activation

1. Start serial capture before opening the coin window.
2. Record customer MAC/session ID.
3. Insert the known denomination sequence.
4. Capture preview credits and purchased minutes.
5. Press Done Paying once.
6. Save the complete serial interval until:
   - active;
   - activation_error; or
   - 40 seconds elapsed.
7. Capture browser POST response and session GET responses.
8. Capture RouterOS:

```routeros
/ip hotspot user print detail
/ip hotspot active print detail
/ip hotspot host print detail
/ip hotspot cookie print detail
```

9. Do not run repeated RouterOS polling; take one snapshot after the failure.

### Decision using the last observed marker

| Last marker | Proven boundary |
|---|---|
| `done-paying begin` plus abort/salesSaved | PortalSession validation/storage/time |
| `done-paying complete` but no activation dequeue | Portal deferred queue |
| Activation dequeued but no worker dispatch | RouterWorker admission |
| Worker dispatch but no worker started | Worker wake/shared-slot boundary |
| Worker started but no driver step 1 | Worker dispatch/platform |
| Driver step 1 only | Credentials |
| Driver step 2 only | Connect/API login |
| Router API user print failure | User lookup |
| User add/set failure | Hotspot user provisioning |
| Active print failure | Active lookup |
| Active login failure | Internet grant request |
| Router budget success but no worker result | Worker completion |
| Worker result but no portal outcome | Outcome mailbox/drain |
| Portal outcome active but browser still Activating | Frontend synchronization |

### Stability confirmation

- Activation command count remains four.
- One RouterOS API session per activation.
- Idle remains zero commands/minute.
- No reconnect storm.
- No worker-busy log flood.
- No watchdog reset.
- No Guru Meditation.
- Heap and DMA recover after activation.
- No additional session/storage writes.

## 16. Required Deliverable Answers

### Exact proven root cause

```text
Not yet proven for the unspecified occasional hardware occurrence.
```

For the known wall-clock log signature only:

```text
salesRecordedAtNow() returns empty;
PortalSessionManager::donePaying() returns before activation.
```

### Exact failing function/line

```text
Unknown until a failing runtime trace identifies the last checkpoint.
```

Known wall-clock signature:

```text
PortalSessionManager::donePaying()
PortalSessionManager.cpp:316-323
```

### RouterWorker state

```text
Unknown for the reported occasional failure.
```

### RouterOS command state

```text
Unknown for the reported occasional failure.
```

### Browser state

`Activating…` is optimistic and is not proof of backend activation.

### Session state

Must be captured from `/api/portal/session`.

### Internet grant state

Must be proven by `/ip/hotspot/active/login` result and one RouterOS active/host
snapshot.

### Competing hypotheses

All source-proven mechanisms remain unconfirmed until the matching trace is
captured.

## 17. Final Phase 1 Verdict

**ROOT CAUSE NOT YET PROVEN — RUNTIME TRACE REQUIRED.**

The code already logs the RouterOS command sequence in sufficient detail.
Four narrowly scoped event markers around portal queue admission, activation
dequeue, first worker rejection, and outcome loss would close the remaining
observability gaps without changing RouterOS traffic, scheduling, polling, queue
architecture, or idle behavior.

No fix or instrumentation was implemented.
