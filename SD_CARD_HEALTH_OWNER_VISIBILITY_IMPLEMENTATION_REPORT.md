# SD Card Health & Owner Visibility — Implementation Report

## Release verdict

**IMPLEMENTED — HARDWARE VALIDATION REQUIRED**

Software builds and focused regression guards pass. A release pass is not
claimed because SD removal, read-only media, capacity thresholds, replay, and
power-loss behavior still require fault-injection testing on the target
ESP32-S3/W5500/SD hardware.

## Implementation summary

- Added cached storage-health states: `HEALTHY`, `DEGRADED`, `WARNING`,
  `CRITICAL`, `READ_ONLY`, and `UNKNOWN`.
- Kept the existing authenticated `GET /api/storage/status` route and extended
  it additively; legacy response fields remain available.
- Added an owner-facing SD Card Health card to Dashboard and System
  Configuration.
- Added state-change-only browser notifications. Initial page load does not
  generate a notification.
- Reused the existing `storage.changed` SSE path and 30-second disconnected-SSE
  fallback interval.
- Added boot and transition diagnostics using the `[storage]` prefix.
- Kept last-write and last-backup observations in RAM; monitoring adds no SD,
  SPIFFS, journal, or configuration writes.
- Added a static regression guard for the monitoring contract.

## Health contract

- `HEALTHY` — writable SD storage, valid layout, no pending replay/recovery.
- `DEGRADED` — core operation continues on emergency storage, or bounded
  replay/recovery work remains.
- `WARNING` — emergency storage is active and at least 70% of its quota is used.
- `CRITICAL` — emergency storage is active and at least 90% is used.
- `READ_ONLY` — SD is mounted and readable but not writable.
- `UNKNOWN` — available cached evidence cannot prove another state.

Normal last-known-good checkpoints do not create Warning/Critical states while
the writable SD path is healthy.

## API addition

`GET /api/storage/status` now returns:

- `mounted`, `mode`, `health`
- `totalSpace`, `usedSpace`, `freeSpace`
- `journalHealthy`
- `lastWrite`, `lastWriteAgeSeconds`
- `lastSuccessfulBackup`, `lastSuccessfulBackupAgeSeconds`
- `pendingReplay`
- `emergencyUsage.percent`, `emergencyUsage.bytes`,
  `emergencyUsage.quotaBytes`
- `crcHealthy`
- `recoveryQueue`
- `filesystemMount`
- `warnings`

Existing `storageMode`, `sdPresent`, `sdMounted`, `fallbackActive`, `capacity`,
and `used` fields are preserved. Unknown CRC, write, backup, or journal values
are returned as `null`, not guessed.

## Expected UI

Healthy:

> SD Card Health — Healthy  
> The SD card is working normally and durable records are protected.  
> No action is needed.  
> Current storage mode: Normal SD Storage

SD removed:

> SD Card Health — Degraded Mode  
> The appliance is still operating on emergency internal storage.  
> Reinsert or replace the SD card soon.

Emergency storage at 70%:

> SD Card Health — Warning  
> Emergency storage is nearing its safe limit.  
> Restore the SD card as soon as possible.

Emergency storage at 90%:

> SD Card Health — Critical  
> Emergency storage is almost full. New durable records may soon be rejected.  
> Reinsert or replace the SD card immediately.

The card has no flashing or animation. Colors are green, orange, yellow, red,
and grey for Healthy, Degraded, Warning, Critical, and Unknown respectively.

## Files modified

Firmware / API:

- `ESP32_S3_Firmware/src/StorageManager.h`
- `ESP32_S3_Firmware/src/StorageManager.cpp`
- `ESP32_S3_Firmware/src/BackupManager.h`
- `ESP32_S3_Firmware/src/BackupManager.cpp`
- `ESP32_S3_Firmware/src/ApiServer.cpp`

Admin:

- `src/types/api.ts`
- `src/services/system.ts`
- `src/hooks/api/useStorageHealth.ts`
- `src/hooks/useDashboardEvents.ts`
- `src/components/StorageHealthCard.tsx`
- `src/pages/DashboardPage.tsx`
- `src/pages/SystemConfigurationPage.tsx`

Documentation / testing:

- `ESP32_S3_Firmware/docs/SD_CARD_HEALTH_OWNER_VISIBILITY.md`
- `ESP32_S3_Firmware/tools/storage-health-regression-check.py`
- `SD_CARD_HEALTH_OWNER_VISIBILITY_IMPLEMENTATION_REPORT.md`

## Resource comparison

Production firmware:

- Before: 105,820 bytes static RAM (documented hardening baseline)
- After: 105,948 / 327,680 bytes (32.3%)
- Change: +128 bytes static RAM
- After flash: 2,376,203 / 2,621,440 bytes (90.6%)
- Recent pre-feature flash build: 2,351,735 bytes
- Change: +24,468 bytes

Installer firmware:

- After: 105,948 / 327,680 bytes (32.3%)
- After flash: 2,378,839 / 2,621,440 bytes (90.7%)

Runtime CPU impact is limited to bounded snapshot work on the existing storage
health cadence: SD/SPIFFS capacity reads, three fixed emergency spool files,
the bounded fallback manifest, and fixed recovery artifact names. API requests
serialize cached values and do not scan growing ledgers.

RouterOS impact:

- Added commands: 0
- Added polling: 0
- Added scripts/schedulers: 0
- Expected MikroTik CPU change: 0

Storage-write impact:

- Added SD writes: 0
- Added SPIFFS writes: 0
- Added journal writes: 0
- Added backup operations: 0

## Software validation

- PASS — production PlatformIO build
- PASS — installer PlatformIO build
- PASS — Admin production build
- PASS — edited-file diagnostics
- PASS — storage-health regression guard
- PASS — eight storage-hardening invariants
- PASS — Ethernet/SD SPI isolation guard
- PASS — SD SPI boot-order guard
- PASS — no RouterOS work in the storage-status route
- PASS — initial UI state does not notify; later health transitions notify once
- PASS — `storage.changed` refreshes the dedicated cached query

Repository-wide TypeScript checking still reports only the known unrelated
carousel/chart dependency, provisioning action, and promo mutation errors.

## Hardware validation checklist

- [ ] Boot with healthy writable SD; verify Healthy and capacity values.
- [ ] Remove SD while idle; verify Degraded Mode and one notification.
- [ ] Remove SD during an active coin and voucher session.
- [ ] Confirm coin acceptance, session activation, expiry, and Admin login.
- [ ] Reinsert SD; verify recovery, replay count reaches zero, and one Healthy
      transition notification.
- [ ] Use read-only media; verify Read Only without write loops.
- [ ] Fill emergency storage across 70% and 90% thresholds.
- [ ] Verify journal and CRC fault indications with controlled test media.
- [ ] Create a manual backup; verify Last Successful Backup.
- [ ] Power-cycle during a staged restore and verify rollback/recovery.
- [ ] Measure heap, largest free block, DMA, and task stack high-water marks.
- [ ] Confirm no watchdog reset or W5500/SD SPI contention.
- [ ] Measure RouterOS command rate at idle: required result is 0 commands/minute.
- [ ] Compare MikroTik CPU before and after: no measurable increase.

## Deployment

- ESP32 firmware upload: required
- Admin rebuild / UploadFS: required
- Captive Portal rebuild: not required
- MikroTik upload or configuration: not required

