# Admin Dashboard Guru Meditation / W5500 DMA Forensic Report

**Branch:** `feature/waveshare-esp32-s3-eth`  
**ELF decoded:** `ESP32_S3_Firmware/.pio/build/waveshare_esp32_s3_eth/firmware.elf`  
**addr2line:** `xtensa-esp-elf-addr2line` (matches physical PCs)

## Verdict

**PROVEN crash site:** W5500 SPI DMA priv-buffer allocation failure → NULL dereference in `setup_priv_desc` / `uninstall_priv_desc`, stack rooted in `emac_w5500_task` (RX path).

**INFERRED trigger:** Opening Admin over ETH (`/admin` → ~651 KB JS + `/api/status` INTERNAL JSON + SSE + optional cache sync) drives `dma_largest` below frame size (`1204 < 1490`).

---

## Decoded backtrace (PROVEN against current ELF)

| PC | Function | File |
|----|----------|------|
| `0x4213e0fd` | `uninstall_priv_desc` | `spi_master.c:1176` |
| `0x4213e881` | `setup_priv_desc` | `spi_master.c:1253` |
| `0x4213ed1f` / `0x4213f479` | `spi_device_polling_start` | `spi_master.c` |
| `0x4212c3eb` | `w5500_spi_read` | `esp_eth_mac_w5500.c:169` |
| `0x4219967b` | `w5500_read` | `:189` |
| `0x4212ae3b` | `w5500_read_buffer` | `:263` |
| `0x4212bf39` | `emac_w5500_receive` | `:738` |
| `0x4212c243` | `emac_w5500_task` | `:836` |

Exception: LoadProhibited, `EXCVADDR=0`, `EXCCAUSE=0x1c`.

Physical prelude: `[dma-alloc-fail] size=1490 caps=0x00000808` (DMA\|INTERNAL) with `dma_largest=1204` → TX fail; then RX `size=636` `largest=500` → panic.

## Admin request sequence (PROVEN)

```
GET /admin → index.html + /assets/*.js (~651 KB over W5500)
GET /api/health
synchronizeAdminClient:
  GET /api/status  (always; DynamicJsonDocument 8192 INTERNAL)
  POST /api/router/cache/sync  (only if cache stale)
SSE GET /api/events
DashboardPage fan-out: /api/status, coin, system/health, …
```

Sales chart APIs are **not** on default Dashboard load (**REJECTED** as sole cause).  
Portal DMA fix `176aec0` moved portal-save JSON to PSRAM; **`/api/status` still uses INTERNAL `DynamicJsonDocument`**.

## Soft-gate coverage (PROVEN)

`hasEthTransmitHeadroom()` (largest DMA ≥ 1536) gates sales-chart HTTP and RouterOS writes — **not** SPA `serveFile` or `/api/status`.

## Minimal fix (approved for implementation)

1. Move `/api/status` (and large `/api/health` appliance body) to `PsramJsonDocument`.
2. Soft-gate large SPIFFS asset serves (`>32 KiB`) when DMA headroom missing → `503 ETH_DMA_LOW` + Retry-After (avoid crash; client retries).
3. Do not disable WDT/DMA; do not rewrite RouterOS; do not touch External AP.

## Classification

| Claim | Status |
|-------|--------|
| Crash in W5500 DMA priv path | **PROVEN** |
| Fragmentation (free > need, largest < need) | **PROVEN** |
| Admin SPA + status concurrent load pattern | **PROVEN** |
| Admin open = exclusive field trigger | **INFERRED** (high confidence) |
| Exact ELF SHA = field dump | **UNKNOWN** |
| Sales chart / always-on RouterOS as necessary cause | **REJECTED** |
| SD FSPI calls `setup_dma_priv_buffer` | **REJECTED** (prior forensics) |
