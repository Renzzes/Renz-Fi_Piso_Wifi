# Renz-Fi Portal Coin and Activation Forensic Contract

**Date:** 2026-08-09  
**Mode:** Forensic investigation only  
**Code changes:** None  
**Architecture changes:** None  
**Scope:** Coin entitlement, Done Paying, activation, RouterWorker, frontend,
portal ownership, build/deployment, regression and performance constraints

## 1. Final forensic verdict

### Bug #1 — PHP2 still displays five minutes

**Root cause:** a backend entitlement-policy mismatch in:

```text
ESP32_S3_Firmware/src/PromoManager.cpp
PromoManager::resolveForAmount()
lines 136-178
```

The function selects one enabled promo: the highest promo coin value less than
or equal to the accumulated amount. It returns only that promo's minutes.
It does not decompose accumulated credit into repeated promo units and does not
apply the unused remainder.

With only a PHP1 → 5-minute promo:

| Accumulated credit | Selected promo | Returned minutes |
|---:|---:|---:|
| PHP1 | PHP1 | 5 |
| PHP2 | PHP1 | 5 |
| PHP3 | PHP1 | 5 |
| PHP4 | PHP1 | 5 |

The observed PHP2 credit proves that coin accumulation succeeded. The incorrect
five-minute value is produced by `PromoManager`, returned by the backend, and
displayed unchanged by the canonical frontend.

This is not an ISR, CoinManager, API serialization, RouterOS, or canonical
frontend arithmetic defect.

The current behavior is explicitly documented in source as a "single enabled
promo" policy. Therefore, the defect is precisely a mismatch between:

- Expected product contract: accumulated PHP1 units each add five minutes.
- Implemented contract: choose one highest qualifying promo and ignore the
  unmatched remainder.

### Bug #2 — Done Paying remains on Activating

**Root cause for the supplied log sequence:** synchronous early return in:

```text
ESP32_S3_Firmware/src/PortalSessionManager.cpp
PortalSessionManager::donePaying()
lines 273-280
```

`salesRecordedAtNow()` returns an empty string when the wall clock is not ready.
`donePaying()` then logs `salesSaved=false` and immediately returns `false`.

That return occurs before:

- Credits are reserved.
- Session state is changed to `activating`.
- The sale is queued.
- Activation is queued.
- RouterWorker is invoked.
- RouterOS API communication begins.

The matching clock function is:

```text
ESP32_S3_Firmware/src/SalesTime.cpp
salesRecordedAtNow()
lines 71-82
```

It returns empty when:

1. Installation state does not allow NTP.
2. `getLocalTime(..., 500)` does not succeed within 500 ms.
3. The resulting year is earlier than 2024.
4. Timestamp formatting fails.

The API then maps every `donePaying() == false` result to the misleading
`NO_CREDITS` HTTP 400 response:

```text
ESP32_S3_Firmware/src/ApiServer.cpp
POST /api/portal/done-paying
lines 2832-2867
```

The frontend paints `Activating…` before the API call completes. Its catch/final
render ordering can preserve or restore that label from stale state, but the
frontend is not the reason no RouterWorker enqueue occurs in the supplied log
sequence.

### First failing customer-pipeline stage

```text
Customer
  -> Captive Portal                         PASS
  -> Start coin window                      PASS
  -> Coin ISR                               PASS
  -> CoinManager pulse resolution           PASS
  -> PortalSession credit accumulation      PASS
  -> Done Paying HTTP request               PASS
  -> Promo resolution                       PASS, but wrong policy for Bug #1
  -> Sales timestamp acquisition            FAIL for Bug #2
  -> Session activation reservation         NOT REACHED
  -> Sale queue                             NOT REACHED
  -> Activation queue                       NOT REACHED
  -> RouterWorker                           NOT REACHED
  -> RouterOS API                           NOT REACHED
  -> Internet granted                       NOT REACHED
```

## 2. Bug #1 evidence and execution trace

### Stage 1 — Coin interrupt

File:

```text
ESP32_S3_Firmware/src/CoinManager.cpp
```

Functions:

- `coinIsrDispatch()` — lines 264-267
- `CoinManager::isrThunk()` — lines 732-734
- `CoinManager::handlePulse()` — lines 736-746

