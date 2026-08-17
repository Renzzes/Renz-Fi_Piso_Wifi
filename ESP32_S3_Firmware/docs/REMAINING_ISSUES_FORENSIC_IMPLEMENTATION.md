# Remaining Issues — Forensic Investigation and Targeted Hardening

**Date:** 2026-08-17  
**Firmware:** Renz-Fi ESP32-S3 + W5500 (`freenove_esp32_s3_wroom`)  
**Scope:** Evidence from the latest physical serial logs. No StorageManager rewrite, no RouterWorker redesign, no HTTP contract break, no SPI bus change, no SD remount-path change.

This report records **proven call chains**, not guesses. Physical re-validation of sales DMA and SPA latency is still required on hardware.

---

## 1. Root Cause Summary

| # | Issue | Classification | Proven root cause |
|---|--------|----------------|-------------------|
| 1 | 180-day sales chart DMA collapse | **Proven root cause** | Chart processing allocated **INTERNAL/DMA-capable SRAM** for JSON buckets, 8 KB cache documents, HTTP `HeapJsonDocument(8192)`, and a full-file `String`+`validateJsonPayload` recover-before-read. That fragmented the DMA pool. |
| 2 | W5500 TX fail after 180-day chart | **Secondary symptom** | `spi_master::setup_dma_priv_buffer` requested **54 bytes, caps=0x00000808 (DMA\|INTERNAL)**. `dma_largest=16` so the aligned alloc failed. W5500 SPI write then failed. W5500 hardware/pins are not implicated. |
| 3 | `sdSelectCard` storm on hot-unplug | **Proven contributing + known limitation** | `readJsonFromSd` called `recoverSdTransaction` **before** the real open. After the first failed open (`streak=1`) recover still probed `.stage`/`.backup` via `SD.exists`/`readSdPayload`. Each `SD.open` internally retries `sdSelectCard` (~500 ms) because there is **no card-detect pin**. |
| 4 | SD remount/recovery | **Successful baseline** | Not changed. |
| 5 | UI “Degraded Mode” while SD is healthy | **Proven root cause** | `refreshRuntimeSnapshot()` set `_snapshotHealth = "DEGRADED"` when `_snapshotPendingConflicts > 0`, even if SD was mounted/writable. `StorageHealthCard` maps DEGRADED to emergency-SD copy. |
| 6 | SPIFFS/SD conflict gen 3 | **Legitimate unresolved state** | Replay/sync `recordConflict()` for `/sessions/portal_sessions.json` when CRC/payload diverges. No auto-merge (frozen). Not stale bookkeeping. |
| 7 | Slow SPA `/dashboard`, `/system-configuration` | **Proven architecture + contributing contention** | `onNotFound` → `StaticFileServer` serves uncompressed `/index.html` (gzip preferred uncompressed when both existed). 140 ms is file transfer. 1.1–1.5 s spikes coincide with DMA/SD pressure from sales. Double timer is two RAII scopes on one request, not double I/O of the body. |
| 8–9 | DMA largest-block trend | **Same as #1** | Three retained 8 KB `DynamicJsonDocument` cache slots + 180-day JSON buckets. |
| 10 | Repeated router deferred logs | **Proven: no job queued** | `enqueueAdminPrepared` returns 503 `ROUTER_RECOVERY_IN_PROGRESS` **without** `enqueueInternal`. Repeated Admin refresh/sync still logged. Frontend now backs off while storage is recovering. |
| 11 | RouterOS 3.8–5 s refresh | **Known limitation / working** | Login + `/system/resource/print` + `/ip/hotspot/print`. No leak proven. Not changed. |
| 12 | Portal/coin | **Working baseline** | Not changed. |
| 13 | Historical AsyncTCP+SD WDT | **Already fixed** | Fail-fast lifecycle preserved. Eager recover-on-read removed (that path was a remaining SD-on-async_tcp cost). |
| 14 | BackupManager `SD.*` | **Real remount race, gated** | Direct `SD.*` remain, but `isSdAvailable()` now also requires `sdIoAllowed()` and `!sdRecoveryInProgress()`. |
| 15 | Progressive DMA over 10 hot-plug cycles | **Not proven fixed** | Code inspected; physical Test 7 still required. |
| 16 | Core-dump flash CRC | **Historical / not this failure** | No reboot/panic in the latest sales/DMA sequence. |

