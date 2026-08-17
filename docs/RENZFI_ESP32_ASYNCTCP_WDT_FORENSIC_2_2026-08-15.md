# Renz-Fi — Second `async_tcp` Task-WDT Forensic

**Date:** 2026-08-15  
**Mode:** SOURCE + decoded hardware backtrace. **NO CODE CHANGES. NO FLASH. NO ROUTEROS/TWDT/SESSION CHANGE.**

**Crash ELF SHA256 prefix:** `c75477655`  
**Matching workspace ELF:** `ESP32_S3_Firmware/.pio/build/freenove_esp32_s3_wroom/firmware.elf`  
**Full SHA256:** `C7547765509E8A5350EEE08B50DB11BD1BF2F1FC22171BEA11FC1CFECBA9F573`

This is a **different ELF** from the first abort (`7839121b5…`). Decode used `xtensa-esp32s3-elf-addr2line -pfia` against this exact file.

**Product state:** Operational customer path (coin → Activate → Connected). This pass answers why `async_tcp` still missed TWDT under Admin + active customer + 2 phones + coin.

---

## 1. Exact backtrace decoding

**Confidence: PROVEN** (SHA prefix match + complete application frames).

| PC | Function | File:line |
|---|---|---|
| `0x420d8cb8` | `spiTransferBytesNL` | `esp32-hal-spi.c:1590` |
| `0x4201267b` | `SPIClass::transferBytes` | `SPI.cpp:305` |
| `0x42012c75` | `sdReadBytes` | `sd_diskio.cpp:221` |
| `0x42012df2` | `sdReadSector` | `sd_diskio.cpp:258` |
| `0x42012f07` | `ff_sd_read` | `sd_diskio.cpp:662` |
| `0x42122226` | `ff_disk_read` | `diskio.c:96` |
| `0x42122739` | `move_window` | `ff.c:1095` |
| `0x421232aa` | `dir_find` | `ff.c:2427` |
| `0x42123569` | `follow_path` | `ff.c:3100` |
| `0x42123a67` | **`f_open`** | `ff.c:3755` |
| `0x4212556b` | `vfs_fat_open` | `vfs_fat.c:388` |
| `0x420e962e` | `esp_vfs_open` | `vfs.c:951` |
| `0x421730da` / `0x42173171` | `fopen` | newlib `fopen.c` |
| `0x420081f9` | `VFSFileImpl` ctor | `vfs_api.cpp:245` |
| `0x420088c9` | `VFSImpl::open` | `vfs_api.cpp:87` |
| `0x42007875` | `fs::FS::open` | `FS.cpp:209` |
| **`0x420a4b2a`** | **`StorageManager::fileSizeBytes`** | **`StorageManager.cpp:1423`** |
| **`0x4202320a`** | **`ApiServer::registerProductionRoutes` lambda** | **`ApiServer.cpp:1090`** |
| `0x4200d64a` | `AsyncCallbackWebHandler::handleRequest` | `WebHandlers.cpp:310` |
| `0x4200ed32` | `AsyncWebServerRequest::_parseLine` | `WebRequest.cpp:687` |
| `0x420071c5` | `_async_service_task` | `AsyncTCP.cpp:328` |
| `0x40382eb5` | `vPortTaskWrapper` | `port.c:139` |

**Deepest application frame:** `StorageManager::fileSizeBytes` at `StorageManager.cpp:1423`.

That line is:

```cpp
File file = SD.open(path, FILE_READ);
```

The caller at `ApiServer.cpp:1090` is **inside `GET /api/status`**:

```cpp
size_t logsBytes = _storage->fileSizeBytes(RenzFiConfig::LOGS_FILE);
```

`LOGS_FILE` is `/logs/logs.json`.

**Classification of the callback:** **A. SD/FAT + B. SPI** (FAT `f_open` / directory walk over the SD SPI bus), invoked from **H. Admin endpoint** (`GET /api/status`). Not SSE, not RGB, not portal, not RouterOS, not the promo reader as the PC.

---

## 2. Root-cause classification

