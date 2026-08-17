# IMPLEMENTATION REPORT
## SD Hot-Unplug AsyncTCP TWDT during remount

**Date:** 2026-08-17  
**Status:** Code fix complete. **Physical validation is still required.**  
**Does not claim hardware validation.**

Related frozen SD baseline: `docs/SD_HOTUNPLUG_STABLE_BASELINE.md`  
RouterOS credential fix: **unchanged** (`docs/ROUTER_SYNC_CREDENTIAL_RECOVERY_FIX_REPORT.md`)

---

## 1. Exact root cause

`FirmwareApp::loop()` → `StorageManager::pollStorageHealth()` took the recursive `STORAGE_LOCK` and then called `attemptSdRecovery()` **while still holding that lock**.

Recovery includes:

- `SD.end()`
- `delay(100)`
- `SD.begin()`
- write probe (`probeSdWritable`)
- layout repair
- fallback reconcile (`syncFallbackToSd`)
- history replay
- `refreshRuntimeSnapshot()` (`SD.usedBytes()` / `SD.totalBytes()`)

`STORAGE_LOCK_TIMEOUT_MS` is **5000 ms**, matching the default task watchdog. Any AsyncTCP handler that called `lockStorage()` (readJson, writeJson, exists, fileSizeBytes, lastError, fillSdStatus, etc.) parked `async_tcp` on the mutex for the entire remount.

Physical evidence: the crash is immediately after

```
[storage-lifecycle] state=SD_REMOUNTING -> SD_READY reason=remount verified
```

with:

```
task_wdt: Task watchdog got triggered.
task_wdt: - async_tcp (CPU 1)
CPU 1: loopTask
```

`loopTask` was the remount owner. `async_tcp` was the starved waiter. DMA at crash (`free=22744`, `largest=18420`) was not heap exhaustion.

RouterOS refresh/sync had already succeeded. This is **not** a credential or RouterOS worker failure.

---

## 2. Exact offending call path

```
Admin GET /api/storage/status   (and/or any concurrent lockStorage caller)
        │
        ▼
AsyncTCP callback
        │
        ▼  waits up to 5s
STORAGE_LOCK
        ▲
        │ held across remount
FirmwareApp::loop
        │
        ▼
pollStorageHealth()          // tryLock + KEEP lock
        │
        ▼
attemptSdRecovery()
  mountSdCard(reinit=true)   // SD.end, delay, SD.begin
  probeSdWritable()
  onSdRecoverySucceeded()    // sync + replay
  refreshRuntimeSnapshot()   // SD.usedBytes
        │
        ▼
TWDT aborts async_tcp
```

`GET /api/storage/status` itself already filled a RAM snapshot (`fillStorageStatus` → `_dashSnap`). It did **not** call `SD.begin`. It was the request associated with recovery because Admin polls it while `loopTask` remounted under the lock.

`POST /api/storage/retry-sd` previously called `retrySd()` → `attemptSdRecovery()` **directly on AsyncTCP**. That path is now request-only.

---

## 3. Why async_tcp was blocked

ESP32 TWDT watches `async_tcp`. A 5 s recursive-mutex wait equals the watchdog. `loopTask` holding `STORAGE_LOCK` across `SD.begin` + verify + sync is sufficient to trigger abort even when the HTTP handler only wanted a status snapshot or a short JSON read.

Watchdog timeout was **not** increased. TWDT was **not** fed or disabled.

---

## 4. Ownership model before / after

**Before**

- Recovery owner: `pollStorageHealth()` on `loopTask` (correct)
- But remount executed **under STORAGE_LOCK**
- `retrySd()` executed remount inside the HTTP callback (incorrect)

**After**

```
AsyncWebServer callback
  → RAM snapshot / set recovery flag
  → return immediately

loopTask pollStorageHealth (single owner)
  → set _sdRecoveryInProgress
  → set SD_REMOUNTING, _sdReadable=false
  → RELEASE STORAGE_LOCK
  → SD remount + verify + sync + replay
  → refresh snapshot
  → clear in-flight guard
  → SD_READY
```

