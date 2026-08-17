# Renz-Fi Session Lifecycle Implementation Report

**Date:** 2026-08-09  
**Scope:** Portal lifecycle status and 30/15-second warnings  
**Architecture:** Unchanged  
**Release verdict:** **IMPLEMENTED — HARDWARE VALIDATION REQUIRED**

## 1. Root-Cause Analysis

### Missing 30/15-second warnings

The captive portal already had:

- Authoritative `secondsLeft` from ESP32.
- A local one-second visual countdown.
- Existing heartbeat/session correction.
- Pause and connection state.

It had no threshold-crossing logic or session-warning element.

Root boundary:

```text
portal/renzfi-app.js
startMainTimer()
```

The timer only decremented and rendered the countdown. It never emitted a
customer warning.

### Stale `Activating…` after a failed request

`handleDonePaying()` displayed `Activating…` before backend acceptance.
Its error path started `syncSessionFromServer()` without returning/awaiting that
promise. The outer `finally()` immediately rendered cached state, which could
restore `Activating…`.

Root boundary:

```text
portal/renzfi-app.js
handleDonePaying()
```

The corrected error path now waits for the existing authoritative session
recovery before final rendering and preserves the activation error message when
the recovered state is not active.

No backend, RouterWorker, or MikroTik change was needed.

## 2. Current Session State Machine

Authoritative lifecycle:

```text
idle
  -> waiting_coin
  -> activating
  -> active
  -> expired
```

Alternative states:

```text
activating -> activation_error
active -> paused -> active
waiting_coin -> idle
active/paused/activating -> expired through termination
```

### State ownership

| Product state | Authoritative owner | Stored representation |
|---|---|---|
| Disconnected | Derived by portal from backend state | `idle`, `expired`, `connected=false`, or zero time |
| Waiting for Payment | `PortalSessionManager` | `sessionState=waiting_coin` |
| Activating | `PortalSessionManager` | `sessionState=activating`, `routerAuthPending=true` |
| Connected | `PortalSessionManager` after worker outcome | `sessionState=active`, `connected=true` |
| Paused | `PortalSessionManager` | `sessionState=paused`, `paused=true` |
| Activation failed | `PortalSessionManager` | `sessionState=activation_error` |
| Expired | `PortalSessionManager` | `sessionState=expired`, `connected=false`, `secondsLeft=0` |

`CoinManager`, `RouterWorker`, `ApiServer`, and the browser do not own the
authoritative lifecycle state.

### Field ownership

| Product field | Source field | Owner |
|---|---|---|
| Seconds remaining | `secondsLeft` | `PortalSessionManager` |
| Minutes purchased before payment | `purchasedMinutes` | `PortalSessionManager` |
| Credits/money | `credits` | `PortalSessionManager` |
| Current unpaid coin amount | `insertedAmount` | `PortalSessionManager` |
| Physical coin totals | `CoinStats` | `CoinManager` |
| Activation pending | `routerAuthPending` | `PortalSessionManager` |
| Cleanup pending | `routerCleanupPending/Queued/Complete` | `PortalSessionManager` |
| Worker currently running | `_running`, `_activeType` | `RouterProvisioningWorker` |
| Connected | `connected` | `PortalSessionManager` |
| Disconnected | Derived | Browser/API consumers |
| Session ID | `sessionId` | `PortalSessionManager` |
| MAC/IP | `macAddress`, `ipAddress` | `PortalSessionManager`, populated from portal request |

## 3. Current Session Flow

```text
Browser displays Disconnected
  -> customer opens coin window
  -> PortalSessionManager sets waiting_coin
  -> browser displays Waiting for Payment
  -> CoinManager forwards accepted denomination
  -> credits and purchasedMinutes accumulate
  -> customer presses Done Paying
  -> PortalSessionManager reserves sale and sets activating
  -> browser displays Activating
  -> portal deferred queue dispatches activation
  -> RouterWorker serializes one RouterOS API session
  -> MikroTik user is added/updated
  -> MikroTik active/login grants Internet
  -> worker publishes success outcome
  -> PortalSessionManager sets active + connected
  -> browser displays Connected
  -> ESP32 and browser countdown proceed
  -> browser displays 30-second warning once
  -> browser displays 15-second warning once
  -> local display reaches zero
  -> browser immediately displays Disconnected and Insert Coin
  -> ESP32 expiration tick sets expired
  -> RouterWorker deauthorizes the Hotspot client
  -> MikroTik active entry, user, and targeted cookies are removed
  -> worker outcome records cleanup result
  -> existing heartbeat confirms expired/disconnected state
```

## 4. Disconnect Behavior

### Timeout detector

Owner:

```text
PortalSessionManager::tickSessions()
```

The existing ESP32 one-second session tick decrements active, unpaused
`secondsLeft`.

When a later tick observes zero:

- `sessionState=expired`
- `connected=false`
- cleanup is marked queued/pending
- one ExpireSession work item is created

### RouterWorker path

