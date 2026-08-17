# Investigation A — Coin → Credit → Time Calculation

**Mode:** Forensic investigation only  
**Code changes:** None  
**Scope:** PHP1 + PHP1 preview-time discrepancy only

## Executive Summary

The first divergent value is produced by:

```text
ESP32_S3_Firmware/src/PromoManager.cpp
PromoManager::resolveForAmount()
lines 136-178
```

Coin pulses and PHP credits accumulate correctly. With accumulated credit of
PHP2, `resolveForAmount()` initially sets a 10-minute fallback, then replaces it
with the five-minute value of the single highest qualifying promo: PHP1.

The current implementation is deliberately designed as contract **A — highest
matching single promo**, not contract **B — linear accumulation**.

Therefore:

- The root cause of the observed five-minute display is proven.
- The expected PHP2 → 10-minute behavior conflicts with the current documented
  PromoManager policy.
- No defect exists in the coin ISR or credit accumulator for this symptom.
- No RouterOS operation occurs during preview calculation.
- No source change was made.

## Business Contract Proven by Source

Current implementation: **A — highest matching promo**.

Source:

```text
ESP32_S3_Firmware/src/PromoManager.cpp:142-146
```

```cpp
// Canonical multi-coin policy (single source of truth for View Rates,
// Insert Money preview, Done Paying, Additional Time):
//   Choose the single enabled promo with the highest coin value that is
//   still <= inserted amount.
```

Selection:

```text
ESP32_S3_Firmware/src/PromoManager.cpp:151-163
```

```cpp
int bestCoin = 0;
int bestMinutes = amount * 5;

if (coin <= amount && coin > bestCoin && minutes > 0) {
  bestCoin = coin;
  bestMinutes = minutes;
  bestPromo = promo;
}
```

Return:

```text
ESP32_S3_Firmware/src/PromoManager.cpp:178
```

```cpp
return bestMinutes;
```

For PHP2 with PHP1 = 5 minutes:

1. Input amount is `2`.
2. Fallback is initialized to `2 * 5 = 10`.
3. The PHP1 promo qualifies because `1 <= 2`.
4. `bestMinutes` is overwritten with `5`.
5. No code applies the unmatched PHP1 remainder.
6. The function returns `5`.

This is the first point where the value differs from the requested linear
contract.

## Value Trace

The trace below assumes the reported scenario: one open coin window, two
separate valid PHP1 insertions, and no Done Paying between them.

`Remaining Time` means the authoritative active-session `secondsLeft`.
The portal's modal `Time` is a separate preview field named
`purchasedMinutes`. Before Done Paying, coin insertion does not add to
`secondsLeft`.

| Stage | Input | Current credit | Computed minutes | Selected promo | Remaining time | Session state | Result |
|---|---|---:|---:|---|---|---|---|
| Coin ISR | Second valid PHP1 pulse edge | Not owned here | Not computed | Not evaluated | Unchanged | Unchanged | PASS |
| CoinManager | Resolved pulse group | Not owned here | Not computed | Not evaluated | Unchanged | Unchanged | PASS |
| PortalSession | Existing PHP1 + new PHP1 | PHP2 | Not yet computed | Not evaluated | Unchanged | `waiting_coin` | PASS |
| Credit accumulator | `credits = 1 + 1` | PHP2 | Not yet computed | Not evaluated | Unchanged | `waiting_coin` | PASS |
| PromoManager | `amount = 2` | PHP2 input | **5** | PHP1 → 5m | Unchanged | Not modified | **FAIL** |
| Session preview | PromoManager output | PHP2 | 5 | Already selected | Unchanged | `waiting_coin` | Downstream propagation |
| Portal API JSON | Session preview document | PHP2 | 5 | Not serialized as a full promo object | Unchanged | `waiting_coin` | Downstream propagation |
| Captive Portal JS | JSON `credits=2`, `purchasedMinutes=5` | PHP2 | 5 | Not recalculated | Unchanged | `waiting_coin` | Downstream propagation |
| Browser UI | Canonical JS state | PHP2 | 5 | Not recalculated | Unchanged | `waiting_coin` | Displays backend values |