---

## 2. Evidence Summary

180-day sequence from hardware:

```
sales-chart:before     dma free=20676  largest=9204
sales-sd-read:before   dma free=14076  largest=9204   (≈6.6 KB already spent)
sales-sd-read:after    dma free=7700   largest=4340
[dma-alloc-fail] size=54 caps=0x00000808 fn=heap_caps_aligned_alloc
                 dma_free=860 dma_largest=16 internal_free=876
spi_master: Failed to allocate priv TX buffer
sales chart exit ok days=180 ...
w5500_spi_write / write_buffer / emac_w5500_transmit failed on next HTTP
```

Total heap stayed ~8.3 MB (PSRAM). Failure is **DMA-capable contiguous block**, not “low heap”.

`caps=0x00000808` = `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`.  
`size=54` matches ESP-IDF `setup_dma_priv_buffer` (aligned SPI DMA private TX buffer), not a sales JSON node.

Chart still printed `exit ok` because aggregation is CPU-side and returns `true` **before/independent of** `emac_w5500_task` transmitting the HTTP body.

`dma_free=860` with `dma_largest=16` is **fragmentation**: free bytes exist as tiny holes; W5500 needs one aligned DMA block ≥ ~54 bytes (practically more with alignment).

---

## 3. Exact Files Changed

| File | Why |
|------|-----|
| `ESP32_S3_Firmware/src/JsonHeap.h` | `PsramAllocator` + `PsramJsonDocument` for CPU-side JSON |
| `ESP32_S3_Firmware/src/SessionManager.cpp` | Bounded `int32_t` chart buckets; PSRAM cache payload; PSRAM sales JSON |
| `ESP32_S3_Firmware/src/StorageManager.cpp` | No eager recover-on-read; stop extra SD probes after open fail; health ≠ conflict |
| `ESP32_S3_Firmware/src/ApiServer.cpp` | Chart/history/records use PSRAM docs; DMA headroom recheck before `sendOk` |
| `ESP32_S3_Firmware/src/BackupManager.cpp` | `isSdAvailable()` honors remount/lifecycle |
| `ESP32_S3_Firmware/src/SpiffsHost.cpp` | Prefer `/index.html.gz` for SPA fallback |
| `ESP32_S3_Firmware/src/web/StaticFileServer.cpp` | Bounded `[http-forensic]` file size for SPA shell |
| `src/components/StorageHealthCard.tsx` | Media vs reconciliation presentation |
| `src/types/api.ts` | Additive `reconciliationStatus`, `recoveryInProgress` |
| `src/services/adminSync.ts` | Do not enqueue router sync during storage recovery |
| `src/pages/SystemConfigurationPage.tsx` | Disable refresh/sync while storage recovering |
| `src/pages/DashboardPage.tsx` | Same backoff on stale-cache banner |
| `scripts/test-sales-chart-buckets.mjs` | Chart bucket regression |
| `scripts/test-storage-health-semantics.mjs` | Health vs conflict regression |
| `ESP32_S3_Firmware/docs/REMAINING_ISSUES_FORENSIC_IMPLEMENTATION.md` | This report |

---

## 4. Exact Functions Changed

- `PsramAllocator::{allocate,deallocate,reallocate}`
- `SessionManager::salesChart`, `salesHistory`, `listSalesRecords`, `aggregateAllSales`
- `StorageManager::validateJsonPayload`, `recoverSdTransaction`, `readJsonFromSd`, `refreshRuntimeSnapshot`, `fillStorageStatusFromSnap`
- `ApiServer` sales chart/history/records handlers
- `BackupManager::isSdAvailable`
- `resolveSpiffsServePath`
- `StaticFileServer::serveStaticOrIndex`
- `synchronizeAdminClient`
- `StorageHealthCard`

---

## 5. Why Each Change Is Correct

### Sales DMA (issues 1, 2, 8, 9)

**Chain (proven):**

