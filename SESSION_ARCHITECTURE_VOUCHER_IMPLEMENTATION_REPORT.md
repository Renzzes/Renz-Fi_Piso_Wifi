# Session Architecture Hardening and Single-Device Voucher Implementation

**Date:** 2026-08-09  
**Architecture:** Existing PortalSessionManager → RouterWorker → RouterOS path retained  
**Release verdict:** **IMPLEMENTED — HARDWARE VALIDATION REQUIRED**

## Root Cause

The previous voucher feature was not connected to the production session
architecture:

- Admin vouchers existed only as local CRUD records.
- The customer voucher form posted directly to MikroTik.
- ESP32 never validated, bound, activated, accounted, expired, or archived a
  customer voucher.
- Coin and portal sales could write the sales file through separate paths.
- RouterWorker used a mutable shared slot and an overwriteable Hotspot outcome.
- Browser heartbeat expiry could delete active entitlement without RouterOS
  cleanup.

## Resulting Architecture

```text
Coin entitlement ──────┐
                       ├─> PortalSessionManager
Voucher entitlement ───┘       ├─> SessionManager accounting/history
                               └─> RouterProvisioningWorker
                                      └─> RouterPlatform
                                             └─> MikroTikDriver
                                                    └─> RouterOS
```

Coin and voucher sessions now use the same:

- session document;
- activation queue;
- RouterWorker;
- RouterOS user/profile/uptime operation;
- activation outcome;
- countdown;
- expiration/deauthorization;
- sales authority;
- retained history.

The entitlement source is identified by `source = portal|voucher`.

## Session Hardening

- RouterWorker request payload is prepared locally and copied while holding the
  dispatch mutex.
- `_running` is reserved before the wake byte is queued.
- Async job IDs are assigned before queue publication.
- Hotspot outcomes use a one-entry FreeRTOS queue.
- Another Hotspot job is rejected until the previous outcome is consumed.
- Router-busy retries are driven by worker-idle notification, not loop
  re-enqueue churn.
- Cleanup failures retry at most three times per causal cleanup event.
- Active/activating/paused entitlement is no longer deleted because browser
  heartbeat stopped.
- Activating and cleanup-pending sessions are replayed after reboot.
- Late activation and pause outcomes cannot revive an expired session.
- `expiring` is now an explicit state until RouterOS confirms deauthorization.

## Single-Device Voucher Contract

Voucher lifecycle:

```text
unused → redeeming → active → expired → archived
                  ↘ activation retry

unused/active/redeeming → disabled (owner only)
```

Implemented rules:

- Codes are normalized to uppercase.
- Code generation checks collisions.
- First redemption permanently binds the voucher to one normalized MAC.
- Same-MAC redemption/reconnect is idempotent.
- A different MAC is rejected.
- Voucher reservation is persisted before activation.
- RouterOS activation uses the existing Hotspot activation path.
- Activation success commits the voucher and sale.
- A failed voucher/accounting commit after RouterOS authorization immediately
  enters cleanup instead of exposing unaccounted Internet.
- Voucher expiry uses absolute wall time and the existing one-second local
  session tick; no RouterOS polling was added.
- Returning customers rely on the retained RouterOS user/cookie. If RouterOS
  presents the portal, one explicit reconnect activation is issued.
- Customer coin, Pause, Resume, and Terminate actions are rejected for voucher
  sessions by firmware and hidden in the portal.
- Owner can terminate, expire, disable, and archive through authenticated APIs.

Voucher generation supports:

- count;
- optional custom code for a one-voucher generation;
- amount;
- minutes/validity;
- redeem-before date;
- RouterOS bandwidth profile;
- display speed.

Only an existing RouterOS bandwidth profile is applied. No RouterOS scheduler,
script, firewall, NAT, proxy, or dynamic profile polling was introduced.

## Unified Sales and Session History

`SessionManager` is now the mutex-protected sales mutation authority.

Stored fields include:

- schema version;
- sale ID and session ID;
- coin/voucher type;
- amount/revenue;
- purchased minutes;
- credits;
- MAC and IP;
- voucher code;
- promo/profile/speed metadata;
- redemption/expiration metadata;
- activation, pause, resume, completion timestamps;
- connected duration;
- operator;
- status and termination reason.

Coin payment records and voucher records use the same `/sales/sales.json`
authority. Session completion updates every retained transaction linked to the
session ID.

Embedded retention is intentionally bounded:

```text
Maximum records: 20
Maximum serialized sales JSON: 12 KiB
```

This prevents the previous unbounded whole-file JSON behavior from consuming
increasing RAM, SD latency, and SPIFFS wear. No second database was created.

## Captive Portal

The visible voucher form now calls:

```text
POST /api/portal/voucher/redeem
POST /api/portal/voucher/reconnect
```

It no longer authenticates generated vouchers directly through the RouterOS
login form.

The browser:

- shows voucher validation and activation state;
- reuses the existing activation wait;
- hides coin/Pause/Resume/Terminate controls for voucher sessions;
- attempts one reconnect when RouterOS presents the portal for an active bound
  voucher;
- introduces no additional interval or RouterOS polling.

## Admin Dashboard

Implemented:

- richer voucher generation;
- all voucher lifecycle statuses;
- bound-device/profile/speed visibility;
- owner Terminate, Disable, and Archive actions;
- voucher Pause/Resume controls hidden in Active Users;
- unified transaction/session history table;
- daily table `Transactions` column corrected.

## Files Changed

Firmware:

```text
ESP32_S3_Firmware/src/RouterProvisioningWorker.h
ESP32_S3_Firmware/src/RouterProvisioningWorker.cpp
ESP32_S3_Firmware/src/PortalSessionManager.h
ESP32_S3_Firmware/src/PortalSessionManager.cpp
ESP32_S3_Firmware/src/VoucherManager.h
ESP32_S3_Firmware/src/VoucherManager.cpp
ESP32_S3_Firmware/src/SessionManager.h
ESP32_S3_Firmware/src/SessionManager.cpp
ESP32_S3_Firmware/src/Models.h
ESP32_S3_Firmware/src/ApiServer.cpp
ESP32_S3_Firmware/src/FirmwareApp.cpp
```

Portal/build:

```text
portal/login.html
portal/renzfi-app.js
portal/renzfi-style.css
scripts/build-mikrotik-portal.mjs
scripts/test-portal-resolver.mjs
deployment/mikrotik-hotspot/*
Final_Build_Portal/*
```

Admin:

```text
src/types/api.ts
src/services/vouchers.ts
src/services/sales.ts
src/hooks/api/useVouchers.ts
src/pages/VouchersPage.tsx
src/pages/ActiveUsersPage.tsx
src/pages/SalesReportsPage.tsx
ESP32_S3_Firmware/data/*
```

## MikroTik Configuration

**MikroTik configuration changes required: NONE**

## RouterOS API Command Budget

```text
Idle:                  0 commands/minute
Voucher redemption:    4 commands
Voucher reconnect:     4 commands, only when captive page requests reconnect
Coin activation:       4 commands
Resume:                4 commands
Pause:                 approximately 3–12 commands
Expiration/terminate:  approximately 4–14 commands
```

Cookie-dependent ranges remain bounded by the existing maximum targeted cookie
removals.

## Resource Impact

Production firmware:

```text
Static RAM: 104,180 / 327,680 bytes (31.8%)
Flash:      2,321,603 / 2,621,440 bytes (88.6%)
```

Installer profile:

```text
Static RAM: 104,180 / 327,680 bytes (31.8%)
Flash:      2,324,219 / 2,621,440 bytes (88.7%)
```

Runtime additions:

- one VoucherManager mutex;
- one one-item Hotspot outcome queue replacing the prior mutex/mailbox;
- bounded temporary sales JSON documents;
- no continuous task;
- no extra timer;
- no idle RouterOS work.

Flash headroom is approximately 11.3%; additional feature work should remain
size-conscious.

## Final_Build_Portal

`npm run build:mikrotik-portal` now automatically synchronizes the owner upload
folder.

Upload exactly:

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
Overlay the existing RouterOS Hotspot directory; do not delete native RouterOS
servlet files.

## Regression Verification

Passed:

- Production firmware build: `freenove_esp32_s3_wroom`
- Installer firmware build: `renzfi_installer`
- Embedded Admin production build
- Changed Admin source ESLint
- Portal JavaScript syntax
- Portal resolver tests
- MikroTik portal build
- ESP32 Admin/recovery asset staging
- Final_Build_Portal hash comparison
- IDE diagnostics for changed files

The repository-wide TypeScript typecheck still reports pre-existing unrelated
issues in carousel/chart dependencies, ProvisioningContext, and `usePromos`.
The production Vite build succeeds.

## Hardware Validation Checklist

1. Redeem a new voucher and verify `unused → redeeming → active`.
2. Attempt the same voucher from another MAC and verify rejection.
3. Reconnect the bound device and verify immediate RouterOS/cookie access.
4. Force captive-page reconnect and verify only one four-command activation.
5. Reboot ESP32 during redeeming and active states.
6. Reboot during cleanup and verify cleanup replay.
7. Power off longer than voucher validity and verify absolute expiration.
8. Verify expired/disabled vouchers cannot reconnect.
9. Verify owner Terminate removes active user, Hotspot user, and cookies.
10. Verify voucher users cannot Pause, Resume, Terminate, or insert coins.
11. Validate coin denomination accumulation and Add Time unchanged.
12. Run simultaneous Admin Sync and customer activation.
13. Validate RouterWorker MAC/job/outcome correlation.
14. Force RouterOS cleanup failure and verify maximum three retries.
15. Confirm idle RouterOS API traffic remains zero.
16. Confirm RouterOS CPU during activation/reconnect/expiry.
17. Fill sales retention and verify bounded 20-record behavior.
18. Validate SD removal and SPIFFS fallback.
19. Run 24–72-hour heap, DMA, and watchdog soak.
20. Upload only `Final_Build_Portal` overlay and verify captive redirect.

## Release Verdict

**IMPLEMENTED — HARDWARE VALIDATION REQUIRED**

Source builds and software regression gates pass. Release Candidate status
requires the hardware checklist, especially single-device binding, reboot
recovery, absolute expiration, RouterOS cleanup failure, CPU, and long-duration
memory validation.