| Claim | Grade |
|---|---|
| Abort PC is `GET /api/status` → `fileSizeBytes("/logs/logs.json")` → `SD.open` → FAT `f_open` → SPI SD transfer **on `async_tcp` CPU 1** | **PROVEN** |
| Previous `/api/status` → `RouterPlatform::load` path was removed; this is a **second remaining SD call on the same handler** | **PROVEN** |
| CPU 0 `IDLE0` / CPU 1 `async_tcp` means the HTTP callback itself was running SD, not RouterWorker | **PROVEN** |
| Heap exhaustion | **RULED OUT** (heap ~8.4 MB, largest ~8.25 MB) |
| `/api/system/rgb` as crashing callback | **RULED OUT** (same class of “last printed request” mistake as crash 1; RGB is O(1) RAM) |
| Sales-cache 567 ms **on loopTask** as the abort PC | **RULED OUT** (different task; PC is `fileSizeBytes`) |
| Promo `/config/promos.json` read as the abort PC | **RULED OUT** (coin path is `loopTask`; PC is `/api/status`) |
| Promo + coin + SSE as **why Admin immediately re-hit `/api/status` while SD was hot** | **HIGH-CONFIDENCE** |
| `STORAGE_LOCK` wait (5 s) inflating total `/api/status` time before/during SD | **POSSIBLE** (not the PC; lock is taken *before* `SD.open`) |
| `EventBus::emit` / `AsyncEventSource::send` as the blocking primitive | **RULED OUT** for this abort (`dEmit=6`; PC is SD open) |
| RouterOS `/login` latency as this TWDT | **RULED OUT** (no RouterOS frames) |
| Core-dump CRC / missing coredump partition | **RULED OUT** as WDT cause (happens after abort) |
| Exact TWDT timeout seconds in this build | **UNKNOWN** (not set in project `platformio.ini` / `Config.h`; Arduino-ESP32 default, typically 5 s) |

**One-sentence answer:**

`async_tcp` failed TWDT because **`GET /api/status` still performs a synchronous SD `open` of `/logs/logs.json` on the HTTP task**, and Admin (especially after coin SSE invalidation) keeps calling that endpoint while loopTask is also using SD (`sales-cache` 0.5–0.8 s, first-coin promo `readJson` 184 ms).

---

## 3. Exact blocking operation

```
_async_service_task (CPU 1)
  → GET /api/status          ApiServer.cpp:912–1098
  → StorageManager::fileSizeBytes("/logs/logs.json")
       ScopedStorageLock     wait up to STORAGE_LOCK_TIMEOUT_MS (5000)
       SD.exists(path)       FAT stat  (previous crash class)
       SD.open(path)         ← abort PC, f_open + SPI
  → (never reached) fillSdStatus() → SD.usedBytes()/totalBytes()
```

`fileSizeBytes` is not a RAM snapshot. It takes the global storage mutex, then talks to the SD card.

---

## 4. Concurrency timeline

Approximate order from the supplied serial (millis `183137` ≈ 3.05 min uptime). Exact inter-request timestamps were not in the paste; order is from the quoted log.

```
loopTask                         async_tcp (CPU 1)              Admin 10.20.0.246
────────                         ─────────────────              ────────────────
[sales-cache] refresh
  elapsed=567 source=SD          (may be in /api/status
   holds STORAGE_LOCK + SD         or waiting on lock)

[mem] heap=8.4MB sse=3 jobs=0

                                 /admin SPIFFS  elapsedMs=147
                                 SLOW HANDLER (threshold=100)
                                 repeated /api/health, /api/status,
                                 /api/storage/status, router GETs, rgb, …

CoinManager::loop()
  pulses=1 peso=1
  PromoManager::loadCache()
    [rates] promo cache miss
    readJson(promos.json)  ~184ms
    STORAGE_LOCK + SD
  RAM credit
  EventBus emit (sse=3)
    portal.coin.credit
    sessions.changed
    coin.accepted / system.status
  [coin-latency] dPromo=184 dEmit=6 dT0_T6=4778
                 (4588 of that is pulse settle, not SD)

                                 SSE → React Query invalidates
                                   ["system","status"] etc.
                                 GET /api/status
                                   … RAM sales snapshot (OK) …
                                   … fileSizeBytes logs.json …
                                   SD.open  ← TWDT abort
E (183137) task_wdt async_tcp
core dump write fails (no partition)
reboot
```