```
GET /api/sales/chart/monthly
  → ApiServer HeapJsonDocument(8192)          // INTERNAL (capacity < ALWAYSINTERNAL)
  → SessionManager::salesChart
       HeapJsonDocument(days*48+256)          // 8896 B INTERNAL for 180 keys
       HeapJsonDocument(JSON_DOC_LARGE=24576) // often PSRAM
       StorageManager::readJsonFromSd
         recoverSdTransaction
           readSdPayload → Arduino String     // INTERNAL
           validateJsonPayload(len+1024)      // INTERNAL DynamicJsonDocument
         SD.open + deserializeJson            // second parse
       new DynamicJsonDocument(8192) cache    // INTERNAL, retained in 3 slots
  → sendOk serializeJson → String → beginResponse
  → W5500 TX on emac_w5500_task
       spi_master setup_dma_priv_buffer(54, DMA|INTERNAL)
       FAIL when largest DMA block = 16
```

Sales chart does **not** call `heap_caps_malloc(DMA)`. It **indirectly** consumes DMA-capable SRAM because ESP32 `malloc` of small/medium blocks uses internal memory.

**Why PSRAM is the correct capability for sales JSON:** those buffers are parsed on the CPU and never used as SPI DMA bounce buffers. W5500/SPI3 **must** keep using DMA-capable internal SRAM. This is not “move buffers to hide the problem”; it is matching capability to consumer.

**Why bounded `int32_t amounts[180]`:** the 180-day request does **not** need 180 JSON object nodes in RAM. With 8 sales records in the log, days only changed bucket count. A JSON map of 180 date keys was unnecessary allocation.

**Why skip eager `recoverSdTransaction` on read:** write path still recovers first. Read path already recovers if `deserializeJson` fails. The eager path doubled DMA cost on every healthy sales GET.

**Why “chart exit ok” was misleading:** return `true` means aggregation succeeded. It does not mean W5500 transmitted. Post-aggregation `hasEthTransmitHeadroom()` now refuses `sendOk` if the DMA pool is already below 1536 bytes.

180 days is **not** clamped. The data model can represent 180 daily buckets in 720 bytes of integers.

### Storage UI (issues 5, 6)

```
refreshRuntimeSnapshot
  previously: pendingConflicts > 0 → health=DEGRADED
  StorageHealthCard HEALTH_COPY.DEGRADED → “emergency internal storage / reinsert SD”
```

That OR was wrong. Conflicts remain in `pendingConflicts`, `conflicts[]`, and warnings. No auto-merge. Generation 3 `/sessions/portal_sessions.json` is expected after a fault-test divergence (SPIFFS fallback wrote sessions while SD was gone/different). Recovery must **not** clear it.

### SD select storm (issue 3)

```
readJsonFromSd → recoverSdTransaction → readSdPayload open fail (streak=1)
  → SD.exists + readSdPayload(.backup/.stage)     // extra selects
  → readJsonFromSd SD.open again
```

After the first failed open, extra probes are skipped. Fail-fast streak=2 and CARD_NONE trip are unchanged. One `SD.open` may still emit several `sdSelectCard` lines (~500 ms) inside IDF `sd_diskio.cpp`. That remainder is the no-CD-pin limitation.

### Router deferred (issue 10)

`enqueueAdminPrepared` already did not queue a job. Frontend backoff prevents 503 storms during `SD_DEGRADED` / remount.

### BackupManager (issue 14)

Entry points already used `isSdAvailable()`. Adding lifecycle/recovery checks is the smallest remount-race gate without rewriting restore.

### SPA (issue 7)

Prefer gzip when present. Log `file_bytes` so the next physical run can separate “large uncompressed HTML” from “contention”. Route ownership (`notFound` → StaticFileServer) is intentional SPA fallback and was **not** changed.

---

## 6. Why Each Change Is Safe

- SD remount owner, SPI pin map, WDT, RouterWorker, coin/session/portal contracts, and no-auto-merge are untouched.
- `recoverSdTransaction` still runs on **write** and on **parse failure**.
- Chart JSON shape `{labels, data}` is unchanged.
- Additive `reconciliationStatus` does not remove fields.
- PSRAM allocator falls back to default `malloc` if SPIRAM alloc fails.
- DMA ETH TX threshold (1536) is unchanged.

---

## 7. DMA Allocation Analysis

