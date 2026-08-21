# Progressive DMA Loss — Admin Poll JSON on INTERNAL Heap

**Branch:** `feature/waveshare-esp32-s3-eth`  
**HEAD before this fix:** `0be06b5`  
**Reference (N16R8, do not regress):**  
`ESP32_S3_Firmware/docs/REMAINING_ISSUES_FORENSIC_IMPLEMENTATION.md`  
`ESP32_S3_Firmware/docs/SALES_CHART_SPI_DMA_FORENSIC.md`  
`ESP32_S3_Firmware/docs/SETUP_FINISH_GURU_MEDITATION_DMA_ROOT_CAUSE.md`  
`docs/RENZFI_GURU_MEDITATION_PREVENTION_BASELINE.md`

## 1. Exact root cause

**PROVEN (source + this physical log):** Dashboard polling allocates **CPU-side JSON in default heap**. On ESP32-S3, blocks below `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` (~16 KiB) go to **INTERNAL / DMA-capable SRAM** — the same pool W5500 `setup_dma_priv_buffer` needs (`caps=0x00000808`).

Exact handlers at failure:

| Request | Previous allocation | Size |
|---------|---------------------|------|
| `GET /api/system/health` | `DynamicJsonDocument(JSON_DOC_MEDIUM)` | **8192 INTERNAL** |
| `GET /api/system/coin` | `DynamicJsonDocument(512)` | INTERNAL |
| `GET /api/system/rgb` | `DynamicJsonDocument(512)` | INTERNAL |
| every `sendOk` | Arduino `String` serialize + `beginResponse` copy | INTERNAL duplicate |

Dashboard (`DashboardPage.tsx`) refetches those plus `/api/status` every `fallbackPollMs` (5 s, faster if SSE down). Concurrent overlapping 8 KiB INTERNAL pools **fragment** DMA (`dma_largest` ratchets 47 KB → 6.6 KB → 16 → 12). Small requests then fail (`size=76`) because the **pool is already shredded**, not because health JSON is large.

**INFERRED:** Setup AP + Ethernet + portal at the same time add extra DMA users; they are not the 8192 allocator.

**REJECTED:** “User reloaded too many times” as a product limit. Reloads only expose the INTERNAL JSON class already proven on N16R8 sales-chart.

**REJECTED:** More `503 ETH_DMA_LOW` as the primary fix. That hides exhaustion; it does not stop INTERNAL JSON from consuming DMA.

## 2. Why previous Waveshare fixes were insufficient

| Commit | What it did | Gap |
|--------|-------------|-----|
| `176aec0` | Portal save JSON → PSRAM | Not Admin poll paths |
| `17a2b38` | `/api/status` + `/api/health` → PSRAM; SPA 503 gate | **Did not** move `/api/system/health` (8192) or `sendOk` String |
| `0be06b5` | Single-flight large SPA files | Failure now on **small** APIs after DMA already gone |

## 3. N16R8 mapping (apply only this class)

From remaining-issues §5:

> `HeapJsonDocument(8192)` / default malloc with capacity &lt; ALWAYSINTERNAL → INTERNAL → `setup_dma_priv_buffer` fails when `dma_largest=16`.

Same number, same caps, same crash class. Sales already uses `PsramJsonDocument`. Admin system poll did not.

## 4. Minimal fix

1. `/api/system/health`, `/coin`, `/rgb`, `/build` → `PsramJsonDocument` (CPU JSON, not SPI bounce buffers).
2. `sendOk` / `sendError` serialize into **PSRAM** and stream via `beginResponse` callback so Arduino `String` does not copy the body into INTERNAL.

Not changed: W5500 pins, RouterOS worker, External AP, Setup Lock, SD false-removal, SPA single-flight, SPIFFS fallback, NVS, WDT.

## 5. Why this stops progressive DMA loss

Working JSON no longer punches 8 KiB holes in the DMA pool on every 5 s poll. After requests finish, W5500 can still allocate 1490-byte priv TX/RX from a contiguous INTERNAL block.

## 6. Guru Meditation

Still a **secondary** IDF NULL after `setup_dma_priv_buffer` fails. Preventing pool shredding keeps the driver off that path. IDF SPI/W5500 not patched.

## 7. Physical validation

**NOT PHYSICALLY VALIDATED.** Required: 10 / 50 dashboard reloads, portal+admin together, SD present/absent; `dma_largest` must return toward a stable baseline (~tens of KB), no `dma-alloc-fail`, no LoadProhibited.