The root-cause search stops at `PromoManager::resolveForAmount()`. Later rows
only show direct propagation of its returned value; they are not alternative
root-cause candidates.

## Stage-by-Stage Evidence

### 1. Coin ISR — PASS

File:

```text
ESP32_S3_Firmware/src/CoinManager.cpp
```

Functions:

- `coinIsrDispatch()` — lines 264-267
- `CoinManager::isrThunk()` — lines 732-734
- `CoinManager::handlePulse()` — lines 736-746

Input:

```text
One valid pulse edge for the second PHP1 insertion
```

Output:

```text
_pulses incremented once
_isrPulseTotal incremented once
```

Expected: one accepted pulse.  
Actual: one accepted pulse, supported by the reported PHP2 credit.  
Result: **PASS**.

No credit, promo, or minute calculation occurs in the ISR.

### 2. CoinManager — PASS

File:

```text
ESP32_S3_Firmware/src/CoinManager.cpp
```

Functions:

- `CoinManager::loop()` — lines 407-465
- `CoinManager::processCoin()` — lines 790-859

Input:

```text
Resolved valid one-pulse group
```

Output:

```text
pesoAmount = 1
PortalSessionManager::onCoinInserted(1)
```

Expected: forward PHP1 exactly once.  
Actual: PHP1 is forwarded; the portal later shows PHP2 total.  
Result: **PASS**.

No minute calculation occurs in CoinManager.

### 3. PortalSession credit accumulator — PASS

File:

```text
ESP32_S3_Firmware/src/PortalSessionManager.cpp
```

Function:

```text
PortalSessionManager::onCoinInserted()
lines 148-205
```

Accumulation:

```text
ESP32_S3_Firmware/src/PortalSessionManager.cpp:181-186
```

```cpp
session["credits"] =
    (session["credits"] | 0) + pesoAmount;
session["insertedAmount"] =
    (session["insertedAmount"] | 0) + pesoAmount;
session["coinWindowRemaining"] = (int)coinInsertTimeoutSecs();
session["sessionState"] = PortalState::WaitingCoin;
```

Input:

```text
Existing credits = 1
pesoAmount = 1
```

Output:

```text
credits = 2
insertedAmount = 2
sessionState = waiting_coin
secondsLeft = unchanged
```

Expected credit: PHP2.  
Actual credit: PHP2.  
Result: **PASS**.

The function does not compute minutes. It marks session state dirty and queues
the existing persistence/event work.

### 4. Session preview dispatch — PASS

File:

```text
ESP32_S3_Firmware/src/PortalSessionManager.cpp
```

Function:

```text
PortalSessionManager::enrichSessionPurchasedMinutes()
lines 114-123
```

Input:

```text
credits = 2
```

Dispatch:

```cpp
out["purchasedMinutes"] =
    _promos->resolveForAmount(credits, nullptr, nullptr, nullptr);
```

Expected dispatch input: PHP2.  
Actual dispatch input: PHP2.  
Result: **PASS**.

PortalSession does recompute entitlement from total credits. It does not pass
only the latest PHP1 denomination.

### 5. PromoManager — FIRST FAIL

File:

```text
ESP32_S3_Firmware/src/PromoManager.cpp
```

Class:

```text
PromoManager
```

Function:

```text
PromoManager::resolveForAmount()
lines 136-178
```

Input:

```text
amount = 2
enabled promo = PHP1 → 5 minutes
```

Expected under the requested linear contract:

```text
10 minutes
```

Actual under the implemented highest-match contract:

```text
selected promo = PHP1
matched coin = PHP1
returned minutes = 5
unmatched remainder = PHP1, unused
```

Result: **FAIL** relative to the requested PHP2 → 10-minute behavior.

This is the first wrong value.

## Downstream Propagation After the First Fail

These stages do not create a second incorrect value. They carry the first
incorrect value to the browser.

### Session preview

