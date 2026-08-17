# Renz-Fi Release Candidate — Final Validation Report

**Date:** 2026-08-09  
**Priority:** Production stability  
**Architecture rewrite:** None  
**Release verdict:** **NO-GO — NOT YET A RELEASE CANDIDATE**

## Executive Summary

The existing Renz-Fi architecture remains the correct production direction:

```text
Captive Portal / Admin
  -> ESP32 APIs
  -> PortalSessionManager
  -> bounded deferred work
  -> RouterProvisioningWorker
  -> RouterPlatform
  -> MikroTikDriver
  -> RouterOS API
```

Normal activation, Pause/Resume, termination, and expiration are event-driven.
Production idle RouterOS API activity remains zero.

The release cannot yet be classified as a Release Candidate because source
review found unresolved lifecycle and data-integrity blockers:

1. Portal activation work can be lost after state becomes `activating`.
2. RouterWorker uses a mutable shared slot behind a one-byte wake queue.
3. Hotspot outcomes use one overwriteable mailbox.
4. Failed expiration/termination cleanup has no established automatic retry.
5. Stale heartbeat cleanup can delete active/paused ESP32 session state without
   RouterOS deauthorization.
6. Reboot recovery does not reconcile activating or cleanup-pending sessions.
7. Voucher administration is not connected to customer redemption.
8. Voucher sales and unified session history do not exist.
9. Rich history is unsafe on the current unbounded whole-file sales JSON.
10. The portal contract test is not green.

No high-risk firmware feature was added during this phase.

The required owner-facing `Final_Build_Portal/` overlay was created and verified.

## 1. Current Architecture

### Customer coin sessions

```text
Coin ISR
  -> CoinManager pulse grouping and denomination
  -> PortalSessionManager credits + purchasedMinutes
  -> Done Paying sale reservation
  -> Portal deferred queue
  -> RouterWorker activation
  -> MikroTik Hotspot user add/set
  -> MikroTik active login/set
  -> RouterWorker outcome
  -> Portal session active/connected
```

### Expiration and termination

```text
PortalSessionManager timer or Terminate request
  -> state expired/disconnected locally
  -> ExpireSession deferred work
  -> RouterWorker deauthorization
  -> RouterOS active remove
  -> RouterOS user remove
  -> targeted cookie removal
  -> cleanup outcome
```

### Pause and Resume

The current implementation already follows the recommended behavior:

```text
Pause
  -> freeze ESP32 countdown
  -> remove RouterOS active access
  -> clear targeted cookies
  -> preserve Hotspot user and remaining time

Resume
  -> reuse RouterWorker activation
  -> reauthorize RouterOS
  -> continue countdown only after successful outcome
```

### Portal source ownership

```text
portal/                             canonical editable source
deployment/mikrotik-hotspot/        generated engineering overlay
Final_Build_Portal/                 owner-facing release upload overlay
Captive Portal/                     deprecated legacy/reference tree
ESP32_S3_Firmware/data/portal/      generated setup/recovery staging
```

## 2. Proposed Architecture

No replacement architecture is proposed.

The safe direction is to preserve existing ownership boundaries:

- `PortalSessionManager` remains authoritative for customer session state.
- `VoucherManager` remains the sole firmware voucher authority.
- `RouterProvisioningWorker` remains the only RouterOS execution path.
- RouterOS remains the enforcement plane.
- ESP32 remains the accounting/session plane.
- Captive Portal remains a presentation/API client.
- Admin Dashboard remains a management client.

### Future voucher extension

When contracts are approved:

```text
Portal voucher entry
  -> ESP32 voucher redemption endpoint
  -> VoucherManager atomic reservation
  -> PortalSessionManager voucher-backed session
  -> existing RouterWorker activation
  -> RouterOS authorization
  -> outcome commits voucher and sale
```

No RouterOS polling, bulk voucher synchronization, RouterOS script, or second
database is required.