| Allocation | Capability needed | Previous | Now |
|------------|-------------------|----------|-----|
| W5500 SPI priv TX (~54 B aligned) | DMA \| INTERNAL | Failed when largest=16 | Unchanged consumer; pool no longer shredded by sales JSON |
| Sales JSON DOM | CPU / PSRAM | Often INTERNAL for 8 KB docs | `PsramAllocator` (`MALLOC_CAP_SPIRAM`) |
| 180-day buckets | CPU | JSON object ~9 KB INTERNAL | `int32_t[180]` |
| Chart cache | CPU | 3 × `DynamicJsonDocument(8192)` INTERNAL, retained | 3 × `SalesChartCachePayload` in PSRAM (~2.7 KB each) |
| `readSdPayload` String on every chart GET | CPU | Always | Not on healthy read path |

**Why heap free > 8 MB can coexist with dma_largest=16:** PSRAM holds the 8 MB heap. DMA-capable SRAM is a small internal pool (~45–48 KB free at rest). Fragmenting that pool with 8 KB JSON documents does not reduce PSRAM.

---

## 8. Sales Memory Analysis

Processing memory (180-day, 8 records): should now be PSRAM JSON for `sales.json` plus 720 B integer buckets (stack) plus compact PSRAM cache.

Response memory: still `sendOk` Arduino `String` envelope (labels+data ≈ a few KB). Recheck DMA **after** processing, **before** that String is sent.

Network DMA memory: W5500 SPI bounce only, unchanged.

The 180-day chart does **not** need all individual sales rows in the **response**. It still loads `sales.json` once into a PSRAM document because the file is small (8 records). Streaming every record is unnecessary at current size; if `sales.json` later approaches `JSON_DOC_LARGE`, streaming would be the next bounded step — not done here.

---

## 9. W5500 Failure Analysis

```
caller: emac_w5500_transmit / w5500_spi_write
  → function: setup_dma_priv_buffer (ESP-IDF spi_master.c:1208)
  → allocation: heap_caps_aligned_alloc size=54 caps=0x00000808
  → memory state: dma_free=860, dma_largest=16
  → failure: ESP_ERR_NO_MEM → "Failed to allocate priv TX buffer"
  → secondary: w5500_write_buffer / emac_w5500_transmit
  → recovery: salesChart still returned true; next HTTP used a recovered-but-fragmented pool
```

Not a W5500 defect. Not a pin or dual-SPI-bus issue. MOSI/MISO/SCK/CS for W5500 and SD were not changed.

---

## 10. SD Hotplug Analysis

Observed: `SD_READY` → ~9 `sdSelectCard` → `tripSdMediaMissing` (`readSdPayload open fail streak`) → `SD_DEGRADED` → remount → `SD_READY`. Ethernet stayed up. No WDT.

Remaining selects inside a single failed `SD.open` are IDF retries. Extra recover probes after `streak>0` are removed. Do not poll SD to “detect faster”.

Remount path (`SD_DEGRADED` → `SD_REMOUNTING` → verify → `SD_READY`) was not modified.

---

## 11. Storage Conflict Analysis

`recordConflict(sdPath, generation, baseCrc, sdCrc, fallbackCrc)` for `/sessions/portal_sessions.json` generation 3 means SPIFFS fallback payload CRC ≠ SD payload CRC (or fallback CRC ≠ manifest). That is the intended no-auto-merge outcome of running portal sessions during SD absence then remounting.

Successful remount does **not** clear conflicts (frozen). UI now shows HEALTHY media + reconciliation attention. Do not delete or merge.

---

## 12. Slow HTTP Analysis

```
/dashboard | /system-configuration
  WebServerManager::onNotFound  (timer A)
    → StaticFileServer::handleNotFound
      → serveStaticOrIndex      (timer B)
        → resolveSpiffsServePath → /index.html (gzip was no)
        → WebResponse::serveFile → beginResponse(SPIFFS, index.html)
```

`/favicon.svg` and `/icons/icon-192.png` are small files with extensions; they resolve directly and stay fast (22–83 ms).