```text
PortalSessionManager::onSessionExpired()
  -> RouterProvisioningWorker::tryEnqueueDeauthorizeHotspotUser()
  -> RouterPlatform::disconnectHotspotUser()
  -> MikroTikDriver::deauthorizeUser()
```

### RouterOS revocation

The existing deauthorization path:

1. Finds the active row by MAC.
2. Removes the active row.
3. Finds/removes the generated Hotspot user.
4. Finds/removes targeted Hotspot cookies.

Internet revocation is requested by:

```text
/ip/hotspot/active/remove
```

No polling is used.

### Session clearing semantics

The active entitlement is cleared:

- `secondsLeft=0`
- `connected=false`
- state becomes `expired`
- portal actions return to Insert Coin

The backend does not immediately delete the session record. It intentionally
retains expired records until cleanup is complete and the retention period
allows removal. That behavior was preserved because immediate record deletion
could lose RouterOS cleanup tracking.

## 5. Desired Session Flow Implemented

```text
Disconnected
  -> Waiting for Payment
  -> Activating
  -> Connected
  -> 30-second warning
  -> Connected
  -> 15-second warning
  -> Connected
  -> Disconnected at zero
```

Warnings:

```text
30 seconds remaining. Insert more coins to continue your session.

15 seconds remaining. Your Internet connection will end soon.
```

Each warning:

- Fires once per threshold crossing.
- Uses the existing one-second browser timer.
- Remains visible for approximately five existing timer ticks.
- Disappears automatically.
- Is hidden while paused or disconnected.
- Is re-armed after an authoritative new activation or Add Time increase.
- Uses `role=status` and `aria-live=polite`.

## 6. Browser State Verification

### Disconnected

Displayed when:

- state is idle/expired;
- connected is false; or
- local remaining time reaches zero.

### Waiting for Payment

The browser now explicitly maps:

```text
sessionState=waiting_coin
```

to:

```text
Waiting for Payment
```

### Activating

Displayed only from:

```text
sessionState=activating
```

or the immediate optimistic Done Paying presentation.

### Connected

Displayed only when:

```text
sessionState=active
connected=true
secondsLeft>0
```

### Activation failure

The existing recovery request is now awaited before the final render.
If the authoritative state is not active, the browser retains the error instead
of immediately overwriting it with cached `Activating…`.

### Zero

The existing local timer reaches zero without a browser refresh.
The status immediately becomes Disconnected and the action label returns to
Insert Coin. The next existing server synchronization confirms backend expiry.

## 7. Files Reviewed

Firmware:

```text
ESP32_S3_Firmware/src/PortalSessionManager.cpp
ESP32_S3_Firmware/src/PortalSessionManager.h
ESP32_S3_Firmware/src/RouterProvisioningWorker.cpp
ESP32_S3_Firmware/src/RouterProvisioningWorker.h
ESP32_S3_Firmware/src/router/RouterPlatform.cpp
ESP32_S3_Firmware/src/router/drivers/MikroTikDriver.cpp
ESP32_S3_Firmware/src/ApiServer.cpp
ESP32_S3_Firmware/src/CoinManager.cpp
```

Portal:

```text
portal/login.html
portal/renzfi-app.js
portal/renzfi-style.css
scripts/build-mikrotik-portal.mjs
deployment/mikrotik-hotspot/*
```

## 8. Files Changed

Canonical portal source:

```text
portal/login.html
portal/renzfi-app.js
```

Generated MikroTik artifacts:

```text
deployment/mikrotik-hotspot/login.html
deployment/mikrotik-hotspot/renzfi-app.js
```

Implementation report:

```text
SESSION_LIFECYCLE_IMPLEMENTATION_REPORT.md
```

No firmware source, RouterWorker, RouterPlatform, MikroTikDriver, API, storage,
or CSS file was changed for this implementation.

## 9. Captive Portal Deployment

Build command executed:

```powershell
$env:RENZFI_APPLIANCE_BASE_URL = "http://10.10.10.2"
npm run build:mikrotik-portal
```

Upload exactly:

```text
deployment/mikrotik-hotspot/login.html
  -> MikroTik Files/hotspot/login.html

deployment/mikrotik-hotspot/renzfi-app.js
  -> MikroTik Files/hotspot/renzfi-app.js
```

Do not upload CSS, MD5, images, audio, admin launcher, XML, or RouterOS scripts
for this change.

## 10. MikroTik Changes Required

**MikroTik configuration changes: NONE**

No change is required to:

- Hotspot profile.
- Firewall.
- NAT.
- Walled garden.
- Scheduler.
- Scripts.
- Proxy.
- Bridge.
- DHCP.
- DNS.
- RouterOS users or profiles.

Only the two generated static portal files require upload.

## 11. RouterWorker Verification

The existing worker remains responsible for:

- Activation admission.
- Serialized RouterOS execution.
- Deauthorization.
- Result publication.
- Portal outcome consumption.

It was not changed.

Existing activation remains:

```text
one RouterOS API session
approximately four functional commands
```

Existing expiration remains event-driven and executes once per expired session.

## 12. CPU and Resource Impact

### RouterOS

