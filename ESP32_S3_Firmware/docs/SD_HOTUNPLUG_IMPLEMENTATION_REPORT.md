# IMPLEMENTATION REPORT
## SD Hot-Unplug / DMA / WDT Fix

**Date:** 2026-08-17  
**Based on:** `docs/SD_HOTUNPLUG_DMA_WDT_FORENSIC.md` (decoded ELF `fa7b858a3`)

---

## Root cause

AsyncWebServer handlers on `async_tcp` call `StorageManager::readJson` → `readJsonFromSd` → `recoverSdTransaction` → `SD.open`/`exists` while the card is gone but `_sdMounted/_sdReadable` remain true until the 60 s health poll. Each `sdSelectCard` blocks up to 500 ms → async_tcp WDT.

W5500 RX (`emac_w5500_task`) then fails a 504-byte DMA alloc (`largest=172`) → IDF NULL → LoadProhibited. `/api/router/cache` is the ETH RX victim, not a cache DMA leak.

## Contributing causes

- No fail-fast media trip on I/O failure  
- Direct SD bypasses (`ApiServer::sendSdFile`, `AssetServer`, Backup/Asset/Ndjson under lock)  
- Lock timeouts during remount/sync  

## Files / functions changed

| File | Functions |
|------|-----------|
| `StorageManager.h/.cpp` | `SdLifecycle`, `setSdLifecycle`, `tripSdMediaMissing`, `handleSdRemoved`, `mountSdCard`, `begin`, `readJsonFromSd`, `readSdPayload`, `recoverSdTransaction`, `exists`, `fileSizeBytes`, `appendHistory`, `onSdRecoverySucceeded`, `fillStorageHealth`, `writeJsonToSdOnce` |
| `ApiServer.cpp` | `sendSdFile` — gate on `healthy()` + `sdIoAllowed()` |
| `web/AssetServer.cpp` | `serveResolvedAsset` — same gate |
| `docs/SD_HOTUNPLUG_DMA_WDT_FORENSIC.md` | Forensic report |
| `docs/SD_HOTUNPLUG_IMPLEMENTATION_REPORT.md` | This report |

## Why necessary

1. **Immediate MEDIA_MISSING** on open failure stops further Select Failed loops on async_tcp.  
2. **Skip SD I/O** when `!_sdMounted || !_sdReadable`.  
3. **Open-only probes** avoid double `sdSelectCard` via `exists`+`open`.  
4. **Lifecycle logs** make remount/sync transitions observable.  
5. **Bypass gates** prevent async downloads from hammering a dead card.

## Preserved behavior

- Conflict policy / no auto-merge  
- Router worker architecture  
- SPIFFS fallback  
- Dual SPI buses (FSPI vs SPI3)  
- WDT timeout unchanged  
- `/api/router/cache` still served from RAM cache  

## Tests

- Firmware compile: (see build log)  
- Physical Test 3–8 from forensic prompt: **REQUIRES HARDWARE**  

## Expected serial (hot-unplug)

```
[storage] Detected SD removal
[storage-lifecycle] state=SD_READY -> SD_DEGRADED reason=readJson open failed
[dma] SD_DEGRADED ...
```

No continuous `sdSelectCard(): Select Failed` spam after the first trip.

## Expected serial (reinsert)

```
[storage-lifecycle] state=SD_DEGRADED -> SD_REMOUNTING
[SD] SD remount: SD.begin OK
[storage-lifecycle] ... -> SD_READY reason=remount verified
[storage-lifecycle] ... -> SD_SYNCING ...
[storage-lifecycle] ... -> SD_READY reason=sync complete
```

## Remaining risks

- First failing open after pull still costs ≤500 ms once (unavoidable without HW detect pin).  
- BackupManager still has many direct `SD.*` calls — owner-only, not on every status poll; still a race if remount coincides with restore.  
- Progressive DMA across 10 hot-plug cycles: **NOT PROVEN fixed** until physical Test 7.  
- IDF still aborts on DMA fail if exhaustion recurs from other causes — application soft-gates reduce pressure but cannot patch IDF NULL deref.