- One in-flight remount (`_sdRecoveryInProgress`).
- Repeated `/api/storage/status` never starts remount.
- While recovery is in progress, `lockStorage()` wait is **0** so AsyncTCP cannot park on the mutex.
- HTTP fail-fast: `_sdReadable=false` during remount so `readJson` uses SPIFFS fallback instead of touching the SD bus.

---

## 5. Recovery state machine

Preserved:

`SD_DISABLED` · `SD_MOUNTING` · `SD_READY` · `SD_DEGRADED` · `SD_REMOUNTING` · `SD_SYNCING` · `SD_FAILED`

Required path (owner only):

```
SD_DEGRADED → SD_REMOUNTING → SD_READY
          or  SD_REMOUNTING → SD_SYNCING → SD_READY
```

---

## 6. Router recovery-gate behavior

Unchanged predicate (live `sdLifecycle()`, not a stale boolean):

| Lifecycle | Admin router jobs |
|-----------|-------------------|
| Mounting / Remounting / Syncing / Degraded | `503 ROUTER_RECOVERY_IN_PROGRESS` |
| Ready (and other non-recovery states) | allowed |

SD_READY does **not** force RouterOS HEALTHY. New observability only:

```
[router-recovery-gate] state=SD_REMOUNTING allowed=no reason=storage_recovery
[router-recovery-gate] state=SD_READY allowed=yes reason=storage_ready
```

---

## 7. HTTP behavior

| Route | Behavior |
|-------|----------|
| `GET /api/storage/status` | RAM snapshot only. Logs `[storage-api] status snapshot state=… recoveryInProgress=…`. Additive `sdLifecycle` / `recoveryInProgress`. |
| `GET /api/status` | Existing `fillDashboardStatus` RAM snapshot. No `fileSizeBytes` / `SD.exists` / `SD.open`. |
| `GET /api/health` | Existing `fillStorageStatus` snapshot. |
| `POST /api/storage/retry-sd` | Sets recovery request flag. Does **not** remount. Returns current `healthy`/`fallback`. |

Existing JSON fields preserved. Routes not renamed.

---

## 8. Files changed

- `src/StorageManager.cpp` / `.h`
- `src/ApiServer.cpp`
- `src/RouterProvisioningWorker.cpp` (gate diagnostics only)
- `tools/sd-async-recovery-contract-check.mjs` (new)
- `docs/SD_HOTUNPLUG_ASYNCTCP_WDT_RECOVERY_FIX_REPORT.md` (this file)

**Not changed:** MikroTikDriver, RouterOsClient, router credentials, W5500/SD SPI/DMA/pins, WDT timeout, captive portal, coin/sales/session behavior, HTTP route names.

---

## 9. Contract tests

```
node tools/sd-async-recovery-contract-check.mjs     14/14 PASS
node tools/router-sync-refresh-contract-check.mjs    20/20 PASS
node tools/routeros-stability-contract-check.mjs     12/12 PASS
node tools/admin-core-isolation-contract-check.mjs   16/16 PASS
```

---

## 10. Build result

```
pio run -e freenove_esp32_s3_wroom
========================= [SUCCESS] Took 58.38 seconds =========================
```

---

## 11. Physical validation required

Do **not** treat this section as completed.

After flashing:

A. Boot normally.  
B. Verify Admin dashboard.  
C. Verify RouterOS Refresh.  
D. Verify RouterOS Sync.  
E. Remove SD.  
F. Confirm `SD_DEGRADED`.  
G. Confirm `GET /api/storage/status` keeps responding without WDT.  
H. Reinsert SD.  
I. Confirm `SD_REMOUNTING` → `SD_READY` (optionally via `SD_SYNCING`).  
J. Confirm **no** `task_wdt`.  
K. Confirm no Guru Meditation.  
L. Confirm no DMA allocation failure.  
M. Confirm router gate: `storage_recovery` while degraded/remounting; `allowed=yes` after `SD_READY`.  
N. Run Refresh.  
O. Run Sync.  
P. Repeat SD remove/reinsert **at least 10 cycles**.

Success: zero TWDT resets, zero Guru, zero stuck `async_tcp`, zero permanent `storage_recovery` gate, `SD_READY` after every successful reinsert, Refresh/Sync after every recovery, credentials unchanged, DMA not collapsing, HTTP responsive during recovery.
