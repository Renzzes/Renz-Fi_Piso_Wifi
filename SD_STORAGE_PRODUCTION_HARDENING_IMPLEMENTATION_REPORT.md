# SD Storage Production Hardening — Implementation Report

Date: 2026-08-09  
Scope: transactional storage, degraded SD operation, append-only history, streaming uploads/backups, restore integrity, and regression protection

## Release verdict

**Software integration: PASS. Production release: NO-GO pending hardware fault-injection validation.**

Both production and installer firmware targets compile. The focused storage regression guard, captive-portal tests, Admin production build, and SD/W5500/router-cache/RBAC guards pass.

Production release remains blocked until physical tests prove SD removal, read-only media, power-loss recovery, SPIFFS capacity behavior, heap/DMA margins, and unchanged RouterOS command counts on the appliance.

## Root cause

The former storage abstraction had five production-integrity boundaries:

1. SD and SPIFFS writes deleted the live JSON before promoting the temporary file.
2. The write that first detected SD failure was not retried against fallback.
3. SPIFFS fallback was not a maintained last-known-good core checkpoint.
4. Fallback recovery could overwrite divergent SD data without generation/conflict evidence.
5. Logs and historical records used whole-array rewrites or bounded snapshots instead of append-only history.

Large uploads, backup assets, and restore entries were also loaded into contiguous RAM buffers. Restore modified live files before validating the complete archive and ran rollback recovery after stateful managers had already loaded.

## Implemented storage architecture

### Tier 1 — internal flash

Retained:

- Firmware and OTA partitions.
- Admin SPA and recovery captive-portal assets.
- NVS owner/operator credentials.
- NVS boot network configuration.
- Bounded last-known-good checkpoints for:
  - Settings and current coin configuration.
  - Current promo definitions.
  - Router configuration and protected connection state.
  - Installation/provisioning/setup state.
  - Approved bounded voucher eligibility state.
  - Active portal-session entitlement state.

Excluded from continuous healthy-write checkpointing:

- Sales history.
- Logs.
- Router cache.
- Existing-network scan cache.
- Portal metadata/history.

During an SD outage, financial/session/voucher history uses bounded emergency spools. Capacity and minimum-free-space checks fail closed before unbounded flash growth.

### Tier 2 — SD

SD remains the canonical mutable store:

```text
/config/
/sales/
/sessions/
/vouchers/
/history/
    /sales/
    /sessions/
    /vouchers/
    /logs/
/logs/
/assets/
/reports/
/backups/
/backup/        legacy API-compatible working files
/exports/
/temp/
/cache/
```

Monthly history files use `YYYY-MM.ndjson`. Events recorded before wall-clock readiness use `undated.ndjson`.

### Tier 3 — RAM

RAM remains transient:

- Existing active objects and Router cache behavior are unchanged.
- Logger retains its existing 500-entry RAM view.
- Ledger deduplication keeps only 24 recent IDs.
- Large upload vectors were replaced by file-backed staging.
- ZIP backup/restore uses a 4 KiB transfer buffer.

No RAM-only financial operation is treated as durably committed.

## Transactional storage

Implemented:

- Recursive storage serialization across AsyncTCP and loop contexts.
- Bounded lock timeout.
- Verified `.new` staging.
- Previous-valid rollback candidate.
- Read-back verification.
- CRC/generation metadata outside application JSON schemas.
- Boot recovery of target/stage/backup candidates.
- Exact-payload SD retry.
- Fallback commit only after bounded SD retries fail.
- Mounted/readable/writable/read-only distinctions.
- Divergence detection before fallback reconciliation.
- No blind replacement of a different SD generation.
- Complete fallback quota accounting, including transaction artifacts and history spools.
- Corrected factory-reset cleanup for setup/provisioning fallback records.
- Normal boot no longer formats SPIFFS after a mount failure.

No public application JSON schema was changed by the storage transaction layer.

## Append-only history

Implemented four event-driven ledgers:

- Sales history.
- Completed-session history.
- Voucher lifecycle history.
- Log history.

Properties:

- One JSON object per line.
- Monthly or undated partition.
- Flush after each event.
- Torn final lines tolerated.
- Stable event IDs for financial/session/voucher events.
- Device/boot/sequence identity for log events.
- Bounded 32 KiB tail deduplication; logs require no ledger scan.
- Bounded SPIFFS spools for sales/session/voucher continuity.
- Idempotent replay through the existing storage-recovery path.
- Torn spool fragments are quarantined without blocking valid replay.
- Existing bounded sales APIs and voucher index behavior remain unchanged.

Legacy `logs.json` is retained but no longer rewritten for every log. Live log APIs continue using the unchanged RAM ring/SSE behavior.

Owner-only month-scoped history endpoints stream SD files directly:

- `/api/history/sales/download?month=YYYY-MM`
- `/api/history/sessions/download?month=YYYY-MM`
- `/api/history/vouchers/download?month=YYYY-MM`
- `/api/history/logs/download?month=YYYY-MM`

`month=undated` is also supported.

## Streaming and RAM hardening

### Asset uploads

- Authenticate before opening the staging file.
- Reject concurrent uploads.
- Stream chunks directly to SD.
- Enforce exact offsets, total bytes, extension, type, and magic signature.
- Verify staged size and checksum.
- Preserve the previous asset before promotion.
- Roll back the asset when metadata persistence fails.
- Reject mutable media changes while SD is unavailable.
- Continue serving bundled SPIFFS defaults in degraded mode.

The normal portal upload path no longer retains/copies a complete 200 KiB banner or approximately 1 MiB music file in RAM.

### Backup generation

- Valid version-1 stored-ZIP format remains supported.
- JSON and asset sources are streamed with a 4 KiB buffer.
- CRC is calculated incrementally.
- No complete asset vector is allocated.
- Output remains SD-backed and is served as a file response.

### Restore

- Restore upload streams directly to SD.
- Owner authentication precedes staging.
- Concurrent restores are rejected.
- The complete ZIP is preflighted before live-file mutation.
- Manifest/version, entry flags/method, sizes, duplicates, local/central offsets, overlap, CRC, JSON root shape, and asset signatures are validated.
- All seven required version-1 JSON entries must exist exactly once.
- Assets remain optional.
- JSON fallback input is capped at 128 KiB before ArduinoJson allocation.
- Validated entries are staged.
- Commit uses a verified transactional journal.
- Previous live files are preserved.
- Failure rolls back in reverse order.
- Boot recovery runs before BuildMetadata, Installation, Auth, Coin, Router, Session, Voucher, or Portal managers load.
- Unprovable recovery blocks subsystem startup instead of loading mixed generations.
- Malformed or ambiguous legacy backups are rejected; valid version-1 backups remain compatible.

## Files modified

### Storage

- `ESP32_S3_Firmware/src/StorageManager.h`
- `ESP32_S3_Firmware/src/StorageManager.cpp`
- `ESP32_S3_Firmware/src/StoragePaths.h`
- `ESP32_S3_Firmware/src/StoragePaths.cpp`
- `ESP32_S3_Firmware/src/Config.h`
- `ESP32_S3_Firmware/src/NdjsonLedger.h`
- `ESP32_S3_Firmware/src/NdjsonLedger.cpp`

Reason: transaction recovery, checkpoints, storage state, append-only ledgers, bounded spools, directory contracts, and reconciliation.

### Firmware

- `ESP32_S3_Firmware/src/FirmwareApp.h`
- `ESP32_S3_Firmware/src/FirmwareApp.cpp`
- `ESP32_S3_Firmware/src/SessionManager.h`
- `ESP32_S3_Firmware/src/SessionManager.cpp`
- `ESP32_S3_Firmware/src/VoucherManager.h`
- `ESP32_S3_Firmware/src/VoucherManager.cpp`
- `ESP32_S3_Firmware/src/Logger.h`
- `ESP32_S3_Firmware/src/Logger.cpp`
- `ESP32_S3_Firmware/src/AssetManager.h`
- `ESP32_S3_Firmware/src/AssetManager.cpp`
- `ESP32_S3_Firmware/src/BackupManager.h`
- `ESP32_S3_Firmware/src/BackupManager.cpp`