`dT0_T6=4778` is **not** 4.8 s of SD. Breakdown from `CoinLatencyTrace`: `dSettle=4588` (pulse grouping) + `dPromo=184` + `dEmit=6`. Promo SD is 184 ms on **loopTask**. The watchdog fires because **`async_tcp` is in a different, still-synchronous SD open**.

---

## 5. Difference from previous crash

| | Crash 1 (ELF `7839121b5`) | Crash 2 (ELF `c75477655`) |
|---|---|---|
| Task / CPU | `async_tcp` CPU 1, CPU 0 IDLE0 | **Identical** |
| HTTP handler | `GET /api/status` | **Identical** |
| FAT primitive | `f_stat` (`SD.exists` in `recoverSdTransaction`) | **`f_open` (`SD.open` in `fileSizeBytes`)** |
| Application caller | `_router->load()` → `router.json` | **`fileSizeBytes(LOGS_FILE)` → `/logs/logs.json`** |
| Previous remediation | Removed `load()` and 3× `sales.json` from `/api/status` | **Did not remove remaining SD stats on the same handler** |

**Verdict:** This is **the same failure class** (synchronous SD on `async_tcp` from `GET /api/status`), **not** a fully independent subsystem. The first fix **did** remove the proven `load()` path. It **did not** finish stripping SD from `/api/status`. The remaining calls were listed as remaining risk in `docs/RENZFI_ESP32_MIKROTIK_STABILITY_REMEDIATION_2026-08-15.md` §13.1; this abort **proves** that risk.

Identical: endpoint, task, SD/SPI, Admin as the client that polls `/api/status`.  
Different: which SD API and which file.

---

## 6. Coin / promo path (aggressive investigation)

**Task:** `FirmwareApp::loop` → `CoinManager::loop` → `onCoinInserted` → `PromoManager::resolveForAmount` → `list` → `loadCache`. **Not ISR. Not `async_tcp`.**

`PromoManager::begin()` sets `_cacheLoaded = false` and does **not** preload. First coin after boot (or any process that never called `list()` yet) always prints:

```
[rates] promo cache miss — reading /config/promos.json
```

That calls `StorageManager::readJson` → **STORAGE_LOCK + SD** (~184 ms here).

Then `onCoinInserted` emits SSE **synchronously** from loopTask (`emitSessionEvent` then `EventBus::emit`). `dEmit=6` with `sse=3` — **not** TWDT-scale.

Admin `useDashboardEvents.ts` maps:

- `sessions.changed` → refetch `["system","status"]` → **`GET /api/status`**
- `system.status` → same
- `coin.accepted` → coin/health/diagnostics (not status, but more HTTP)

Throttle is 500 ms, not a suppress. Coin therefore **predictably causes Admin to call `/api/status` immediately**, while loopTask may still hold `STORAGE_LOCK` (promo `readJson`, then queued `SaveSessions`).

**Promo is a HIGH-CONFIDENCE lock/SD contender and a trigger for the Admin refetch. It is not the decoded PC.**

Do not move this work into the ISR. A later narrow fix may **preload** the promo RAM cache on `begin()` / loopTask (same pattern as sales snapshot). Do not implement in this forensic pass.

---

## 7. STORAGE_LOCK vs sales-cache vs AsyncTCP

`STORAGE_LOCK_TIMEOUT_MS = 5000`. Almost every `StorageManager` I/O takes `ScopedStorageLock` with that timeout.

loopTask holders (SAFE for TWDT of `async_tcp` **unless** HTTP waits on the same mutex):

