# FORENSIC ROOT CAUSE REPORT
## SD Hot-Unplug / Reinsert / DMA Exhaustion / WDT / Guru Meditation

**Date:** 2026-08-17  
**ELF:** `firmware.elf` SHA256 prefix `fa7b858a3` — **matches** crash dump (`ELF file SHA256: fa7b858a3`)  
**Mode:** Investigation complete before implementation

---

## 1. Executive conclusion

### PROVEN ROOT CAUSE

**Compound failure, two linked layers:**

1. **async_tcp WDT (PROVEN):** HTTP handlers on `async_tcp` perform **synchronous SD I/O** after the card is physically removed but while `_sdMounted/_sdReadable` remain true (health poll is only every **60 s**). Each `sdSelectCard()` blocks up to **500 ms**. Decoded WDT backtrace shows the exact chain ending in `sdWait` / `sdSelectCard` under `StorageManager::recoverSdTransaction` ← `readJsonFromSd` ← `GET /api/router/settings` (`ApiServer.cpp:2868`).

2. **Guru Meditation / DMA (PROVEN call site):** Core 0 `emac_w5500_task` fails `setup_priv_desc` allocating a **504-byte** `MALLOC_CAP_DMA|INTERNAL` RX bounce buffer (`dma_free=612`, `dma_largest=172`). IDF then proceeds into **NULL** (`EXCVADDR=0`) → LoadProhibited. This is the **W5500 SPI3** path, not FSPI/SD.

**Causal link (HIGH CONFIDENCE):** Prolonged SD-select blocking on `async_tcp` starves Ethernet packet processing → RX backlog → W5500 RX task demands DMA bounce buffers under already-fragmented DMA (timeline ends at largest=172) → alloc fail → NULL deref in IDF SPI master. `/api/router/cache` is the **victim request that generated RX traffic**, not a DMA allocator/leak in cache code (`fillRouterCache` is RAM/`RouterCacheManager`).

### CONTRIBUTING FACTORS

- No fail-fast media trip on SD open/exists failure from I/O paths (only poll/`verifySdHealthy`).
- `recoverSdTransaction()` always probes `SD.exists` / opens before every `readJsonFromSd` while mounted.
- Direct SD users outside StorageManager mutex: `ApiServer::sendSdFile`, `BackupManager`, `AssetManager`, `NdjsonLedger`, `AssetServer` (validated vs `SD_CARD_READ_WRITE_HOTPLUG_FORENSIC.md`).
- Storage lock 5 s timeouts while remount/sync holds the recursive mutex.
- Fallback writes with `baseCrc=0` during absence → expected conflict on remount (not the crash).

### RULED OUT

| Claim | Status |
|-------|--------|
| Dead SD card | **RULED OUT** — remount + write verify succeeded |
| `/api/router/cache` leaks DMA | **RULED OUT** — GET uses in-RAM cache; crash in `emac_w5500_task` |
| SD FSPI uses `setup_dma_priv_buffer` | **RULED OUT** — Arduino SD uses FIFO polling HAL |
| Need to raise WDT timeout | **RULED OUT** as a fix (diagnostic signal) |
| Need to merge SPI buses | **RULED OUT** |

---

## 2. WDT call chain (decoded)

ELF match confirmed.

```
_async_service_task (AsyncTCP.cpp:328)          [async_tcp / CPU1]
  → AsyncWebServerRequest::_onData / _parseLine
  → middleware → ApiServer lambda ApiServer.cpp:2868
       GET /api/router/settings
  → RouterPlatform::fillPublicSettings
  → MikroTikDriver::fillPublicSettings (MikroTikDriver.cpp:150)
  → StorageManager::readJson
  → StorageManager::readJsonFromSd (StorageManager.cpp:1017)
  → StorageManager::recoverSdTransaction (StorageManager.cpp:894)
  → FS::exists → VFSFileImpl → f_opendir / ff_sd_read
  → sdReadSector → sdSelectCard (sd_diskio.cpp:127)
  → sdWait(..., 500) (sd_diskio.cpp:106)   ← blocks up to 500ms per select
  → SPIClass::transfer → spiTransferByteNL
```

**Why WDT fires:** `async_tcp` must service sockets; it instead spins in SD card-select retries for a missing card. Task WDT aborts.

---

## 3. Guru Meditation call chain (decoded)

```
vPortTaskWrapper
  → emac_w5500_task (esp_eth_mac_w5500.c:836)     [Core 0 ETH RX]
  → emac_w5500_receive (...:738)
  → w5500_read_buffer (...:263)
  → w5500_read / w5500_spi_read (...:169)
  → spi_device_polling_start (spi_master.c)
  → setup_priv_desc (spi_master.c:1253)
  → uninstall_priv_desc / setup_dma_priv_buffer path
  → heap_caps_aligned_alloc(504, caps=0x808) FAIL
       dma_free=612  dma_largest=172
  → continues with invalid/NULL descriptor
  → LoadProhibited EXCVADDR=0x00000000
```

`0x40056f59` is ROM/low stub (`??` in addr2line) — consistent with memcpy/null deref after failed setup.

---

## 4. DMA allocation call chain vs `/api/router/cache`

```
GET /api/router/cache
  → (Ethernet RX of request frames)
       → emac_w5500_task → 504-byte DMA RX bounce   ← FAILS HERE
  → [never reaches application fillRouterCache for the panic path]

Application fillRouterCache:
  RouterPlatform::fillRouterCache → RouterCacheManager::fillPublic (RAM)
  No heap_caps DMA alloc in that path.
```

**Conclusion:** `/api/router/cache` is **temporal victim**, not root allocator. Required 504 B contiguous DMA unavailable after prior degradation.