Reason: early restore recovery, event-owned history records, log append behavior, bounded uploads, streamed backups, and safe restore.

### API

- `ESP32_S3_Firmware/src/ApiServer.h`
- `ESP32_S3_Firmware/src/ApiServer.cpp`

Reason: authenticated streaming upload state and owner-only SD history downloads. Existing endpoint response schemas remain unchanged.

### Testing

- `ESP32_S3_Firmware/tools/storage-hardening-regression-check.py`

Reason: executable recovery models and source-level guards for transaction ordering, checkpoints, divergence, ledgers, quotas, boot restore, upload authentication, and backup limits.

### Documentation

- `ESP32_S3_Firmware/docs/STORAGE_ARCHITECTURE.md`
- `SD_STORAGE_IMPLEMENTATION_STOP_REPORT.md`
- `SD_STORAGE_PRODUCTION_HARDENING_IMPLEMENTATION_REPORT.md`

Reason: storage contract, pre-change blocker decisions, and final implementation evidence.

## Regression analysis

| Feature | Result |
|---|---|
| Coin detection / anti-double-coin | No GPIO, ISR, debounce, or pulse-path changes |
| Coin accumulation / Done Paying | Existing calculation and lifecycle retained; persistence failure still fails closed |
| RouterWorker | No architecture or queue change |
| Router synchronization/cache | No new refresh, polling, or RouterOS reads |
| Captive Portal | Existing routes/UI preserved; portal tests pass |
| MikroTik integration | No driver or RouterOS command changes |
| Authentication/RBAC | NVS ownership unchanged; RBAC guard passes |
| Voucher system | Existing index/API/lifecycle retained; bounded eligibility checkpoint and append-only history added |
| Sales reports | Existing bounded endpoints unchanged; separate raw history downloads added |
| Promo rates | Existing schema/API retained; bounded checkpoint added |
| Provisioning/setup | Existing six-step flow unchanged |
| Admin SPA | Production build passes |
| W5500 Ethernet | Initialization order and isolated SPI bus unchanged |
| SPIFFS Admin assets | Remain internal; format-on-mount-failure removed |
| Installation state | Missing SD no longer permits fallback recovery to overwrite divergent state |
| Backup/restore | Valid v1 compatibility retained; invalid archives now fail before mutation |

## Flash-wear improvement

- Legacy log-array rewrites were removed.
- Sales/session/voucher history grows by line append on SD.
- Router cache and scan cache are excluded from continuous SPIFFS checkpoints.
- Sales and portal metadata are excluded from healthy-write flash mirroring.
- Large mutable banner/music fallback writes to SPIFFS are rejected.
- SPIFFS checkpoints are bounded to agreed boot-critical state.

## RAM improvement

Largest removed allocation peaks:

- Asset upload duplication: approximately 200 KiB–1 MiB retained buffer and copy removed.
- Backup asset buffering: up to approximately 1 MiB replaced by 4 KiB streaming.
- ZIP restore entry allocation: up to approximately 3 MiB replaced by 4 KiB streaming.
- Restore upload: file-backed rather than complete-body storage.

Static firmware RAM after integration:

- Production: 105,820 / 327,680 bytes (32.3%).
- Installer: 105,820 / 327,680 bytes (32.3%).

These are link-time figures; runtime heap, largest block, DMA, PSRAM, and stack high-water marks still require hardware measurement.

## SD utilization improvement

- Growing histories now use SD NDJSON partitions.
- Backups, restore staging, rollback files, exports, media, and histories remain SD-owned.
- Ledger growth no longer consumes internal flash.
- Monthly files allow bounded retrieval without loading complete appliance history.