`PortalSessionManager::enrichSessionPurchasedMinutes()` stores the returned
five-minute value as:

```json
"purchasedMinutes": 5
```

### Portal API JSON

File:

```text
ESP32_S3_Firmware/src/ApiServer.cpp
```

Route:

```text
GET /api/portal/session
lines 2785-2801
```

The response contains the correctly accumulated credit and incorrectly resolved
preview:

```json
{
  "credits": 2,
  "insertedAmount": 2,
  "purchasedMinutes": 5,
  "sessionState": "waiting_coin"
}
```

`secondsLeft` remains whatever authoritative active-session value existed
before the insertion. For a new unpaid session it remains zero until
Done Paying.

### Captive Portal JavaScript

Canonical file:

```text
portal/renzfi-app.js
```

Functions:

- `normalizeSession()` — lines 97-123
- `applyNormalizedSession()` — lines 216-280
- `renderCredits()` — lines 854-857
- `renderCoinModal()` — lines 884-914

The canonical frontend does not calculate `credits * 5`.

```text
portal/renzfi-app.js:888-904
```

```javascript
var purchased = Number(state.purchasedMinutes) || 0;
dom.coinTimeEl.textContent =
    purchased > 0 ? purchased + "m" : "0m";
```

Browser output:

```text
Credit = PHP2
Time = 5m
```

The frontend displays the two backend fields without combining them.

## Root Cause

**ROOT CAUSE PROVEN.**

The observed behavior is caused by a deliberate single-highest-promo policy in
`PromoManager::resolveForAmount()`.

The resolver:

1. Receives the correct accumulated amount.
2. Finds PHP1 as the highest enabled promo not greater than PHP2.
3. Replaces the 10-minute fallback with that promo's five minutes.
4. Discards the unmatched PHP1 remainder.

The bug is a business-contract mismatch, not a failure to count the second coin.

## Alternative Hypotheses Considered

### Coin ISR loses the second pulse

Rejected for this symptom.

Evidence: portal credit becomes PHP2. A lost second pulse could not produce the
reported accumulated credit.

### Coin debounce or anti-double-pulse logic rejects the insertion

Rejected for this symptom.

Evidence: the accepted session credit increases by PHP1 exactly once.

### CoinManager resolves the second pulse to the wrong denomination

Rejected.

Evidence: total credit changes from PHP1 to PHP2.

### PortalSession overwrites rather than accumulates credit

Rejected.

Evidence: `onCoinInserted()` uses addition, and the observed credit is PHP2.

### PortalSession computes entitlement from only the latest denomination

Rejected.

Evidence: `enrichSessionPurchasedMinutes()` passes total `credits` to
`resolveForAmount()`.

### Frontend ignores accumulated credit

Rejected.

Evidence: it displays PHP2 correctly and separately displays backend
`purchasedMinutes`.

### RouterOS changes the preview minutes

Rejected.

Evidence: preview calculation is ESP32-local and no RouterWorker or RouterOS
operation is involved.

### Persistence truncates the value

Rejected as the first divergence.

Evidence: the resolver returns five before JSON persistence or serialization
could alter it.

## Safest Future Fix — Not Implemented

The narrowest candidate surface is:

```text
ESP32_S3_Firmware/src/PromoManager.cpp
PromoManager::resolveForAmount()
```

Before a patch is prepared, the product contract must define behavior for all
non-exact amounts, not only PHP2:

```text
PHP1 = ?
PHP2 = ?
PHP3 = ?
PHP4 = ?
PHP5 = ?
PHP6 = ?
```

In particular, if PHP5 has a promotional rate, PHP6 requires an explicit rule:

- PHP5 package plus PHP1 remainder.
- Six PHP1 units.
- Exact-match only.
- Another documented policy.

The safest implementation candidate would change only the bounded arithmetic
inside the existing resolver and preserve all current callers.

It must:

- Use the already loaded promo list.
- Remain bounded by the number of promos.
- Add no task, timer, loop outside the existing function call, queue, or
  RouterOS request.