`handlePulse()` applies post-group and debounce guards, then increments
`_pulses` and `_isrPulseTotal`.

No minute calculation occurs in the ISR.

### Stage 2 — Pulse group to peso amount

File:

```text
ESP32_S3_Firmware/src/CoinManager.cpp
```

Functions:

- `CoinManager::loop()` — lines 407-465
- `CoinManager::processCoin()` — lines 790-859

`processCoin()` resolves the pulse count to a peso denomination and calls:

```cpp
_portalSessions->onCoinInserted(pesoAmount);
```

The default mapping includes one pulse → PHP1. No entitlement minutes are
computed here.

### Stage 3 — Session credit accumulation

File:

```text
ESP32_S3_Firmware/src/PortalSessionManager.cpp
```

Function:

```text
PortalSessionManager::onCoinInserted()
lines 148-205
```

The authoritative operations are:

```cpp
session["credits"] =
    (session["credits"] | 0) + pesoAmount;
session["insertedAmount"] =
    (session["insertedAmount"] | 0) + pesoAmount;
```

The second PHP1 correctly changes both totals from one to two. The reported
`Credit = PHP2` is consistent with this source and rules out a lost second coin
at this stage.

### Stage 4 — Session preview entitlement

File:

```text
ESP32_S3_Firmware/src/PortalSessionManager.cpp
```

Function:

```text
PortalSessionManager::enrichSessionPurchasedMinutes()
lines 114-123
```

The API preview is recomputed from total session credits:

```cpp
out["purchasedMinutes"] =
    _promos->resolveForAmount(credits, nullptr, nullptr, nullptr);
```

The session does recompute entitlement. It does not retain only the first
coin's previous value. The recomputation calls the incorrect policy.

### Stage 5 — Exact computation break

File:

```text
ESP32_S3_Firmware/src/PromoManager.cpp
```

Function:

```text
PromoManager::resolveForAmount()
lines 136-178
```

Relevant source behavior:

```cpp
int bestMinutes = amount * 5;

if (coin <= amount && coin > bestCoin && minutes > 0) {
  bestCoin = coin;
  bestMinutes = minutes;
}

return bestMinutes;
```

For PHP2 with an enabled PHP1 → 5-minute promo:

1. Fallback starts as `2 * 5 = 10`.
2. PHP1 promo satisfies `coin <= amount`.
3. `bestMinutes` is overwritten with `5`.
4. No operation applies the remaining PHP1.
5. The function returns `5`.

This is the exact line-level root cause.

### Stage 6 — Portal API response

File:

```text
ESP32_S3_Firmware/src/ApiServer.cpp
```

Route:

```text
GET /api/portal/session
lines 2785-2801
```

The route calls `PortalSessionManager::getSession()`, which invokes
`enrichSessionPurchasedMinutes()`, then serializes the returned session.

Therefore, the backend returns the wrong `purchasedMinutes`.

### Stage 7 — Canonical frontend

File:

```text
portal/renzfi-app.js
```

Functions:

- `normalizeSession()` — lines 97-123
- `applyNormalizedSession()` — lines 216-280
- `renderCredits()` — lines 854-857
- `renderCoinModal()` — lines 884-914

`renderCoinModal()` explicitly treats the backend as authoritative:

```javascript
var purchased = Number(state.purchasedMinutes) || 0;
dom.coinTimeEl.textContent = purchased > 0 ? purchased + "m" : "0m";
```

The canonical frontend does not calculate `credits * 5`; it displays the
backend's five-minute result.

### Stage 8 — Done Paying repeats the same incorrect calculation

File:

```text
ESP32_S3_Firmware/src/PortalSessionManager.cpp
```

Function:

```text
PortalSessionManager::donePaying()
lines 207-405
```

Promo resolution occurs at lines 257-260 and again after the concurrency recheck
at lines 314-322. The final result becomes:

```cpp
const long purchasedSeconds = (long)saleMinutes * 60L;
session["secondsLeft"] = newRemaining;
```

For PHP2 under the current PHP1 promo policy, `saleMinutes` is five and
`secondsLeft` becomes 300, not 600.

RouterOS receives the already incorrect duration. RouterOS does not truncate it.

## 3. Bug #2 evidence and execution trace

### Stage 1 — HTTP request

File:

```text
ESP32_S3_Firmware/src/ApiServer.cpp
```

Route:

```text
POST /api/portal/done-paying
lines 2832-2869
```

The handler extracts `mac` and calls:

```cpp
_portalSessions->donePaying(mac)
```

### Stage 2 — Initial session and promo validation

File:

```text
ESP32_S3_Firmware/src/PortalSessionManager.cpp
```

Function:

```text
PortalSessionManager::donePaying()
lines 207-263
```

The function:

1. Finds the session.
2. Reads credits and state.
3. Handles idempotent in-flight/active requests.
4. Rejects zero credits.
5. Computes promo minutes.
6. Releases the state lock.

The supplied logs continue past `done-paying begin`, so the request entered this
function.

### Stage 3 — Storage gate

Lines 265-270 return early if `_storage` is null.

The supplied `wall clock not ready` message proves this was not the matching
branch; execution reached the timestamp gate.

### Stage 4 — Exact stopping point

Lines 273-280:

```cpp
recordedAt = salesRecordedAtNow();
if (recordedAt.isEmpty()) {
  Serial.println("[portal] salesSaved=false");
  _logger->error(
      "portal",
      "donePaying: wall clock not ready — sale not recorded");
  return false;
}
```

The supplied logs match this branch exactly.

### Stage 5 — Why the wall clock is empty

File:

```text
ESP32_S3_Firmware/src/SalesTime.cpp
```

Functions:

- `salesTimeBegin()` — lines 43-61
- `salesTimeReady()` — lines 63-69
- `salesRecordedAtNow()` — lines 71-82

NTP configuration is deferred unless installation state allows it.
`salesRecordedAtNow()` performs a synchronous `getLocalTime()` with a 500 ms
budget and returns empty if time is unavailable or invalid.

Source evidence proves the immediate reason is timestamp unavailability.
Repository source alone does not prove which of the following hardware-state
conditions caused it:

- Installation state did not permit NTP.
- NTP had not synchronized.
- NTP servers were unreachable.
- The 500 ms call timed out.
- The clock year was invalid.

The exact sub-cause requires runtime installation-state and time-sync evidence.
The report does not select one without logs.

### Stage 6 — API response

When `donePaying()` returns false, `ApiServer.cpp:2864-2867` sends:

```text
HTTP 400
code: NO_CREDITS
message: No credits to convert — insert coins first
```

That error is inaccurate for the wall-clock failure. It does not mean the
request is unresolved; the backend returns an error response.

### Stage 7 — Unreached session reservation

The following block at `PortalSessionManager.cpp:314-363` is not reached:

- Recheck credits under lock.
- Calculate final sale minutes.
- Clear credits and inserted amount.
- Set `secondsLeft`.
- Set `sessionState = activating`.
- Set `routerAuthPending = true`.

### Stage 8 — Unreached sale queue

The following call at line 367 is not reached:

```cpp
enqueueRecordSale(...)
```

Consequently, no sale persistence operation begins.

### Stage 9 — Unreached activation queue

The following call at line 390 is not reached:

```cpp
enqueueActivateSession(mac);
```

This is why there is no RouterWorker enqueue in the supplied logs.

### Stage 10 — Unreached RouterWorker

Normal activation would proceed through:

```text
PortalSessionManager::loop()
PortalSessionManager::processDeferredWork()
PortalSessionManager::onSessionActivated()
RouterProvisioningWorker::tryEnqueueActivateHotspotUser()
RouterProvisioningWorker::taskLoop()
RouterProvisioningWorker::runOp()
RouterPlatform::provisionHotspotUser()
MikroTikDriver::provisionHotspotUser()
RouterOsClient command transport
```

None of these activation stages is reached for the supplied wall-clock failure.

### Stage 11 — Frontend behavior

File:

```text
portal/renzfi-app.js
```

Functions:

- `waitForActivation()` — lines 627-645
- `handleDonePaying()` — lines 648-692
- `renderStatus()` — lines 829-848

`handleDonePaying()` sets the status text to `Activating…` at lines 654-657
before `donePayingAPI()` resolves.

On HTTP 400:

1. The promise rejects.
2. The catch displays a service/error notice.
3. `syncSessionFromServer()` is started but not awaited.
4. `finally()` immediately calls `render()`.
5. If cached state remains `activating`, `renderStatus()` restores
   `Activating…`.