---

## 5. Hypothesis verdicts

| ID | Hypothesis | Verdict |
|----|------------|---------|
| H1 | Repeated blocking SD ops after removal | **PROVEN** (Select Failed spam + stack) |
| H2 | Those ops run on async_tcp | **PROVEN** (WDT backtrace) |
| H3 | Cause async_tcp WDT | **PROVEN** |
| H4 | Storage transactions accumulate / lock starvation | **PROVEN** (log: Timed out waiting for storage transaction lock) |
| H5 | SD access outside StorageManager mutex | **PROVEN** (ApiServer/Backup/Asset/NdjsonLedger/AssetServer) |
| H6 | Storage/recovery causes severe DMA depletion | **HIGH CONFIDENCE** (timeline + WDT→RX backlog mechanism); not a proven app DMA leak |
| H7 | `/api/router/cache` requests DMA SPI RX | **PROVEN** as ETH RX of that HTTP exchange; not cache JSON code |
| H8 | SPI fails 504-byte DMA alloc | **PROVEN** (dma-alloc-fail log) |
| H9 | Failure → NULL → LoadProhibited | **PROVEN** (EXCVADDR=0, IDF path) |
| H10 | A/B/C/D | **C PROVEN as primary** (SD failure + concurrent ETH); **A** contributes; **B** not proven as independent leak; **D** remount race **PLAUSIBLE** for bypass callers, not required for WDT |

---

## 6. SD ownership graph

| Owner | Role |
|-------|------|
| `SdSpi.cpp` `g_sdSpi(FSPI)` | Sole SD SPIClass |
| `StorageManager::mountSdCard` | Only intended `SD.begin` / remount `SD.end`+begin |
| `StorageManager::handleSdRemoved` | `SD.end()` on media loss |
| **Bypass users** | `ApiServer::sendSdFile`, `BackupManager` (many `SD.*`), `AssetManager`, `web/AssetServer`, `NdjsonLedger` |

**FSPI ≠ SPI3:** Confirmed. W5500 `SPI3_HOST` independent. Remount must not touch W5500 pins (existing SdSpi comments).

**StorageManager is NOT a global serialization boundary** for all SD I/O — bypasses can race `SD.end()`.

---

## 7. Storage lock graph

- Recursive mutex, timeout **5 s** (`STORAGE_LOCK_TIMEOUT_MS`).
- `readJson`/`writeJson`/`pollStorageHealth`/`attemptSdRecovery`/`syncFallbackToSd` take lock.
- API on async_tcp waits for lock while remount+sync runs → timeouts.
- Poll uses `tryLockStorage` (good for loopTask).

---

## 8. SPI ownership

```
W5500 → SPI3_HOST → ETH.begin once at boot → IDF DMA (setup_dma_priv_buffer)
SD    → FSPI/SPI2 → renzFiSdSpiBegin → Arduino FIFO SPI (no IDF priv DMA)
```

Repeated `SPI.end`/`begin` on FSPI during remount: **intended** for SD only; must remain serialized.

---

## 9. DMA timeline (from supplied logs)

| Stage | dma free | largest | minimum |
|-------|----------|---------|---------|
| boot | 196244 | 172020 | 190848 |
| after W5500 | ~168480 | ~163828 | — |
| healthy SD | ~163948 | ~163828 | — |
| production | ~56360 | ~51188 | 44264 |
| later | 28928 | 17396 | 7592 |
| sd-readJson before | 9368 | 4596 | 836 |
| after | 8932 | 4340 | — |
| alloc-fail | **612** | **172** | — |
| need | **504** | — | — |

Not enough contiguous DMA for 504 B. Pattern fits **fragmentation + concurrent W5500 demand**, not necessarily a permanent leak. Remount success does not restore DMA by itself.

---

## 10. Race conditions (exact)

1. Card pulled → `_sdReadable` still true for ≤60 s → every `readJson` hits SD from async_tcp.
2. Remount `SD.end()` while `BackupManager`/`AssetManager`/`NdjsonLedger` hold live `File` / call `SD.open` without mutex.
3. Remount holds storage lock during sync → other handlers timeout.
4. async_tcp blocked on SD → ETH RX backlog → W5500 DMA pressure.

---

## 11. Conflict / baseCrc=0

**Expected** when fallback written during SD absence without a prior SD CRC (`baseCrc==0`). Remount correctly retains divergent SD and dirty SPIFFS (no auto-merge). **Not crash root cause.**

---

## 12. Root-cause confidence

| Item | Confidence |
|------|------------|
| WDT = SD select on async_tcp via readJson/recoverSdTransaction | **PROVEN** |
| Guru = W5500 RX DMA alloc fail → NULL | **PROVEN** |
| Link WDT blocking → DMA pressure | **HIGH CONFIDENCE** |
| Progressive DMA leak from remount alone | **NOT PROVEN — REQUIRES PHYSICAL VALIDATION** (Test 7) |
| Bypass File-after-SD.end as this crash’s direct cause | **PLAUSIBLE**, secondary |

---

## 13. Implementation intent (next)

Minimal fixes targeting proven causes only:

1. Fail-fast: any SD open/exists failure while “mounted” → immediate `handleSdRemoved` / MEDIA_MISSING.
2. Never call `recoverSdTransaction` / SD I/O when `!_sdMounted || !_sdReadable`.
3. Gate bypass call sites with `healthy()` / mounted checks (fail fast, no select loops).
4. Explicit storage lifecycle logging (state transitions).
5. Keep DMA soft-gates; do not raise WDT; do not merge buses; preserve conflict policy.