- `refreshHealthSnapshots` → `refreshRuntimeSnapshot` (SD stats, `SD.exists` restore artifacts)
- `refreshSalesSummarySnapshot` → `readJson(sales.json)` — **measured 562–768 ms**
- coin promo `readJson(promos.json)` — **184 ms**
- portal `SaveSessions`

HTTP waiters (UNSAFE if lock is held or if they then do SD themselves):

- `GET /api/status` → `getSpiffsUsedBytes/TotalBytes` (lock + SPIFFS) **then** `fileSizeBytes` (lock + **SD.open**) **then** `fillSdStatus` (lock + **`SD.usedBytes()` / `totalBytes()`**, filesystem-wide, can be very slow)
- `GET /api/health` / `/api/storage/status` / `/api/system/health` → `fillStorageStatus` (lock, RAM snapshot **after** lock)

SD (FSPI/SPI2) and W5500 (SPI3) are **independent buses** (`SdSpi.cpp`). This is **not** W5500/SPI-share contention. `async_tcp` is simply **inside SD FAT on CPU 1**, so it cannot return to the AsyncTCP loop that services TWDT.

---

## 8. SSE / EventBus

`EventBus::emit` → `AsyncEventSource::send` from loopTask. With `sse=3`, coin emit took **6 ms**. **RULED OUT** as this abort’s blocking primitive. Do not disable SSE. Coin-triggered **refetch of `/api/status`** is the coupling that matters.

---

## 9. Production HTTP vs storage (classification)

### `GET /api/status` (proven unsafe — abort site)

| Call | Class |
|---|---|
| `salesToday/Week/Month` | **SAFE** (RAM snapshot; first fix) |
| `cachedRouterHost/Configured` | **SAFE** (RAM; first fix) |
| `mergedActiveUserStats` → `SessionManager::appendActiveUsers` → `users.json` | **UNSAFE** (SD `readJson`; not this PC; still next-crash material) |
| `getSpiffsUsedBytes/TotalBytes` | **POTENTIALLY BLOCKING** (lock 5 s + SPIFFS) |
| **`fileSizeBytes(LOGS_FILE)`** | **UNSAFE — PROVEN abort** |
| `fillSdStatus` | **UNSAFE** (would run next: live `SD.usedBytes`) |
| `fillStorageStatus` | **POTENTIALLY BLOCKING** (5 s lock for RAM copy) |

**`GET /api/status` has no `RequestTimer`.** SLOW HANDLER logging does **not** cover this callback. The `/admin` `elapsedMs=147` line is `StaticFileServer` / SPIFFS, **not** the abort.

### Other production handlers

| Endpoint | Class | Notes |
|---|---|---|
| `GET /api/health` | POTENTIALLY BLOCKING | `fillStorageStatus` lock; `SPIFFS.exists("/index.html")` in ProductionHandoff; provisioned path skips `router->load()` |
| `GET /api/storage/status` | POTENTIALLY BLOCKING | `fillStorageStatus` |
| `GET /api/system/health` | POTENTIALLY BLOCKING | `fillStorageStatus` |
| `GET /api/system/rgb` | **SAFE** | RAM `fillStatus` |
| `GET /api/system/coin` | SAFE | RAM |
| `GET /api/router/jobs/*` | SAFE | worker poll, no ROS |
| `GET /api/router/settings` | **UNSAFE** | `fillPublicSettings` → `readJson(router.json)` |
| `GET /api/router/cache` / wireless | SAFE if cache RAM | not this PC |
| `GET /api/router/profiles` | SAFE | cache-only in `RouterPlatform::listProfiles` |
| `GET /api/sales/history` / `salesChart` | UNSAFE | owner; not 5 s dashboard poll |
| `GET /api/settings` | UNSAFE | `readJson(SETTINGS_FILE)` |
| `GET /admin` | POTENTIALLY BLOCKING | SPIFFS; 147 ms logged |
| Portal `/api/portal/session` / heartbeat | SAFE vs RouterOS | local session RAM; no ROS |

---

## 10. Existing duration logging — gaps

