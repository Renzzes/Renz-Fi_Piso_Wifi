# Admin Login TWDT Root Cause (ELF `8f93b74f9`)

## Incident Summary

- Symptom: Task watchdog reset after Admin login and around `/api/storage/status`.
- Watchdog line: failed task/user `async_tcp` (CPU1), while CPU1 currently running `loopTask`.
- Constraint validation: this is **not** assumed to be the owner or Wi-Fi-selection endpoint incident class; decoded against the exact ELF first.

## Exact Hardware Log (provided)

- `URL : /api/storage/status`
- `[net-diag] Ping 10.10.10.1 SUCCESS`
- `E ... task_wdt: Task watchdog got triggered`
- `E ... task_wdt: - async_tcp (CPU 1)`
- `E ... task_wdt: CPU 1: loopTask`
- Backtrace list ending with `0x4204ef07`, `0x4204fd15`, `0x4204613a`, `0x420d4dba`, `0x40382ea5`
- ELF SHA256 prefix: `8f93b74f9`

## ELF Verification

- Workspace ELF: `ESP32_S3_Firmware/.pio/build/freenove_esp32_s3_wroom/firmware.elf`
- SHA256: `8F93B74F9593A7EB3A59314F9A5E2E1CB860596E195E58AEBBD44BE53C559473`
- Match: **YES** (authoritative decode)

## Decoded Backtrace (relevant frames)

| PC | Function | File | Line | Task | Execution context | Operation occurring |
|---|---|---|---:|---|---|---|
| `0x4204fd15` | `FirmwareApp::loop` | `src/FirmwareApp.cpp` | 296 | `loopTask` | Main firmware loop | periodic health refresh gate |
| `0x4204ef07` | `FirmwareApp::refreshHealthSnapshots` | `src/FirmwareApp.cpp` | 314 | `loopTask` | Called every 2s when production registered | calls `_storage.refreshRuntimeSnapshot()` |
| `0x420a3efd` | `StorageManager::refreshRuntimeSnapshot` | `src/StorageManager.cpp` | 2114 | `loopTask` | Runtime snapshot | computes fallback usage by scanning SPIFFS files |
| `0x420a004e` | `StorageManager::fallbackTotalBytes` | `src/StorageManager.cpp` | 1107 | `loopTask` | Snapshot helper | iterates fallback paths and stage/backup files |
| `0x4209ff91` | `StorageManager::spiffsFileSize` | `src/StorageManager.cpp` | 1081 | `loopTask` | Snapshot helper | `SPIFFS.exists` before opening file |
| `0x42013b43` | `SPIFFSImpl::exists` | Arduino SPIFFS | 38 | `loopTask` | FS VFS stat | SPIFFS metadata probe |
| `0x403896ff` | `esp_flash_read` | ESP-IDF | 972 | `loopTask` | flash/cache critical section | blocking SPI flash read |

## Task Ownership and CPU Ownership

- `async_tcp`: pinned to CPU1 (`CONFIG_ASYNC_TCP_RUNNING_CORE=1`).
- `loopTask`: runs on CPU1 (Arduino default).
- `router_worker`: unpinned (`ROUTER_WORKER_CORE_AFFINITY=-1`), not in this stack.
- Failure mechanism: `loopTask` performs heavy SPIFFS snapshot scanning long enough that `async_tcp` cannot run/reset TWDT.

## Proven Blocking Operation

- Exact blocking chain:
  - `FirmwareApp::refreshHealthSnapshots` (2s cadence)
  - `StorageManager::refreshRuntimeSnapshot`
  - `fallbackTotalBytes()` -> many `spiffsFileSize()` calls
  - each `spiffsFileSize()` performs `SPIFFS.exists` / `stat`
  - eventually `esp_flash_read` in SPI flash critical path
- This is loopTask work, not RouterOS, not worker wait, not HTTP callback filesystem write.

## Why Admin Login Is Not Root Cause

- Admin login succeeded (`[INFO] auth: Login successful`).
- No auth/login frame appears in decoded stack.
- Login is temporal trigger (dashboard opens/polling starts), but not the blocking instruction.

## Why `/api/storage/status` Is Not Root Cause

- Endpoint handler calls `_storage->fillStorageStatus(...)` only (cached snapshot fields under lock).
- Decoded stack never enters `ApiServer` storage route.
- Crash happens in concurrent loopTask snapshot refresh, not within async_tcp handler body.

## Storage Conflict Analysis

- Boot log reports conflicts for `/config/installation.json` and `/config/provisioning.json`, sync incomplete.
- Conflict state increases fallback artifacts and metadata that snapshot scanning must inspect.
- In this incident, conflicts are a **contributing amplifier**, not the direct blocking frame.
- No evidence here of infinite `syncFallbackToSd()` retry loop at crash moment (stack does not show sync/replay functions).

