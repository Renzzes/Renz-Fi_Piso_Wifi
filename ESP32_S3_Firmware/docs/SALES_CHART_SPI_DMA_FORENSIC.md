# Sales Chart / SPI DMA Crash — Forensic Audit

**Date:** 2026-08-16  
**Incident:** `spi_master: setup_dma_priv_buffer: Failed to allocate priv TX buffer` → `abort()`  
**Trigger context:** `GET /api/sales/chart/weekly` then `[sales] chart enter days=180`  
**Pre-crash snapshot:** heap≈8.3 MB healthy; `dma free=36208 largest=19444 minimum=708`

---

## 1. Suspected source files / functions

| Role | File / function | Evidence |
|------|-----------------|----------|
| **Crash site (IDF)** | ESP-IDF `spi_master` → `setup_dma_priv_buffer` | Exact log line; allocates `MALLOC_CAP_DMA` bounce buffer for SPI TX |
| **Only app SPI path using IDF DMA** | `EthernetManager::begin` → `ETH.begin(... SPI3_HOST ...)` → `spi_bus_initialize(..., SPI_DMA_CH_AUTO)` in Arduino `ETH.cpp` | SD Arduino HAL uses FIFO polling (`esp32-hal-spi.c` `__spiTransferBytes`) — **no** `setup_dma_priv_buffer` |
| **Trigger (not allocator)** | `SessionManager::salesChart` → `StorageManager::readJsonFromSd(SALES_FILE)` → HTTP reply over W5500 | Chart does not call `heap_caps_malloc(DMA)`; it stresses SD + large JSON then ETH TX |
| **Contributing pressure** | `ApiServer` chart handler previously `HeapJsonDocument(2048)` for up to 180-day payloads | Undersized doc → overflow/realloc in **internal** SRAM while DMA min already hit 708 B |
| **Prior same failure class** | `RouterOsClient` / portal `/file/print` (see `SETUP_FINISH_GURU_MEDITATION_DMA_ROOT_CAUSE.md`) | Same `setup_dma_priv_buffer` under W5500 load |

**Verdict:** Chart operation **triggers** a W5500 SPI TX while DMA-capable internal SRAM is already critically depleted (`minimum=708`). Chart is not the DMA allocator; W5500 SPI3 is.

---

## 2. DMA allocation lifecycle

```
Boot
  DmaMemoryMonitor::install()          → failed-alloc hook
  ETH.begin(SPI3_HOST, SPI_DMA_CH_AUTO)
       └─ spi_bus_initialize           → claims GDMA channel(s)
       └─ esp_eth_mac_new_w5500        → ongoing TX/RX SPI xfers
            └─ per xfer: setup_dma_priv_buffer(MALLOC_CAP_DMA)
                 if user/pbuf buffer not DMA-capable → bounce alloc
                 free after xfer (when path succeeds)

  renzFiSdSpiBegin(FSPI) / SD.begin
       └─ spiStartBus + FIFO register xfers
            └─ NO setup_dma_priv_buffer (Arduino HAL)

Runtime ETH traffic (Admin HTTP, SSE, portal, RouterOS)
  → repeated priv TX/RX DMA allocs on SPI3
  → freeDma / largestDma fluctuate; minDma ratchets down under pressure

Sales chart miss
  → Soft-gate if largestDma < 1536 (new)
  → else SD read (FIFO) + HeapJson (~PSRAM/internal) + ETH response TX
       └─ TX needs priv DMA buffer → abort if alloc fails
```

---

## 3. Leak / fragmentation / repeated allocation evidence

| Finding | Classification |
|---------|----------------|
| `dma minimum=708` with later `free=36208 largest=19444` | **Fragmentation / prior exhaustion**, not necessarily a permanent leak at crash instant |
| No app-level `heap_caps_malloc(MALLOC_CAP_DMA)` in firmware sources | DMA consumers are **framework/IDF** (W5500 SPI master), not sales JSON |
| `ETH` `spi_devcfg.queue_size = 20` | Up to many in-flight SPI transactions → concurrent DMA bounce demand |
| SD remount (`renzFiSdSpiBegin(true)` → `end`/`begin`) | Can stress FSPI; does **not** call IDF `setup_dma_priv_buffer`; still competes for internal SRAM during remount |
| Chart `HeapJsonDocument(2048)` for 180-day series | **Evidence-backed** internal realloc pressure (capacity bug) |
| Weekly log showing `days=180` | Likely monthly request adjacent in UI, or operator conflation; route table maps weekly→28, monthly→180 |