### Future sales/history extension

Coin and voucher should emit one versioned ESP32-local sale/session event shape.
Do not add requested rich fields directly to the current unbounded whole-file
JSON array until bounded retention or a rotated/streamed local journal is
approved.

## 3. Root-Cause Analysis

### Activating can remain stuck

Source-proven mechanisms:

- `enqueueActivateSession()` return is ignored after local reservation.
- Worker-busy retry enqueue failure is ignored.
- RouterWorker payload can be overwritten between wake enqueue and
  `_running=true`.
- A second Hotspot outcome can overwrite the first.
- Outcome publication can fail on mutex timeout.
- A reboot can restore `activating` without requeueing activation.
- Synchronous sale/session storage can block FIFO activation processing.

The exact physical-device cause still requires a matching runtime trace.

### Expiration may not guarantee revocation

Normal expiration correctly queues RouterOS deauthorization.

Failure gaps:

- Failed deauthorization remains pending without an established automatic retry.
- Queue-busy abandonment clears queued state without scheduling another attempt.
- Stale-heartbeat cleanup can delete an active/paused record without first
  scheduling RouterOS cleanup.

Therefore source alone cannot guarantee that every failure case revokes Internet.

### Voucher framework is incomplete

Current Admin vouchers:

- Generate/list/find/delete local JSON records.
- Store code, amount, minutes, status, and expiry text.

Current customer voucher form:

- Posts directly to MikroTik `$(link-login-only)`.
- Does not call ESP32.

Missing:

- ESP32 redemption endpoint.
- Voucher validation/reservation.
- MAC/IP binding.
- RouterWorker authorization.
- Voucher sale.
- Used/expired lifecycle.
- Device/data/speed enforcement.
- Persistent reconnect contract.
- Owner termination of a voucher-backed session.

### Sales/history is incomplete

Coin sales store:

- ID
- timestamp/recorded time
- amount
- session ID
- MAC
- payment type
- purchased duration

Voucher sales do not exist.
Session lifecycle history does not exist.
The current UI displays daily aggregate history only.

## 4. Final Session Validation

### Normal path

| Transition | Source verdict |
|---|---|
| Disconnected → Waiting | Implemented |
| Waiting → credits accumulated | Implemented |
| Done Paying → Activating | Implemented |
| Activating → RouterWorker | Implemented, but enqueue-loss paths exist |
| RouterWorker → RouterOS | Implemented |
| RouterOS active login → Internet | Implemented |
| Worker success → Connected | Implemented |
| Connected countdown | Implemented |
| 30/15-second warnings | Implemented in portal |
| Zero → local Disconnected | Implemented |
| Zero → RouterOS deauthorization | Implemented, but failure retry gap exists |

### Add Time

Current accumulated entitlement path is correct:

```text
each denomination's configured minutes
  -> accumulated purchasedMinutes
  -> Done Paying saleMinutes
  -> existing seconds + purchased seconds
  -> RouterOS total limit-uptime
```

Preview and activation use the same accumulated session value.

## 5. Terminate Session

Already implemented.

Current behavior:

1. Customer confirms termination.
2. Portal API calls `PortalSessionManager::terminateSession()`.
3. Credits, inserted amount, pending purchased minutes, and remaining seconds
   are cleared.
4. State becomes expired/disconnected.
5. One RouterWorker deauthorization is queued.
6. RouterOS active session, user, and targeted cookies are removed.
7. Browser immediately displays Disconnected.

Constraint:

The HTTP response is local and asynchronous. It does not wait for RouterOS
cleanup confirmation. The existing cleanup failure gap prevents a universal
revocation guarantee.

No Terminate implementation change was made.

## 6. Pause / Resume

The requested revoke-and-reauthorize architecture already exists.

No maximum pause count exists.

If introduced later, the safest scope is **per session**, not per device:

- Session state already owns pause lifecycle.
- Device-wide counting would require cross-session durable history.
- Increment the count only after a successful RouterOS pause outcome.
- Preserve remaining time across reboot.
- Define whether the fourth request is rejected or terminates the entitlement.

This feature is not safe to add before:

- stale-heartbeat cleanup preserves paused sessions;
- late outcomes cannot overwrite terminated state;
- cleanup retry semantics are defined;
- reboot recovery handles paused/resume-pending state.

No Pause/Resume code was changed.

## 7. Voucher System Forensic

### Implemented

- Owner-only Admin voucher page.
- Owner-only firmware list/create/find/delete APIs.
- Local voucher JSON storage.
- Basic print-page action.
- Whole-appliance backup includes vouchers.
- Existing RouterWorker activation can technically support voucher sessions.

### Missing

- Customer ESP32 redemption.
- Atomic unused→redeeming→active/used state.
- Collision protection.
- Case-normalized lookup.
- RouterOS provisioning.
- Voucher-backed PortalSession.
- Voucher sales.
- Expiration enforcement.
- Persistent reconnect/reauthorization policy.
- Multi-device and unlimited-device semantics.
- Data caps.
- Speed/profile snapshot.
- Export template.
- Admin termination linked to voucher state.

### Persistent login

The expected three-day reconnect behavior is not implemented as an ESP32 voucher
contract.

It requires explicit decisions for:

- wall-clock versus consumed-time validity;
- RouterOS cookie/user persistence;
- reboot downtime;
- one-device versus multiple-device binding;
- entitlement transfer;
- clock-unavailable behavior;
- power-loss recovery.

Voucher implementation was rejected for this phase because these contracts are
not defined and the existing redemption bridge does not exist.

## 8. Sales Report and Session History

### Current

- Coin sale transaction file exists.
- Today/week/month totals exist.
- Daily aggregate history exists.
- CSV export exists but advertises fields not populated by current records.

### Missing requested fields

- IP snapshot.
- Voucher code.
- Promo ID/name.
- Credits snapshot.
- Speed snapshot.
- Expiration.
- Operator.
- Actual connected duration.
- Termination reason.
- Voucher revenue/event.

### Storage blocker

Every append reads and rewrites the entire sales array with a 24 KB JSON
document. Richer records reduce safe retention and increase SD/SPIFFS pressure.

No rich history was implemented.

## 9. MikroTik CPU Stabilization

### Command budget

| Operation | RouterOS API sessions | Functional command range |
|---|---:|---:|
| Idle portal/dashboard cached state | 0 | 0 commands/minute |
| Activation | 1 | 4 |
| Add Time | 1 | 4 attempted |
| Resume | 1 | 4 |
| Pause | 1 | approximately 3–12 |
| Terminate/expiration | 1 | approximately 4–14 |
| Voucher redemption | Not implemented | 0 |

Variable Pause/Terminate counts depend on active/user presence and zero-to-eight
targeted cookies.

### Stability controls retained

- One RouterWorker.
- RouterOS session serialization.
- Command pacing.
- Worker deadline.
- Transport backoff.
- DMA headroom checks.
- No idle RouterOS API polling.
- No RouterOS scheduler/script.

## 10. Captive Portal Cleanup

### Canonical

```text
portal/
```

### Generated

```text
deployment/mikrotik-hotspot/
ESP32_S3_Firmware/data/portal/
```

### Deprecated but not yet deletable

```text
Captive Portal/
```

Do not delete it until:

- the complete live router `hotspot/` tree is archived;
- native RouterOS servlet files are verified;
- RouterOS v6/v7 hardware validation passes;
- rollback artifacts are preserved.

### Remote RouterOS files that must be preserved

- `alogin.html`
- `redirect.html`
- `status.html`
- `logout.html`
- `error.html`
- `errors.txt`
- `api.json`
- WISPr/XML/advertisement files when enabled

`Final_Build_Portal/` is an overlay, not a full replacement directory.

## 11. Files Reviewed