There is also no request timeout/abort in the API helpers. The nominal 35-second
activation deadline is checked only after `fetchSession()` resolves, so a hung
fetch can bypass that deadline.

These frontend defects explain why the visible label can persist. They do not
explain the absence of RouterWorker enqueue in the matching wall-clock log path.

## 4. Customer activation pipeline contract

### Intended success path

```text
1. Browser opens coin window
2. Coin ISR counts valid pulses
3. CoinManager resolves pulse group to pesos
4. PortalSessionManager accumulates credits
5. PromoManager resolves accumulated entitlement
6. Browser sends Done Paying
7. PortalSessionManager obtains a sale timestamp
8. Session reserves credits and enters activating
9. RecordSale work is queued
10. ActivateSession work is queued
11. Portal loop dispatches activation
12. RouterWorker accepts HotspotUser
13. MikroTikDriver provisions/updates the Hotspot user
14. MikroTikDriver authorizes the active client
15. Worker publishes activation outcome
16. PortalSessionManager changes state to active
17. Portal polling observes active + connected
18. Browser displays Connected
```

### First failure for Bug #1

Stage 5: accumulated entitlement resolution.

### First failure for Bug #2

Stage 7: sale timestamp acquisition.

## 5. RouterWorker verification

### Normal activation enqueue

File:

```text
ESP32_S3_Firmware/src/RouterProvisioningWorker.cpp
```

Function:

```text
RouterProvisioningWorker::tryEnqueueActivateHotspotUser()
lines 252-277
```

The operation is non-blocking:

1. Validate queue, mutex, and completion semaphore.
2. Try to acquire `_dispatchMutex` with zero wait.
3. Reject when `_running` is true.
4. Copy the user into `_slot`.
5. Send one wake byte to the queue with zero wait.
6. Release the mutex and return.

### Queue and worker

The RouterWorker queue depth is one. The queue transports a wake byte while the
actual operation payload remains in shared `_slot`.

The worker receives the wake token, sets active/running state, dispatches the
operation, then publishes a result.

### Completion

Hotspot outcomes are stored in one shared mailbox:

```text
RouterProvisioningWorker::takeHotspotOutcome()
lines 325-338

RouterProvisioningWorker::publishHotspotOutcome()
lines 340-353
```

`PortalSessionManager::drainHotspotOutcomes()` converts successful activation to
`active` and failure to `activation_error`.

### Timeout and retry

- Router worker deadline: 20 seconds.
- Blocking wrapper budget: 25 seconds.
- Portal busy-retry budget: 25 seconds.
- Accepted RouterOS activation failures are not automatically retried.
- A later Resume action is the existing recovery path.
- Global RouterOS connection failures use exponential backoff.

### Determination for the supplied failure

Activation is never queued because `donePaying()` returns at line 280.
RouterWorker does not cause that specific failure.

### Independent source risks

These are latent risks, not the root cause of the supplied wall-clock symptom:

1. Queue payload is stored in a shared slot while the queue holds only a wake
   byte.
2. `_running` becomes true in the worker after queue publication, leaving a
   publication window.
3. The Hotspot outcome mailbox holds only one pending result and can be
   overwritten.
4. Busy activation work is re-enqueued without a delay.
5. Some deferred enqueue return values are ignored.
6. An accepted activation has no PortalSessionManager watchdog for a lost
   outcome.

No change to RouterWorker is required to remove the wall-clock early return.
Any future worker hardening must be treated as a separate change with separate
hardware validation.

## 6. PromoManager contract verification

### Current implemented contract

```text
Select one enabled promo with the highest coin value <= total amount.
Return that promo's minutes.
Fallback to amount * 5 only when no promo list can be loaded or no promo
overwrites the fallback.
```

### Expected contract stated by this investigation

```text
PHP1 -> 5 minutes
PHP2 -> 10 minutes
PHP3 -> 15 minutes
PHP4 -> 20 minutes
```

### Contract mismatch

The current implementation cannot satisfy the expected sequence when an enabled
PHP1 promo exists, because the PHP1 promo overwrites the cumulative fallback
for every amount from PHP1 up to the next qualifying promo.

The required future product decision is not yet encoded:

- Repeated-unit decomposition.
- Highest package plus remainder decomposition.
- Exact-match package with base-rate remainder.
- Another explicit promo-combination policy.

No implementation should begin until that combination policy is formally chosen,
especially for amounts such as PHP6 with PHP1 and PHP5 promos.

## 7. Portal session lifecycle verification

### Session creation

`startCoinWindow()` finds or creates a session, marks the coin window active,
sets `waiting_coin`, and queues persistence/events.

### Coin insertion

`onCoinInserted()` increments `credits` and `insertedAmount`, refreshes the coin
window, updates timestamps, and keeps the state at `waiting_coin`.

### Multiple insertions

Credit state remains internally consistent for repeated PHP1 insertions.
The inconsistent value is derived `purchasedMinutes`, not stored credits.

### Done Paying

On a successful path, Done Paying:

- Rechecks idempotency.
- Resolves entitlement.
- Obtains a sale timestamp.
- Reserves credits.
- Converts minutes to seconds.
- Sets `activating`.
- Queues sale, persistence, activation, and events.

On the supplied failure path, it returns before reservation, so existing credits
should remain available in authoritative session state.

### Activation

Activation is asynchronous. RouterWorker success changes the session to
`active`; failure changes it to `activation_error` while preserving purchased
time.

### Heartbeat

Heartbeat updates device presence and authoritative session information. It
does not perform the missing Done Paying enqueue.

### Completion and expiration

Normal expiration queues RouterOS cleanup and clears/updates session state.
That path is unrelated to the two reported failures.

## 8. Frontend verification

### Portal polling

- Coin-window session polling: every two seconds.
- Activation polling: sequential 500 ms checks.
- Heartbeat/session refresh: every ten seconds.
- Local timer interpolation: every second without RouterOS calls.

### Bug #1 frontend verdict

The canonical frontend does not ignore accumulated credits. It reads both:

- `credits` for PHP display.
- `purchasedMinutes` for time display.

The two fields differ because the backend computes them under different rules.

### Bug #2 frontend verdict

The backend returns HTTP 400 for the wall-clock branch. It does not leave a
server-side promise unresolved.

The frontend can nevertheless leave `Activating…` visible because:

- It paints that state optimistically.
- Recovery synchronization is not awaited before final rendering.
- Cached `activating` state can overwrite the error label.
- API fetches have no request timeout.

## 9. Voucher architecture determination

Coin-session activation does not activate a `VoucherManager` voucher.

The coin path provisions a MikroTik Hotspot user derived from the portal session
and device identity. It does not call `VoucherManager::markActive()`.

`VoucherManager` stores locally generated voucher records. The captive login
form submits voucher credentials directly to MikroTik Hotspot.

No source-proven automatic path was found that creates matching RouterOS users
for locally generated vouchers.

This finding is independent of Bug #2. The supplied Done Paying request is a
coin-session activation path, not a voucher redemption path.

## 10. Captive portal source ownership contract

### Single source of truth

```text
portal/
```

This is the only portal source folder developers should edit.

### Generated MikroTik upload bundle

```text
deployment/mikrotik-hotspot/
```

This folder is generated by the MikroTik portal build, but also contains
deployment documentation and configuration helpers. Generated portal assets in
this folder should never be edited directly.

### Legacy portal tree

```text
Captive Portal/
```

Its own README marks it deprecated. It is excluded from the current build.
It must not be used as the source for Bug #1 or Bug #2 changes.

### Generated ESP32 recovery staging

```text
ESP32_S3_Firmware/data/portal/
```

This is generated SPIFFS staging for recovery/setup use. It should never be
edited directly.

### Physical MikroTik runtime

The production router serves files uploaded to:

```text
Files/hotspot/
```

The upload is manual. Repository source cannot prove which revision is currently
installed on a physical router.

## 11. Build pipeline contract

Command:

```powershell
$env:RENZFI_APPLIANCE_BASE_URL = "http://10.10.10.2"
npm run build:mikrotik-portal
```

`package.json:17` maps this command to:

```text
scripts/build-mikrotik-portal.mjs
```

The build:

- Reads `portal/`.
- Copies required static assets.
- Replaces the appliance URL placeholder in `renzfi-app.js`.
- Generates the MikroTik deployment bundle.

Changes in `portal/` do **not** propagate automatically. The build command must
be run manually, followed by manual file upload to MikroTik.