140–145 ms: plausible uncompressed SPA shell transfer over SPIFFS+W5500.  
1116–1565 ms: not explained by route fallback CPU; matches concurrent DMA/SD stress (sales). Fixing sales DMA is the primary latency hardening. Gzip preference helps when `.gz` is actually on SPIFFS (staging currently copies uncompressed `index.html`; gzip is opportunistic).

Do not raise the 100 ms SLOW threshold.

---

## 13. RouterWorker Analysis

Deferred = **reject, no queue, no job object**. Repeated logs were Admin retries during `SD_DEGRADED`. Gate kept. Admin connect no longer calls `POST /api/router/cache/sync` while `recoveryInProgress` / `recoveryMode` / unmounted.

RouterOS 3.8–5 s is login+print time. No retention fix (no leak evidence).

---

## 14. Regression Risks

| Risk | Mitigation |
|------|------------|
| Interrupted SD write not visible until parse fails | Recover still on deserialize error and on every write |
| PSRAM JSON alloc fail | Allocator falls back to generic malloc |
| Chart stack (`dateKeys`+`amounts` ≈ 2.7 KB) on async_tcp | Same order as previous `dateKeys[180]`; 16 KB AsyncTCP stack |
| HEALTHY+conflict missed by operators | Warnings + conflict list + reconciliation row retained |
| Gzip missing on SPIFFS | Falls back to `/index.html` as before |

---

## 15. Tests Added

- `scripts/test-sales-chart-buckets.mjs`
- `scripts/test-storage-health-semantics.mjs`

---

## 16. Tests Run

- `node scripts/test-sales-chart-buckets.mjs` — pass
- `node scripts/test-storage-health-semantics.mjs` — pass
- `node scripts/test-sales-uptime-aggregation.mjs` — 14 checks passed
- `pio run -e freenove_esp32_s3_wroom` — SUCCESS (43.39 s)

Host `tsc --noEmit` still reports pre-existing errors in unused UI kit modules (`embla-carousel-react`, `recharts`) unrelated to this change.

---

## 17. Hardware Tests Still Required

1. Repeat 7 → 28 → 180 → monthly → history → records. Record DMA free/largest after each. **No** `dma-alloc-fail`, **no** `setup_dma_priv_buffer`, **no** W5500 TX errors.
2. Confirm 180-day chart JSON still has 180 labels.
3. SD remove → degrade → reinsert → remount (baseline must still pass). Count `sdSelectCard` lines (expect fewer than the previous recover-probe storm; some IDF retries remain).
4. Confirm Storage card: media HEALTHY + reconciliation attention with portal_sessions conflict still listed.
5. SPA: note `[http-forensic] file_bytes` and gzip=yes/no for `/dashboard`.
6. Ten hot-plug cycles for progressive DMA (issue 15) — **not claimed fixed**.

---

## 18. Before/After Memory Metrics

**Before (physical, 180-day):**

| Stage | dma free | dma largest |
|-------|----------|-------------|
| chart before | 20676 | 9204 |
| sd-read after | 7700 | 4340 |
| alloc-fail | 860 | **16** |
| chart after | 17196 | 4596 |

**After:** firmware-only; fill from the next serial capture. Expect sd-read delta much smaller (no String+validate recover), chart after largest remaining in the same band as pre-chart (~9–18 KB), never 16.

---

## 19. Before/After Latency Metrics

| Path | Before (physical) | After |
|------|-------------------|--------|
| 180-day chart | 325 ms CPU ok, then W5500 fail | Re-measure; must not fail TX |
| `/dashboard` | 140 / 636 / 1565 ms | Re-measure; 1 s spikes should drop if they were DMA contention |
| favicon / icon-192 | 22–83 ms | Unchanged |

---

## 20. Final Production-Readiness Assessment

Core operational paths (Ethernet, SD remount, RouterOS gate, coin/portal) remain the frozen baseline and were not redesigned.

The 180-day DMA failure had a **proven, specific chain**. The hardening matches that chain: stop putting sales working sets in DMA-capable SRAM, stop double-reading sales.json into INTERNAL Strings, stop calling a healthy SD “Degraded Mode” because of an owner-review conflict.

**Not production-certified until the hardware tests in §17 pass.** Do not treat host tests or `pio run` as a substitute for the 180-day chart + W5500 serial capture.