Primary firmware:

```text
PortalSessionManager.cpp/.h
RouterProvisioningWorker.cpp/.h
RouterPlatform.cpp
MikroTikDriver.cpp
RouterOsClient.cpp
VoucherManager.cpp/.h
SessionManager.cpp/.h
CoinManager.cpp/.h
PromoManager.cpp/.h
ApiServer.cpp/.h
StorageManager.cpp/.h
AuthManager.cpp/.h
RouterCacheManager.cpp/.h
InstallationStateManager.cpp/.h
```

Frontend/server/deployment:

```text
portal/*
deployment/mikrotik-hotspot/*
Captive Portal/*
ESP32_S3_Firmware/data/portal/*
src/pages/VouchersPage.tsx
src/pages/SalesReportsPage.tsx
src/pages/ActiveUsersPage.tsx
src/services/vouchers.ts
server/routes/vouchers.ts
server/services/mikrotik/vouchers.ts
scripts/build-mikrotik-portal.mjs
```

## 12. Files Changed in This Phase

Created release overlay:

```text
Final_Build_Portal/README.md
Final_Build_Portal/login.html
Final_Build_Portal/renzfi-app.js
Final_Build_Portal/renzfi-style.css
Final_Build_Portal/md5.js
Final_Build_Portal/Default-Banner.png
Final_Build_Portal/bg_music.mp3
Final_Build_Portal/coin.mp3
Final_Build_Portal/success.mp3
```

Created report:

```text
RELEASE_CANDIDATE_FINAL_VALIDATION_REPORT.md
```

No production firmware, RouterWorker, RouterPlatform, MikroTikDriver, voucher,
sales, session-history, Admin, or portal source was changed during this phase.

## 13. MikroTik Configuration Changes Required

**MikroTik configuration changes required: NONE**

No firewall, NAT, Hotspot, scheduler, script, proxy, bridge, DHCP, or DNS change
is required.

## 14. Portal Files Requiring Upload

Owner upload source:

```text
Final_Build_Portal/
```

Upload exactly these eight files into the existing active RouterOS `hotspot/`
directory:

```text
login.html
renzfi-app.js
renzfi-style.css
md5.js
Default-Banner.png
bg_music.mp3
coin.mp3
success.mp3
```

Do not upload `README.md`.
Do not delete other remote Hotspot files.

## 15. Final_Build_Portal Validation

The folder contains:

```text
README.md
login.html
renzfi-app.js
renzfi-style.css
md5.js
Default-Banner.png
bg_music.mp3
coin.mp3
success.mp3
```

Verification:

- Every deployable file hash matches the generated
  `deployment/mikrotik-hotspot/` artifact.
- Final JavaScript syntax passes `node --check`.
- Generated JavaScript targets `http://10.10.10.2`.
- README identifies `http://10.20.0.1/login` as the customer gateway.

Excluded intentionally:

- `admin.html`
- `favicon.ico`
- `index.html`
- extensionless `admin`
- `New Text Document.txt`
- `.rsc` files
- migration documents
- legacy RouterOS servlet copies

## 16. CPU and ESP32 Memory Impact

This phase added no runtime firmware behavior.

```text
ESP32 CPU impact: none
RouterOS CPU impact: none
RouterOS commands added: zero
Idle RouterOS command rate: 0 commands/minute
ESP32 static RAM impact: none
ESP32 heap impact: none
DMA impact: none
PSRAM impact: none
Flash-write impact: none
SD/SPIFFS write impact: none
```

The portal lifecycle warnings implemented previously run in the existing browser
timer and add no ESP32/RouterOS work.

## 17. Regression Analysis