## Finish-State Inconsistency Analysis

- Observed sequence: finish verify failed (`verifyProvisionedPersisted success=false`) yet later heartbeat shows `install=provisioned`.
- Source behavior allows state advancement to `Provisioned` and later verification failure path.
- This inconsistency is real and should remain tracked; for this crash, stack shows runtime snapshot SPIFFS scan as direct trigger.

## RouterWorker / MikroTik CPU Analysis

- Backtrace has no `RouterProvisioningWorker`, no RouterOS client frames.
- `jobs=0 queue=0` is consistent with idle worker at crash point.
- No evidence this incident is due to RouterOS command storms or reconnect loops.

## Comparison with Prior TWDT Classes

- Owner TWDT: async endpoint SD/history writes in callback.
- Wi-Fi-selection TWDT: async endpoint `persist()`/`writeJson` callback.
- This incident: **loopTask telemetry scanning SPIFFS** starving async_tcp on shared CPU1.
- Same broad boundary class (long FS work on CPU1), different entry point and call chain.

## Proven Root Cause

`loopTask` runs heavy SPIFFS fallback-size/recovery telemetry every health refresh cycle, and this can monopolize CPU1 long enough to starve `async_tcp`, causing TWDT. The failing instruction path is SPIFFS `exists/stat` -> `esp_flash_read`, proven by ELF-matched backtrace.

## Exact Fix Implemented (smallest production-safe)

- Throttled heavy runtime snapshot SPIFFS walks inside `StorageManager::refreshRuntimeSnapshot`:
  - new interval constant: `STORAGE_SNAPSHOT_HEAVY_INTERVAL_MS = 30000`
  - heavy computations (`fallbackTotalBytes`, pending replay count, recovery queue manifest/spool probes) run at most once per interval.
  - lightweight snapshot fields still update each call.
- Durability preserved:
  - no change to transactional SD writes, CRC, rollback, SPIFFS checkpoints, fallback sync, or conflict semantics.
- No TWDT/feed/timeout changes.
- No CPU affinity hacks.

## Files Changed

- `ESP32_S3_Firmware/src/Config.h`
- `ESP32_S3_Firmware/src/StorageManager.h`
- `ESP32_S3_Firmware/src/StorageManager.cpp`

## Files Intentionally Untouched

- `StorageManager` transactional write/recovery design
- `RouterProvisioningWorker` / RouterOS execution architecture
- setup wizard flow structure
- watchdog config/timeouts

## Build / Tests

- Build: `pio run -e freenove_esp32_s3_wroom` -> **SUCCESS**
- Size:
  - RAM: `32.5%` (`106340 / 327680`)
  - Flash: `91.6%` (`2401503 / 2621440`)
- Portal lifecycle test: `22/22 PASS`

## Static Regression Checks

- No new sync `writeJson` added in `SetupServer` routes.
- No new sync `dispatch/wait` added in `ApiServer`/`SetupServer`.
- No TWDT timeout or disable change.
- No new `delay()` introduced in modified files.
- No StorageManager redesign / duplicate persistence pipeline added.

## Hardware Validation Procedure (required, not executed here)

- Boot with current SD and with reproducible conflict state if safe.
- Admin login and dashboard open >=10 minutes.
- Repeated `/api/storage/status` and router-status checks.
- Monitor serial for TWDT/Guru Meditation.
- Monitor heap/largest block/DMA minima.
- Validate customer lifecycle (coin -> Done Paying -> Internet -> expiry/pause/resume/terminate).
- Reboot persistence and repeat admin login.

## Unproven Hypotheses (explicitly not claimed)

- async_tcp callback itself blocked in `/api/storage/status`.
- RouterWorker or RouterOS operation caused this reset.
- mutex deadlock/lock-timeout as direct trigger in this stack.

## Final Acceptance Checklist (scored)

| Category | Result | Passed / Total | Percent |
|---|---|---:|---:|
| ESP32 Stability (source/build) | PASS | 4 / 4 | 100% |
| Setup Stability (source/build) | PASS | 3 / 3 | 100% |
| Admin Stability (hardware) | NOT TESTED | 0 / 6 | 0% |
| Storage Stability (hardware) | NOT TESTED | 0 / 5 | 0% |
| RouterOS Stability (hardware) | NOT TESTED | 0 / 4 | 0% |
| MikroTik CPU Stability (hardware) | NOT TESTED | 0 / 4 | 0% |
| Internet Grant Lifecycle (hardware) | NOT TESTED | 0 / 6 | 0% |
| Persistence/Reboot (hardware) | NOT TESTED | 0 / 4 | 0% |
| Regression Safety (static) | PASS | 5 / 5 | 100% |

Production gate status: **NOT READY** until mandatory hardware categories are PASS.
