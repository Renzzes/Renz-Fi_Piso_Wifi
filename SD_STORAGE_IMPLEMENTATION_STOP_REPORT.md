# SD Storage Production Hardening — Pre-Implementation STOP Report

Date: 2026-08-09  
Status: **STOPPED BEFORE FIRMWARE MODIFICATION**  
Reason: the requested guarantees contain unresolved compatibility conflicts and source-proven regression risks

## Decision

No firmware, Admin, portal, RouterOS, build-script, or storage implementation file was modified.

The implementation was stopped because the request explicitly requires stopping before code changes when existing functionality is at risk. The pre-change dependency analysis found multiple such risks, including one contract contradiction that cannot be resolved safely without a product/storage decision.

## Blocking contract conflict

The requested degraded-mode contract requires voucher redemption to continue while the SD card is unavailable.

At the same time:

- The voucher database and voucher history must be SD-canonical.
- Tier 1 internal flash is limited to boot-critical state and lists only the current active-session checkpoint, not unused voucher eligibility.
- Internal flash must not contain growing databases.
- RAM cannot be used as deferred durable storage.

An unused voucher can only be validated and atomically reserved if its code, validity, status, and device binding are available in durable storage. After an SD-less cold boot, RAM does not contain that catalog. The current SPIFFS fallback can synthesize an empty voucher array, which makes existing unused vouchers unavailable.

Therefore all four guarantees cannot simultaneously be true:

1. Full offline voucher redemption during an SD outage.
2. Voucher database exists only on SD.
3. No bounded unused-voucher eligibility checkpoint in internal flash.
4. Correct behavior after a cold boot with no SD.

### Minimal safe resolution

Permit a **fixed-size, boot-critical voucher eligibility checkpoint** in Tier 1. It would contain only currently redeemable voucher records required for degraded operation, remain bounded by bytes/records, exclude history, and fail closed before RouterOS authorization if capacity is exhausted.

Without that exception, the safe behavior is to keep active voucher sessions/reconnect working but reject redemption of previously unused vouchers while SD is unavailable. That would violate the requested “voucher redemption continues” guarantee.

## Additional source-proven regression risks

### 1. Paid activation can outrun failed persistence

The portal deferred-work path can continue toward activation after a session or accounting persistence failure. Storage hardening must guarantee durable entitlement/accounting state before enqueueing RouterOS activation. Changing this sequence is high risk because it touches the working Done Paying → RouterWorker boundary.

Required invariant:

- Failed persistence produces zero RouterOS activation commands.
- Retry remains event-driven and bounded.

### 2. Existing fallback recovery can overwrite valid SD data

Current recovery copies whole SPIFFS fallback files over SD files. Replacing this with generation-aware merge changes boot and recovery semantics across settings, promos, router configuration, vouchers, sales, installation, setup, and portal sessions.

Required invariant:

- A replacement or independently modified SD card must create a conflict, never silent overwrite.
- Legacy fallback manifests require an idempotent migration path.

### 3. First failed SD write is currently lost

The first failed write changes the backend state but is not retried against fallback. Retrying safely requires serializing storage operations and ensuring the exact original payload is committed once.

Required invariant:

- No duplicate sale, voucher transition, or session transition after retry.
- Concurrent AsyncTCP and loop writes cannot interleave transaction artifacts.

### 4. Atomic replace is not currently power-loss safe

Both SD and SPIFFS JSON paths delete the destination before renaming the temporary file. Correcting this requires backup generations, validation, rollback, and boot recovery. FAT/SPIFFS rename behavior must be validated on hardware.

### 5. Append-only ledgers change report semantics

Current sales reports read a bounded 20-record `sales.json`. Reading all append-only history would preserve API shape but change totals/history behavior from bounded recent data to full historical data. This is desirable for the new requirement but is still an observable behavior change.

Required decision:

- Existing endpoints should report full ledger history, or
- Existing endpoints retain current bounded semantics and new endpoints expose complete history.

The prompt disallows API redesign, so this behavior must be explicitly selected.