```text
Additional RouterOS commands: 0
Additional RouterOS sessions: 0
Additional polling: 0
Idle RouterOS commands: 0 commands/minute
Expected RouterOS CPU impact: none
```

### ESP32

```text
Firmware changes: none
Additional ESP32 tasks: 0
Additional ESP32 loops: 0
Additional RouterWorker work: 0
Additional API requests: 0
Additional storage writes: 0
Heap impact: none
DMA impact: none
PSRAM impact: none
Flash-write impact: none
```

### Browser

The existing one-second timer now performs:

- A few integer comparisons.
- Two one-shot boolean checks.
- Notice visibility updates at threshold crossings.

No additional `setInterval`, `setTimeout`, API poll, heartbeat, or EventSource
was added.

## 13. Regression Analysis

| Subsystem | Impact |
|---|---|
| Coin ISR | None |
| Coin timing/debounce | None |
| Coin accumulation | None |
| Promo accumulation | None |
| Voucher flow | None |
| Session activation | Backend unchanged |
| Add Time | Existing backend flow unchanged; warning flags re-arm |
| RouterWorker | None |
| RouterPlatform | None |
| MikroTikDriver | None |
| Router Sync | None |
| Router cache | None |
| WAN monitoring | None |
| Admin Dashboard | None |
| Authentication/RBAC | None |
| HTTP plane | None |
| InstallationState | None |
| Storage/SPIFFS/SD | None |
| Portal heartbeat | Frequency unchanged |
| Portal polling | Frequency unchanged |
| Countdown | Existing timer reused |
| Captive redirect | HTML route/tokens unchanged |
| RouterOS command budget | Unchanged |
| Idle CPU | No background work added |

### Primary portal risks

- Browser cache may retain old JavaScript after upload.
- `login.html` and `renzfi-app.js` must be deployed together because the JS
  expects the new `sessionNotice` element.
- Hidden or closed mobile captive-assistant pages cannot guarantee timely
  in-page warnings.
- Existing browser timer throttling can delay visual alerts while the page is
  backgrounded.

Phone OS notifications were not added. Reliable phone notifications would
require secure-origin/service-worker/native-app architecture and are outside
this localized change.

## 14. Verification Performed

### JavaScript syntax

```text
node --check portal/renzfi-app.js
PASS

node --check deployment/mikrotik-hotspot/renzfi-app.js
PASS
```

### IDE diagnostics

```text
No linter errors in changed canonical files.
```

### Portal build

```text
npm run build:mikrotik-portal
PASS
```

The generated JavaScript contains:

```text
RENZFI_APPLIANCE_BASE_URL = "http://10.10.10.2"
```

### Existing portal contract test

```text
npm run test:portal
FAIL
```

The failure is the previously identified test expectation mismatch:

```text
Expected two validation messages.
Validator currently returns four messages for the intentionally unresolved
placeholder fixture.
```

The failure occurs before exercising the lifecycle changes and was not modified
because it is outside this localized implementation.

## 15. Hardware Validation Checklist

### Status lifecycle

- Fresh portal displays Disconnected.
- Opening the coin window displays Waiting for Payment.
- Done Paying displays Activating.
- Successful RouterOS authorization displays Connected.
- RouterOS failure displays Activation failed, not stale Activating.
- Countdown zero displays Disconnected without refresh.
- Insert Coin returns without Wi-Fi reconnection.

### Warnings

- 31→30 seconds shows the 30-second message once.
- The 30-second message disappears automatically.
- 16→15 seconds shows the 15-second message once.
- The 15-second message disappears automatically.
- No warning repeats every second.
- Pause suppresses/hides warnings.
- Resume continues countdown correctly.
- Add Time above a threshold re-arms the applicable warning.
- A new session re-arms both warnings.
- Zero hides any warning.

### Activation and disconnect

- Confirm `/ip/hotspot/active/login` grants access.
- Confirm worker success changes session to active.
- Confirm `secondsLeft` equals purchased minutes × 60.
- Confirm natural expiry queues one deauthorization.
- Confirm active row removal disconnects Internet.
- Confirm portal returns to Disconnected immediately.
- Confirm expired record cleanup remains intact.

### Stability

- Idle RouterOS command count remains zero per minute.
- Activation command count remains unchanged.
- No added request appears at 30 or 15 seconds.
- No watchdog reset.
- No Guru Meditation.
- No change in ESP32 heap/DMA behavior.
- No RouterOS CPU spike.
- Captive redirect remains functional.
- Voucher login remains functional.
- Admin Dashboard and Synchronize Router remain functional.

### Deployment

- Upload both generated files.
- Confirm MikroTik file timestamps/sizes changed.
- Open a private/incognito session to avoid stale JavaScript.
- Validate on Android, iOS, Windows, and captive-assistant browsers.

## 16. Release Verdict

**IMPLEMENTED — HARDWARE VALIDATION REQUIRED**

The implementation is confined to the customer portal, uses the existing timer
and session data, adds no RouterOS or ESP32 background activity, and preserves
the current production architecture.