`deployment/mikrotik-hotspot/upload-hotspot-files.rsc` configures
`html-directory` and the ESP32 walled garden. It does not upload files.

## 12. Exact deployment impact

### Bug #1 backend entitlement correction

Current evidence requires no portal source change.

MikroTik upload set:

```text
None
```

Deployment surface:

```text
ESP32 firmware only
```

Primary source surface for a future implementation:

```text
ESP32_S3_Firmware/src/PromoManager.cpp
```

A focused regression test should be added, but no test or source change is made
by this investigation.

### Bug #2 backend wall-clock correction

Current evidence requires no portal source change to reach RouterWorker.

MikroTik upload set:

```text
None
```

Deployment surface:

```text
ESP32 firmware only
```

Potential source surfaces depend on the approved future policy:

```text
ESP32_S3_Firmware/src/PortalSessionManager.cpp
ESP32_S3_Firmware/src/SalesTime.cpp
ESP32_S3_Firmware/src/ApiServer.cpp
```

No policy or fix is approved by this report.

### Optional frontend error-state hardening

If a later approved change only corrects timeout/error rendering:

Canonical source:

```text
portal/renzfi-app.js
```

Generated upload artifact:

```text
deployment/mikrotik-hotspot/renzfi-app.js
```

Exact physical MikroTik upload:

```text
Files/hotspot/renzfi-app.js
```

No HTML, CSS, MD5, image, audio, admin launcher, `.rsc`, redirect shell, or XML
file is required for a JavaScript-only error-state change, assuming the router
already runs the current `login.html`.

## 13. Regression and performance matrix

No future fix is approved in this report. The following are mandatory review
conditions for candidate changes.

### Candidate A — Promo entitlement combination policy

| Risk category | Assessment |
|---|---|
| Regression | High business-rule risk for mixed amounts and overlapping promos |
| CPU | Must remain bounded by the small promo list |
| Memory | Avoid amount-sized dynamic programming and additional large JSON documents |
| RouterOS | No additional commands permitted |
| Portal | Preview and final entitlement must use the same resolver |
| Captive portal | No upload required unless display behavior is separately changed |
| Voucher | Must not alter voucher pricing semantics unintentionally |
| RouterWorker | No change required |
| Coin | Pulse and peso accumulation must remain unchanged |
| Session | Preserve add-time and active-profile behavior |
| Activation | RouterOS must receive exactly the computed seconds |
| SPIFFS/SD | No extra writes permitted |
| Admin dashboard | Promo editor assumptions must match the chosen policy |
| Build/deployment | Firmware build/flash only |

Performance acceptance:

- Zero extra RouterOS API calls.
- Zero extra polling.
- No busy loop.
- No synchronous RouterOS wait.
- O(number of promos) or another tightly bounded calculation.
- No per-amount heap growth.

### Candidate B — Decouple activation from unavailable wall clock

| Risk category | Assessment |
|---|---|
| Regression | High accounting/idempotency risk |
| CPU | No retry loop or repeated timestamp checks |
| Memory | Use bounded fixed state; avoid duplicate sale documents |
| RouterOS | Keep one activation sequence only |
| Portal | Preserve credits on all rejected requests |
| Captive portal | No file upload required |
| Voucher | No impact to voucher path |
| RouterWorker | Preserve one non-blocking enqueue |
| Coin | No change |
| Session | Prevent duplicate credit consumption on retries |
| Activation | Internet grant and sale recording must have explicit consistency policy |
| SPIFFS/SD | Do not write provisional sales repeatedly |
| Admin dashboard | Sales timestamps/status must remain understandable |
| Build/deployment | Firmware build/flash only |

Performance acceptance:

- Remove or avoid the current 500 ms request-path wait rather than increasing it.
- No NTP polling loop.
- No RouterOS retry loop.
- No extra RouterOS scans.
- No additional session persistence per loop.

### Candidate C — Accurate Done Paying API error

| Risk category | Assessment |
|---|---|
| Regression | Low if response schema remains backward compatible |
| CPU/memory | Negligible |
| RouterOS | Zero impact |
| Portal | Frontend error parsing must remain compatible |
| Session/activation | No state mutation |
| Storage | Zero impact |
| Build/deployment | Firmware only unless frontend presents the new code |