No proven unbounded app DMA leak found. Depletion pattern matches **W5500 SPI DMA bounce under concurrent load + internal fragmentation**, previously seen during Finish portal verify.

---

## 4. Minimal fix (evidence-based; no SPI redesign)

1. **Instrumentation:** `DmaMemoryMonitor::ScopedProbe` before/after SD mount/read/write, W5500 begin, sales chart, portal load/save; keep router/voucher probes; `heap_caps_register_failed_alloc_callback` logs size/caps/fn + DMA/internal free.
2. **Soft-gate:** Refuse sales-chart rebuild when `largest DMA block < 1536` (same threshold as RouterOS) → HTTP `503 SPI_DMA_LOW` instead of continuing into ETH TX that can `abort()`.
3. **Capacity:** Chart response `HeapJsonDocument` 2048 → **8192** (matches chart cache payload capacity) to stop undersized monthly JSON pressure on internal heap.
4. **Core dump partition:** Add `coredump` 64 KB; shrink SPIFFS by 64 KB so flash layout matches IDF’s already-enabled flash coredump path (`No core dump partition found` → fixed).

**Not done (explicitly deferred):** redesign SPI, raise heap arbitrarily, disable DMA, change RouterOS/voucher/portal business logic, persistent static W5500 DMA buffers inside IDF (would require SPI/ETH architecture change).

---

## 5. Files changed

- `src/DmaMemoryMonitor.h` / `.cpp` — snapshot API, ScopedProbe, alloc-fail hook
- `src/FirmwareApp.cpp` — `DmaMemoryMonitor::install()` at boot
- `src/StorageManager.cpp` — DMA probes on mount / readJson / writeJson
- `src/SessionManager.cpp` — sales-chart probe + DMA soft-gate
- `src/ApiServer.cpp` — chart HTTP DMA precheck; 8192 response doc; `SPI_DMA_LOW`
- `src/EthernetManager.cpp` — probe around `ETH.begin`
- `src/PortalSessionManager.cpp` — probe around load/save
- `partitions_custom.csv` — `coredump` + SPIFFS shrink
- `tools/esp32-mikrotik-stability-contract-check.mjs` — require coredump present
- `docs/SALES_CHART_SPI_DMA_FORENSIC.md` — this report

---

## 6. Why this prevents `setup_dma_priv_buffer` failure (for this trigger)

- Soft-gate stops chart rebuild (SD + large reply) when contiguous DMA is already below the known W5500 TX need → avoids the path that raced into priv TX alloc under `minimum≈708`.
- Larger chart JsonDocument avoids overflow realloc into internal SRAM during the same window.
- Alloc-fail hook + ScopedProbe deltas identify **which** later op shrinks DMA if exhaustion returns.
- Does **not** claim to eliminate all W5500 DMA pressure (RouterOS/SSE can still stress DMA; those paths already have headroom checks).

---

## 7. Regression risks

| Risk | Mitigation |
|------|------------|
| Chart returns 503 under DMA pressure | UI already treats chart errors; cache hit still serves when warm |
| Serial noise from ScopedProbe on every SD JSON read | Temporary forensic density; can rate-limit after field capture |
| SPIFFS −64 KB | Staged admin+portal must still fit; validate with `npm run build:esp32` / uploadfs size check |
| Partition table change | Requires full re-flash of partition table (not OTA-only app) |
| Soft-gate false positives | Threshold matches existing RouterOS `kMinLargestDmaBlockForEthTx` (1536) |

---

## Field follow-up

1. Flash firmware + partition table.  
2. Reproduce Sales Reports (daily/weekly/monthly).  
3. Capture `[dma] *:before/after/delta` and any `[dma-alloc-fail]` lines.  
4. Confirm boot no longer prints `No core dump partition found`.  
5. If DMA still ratchets down without chart, next suspect is concurrent RouterOS/SSE ETH SPI — use delta labels, not SPI redesign yet.