### 6. Monthly ledgers require a clock fallback

The device clock may not be ready when an event occurs. A strict `YYYY-MM.ndjson` requirement cannot safely classify those events.

Minimal safe rule:

- Write pre-clock events to `undated.ndjson`.
- Preserve the original monotonic/boot identity.
- Do not silently move or retimestamp records later.

### 7. Safe restore is intentionally stricter

Adding full CRC, path, size, schema, duplicate-entry, and archive validation will reject malformed backups that the current permissive restore might accept. This is a security and integrity improvement but an observable compatibility change.

Required invariant:

- Existing valid version-1 backups remain accepted.
- Invalid or ambiguous backups fail before any live file changes.

### 8. Multi-file restore cannot be physically atomic

A restore updates several files. It requires staging, a commit journal, reverse-order rollback, and boot recovery. This is a significant internal storage-state-machine addition even though no public API changes.

### 9. Existing factory reset and quota gaps

Current cleanup/accounting omits some files:

- Fallback cleanup omits provisioning, router-connection, and setup-wizard state.
- Factory reset does not consistently remove their SD counterparts.
- Fallback aggregate accounting omits existing-network scan.

Implementing new checkpoints before fixing these gaps could retain stale owner/setup/router state after reset.

### 10. Streaming upload authentication/concurrency

Current upload state is global and full-buffered. Streaming directly to a temporary file is safe only after:

- Authentication is verified before the first chunk is written.
- Concurrent uploads are rejected or isolated.
- Disconnect cleanup cannot delete a response-owned file.
- Metadata failure restores the previous asset.

### 11. SPIFFS mount safety

One boot path uses format-on-mount-failure behavior. A transient mount fault can therefore erase Admin SPA, recovery portal assets, and emergency checkpoints. Tier 1 cannot be considered reliable until formatting is removed from normal boot and made an explicit recovery action.

## Pre-change regression map

| Domain | Storage dependency | Required invariant |
|---|---|---|
| Coin detection / anti-double-coin | GPIO/ISR and settings | Storage work must not touch pulse timing or ISR paths |
| Coin accumulation | Portal session + promo/settings | Existing accumulation result and preview contract remain identical |
| Done Paying | Session/accounting persistence before activation | No RouterOS activation unless durable commit succeeds |
| Voucher redemption | Voucher record + portal session + sale | One voucher/one MAC; atomic reserve; no duplicate sale |
| Voucher reconnect | Active session/voucher checkpoint | Remaining time and MAC binding survive reboot |
| Promo | `promos.json` | Current rates available without SD or payment fails closed |
| Sales | `sales.json` and proposed ledger | Existing API schema and idempotent sale IDs remain stable |
| Captive Portal | SPIFFS assets + session APIs | No route, asset, redirect, or control changes |
| RouterWorker | Event queue only | No new worker, polling, or command path |
| Router Cache / Sync | Rebuildable SD cache | No implicit RouterOS refresh; explicit sync only |
| Provisioning | Multiple setup JSON files | Frozen six-step wizard and resume behavior remain unchanged |
| Recovery | NVS + SPIFFS + SD reset paths | No stale setup/owner/router checkpoint survives reset |
| Auth / RBAC | NVS credentials, RAM sessions | No credential migration to removable media |
| Admin SPA | SPIFFS image | Remains available without SD |
| Hotspot / RouterOS | Router credentials + session state | Same commands and ordering; zero idle commands |
| W5500 | Initialized before SD on separate bus | Storage code must not alter bus order, host, or pins |
| Memory / DMA | JSON, vectors, task stacks | Streaming must improve heap without reducing DMA headroom |
| Installation State | SD + fallback | Missing SD must never imply Factory mode |

## Source files reviewed

Primary implementation boundaries reviewed:

- `ESP32_S3_Firmware/src/StorageManager.h`
- `ESP32_S3_Firmware/src/StorageManager.cpp`
- `ESP32_S3_Firmware/src/StoragePaths.h`
- `ESP32_S3_Firmware/src/StoragePaths.cpp`
- `ESP32_S3_Firmware/src/Config.h`
- `ESP32_S3_Firmware/src/SessionManager.h`
- `ESP32_S3_Firmware/src/SessionManager.cpp`
- `ESP32_S3_Firmware/src/PortalSessionManager.h`
- `ESP32_S3_Firmware/src/PortalSessionManager.cpp`
- `ESP32_S3_Firmware/src/VoucherManager.h`
- `ESP32_S3_Firmware/src/VoucherManager.cpp`
- `ESP32_S3_Firmware/src/Logger.h`
- `ESP32_S3_Firmware/src/Logger.cpp`
- `ESP32_S3_Firmware/src/BackupManager.h`
- `ESP32_S3_Firmware/src/BackupManager.cpp`
- `ESP32_S3_Firmware/src/AssetManager.h`
- `ESP32_S3_Firmware/src/AssetManager.cpp`
- `ESP32_S3_Firmware/src/ApiServer.h`
- `ESP32_S3_Firmware/src/ApiServer.cpp`
- `ESP32_S3_Firmware/src/FirmwareApp.cpp`
- `ESP32_S3_Firmware/src/InstallationStateManager.cpp`
- `ESP32_S3_Firmware/src/NetworkSettingsManager.cpp`
- `ESP32_S3_Firmware/src/AuthManager.cpp`
- `ESP32_S3_Firmware/src/router/drivers/MikroTikDriver.cpp`
- `ESP32_S3_Firmware/src/RouterProvisioningWorker.cpp`
- `ESP32_S3_Firmware/partitions_custom.csv`
- `ESP32_S3_Firmware/platformio.ini`
- Existing storage, setup, Ethernet/SD isolation, router cache, and RBAC guard scripts

## Proposed implementation boundary after contract resolution

### Stage 1 — mandatory integrity foundation

- Transactional single-file writes with verified `.new`, rollback generation, and boot recovery.
- Explicit mounted/readable/writable/read-only state.
- Retry the exact first failed write once, then commit to an allowed bounded checkpoint.
- Storage serialization mutex.
- Generation/CRC sidecars without changing application JSON schemas.
- Safe v1 fallback-manifest migration and conflict detection.
- Fix factory-reset and fallback quota omissions.
- Remove format-on-normal-mount-failure behavior.

### Stage 2 — bounded degraded operation

- Last-known-good installation, router, promo, and active entitlement checkpoints.
- Agreed bounded voucher eligibility checkpoint, if approved.
- Bounded emergency accounting journal.
- Fail closed before activation when no durable commit is possible.

### Stage 3 — append-only SD history

- Sales ledger.
- Completed-session ledger.
- Voucher terminal-event ledger.
- Rotated log ledger.
- Deterministic event IDs and torn-final-line tolerance.
- Bounded compatibility indexes preserving current API schemas.

### Stage 4 — streaming and backup hardening

- Stream asset uploads to staged files.
- Stream backup entries and exports.
- Validate full archive before restore.
- Commit/rollback journal for restore.
- Preserve valid version-1 backup compatibility.

## RouterOS and CPU impact

The proposed storage-only architecture requires:

- Zero new RouterOS commands.
- Zero idle RouterOS API commands.
- No RouterOS scripts or scheduler.
- No new worker or busy loop.
- No implicit router synchronization.

All storage work must be triggered by existing lifecycle events, explicit owner requests, boot recovery, or the existing storage-health path.

## Required decisions before implementation

1. Approve a fixed-size Tier 1 voucher eligibility checkpoint, or accept that unused voucher redemption is unavailable while SD is absent.
2. Confirm whether existing sales report endpoints should switch to complete ledger history or retain their current bounded-recent semantics.
3. Confirm that malformed/ambiguous legacy backups may be rejected while all valid version-1 backups remain supported.

## Release verdict

**NO-GO — implementation intentionally not started.**

The storage architecture can be hardened without RouterOS or UI redesign, but the voucher degraded-mode guarantee and reporting compatibility must be resolved first. Starting code changes before those decisions would violate the explicit zero-regression and STOP requirements.
