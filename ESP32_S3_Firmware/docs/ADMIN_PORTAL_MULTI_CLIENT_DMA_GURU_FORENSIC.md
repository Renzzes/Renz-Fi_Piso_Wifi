# Concurrent Admin + Portal → W5500 SPI DMA Guru Meditation

**Date:** 2026-08-23  
**Firmware:** `0.5.0-w5500`  
**Crash ELF SHA256 prefix (log):** `6c97c4e8f`  
**Symptom:** `Guru Meditation Error: Core 1 panic'ed (LoadProhibited)` with `EXCVADDR: 0x00000000` under 2 captive-portal phones + 1 Admin tablet.

---

## 1. Proven root cause

**DMA-capable internal SRAM exhaustion on the W5500 SPI3 path**, followed by an ESP-IDF SPI master null-deref when RX bounce-buffer allocation fails.

This is **not** a SoftAP DNS bug, not SD FSPI using `setup_dma_priv_buffer`, and not “Admin somehow breaks Core sessions.” Admin and portal are both HTTP clients on Ethernet; they compete for the same small `MALLOC_CAP_DMA` pool that W5500 needs for every frame.

### Crash-site chain (proven by serial)

```
[dma-alloc-fail] size=444 caps=0x00000808 task=w5500_tsk
                 dma_free=236 dma_largest=24 internal_free=6072
E (...) spi_master: setup_dma_priv_buffer(...): Failed to allocate priv RX buffer
Guru Meditation Error: Core 1 panic'ed (LoadProhibited)
EXCVADDR: 0x00000000
```

Same class as prior forensics (`SETUP_FINISH_GURU_MEDITATION_DMA_ROOT_CAUSE.md`,
`SD_HOTUNPLUG_DMA_WDT_FORENSIC.md`, `SALES_CHART_SPI_DMA_FORENSIC.md`):

1. `emac_w5500_task` / TX path needs an aligned DMA bounce buffer (`heap_caps_aligned_alloc`, caps `0x808` = DMA|INTERNAL).
2. Alloc fails when `dma_largest` is far below one frame (~444–1500 B).
3. IDF proceeds with a NULL descriptor → `LoadProhibited` at address 0.

---

## 2. Proven trigger (this incident)

Timeline from the captured log:

| Stage | Evidence |
|-------|----------|
| Steady | `dma≈21 KB`, `largest≈17 KB` (SoftAP + ETH already consuming most of the DMA pool vs boot ~42 KB) |
| Admin SPA load | `GET /assets/BliBvLvi.js` (666 KB streamed); CSS rejected `503 ETH_DMA_LOW in-flight` (existing large-asset gate worked) |
| Pressure | `dma minimum` falls to **1880 → 332** |
| First TX fail | `[dma-alloc-fail] size=1490 task=async_tcp dma_free=440 dma_largest=148` + W5500 TX errors |
| Multi-client | Portal `10.20.0.253` + `10.20.0.186` session/heartbeat + Admin `10.20.0.141` health/status/login fan-out |
| Collapse | `dma_free=236 dma_largest=24` |
| Fatal | `w5500_tsk` RX `size=444` alloc fail → Guru |

**Why multi-device + used Admin triggers it:**

- Admin SPA streams a large JS bundle over W5500 SPI (each chunk needs DMA bounce buffers).
- Admin also storms `/api/health` (and after login `/api/status`, `/api/system/*`) — each JSON response was only **paced mid-stream**, not **admitted** with a stricter DMA floor / concurrency cap.
- Two portal phones add `/api/portal/session` + heartbeat traffic on the same Ethernet DMA pool.
- SoftAP remains up (`SetupApReady`) and permanently reduces resting DMA headroom.

Heap free (~8 MB PSRAM) stays healthy the whole time — **irrelevant** to W5500 SPI DMA.

---

## 3. Why previous gates were insufficient

Existing protections:

- Large SPA single-flight (`in-flight` 503) — **worked** (CSS deferred).
- Chunk `RESPONSE_TRY_AGAIN` when `largest < 1536` — helps **in-flight** streams only.
- RouterOS / sales-chart gates — not on this path.

**Gap:** Many **new** small/medium JSON responses could start while `largest ≥ 1536`, then transmit together and drive `largest` to tens of bytes. W5500 **RX** has no application gate — any inbound frame then panics.

---

## 4. Targeted fix applied

| Change | Purpose |
|--------|---------|
| `kMinLargestDmaBlockForHttpStart = 3072` | Admit new HTTP only with spare room for W5500 RX (~504) + TX |
| `kCriticalDmaFloorForW5500Rx = 768` | Below this: drop client / skip SSE instead of racing SPI |
| Shared paced HTTP slots (`kMaxPacedHttpInFlight = 2`) | Cap concurrent SPA + ApiServer JSON body streams |
| Gate **all** ApiServer `sendJsonResponse` at admit time | Stops health/status/portal fan-out from starting under low DMA |
| EventBus reject/skip + MemoryDiagnostics close SSE when critical | Shed observational Admin SSE so Core Ethernet survives |
| Critical path closes socket instead of sending 503 | Avoid TX that needs DMA when RX already cannot allocate |

Core (coin / portal session / sales) does **not** depend on Admin SSE. Degraded Admin UI (503 / reconnect) is preferred over reboot.

---

## 5. Expected post-fix behaviour

Under 2 portal phones + active Admin:

- Possible brief `503 ETH_DMA_LOW` / `Retry-After: 2` on Admin or portal polls.
- Possible `[http-quiesce] ETH DMA critical — closing SSE…` then Admin EventSource reconnects.
- **No** `setup_dma_priv_buffer` → Guru / reboot.
- Portal sessions continue; Admin remains usable after DMA recovers.

---

## 6. Validation checklist

1. Flash firmware with this fix.
2. Open Admin on tablet; log in and use Dashboard (not idle).
3. Open captive portal on two phones; keep session/heartbeat alive.
4. Watch Serial: may see `ETH_DMA_LOW` / `concurrency` / SSE quiesce; must **not** see Guru / `priv RX buffer` panic.
5. Confirm `[dma] periodic-dma` `largest` stays above ~768 outside brief dips, and recovers.

---

## 7. Related docs

- `SETUP_FINISH_GURU_MEDITATION_DMA_ROOT_CAUSE.md`
- `SD_HOTUNPLUG_DMA_WDT_FORENSIC.md`
- `SALES_CHART_SPI_DMA_FORENSIC.md`
- `REMAINING_ISSUES_FORENSIC_IMPLEMENTATION.md`