### Candidate D — Frontend timeout and final-render ordering

| Risk category | Assessment |
|---|---|
| Regression | Medium UI-state risk |
| CPU | Do not increase 500 ms activation polling frequency |
| Memory | AbortController/timer state must remain bounded |
| RouterOS | Zero direct RouterOS traffic |
| Portal | Must not hide a later successful activation |
| Captive portal | Upload only generated `renzfi-app.js` |
| Session | Backend remains authoritative |
| Activation | Must distinguish rejected, pending, failed, and timed-out states |
| Build/deployment | Rebuild bundle, upload one JS file, clear/verify browser cache |

Performance acceptance:

- Keep or reduce current request count.
- Never overlap activation polls.
- Keep heartbeat frequency unchanged.
- No continuous retry after timeout.

### Candidate E — Independent RouterWorker hardening

| Risk category | Assessment |
|---|---|
| Regression | Very high because all RouterOS operations share the worker |
| CPU | No lock spin or immediate requeue loop |
| Memory | Use a small bounded queue; avoid copied dynamic JSON/String payload expansion |
| RouterOS | Preserve global serialization, pacing, deadlines, and backoff |
| Portal | Preserve activation outcome semantics |
| Voucher | Separate from voucher provisioning |
| Coin | No impact until Done Paying |
| Session | Every accepted job must produce exactly one outcome |
| Activation | No duplicate user creation or time grant |
| SPIFFS/SD | No persistence retry per worker loop |
| Admin dashboard | Setup/admin jobs must remain functional |
| Build/deployment | Firmware only; full hardware regression required |

This candidate is not required for the two reported root causes and must not be
bundled with their fixes.

## 14. RouterOS CPU and API impact

### Current Bug #1 path

Preview recomputation is ESP32-local and issues zero RouterOS commands.

Changing entitlement arithmetic must not add RouterOS traffic. RouterOS should
continue receiving one normal activation sequence after Done Paying.

### Current Bug #2 failure path

The function returns before RouterWorker. RouterOS CPU and API impact are zero
for the failed request.

### Future-fix hard limits

Reject any candidate that adds:

- RouterOS polling.
- Repeated `/ip/hotspot/*/print` scans.
- Additional bridge, wireless, DHCP, route, or ping scans.
- Synchronous RouterOS waits in the HTTP task.
- Activation retry without bounded backoff and idempotency.
- Extra RouterOS sessions for timestamp or sales handling.

## 15. ESP32 RAM, DMA, and flash impact

### RAM

The promo resolver already allocates a medium heap JSON document. A future
calculation must not add large amount-indexed arrays or duplicate the promo JSON.

### DMA

Neither root cause requires additional Ethernet buffers. Any candidate requiring
larger request/response payloads or concurrent RouterOS sessions must be rejected.

### Flash and SD

Do not add:

- Persistence on every activation poll.
- Repeated provisional sale writes.
- Immediate full-session saves in a retry loop.
- Additional SPIFFS fallback rewrites.

One successful payment should retain a bounded persistence sequence.

## 16. Safe future implementation plan

This is a sequencing contract only. It authorizes no implementation.

### Phase 1 — Freeze runtime evidence

1. Capture the active promo JSON.
2. Capture one PHP1/PHP2/PHP3/PHP4 session response.
3. Capture installation state and wall-clock/NTP readiness at Done Paying.
4. Capture the HTTP status/body returned for the matching failure.
5. Capture RouterWorker logs proving no enqueue follows the wall-clock return.

### Phase 2 — Approve entitlement business rule

1. Define expected minutes for every amount from PHP0 through the maximum coin
   window amount.
2. Define behavior for mixed package/remainder values such as PHP6.
3. Define whether speed/device profile comes from the largest package, exact
   amount, or another explicit policy.
4. Do not modify `PromoManager` before this table is approved.

### Phase 3 — Specify sales/activation consistency

1. Decide whether Internet activation may proceed when wall time is unavailable.
2. Define the durable timestamp/status representation for that case.
3. Define retry and power-loss behavior.
4. Preserve exactly-once credit consumption and at-most-once RouterOS time grant.

### Phase 4 — Isolate changes

Use separate changes for:

1. Promo arithmetic.
2. Sales-time activation gate.
3. API error code.
4. Frontend error/timeout rendering.
5. RouterWorker hardening, if separately approved.

Do not combine RouterWorker redesign with either root-cause correction.

### Phase 5 — Validate before deployment

Required tests:

- PHP1/PHP2/PHP3/PHP4 preview and final seconds.
- Mixed promo amounts and remainder policy.
- Done Paying with valid wall time.
- Done Paying before NTP readiness.
- Repeated Done Paying request.
- Power interruption around sale reservation.
- Worker busy at activation enqueue.
- RouterOS success, failure, timeout, and lost connectivity.
- Frontend HTTP 400, activation error, timeout, and later success.
- No increase in RouterOS command count.
- No increase in polling frequency.
- Stable heap, DMA, watchdog, SPIFFS, and SD behavior.

### Phase 6 — Minimal deployment

- Backend-only corrections: flash ESP32 firmware; upload no MikroTik portal files.
- Frontend-only correction: build from `portal/` and upload only
  `deployment/mikrotik-hotspot/renzfi-app.js`.
- Do not upload all portal files when only JavaScript changed.

## 17. Evidence index

| Subject | File | Function/region |
|---|---|---|
| Coin ISR | `ESP32_S3_Firmware/src/CoinManager.cpp` | `handlePulse()`, 736-746 |
| Pulse conversion | `ESP32_S3_Firmware/src/CoinManager.cpp` | `processCoin()`, 790-859 |
| Credit accumulation | `ESP32_S3_Firmware/src/PortalSessionManager.cpp` | `onCoinInserted()`, 148-205 |
| Preview minutes | `ESP32_S3_Firmware/src/PortalSessionManager.cpp` | `enrichSessionPurchasedMinutes()`, 114-123 |
| Promo calculation | `ESP32_S3_Firmware/src/PromoManager.cpp` | `resolveForAmount()`, 136-178 |
| Done Paying | `ESP32_S3_Firmware/src/PortalSessionManager.cpp` | `donePaying()`, 207-405 |
| Clock readiness | `ESP32_S3_Firmware/src/SalesTime.cpp` | `salesRecordedAtNow()`, 71-82 |
| Portal API | `ESP32_S3_Firmware/src/ApiServer.cpp` | portal routes, 2785-2869 |
| Frontend activation | `portal/renzfi-app.js` | `waitForActivation()`, `handleDonePaying()`, 627-692 |
| Frontend status | `portal/renzfi-app.js` | `renderStatus()`, 829-848 |
| Frontend minutes | `portal/renzfi-app.js` | `renderCoinModal()`, 884-914 |
| Worker enqueue | `ESP32_S3_Firmware/src/RouterProvisioningWorker.cpp` | `tryEnqueueActivateHotspotUser()`, 252-277 |
| Worker outcome | `ESP32_S3_Firmware/src/RouterProvisioningWorker.cpp` | `takeHotspotOutcome()` / `publishHotspotOutcome()`, 325-353 |
| Build command | `package.json` | `build:mikrotik-portal`, line 17 |
| Build implementation | `scripts/build-mikrotik-portal.mjs` | source copy/substitution |
| Deployment config | `deployment/mikrotik-hotspot/upload-hotspot-files.rsc` | lines 1-32 |

## 18. Final conclusion

1. Bug #1 is caused by the backend's single-best-promo policy in
   `PromoManager::resolveForAmount()`. Credits accumulate correctly; the
   canonical frontend displays the backend result correctly.
2. Bug #2's supplied logs identify a synchronous wall-clock early return in
   `PortalSessionManager::donePaying()`. Activation is never queued.
3. RouterWorker and RouterOS are not reached in that failure path.
4. The frontend can preserve the visible `Activating…` label after the backend
   rejects the request, but it is a secondary display/recovery defect.
5. `portal/` is the source of truth.
   `deployment/mikrotik-hotspot/` is the generated upload bundle.
   `Captive Portal/` is legacy.
   `ESP32_S3_Firmware/data/portal/` is generated recovery staging.
6. Backend fixes require no MikroTik portal upload.
7. A frontend-only correction requires only `renzfi-app.js` to be rebuilt and
   uploaded.
8. No fix, refactor, optimization, cleanup, or architecture change was made or
   approved by this forensic investigation.