## CPU and RouterOS impact

### RouterOS

- New idle commands: **0**
- New lifecycle commands: **0**
- New polling: **0**
- New scripts/scheduler: **0**
- RouterWorker changes: **none**

Expected idle RouterOS API traffic remains **0 commands/minute**.

### ESP32

- Healthy event writes add one bounded SD append for history.
- Dedupe work is bounded to 24 RAM IDs and at most a 32 KiB ledger tail.
- Logs perform no ledger scan.
- Backup/restore/upload processing remains synchronous and owner/event initiated.
- No worker, timer, poller, or busy loop was introduced.

## Verification

Passed:

- PlatformIO `freenove_esp32_s3_wroom`.
- PlatformIO `renzfi_installer`.
- Storage hardening regression guard: 8/8 groups.
- W5500/SD isolation guard.
- SD boot-order guard.
- Router-cache synchronization guard.
- Setup-wizard RBAC guard.
- Captive-portal resolver regression test.
- MikroTik portal production bundle generation.
- Admin production build.
- IDE diagnostics for modified files.
- `git diff --check`.

Known repository-wide validation limitations not introduced by this change:

- `npm run typecheck` fails on missing `embla-carousel-react`/`recharts` dependencies and existing unrelated TypeScript errors.
- `npm run lint` fails repository-wide with pre-existing CRLF/Prettier errors.
- Six older setup/router static guards assert obsolete wizard wording or removed symbols and remain stale.

## Deployment impact

- MikroTik upload required: **No storage requirement**. The portal bundle was regenerated and only needs upload if portal artifacts are being deployed.
- ESP32 firmware required: **Yes**.
- UploadFS required: **No** for storage code alone; immutable assets remain unchanged.
- Admin rebuild required: **No source change required**, but the production build was validated.
- Portal rebuild required: **No source change required**, but the deployment bundle was regenerated.

## Hardware validation checklist

1. Boot an installed appliance with healthy SD.
2. Boot with SD absent; verify installation does not return to Factory mode.
3. Verify Admin login and SPIFFS portal/Admin assets without SD.
4. Complete coin payment, activation, pause/resume, termination, and expiry without SD.
5. Redeem an eligible checkpointed voucher and reconnect an active voucher without SD.
6. Remove SD immediately before, during, and after each durable payment/session/voucher commit.
7. Insert a readable but write-protected SD and verify degraded classification.
8. Reinsert the original SD after offline transactions; verify idempotent spool replay.
9. Insert a divergent/replacement SD; verify conflict retention and no overwrite.
10. Cut power after stage write, previous-file preservation, promotion, verification, and cleanup.
11. Cut power at every restore rename; verify all-old rollback or complete committed generation.
12. Corrupt primary, staged, and rollback JSON candidates individually.
13. Corrupt primary, staged, and backup restore journals individually.
14. Fill SPIFFS to soft/hard/minimum-free thresholds; verify fail-closed payment behavior.
15. Fill SD and exercise asset upload, ledger append, backup, and restore failure.
16. Upload maximum banner/music assets while measuring free heap and largest block.
17. Export and restore a maximum valid v1 backup while measuring heap/DMA/PSRAM.
18. Validate torn final NDJSON and spool lines.
19. Confirm monthly and undated history downloads stream correctly.
20. Count RouterOS commands for 30 minutes idle and every session lifecycle action.
21. Measure MikroTik CPU before/after under equivalent customer load.
22. Record ESP32 minimum free heap, largest free block, DMA largest block, task high-water marks, and watchdog status.

## Final statement

The storage implementation is code-complete and passes available software validation. It preserves the RouterWorker, RouterOS, session, portal, Admin, coin, voucher, and provisioning architectures.

It must not be labeled production-ready until the hardware checklist passes, especially randomized power interruption, physical SD removal/reinsertion, constrained SPIFFS, and RouterOS command-count validation.