`WebRequestDiagnostics::RequestTimer` logs begin/end/`SLOW HANDLER` (threshold 100 ms) **only where constructed**. Production **`GET /api/status` does not use it.**

Today you can see:

- path + elapsed for `/admin` and setup/static handlers
- **not** endpoint/handler/elapsed/task/lock-wait for `/api/status`

**Proposed instrumentation (do not implement now):**

- `RequestTimer` on `GET /api/status` and `GET /api/health` only
- one line if `fileSizeBytes` / lock wait / SD open exceeds N ms
- **no** per-second spam; no TWDT feed; no extra SD

---

## 11. Minimum safe fix boundary (recommendation only)

**Do not implement in this task.**

Smallest architecture-preserving fix:

1. **Remove all live SD/SPIFFS volume probes from `GET /api/status`:**
   - `fileSizeBytes(LOGS_FILE)`
   - `fillSdStatus()` (live `SD.usedBytes`)
   - optionally `getSpiffsUsedBytes/TotalBytes`
2. Serve `logsUsedKb` / SD used/total / flash used from **`refreshRuntimeSnapshot()` RAM fields** already refreshed on loopTask (~2 s). If logs size is not in the snapshot yet, add **one** `fileSizeBytes` there (loopTask), not in the HTTP callback.
3. Keep sales snapshot and `cachedRouterHost` as they are.
4. Optionally **preload `PromoManager` cache on `begin()` / first loopTask**, so the first coin is not a `promos.json` miss under Admin load. Do not put that read on `async_tcp` or ISR.
5. Do **not** increase TWDT. Do **not** `esp_task_wdt_reset` in the handler. Do **not** disable SSE or Admin.

Required fix type from the user’s list: **(1) HTTP callback O(1) for `/api/status` storage fields** and **(2) storage already-cached on loopTask**. Lock architecture change is **not** required if HTTP stops taking `STORAGE_LOCK` for those fields. Promo preload is **optional follow-on**, not the proven PC.

---

## 12. Regression risks — do not touch

- sessionGeneration, duplicate-activation guard, no `active/set`
- session clock / Connected-after-RouterOS-success
- Connected-only HealthProbe suppression
- Ethernet Activate gate
- sales summary off `async_tcp` (do not revert)
- one RouterWorker, no idle ROS poll, no ROS from portal heartbeat
- voucher expiry, pause/resume/terminate, payment math
- W5500 / SPI host split, TWDT config, setup wizard
- SSE functionality (Admin refetch storm is a **consumer** of unsafe `/api/status`, not a reason to remove SSE)

---

## 13. Hardware validation plan (after a future narrow fix)

| ID | Scenario |
|---|---|
| A | Admin open, no customer — soak `/api/status` |
| B | Admin + 1 Connected customer |
| C | Admin + 2 phones / captive portal |
| D | Admin + 2 phones + **coin activation** (the trigger in this log) |
| E | SSE connected (`sse>=2`) |
| F | repeated purchases |
| G | Verify window (no Connected-only HealthProbe loop) |
| H | 10–30 min soak |
| I | RouterOS CPU observation (separate domain) |
| J | ESP32: **zero** `task_wdt` / Guru / unexpected reboot |

Acceptance also: portal Connected + timer correct, Admin usable, SSE up, RouterOS reachable, no API login storm.

**Firmware stability is not claimed until that soak passes.** This document does not flash.

---

## 14. Core dump (unchanged finding)

`CRC=0x7bd5c66f instead of 0x0` / `No core dump partition found!` is the missing `coredump` slot in `partitions_custom.csv`. Watchdog first. Do not add a partition to “fix” TWDT.

---

## 15. Explicit statement

The first remediation **successfully removed** `/api/status` → `RouterPlatform::load` → `recoverSdTransaction` → `SD.exists`.

`GET /api/status` **still** calls `fileSizeBytes("/logs/logs.json")` on `async_tcp`. That is the **proven** second watchdog path.

Coin/promo SD + SSE-driven Admin refetch explain **why it died during a coin**, not **which instruction** was running. The instruction was `SD.open` for the logs file inside `/api/status`.
