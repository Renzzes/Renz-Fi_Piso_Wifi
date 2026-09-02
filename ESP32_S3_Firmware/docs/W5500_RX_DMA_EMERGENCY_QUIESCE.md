# W5500 RX DMA Emergency Quiesce (LoadProhibited fix)

## Proven crash chain

```
Admin dashboard burst + 2 portal clients + SSE
  → concurrent sd-readJson + static files (favicon/icons)
  → dma_largest collapses (11252 → 36 bytes)
  → [dma-alloc-fail] task=w5500_tsk size=496 caps=0x808
  → setup_dma_priv_buffer: Failed to allocate priv RX buffer
  → ESP-IDF W5500 driver dereferences NULL
  → LoadProhibited EXCVADDR=0x00000000
```

**Not CPU overload** — ping 0–1 ms, PSRAM heap ~8 MB free. Failure is **DMA-capable internal heap fragmentation**.

## Fixes applied

### 1. Reactive emergency quiesce (`DmaMemoryMonitor`)

- `heap_caps` failed-alloc hook classifies W5500 SPI bounce failures (`caps=0x808`, size 32–1600, task `w5500_tsk` / `emac_w5500` / `async_tcp`).
- On match: `signalW5500SpiAllocFailure()` sets `g_ethDmaEmergency` for 8 s.
- While emergency: `isEthDmaCritical()` true, `hasHttpServeHeadroom()` false → new HTTP/SSE rejected, SSE closed on next `checkEthDmaQuiesce()` loop tick.
- `tickEmergencyRecovery()` clears flag once `dma_largest ≥ 3072` after hold window.

### 2. Small static file pacing (`WebResponse::serveFile`)

- Favicon, icons, manifest: paced chunk stream + HTTP slot (max 2 in flight) + `RESPONSE_TRY_AGAIN` when DMA low.
- Prevents unguarded `beginResponse(fs, …)` from racing W5500 RX during Admin fan-out.

### 3. Ungated HTTP paths

- `serveNotFound`, `serveRedirect`, `serveOptions`: DMA admit before `beginResponse`.
- `ErrorHandler::serve`, `StaticFileServer` fallback page: `ensureEthTransmitHeadroom`.
- `ApiServer::sendWorkerResult`, `sendAdminJobAccepted`: gated / paced JSON path.

### 4. Portal coin UI poll (`portal/renzfi-app.js`)

- `COIN_POLL_MS`: 2000 → **500** during coin modal so credits appear within 500 ms when SSE is quiesced.

## Operational note

Coin **detection** (GPIO) is unchanged (~200 ms settle). Faster poll only improves **display** when SSE is unavailable under DMA pressure.

Rebuild MikroTik portal bundle after editing `portal/renzfi-app.js`:

```bash
npm run build:mikrotik-portal
```
