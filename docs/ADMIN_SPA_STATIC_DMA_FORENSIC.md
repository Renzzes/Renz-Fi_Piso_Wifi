# Admin SPA Static Asset → DMA Exhaustion Forensic Report (Stage 2)

**Branch:** `feature/waveshare-esp32-s3-eth`  
**Preserves:** `184609d`, `17a2b38`, `fe3683e`  
**Physical trigger (this log):** `GET /assets/wu37L76w.js` then `GET /assets/I9SS2idp.css` then `[dma-alloc-fail] size=1490`

## Verdict

| Item | Classification |
|------|----------------|
| Crash site (W5500 SPI DMA priv NULL deref) | **PROVEN** (unchanged from Stage 1) |
| Memory failure = DMA-capable internal heap exhaustion/fragmentation | **PROVEN** (`dma_largest=980` then `500`; `internal_free` still ~6–10 KB) |
| Files are **not** loaded entirely into RAM | **PROVEN** |
| `17a2b38` ETH_DMA_LOW gate does not fire on the triggering requests | **PROVEN** |
| Concurrent large SPIFFS/ETH responses collapse W5500 DMA | **PROVEN** (mechanism) / **INFERRED** (this log’s exact overlap) |
| Guru Meditation is a **secondary** driver failure after alloc fail | **PROVEN** |

---

## 1. Exact asset sizes (PROVEN)

From `dist/assets` and `ESP32_S3_Firmware/data/assets` (identical):

| File | Uncompressed bytes | Vite gzip (build report) | Served | gzip | MIME |
|------|-------------------:|-------------------------:|--------|------|------|
| `wu37L76w.js` | **651288** | 199010 | 651288 | **no** | `application/javascript` |
| `I9SS2idp.css` | **98293** | 16170 | 98293 | **no** | `text/css` |
| `BA_qmpt1.png` | 38573 | — | 38573 | no | `image/png` |
| `ClYcNINa.png` | 141605 | — | 141605 | no | `image/png` |

`gzip=no` is **PROVEN** in `resolveSpiffsServePath`: uncompressed path is chosen if it exists (`SpiffsHost.cpp`). Staging copies Vite output as-is; no `.gz` siblings.

Both JS and CSS **exceed 32 KiB**. PNGs do too.

Arduino SD/SPIFFS `File::read` copies into the caller buffer. That buffer is **not** DMA-mapped flash; it is a heap `std::array` (see below).

---

## 2–4. Static file implementation — buffering (PROVEN)

```
GET /assets/*
  StaticFileServer::serveStaticOrIndex
    WebResponse::serveFile
      req->beginResponse(SPIFFS, path, mime)
        new AsyncFileResponse  → fs.open (File handle only)
      req->send → AsyncAbstractResponse::_respond
        write_send_buffs  (synchronous first fill)
          new std::array<uint8_t, ASYNC_RESPONCE_BUFF_SIZE>
          AsyncFileResponse::_fillBuffer → File::read
          AsyncClient::add → lwIP pbuf
          W5500 SPI TX → setup_dma_priv_buffer (~1490)
```

`ASYNC_RESPONCE_BUFF_SIZE = CONFIG_LWIP_TCP_MSS * 2 = 1436 * 2 = **2872** bytes`  
(`WebResponseImpl.h`)

`AsyncFileResponse::_fillBuffer` = `_content.read(data, len)` — **chunked, not whole-file**.

First `_respond()` fills the TCP window in a `do/while` until `client()->space()==0` or buffer empty. `CONFIG_LWIP_TCP_WND_DEFAULT` = **5760**.

So per connection, one `_respond`/`_ack` typically injects up to ~5.7 KB into lwIP, **not** 651 KB into a single heap block.

**Hypothesis A (whole-file RAM buffer): REJECTED.**  
**Hypothesis B (double/triple copy of each chunk): PROVEN** — SPIFFS read → 2872 `_send_buffer` (default heap / INTERNAL for <16 KB) → lwIP pbuf (often DMA) → W5500 SPI bounce (`MALLOC_CAP_DMA|INTERNAL`, size 1490). Worst case is **per in-flight connection × TCP window**, not file size.

`elapsedMs` on StaticFileServer is **handler** time (`RequestTimer`), not transfer completion. JS `374 ms` / CSS `1515 ms` do **not** prove sequential full transfers.

---

## 6. Why `17a2b38` was insufficient (PROVEN)

Gate in `StaticFileServer.cpp`:

- Threshold: file ≥ **32768** bytes  
- Metric: `heap_caps_get_largest_free_block(MALLOC_CAP_DMA) >= 1536`  
- Evaluated **once**, **before** `beginResponse`, when this log still showed **dma=46292 / largest=45044**

Therefore both JS and CSS **pass** the gate. The collapse happens **during** W5500 TX of those streams.

The gate does **not** limit concurrent large responses. Chrome HTTP/1.1 opens multiple connections; JS+CSS+PNGs can all be in-flight.

A 503 after DMA is already dead is too late; a 503 that never runs because DMA was healthy at accept is useless.

---

## 7. Why 1490 / 636 / caps `0x00000808` (PROVEN)

`caps=0x00000808` = `MALLOC_CAP_DMA (0x800)` | `MALLOC_CAP_INTERNAL (0x8)` — ESP-IDF `setup_dma_priv_buffer` bounce buffer for SPI3/W5500.

**1490** ≈ Ethernet frame-class TX bounce (MTU ~1500 + SPI overhead alignment).  
**636** ≈ smaller RX bounce for a received frame.

These **must** stay in DMA-capable internal RAM. Application file buffers **may** live in PSRAM; they are not the SPI priv buffer.

---

## 8. Exception handling (PROVEN)

Sequence: alloc fail → `spi transmit failed` → `w5500_write_buffer` fail → `emac_w5500_transmit` fail → later RX alloc fail → `LoadProhibited` `EXCVADDR=0` in `uninstall_priv_desc` / `setup_priv_desc`.

**A (secondary consequence of unhandled NULL after alloc fail): PROVEN.**  
**B (unrelated NULL bug): REJECTED** as a separate root — same stack as DMA fail.

IDF W5500/SPI driver is third-party; this pass **does not** patch it. Survival requires **not** driving TX/RX when `dma_largest < 1490`.

---

## 9–12. Concurrency, SSE, SD, portal-save

| Hypothesis | Classification | Notes |
|------------|----------------|-------|
| C. Concurrent Admin large assets | **PROVEN** mechanism; **INFERRED** exact overlap in this log | JS then CSS logged; PNGs also >32 KiB |
| D. SSE | **INFERRED** small contributor | `EventSource(/api/events)` when logged in; 15–30 s heartbeat; not 40 KB DMA |
| E. Portal traffic | **INFERRED** amplifier | Same ETH/DMA pool |
| F. Portal-save `dmaFree=-11160` despite PSRAM JSON | **PROVEN** drop; **INFERRED** cause = `writeJson` `serializeJson` → Arduino `String` + SD stage/readback (INTERNAL for ~11 KB < SPIRAM always-internal threshold) | JSON pool is PSRAM; **String/SD path is not** |
| G. `sd-readJson` −400..−1152 | **REJECTED** as primary | Temporary ScopedProbe deltas; cannot explain 46 KB → 1 KB |
| H. W5500 1490 DMA need | **PROVEN** | |
| I. Driver NULL after fail | **PROVEN** secondary | |
| J. Fragmentation | **PROVEN** | free DMA 2228 vs largest 500; internal_free 10064 |
| K. Leak | **UNKNOWN** | Collapse tracks live transfers; recovery not observed because panic |

---

## Memory timeline (from supplied runtime)

| Phase | DMA | Kind |
|-------|-----|------|
| Boot | ~194572 | baseline |
| W5500 init | ~166868 | **permanent** driver |
| SD mount | ~162328 | small permanent |
| Idle/healthy before Admin | **46292 / largest 45044** | remaining DMA after subsystems |
| portal-save | 14436 → 3276 (−11160) | **temporary** INTERNAL copies during SD write |
| JS+CSS serve | → 1412 / largest 980 | **in-flight ETH TX** |
| TX fail 1490, then RX fail 636 | largest 500 | **failure** |
| Guru | — | secondary NULL |

Do **not** call this “ESP32 ran out of RAM.” Internal heap still had kilobytes. DMA-capable contiguous blocks did not.

---

## Minimal fix (this checkpoint)

1. **Single-flight** large SPIFFS assets (≥32 KiB): second concurrent request → `503 ETH_DMA_LOW` + `Retry-After` (browser retries; appliance stays up).  
2. **DMA-paced callback stream**: `beginResponse(mime, len, filler)` — if `dma_largest < 1536`, return `RESPONSE_TRY_AGAIN` (pause fill; do not end the body).  
3. Keep start-of-request ETH_DMA_LOW when already exhausted.  
4. Do not move W5500 SPI priv buffers to PSRAM.  
5. Do not rewrite AsyncWebServer; do not change RouterOS/AP/WDT.

**Physical validation: NOT DONE.**