- Keep preview and Done Paying on the same resolver.
- Preserve existing speed/profile selection rules.
- Include table-driven tests for exact and remainder amounts.

### Important existing behavior

The current code calls `resolveForAmount()` when a session preview is generated,
including session API refreshes. A narrow resolver patch would add **zero new
recalculations**, but it would preserve that existing call frequency.

Moving calculation exclusively into `onCoinInserted()` would require storing a
new derived session value and changing session/persistence behavior. That is a
broader change with additional regression and flash-risk surface and is not the
safest patch for this defect.

## Files That Would Change

Minimum future production source:

```text
ESP32_S3_Firmware/src/PromoManager.cpp
```

Focused test source should also be added or updated after the business rule is
approved.

No source file was changed by this investigation.

## Files That Must Not Change

```text
ESP32_S3_Firmware/src/CoinManager.cpp
ESP32_S3_Firmware/src/CoinManager.h
ESP32_S3_Firmware/src/PortalSessionManager.cpp
ESP32_S3_Firmware/src/PortalSessionManager.h
ESP32_S3_Firmware/src/RouterProvisioningWorker.cpp
ESP32_S3_Firmware/src/RouterProvisioningWorker.h
ESP32_S3_Firmware/src/RouterOsClient.cpp
ESP32_S3_Firmware/src/RouterOsClient.h
ESP32_S3_Firmware/src/ApiServer.cpp
portal/renzfi-app.js
portal/login.html
deployment/mikrotik-hotspot/*
Captive Portal/*
```

No change is required to:

- Coin ISR timing.
- Pulse debounce.
- Anti-double-pulse behavior.
- Portal session activation.
- RouterWorker.
- RouterOS API.
- Voucher architecture.
- Authentication.
- WAN detection.
- Router synchronization.
- Hotspot configuration.
- Portal heartbeat.
- SPIFFS or SD persistence.
- Installation state.

## Regression Risks

### Main business-rule risk

Changing from a single highest promo to accumulation can alter entitlement for
every amount above the lowest enabled promo.

Mixed promo amounts and profile selection must be explicitly tested.

### Session risk

Preview and Done Paying both call the same resolver. They must remain identical
so displayed time equals activated time.

### Promo profile risk

The resolver also selects the MikroTik speed/profile metadata associated with a
promo. Combination rules must define which profile wins without adding
RouterOS queries.

### Coin risk

None if CoinManager and PortalSession accumulation are untouched.

### Activation risk

Low if only the resolver changes, but activated seconds will intentionally
change. Exact entitlement values require hardware validation.

### Portal risk

None expected. The canonical frontend already displays backend-authoritative
minutes.

### Storage risk

None expected if no new session field or persistence path is added.

## Performance Contract

### RouterOS impact

```text
Expected: none
Additional RouterOS commands: zero
Additional RouterOS polling: zero
```

### ESP32 RAM impact

```text
Expected incremental impact: none
```

The future candidate must reuse the existing promo document and avoid
amount-sized arrays or new persistent buffers.

### Flash and SD impact

```text
Expected: none
Additional writes: zero
```

### CPU impact

```text
Expected measurable impact: none
```

The future calculation must remain bounded by the existing small promo list.
It must add no continuous work.

### Prohibited future behavior

- Continuous polling.
- Additional RouterOS API commands.
- Busy loops.
- New timers.
- Higher polling frequency.
- Synchronous RouterOS waits.
- Repeated persistence.
- Additional bridge, Hotspot, wireless, DHCP, route, WAN, or ping scans.

## Deployment Impact

For a backend-only PromoManager correction:

```text
ESP32 firmware build and flash: required
MikroTik portal file upload: none
MikroTik configuration change: none
Captive portal rebuild: none
SPIFFS upload: none
SD migration: none
```

## Release Verdict

**ROOT CAUSE PROVEN — SAFE PATCH CAN BE PREPARED**

Preparation remains conditional on approving the exact accumulation/remainder
business contract. This report does not authorize or implement that patch.
