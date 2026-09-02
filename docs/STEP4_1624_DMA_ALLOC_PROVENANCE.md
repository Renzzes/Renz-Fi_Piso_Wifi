# Step 4 1624-byte DMA alloc (caps 0x80c) — provenance

**Status:** source identified statically. Temporary instrumentation added. **No product fix. No flash. No commit.**

## Caps decode (correct IDF bits)

| Mask | Bits | Meaning |
|------|------|---------|
| `0x00000808` | INTERNAL\|DMA | W5500 `heap_caps_aligned_alloc` SPI bounce (~1394) |
| `0x0000080c` | INTERNAL\|DMA\|**8BIT** | `heap_caps_malloc` / calloc via malloc |

`0x4` = `MALLOC_CAP_8BIT`, `0x8` = `MALLOC_CAP_DMA`, `0x800` = `MALLOC_CAP_INTERNAL`.

The new failure is **not** `setup_dma_priv_buffer`.

## Exact 0x80c call site

**File:** ESP-IDF `components/esp_wifi/esp32s3/esp_adapter.c`  
(Arduino-ESP32 3.3.x / IDF 5.5.x used by this firmware)

```c
realloc_internal_wrapper  → heap_caps_realloc(..., MALLOC_CAP_8BIT|MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL)
calloc_internal_wrapper   → heap_caps_calloc(...,  same mask)   // fails inside heap_caps_malloc
zalloc_internal_wrapper   → heap_caps_calloc(1, size, same mask)
._malloc_internal         → esp_coex_common_malloc_internal_wrapper  // same mask
```

These are the WiFi OS-adapter `_malloc_internal` family. The closed-source WiFi lib uses them for **DMA-capable internal** TX/RX/AMPDU buffers while SoftAP is up.

**Classification:** **3. WiFi/lwIP DMA buffer** (WiFi/coex internal), **not** W5500 bounce, **not** SD SPI DMA, **not** application JSON.

`fn=heap_caps_malloc` is the allocator; `calloc_internal` still reports malloc because calloc calls malloc.

**Why 1624:** WiFi blob TX/MSDU class (~1600 + 802.11 header). Not `ETH_MAX_PACKET_SIZE` (1522). W5500 RX `malloc(copy_len)` uses **default** `malloc`, not 0x80c.

**Why Step 4:** Management AP is serving the wizard over WiFi (`async_tcp`) **while** `loopTask` runs deferred `persist()` → `sd-writeJson`. Log:

```
sd-writeJson:before  free=5496 largest=2932
dma-alloc-fail size=1624 caps=0x80c  largest=628   ← concurrent WiFi DMA during SD write
sd-writeJson:after   free=11844 largest=4596      ← persist released INTERNAL
```

Then later `configure-existing-network` hits `largest=1076` < 1536 → existing ETH_DMA_LOW gate → HTTP 502. RouterOS itself is healthy (test job 200).

## What collapses dma_largest

| Transition | Cause | Label |
|------------|--------|--------|
| ~7412 → 2932 after selection | Deferred persist `DynamicJsonDocument(JSON_DOC_SMALL=2048)` + Arduino `String` serialize in `writeJson` on INTERNAL (`ALWAYSINTERNAL=4096`) | **STRONGLY INDICATED** |
| 2932 → 628 during sd-writeJson | Same persist window + WiFi 1624 DMA + ETH traffic | **PROVEN overlap** |
| Recover to 4596 after write | Persist buffers freed | **PROVEN** |
| Later 1076 at router-write | Further INTERNAL/WiFi/ETH use before worker TX | **STRONGLY INDICATED** |

SD is the **window**, not an IDF `setup_dma_priv_buffer` caller.

## Instrumentation added (temporary)

- Fail hook: `task=` + backtrace **only** for 0x80c / size 1624
- `[dma-trace]` at wifi-selection, durable persist, configure enqueue, configure job, ETH_DMA_LOW connect
- `/api/setup/status` traces only when `dma_largest < 4096`

## Minimal fix (NOT implemented)

Do **not** move WiFi DMA to PSRAM. Do **not** remove the ETH_DMA_LOW gate.

1. Persist path: `RouterProvisioningManager::persist` + `StorageManager::writeJson` serialized body → PSRAM (same N16R8 JSON-off-INTERNAL class), so SoftAP 1624 DMA still has a contiguous block.  
2. Optional later: worker `configure-existing-network` wait for headroom instead of immediate 502 (behavior change; not required to identify 1624).

Would touch: `RouterProvisioningManager.cpp`, `StorageManager.cpp` write serialize. Not pins, not RouterOS command count, not wizard.

## Tests after a future fix

Step 4 Finish → job 4 HTTP 200 → Step 5; no 1624 0x80c fail; `dma_largest` stays ≥ 1536 at `before router-write`; RouterOS test still 200.