| Area | RC status |
|---|---|
| Coin ISR/anti-double pulse | Untouched |
| Coin accumulation | Implemented; hardware validation required |
| Promo rates | Untouched in this phase |
| RouterWorker | Untouched; race blockers remain |
| Router Sync/cache | Untouched |
| Admin Dashboard | Untouched |
| Captive redirect | Bundle retains RouterOS tokens and dependencies |
| Heartbeat/polling | Unchanged |
| Authentication/RBAC | Untouched |
| SPIFFS/SD | Untouched in this phase; history scalability blocker remains |
| Sales | Basic coin sales only |
| Voucher | CRUD shell only; redemption absent |
| Session history | Absent |
| WAN/Hotspot | Untouched |
| Timer | Existing event-driven countdown retained |
| Memory/DMA | No new firmware allocation |
| InstallationState | Untouched; recovery risk remains |

## 18. Hardware Validation Checklist

### Portal bundle

- Back up the full router `hotspot/` directory.
- Overlay the eight files only.
- Confirm native servlet files remain.
- Validate HTTP redirect on RouterOS v6 and v7.
- Validate CHAP login and wrong-password behavior.
- Validate cache/private-browser behavior.

### Coin lifecycle

- PHP1 + PHP1 = 10 minutes.
- Mixed denominations sum correctly.
- Preview equals sale minutes.
- Sale minutes × 60 equals ESP32 seconds.
- RouterOS limit matches ESP32 seconds.
- Done Paying double-click remains idempotent.

### Activation concurrency

- Activate while Admin Sync is running.
- Activate two customer sessions near-simultaneously.
- Force portal queue pressure.
- Force RouterWorker busy.
- Capture worker slot/job/outcome MAC correlation.
- Confirm no session remains activating.

### Pause/Resume/Terminate

- Pause removes active Internet and preserves time.
- Cookies cannot auto-restore paused access.
- Resume restores access once.
- Repeated Resume does not duplicate time/access.
- Terminate removes active/user/cookies.
- Late activation/pause outcomes cannot revive terminated state.

### Expiration

- Natural expiration revokes active access.
- Close portal for more than 120 seconds during an active session.
- Close portal for more than 120 seconds while paused.
- Reboot while active, paused, activating, and cleanup pending.
- Force deauthorization failure and verify deterministic recovery.

### Voucher

- Do not claim production voucher redemption until the ESP32 bridge exists.
- Inventory any externally created RouterOS voucher users separately.

### Storage/resources

- Run 24–72-hour heap/DMA/PSRAM soak.
- Exercise SD removal/recovery.
- Exercise SPIFFS fallback.
- Grow sales history to parser/retention boundaries.
- Verify no `.bad` rollover loses accounting unexpectedly.
- Verify OTA update and rollback.

### RouterOS CPU

- Capture idle, activation, pause, resume, termination, and sync CPU.
- Verify idle remains zero API commands/minute.
- Verify activation remains four commands.
- Verify variable cookie cleanup remains bounded.
- Verify no reconnect storm or RouterOS script activity.

## 19. Safe Implementation Plan

No further implementation is approved as one combined change.

Recommended isolated order:

1. Capture runtime evidence for the actual Activating failure.
2. Fix only the proven portal-queue or outcome boundary.
3. Prevent stale-heartbeat deletion from bypassing RouterOS cleanup.
4. Add deterministic reboot recovery for activating/paused/cleanup-pending
   sessions.
5. Correlate outcomes with expected lifecycle state before applying them.
6. Validate expiration/termination revocation on hardware.
7. Define voucher product contracts.
8. Implement voucher redemption as a dedicated feature using existing
   VoucherManager → PortalSessionManager → RouterWorker.
9. Define bounded sales/history retention before adding fields.
10. Implement per-session pause count only after lifecycle recovery is stable.

Every step must remain separately reversible and preserve zero idle RouterOS
commands.

## 20. Release Verdict

**NO-GO — NOT YET A RELEASE CANDIDATE**

The portal release bundle is prepared, but source-level activation, outcome,
cleanup, reboot, voucher, and history gaps remain. Hardware validation and
isolated lifecycle hardening are required before Release Candidate designation.
