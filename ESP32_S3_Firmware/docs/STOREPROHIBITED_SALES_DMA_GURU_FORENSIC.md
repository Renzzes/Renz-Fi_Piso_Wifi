# StoreProhibited Guru after Done Paying / RecordSale — Proven Root Cause

**Date:** 2026-08-23  
**Crash ELF SHA256 prefix (log):** `6dad3639b`  
**Exception:** `Core 1 panic'ed (StoreProhibited)` `EXCVADDR: 0x000000b4`  
**Decoded crash site (matching build class):**  
`ArduinoJson::TextFormatter::writeRaw` → `AsyncWebServerRequest::_send` → `async_tcp`

---

## 1. Verdict

**Yes — still a DMA-class failure**, but a **different crash site** than the earlier LoadProhibited / W5500 RX NULL path.

| Layer | Proven finding |
|-------|----------------|
| **Why DMA died** | `RecordSale` after activate loaded/saved `sales.json` with a **large JSON document + Arduino String serialize allocated from INTERNAL/DMA SRAM** (`sales-sd-read` delta ≈ **−8600** bytes; `dma_largest` 9204 → 4340 → later **28**) |
| **Why Guru fired** | Concurrent portal heartbeat hit ETH DMA critical; W5500 `setup_dma_priv_buffer(54)` failed on `async_tcp`; a concurrent `AsyncWebServerRequest::_send` then **wrote through a bad/null String buffer** (`StoreProhibited` @ `0xb4`) |
| **Not the cause** | SoftAP, MikroTik activate itself (activate completed `ok=yes`), portal session logic |

---

## 2. Timeline (from user serial)

1. Done Paying → activate queued → RouterOS reuse OK (`remaining=1800`).
2. `worker_done ok=yes` → **sales-sd-read** then **sales-sd-write** while portal session/heartbeat storm continues.
3. DMA collapses: `free=780 largest=28`.
4. `[http] drop reason=api-json-admit (ETH DMA critical)` — admit gate worked.
5. `[dma-alloc-fail] size=54 … task=async_tcp` + W5500 TX fail.
6. **StoreProhibited** in ArduinoJson `_send` (not `emac_w5500_task` LoadProhibited).

Prior HTTP admit/SSE gates limited *new* responses but **could not stop sales I/O from eating the DMA pool**, and **`client->close()` on critical** still raced TX/`_send`.

---

## 3. Why it still triggered after the previous DMA fix

Previous fix raised HTTP admit floor + capped concurrent SPA/JSON streams + SSE quiesce.

That does **not** protect against:

1. **loopTask / portal work queue** `RecordSale` → `SessionManager::upsertSale` using `HeapJsonDocument(JSON_DOC_LARGE=24576)` (INTERNAL malloc).
2. `StorageManager::writeJsonToSd` → `serializeJson(doc, Arduino String)` (another INTERNAL allocation).
3. Critical-path `dropClient()` → `client->close()` still needing W5500 TX while `_send` is in flight.

So portal + Admin can be gated correctly and **still reboot** when a sale is persisted under load.

---

## 4. Targeted fix applied

| Change | Effect |
|--------|--------|
| Sales mutations use `PsramJsonDocument` (not `HeapJsonDocument` LARGE) | Sales parse pool leaves INTERNAL/DMA alone |
| `writeJsonToSd` uses `serializeJsonToPsram` | No Arduino String on the SD write path |
| RecordSale waits briefly for HTTP DMA headroom when critical | Avoids overlapping sale SD with an already-starved ETH |
| Critical HTTP drop: **no socket close/send** | Avoids racing `_send`/TX into StoreProhibited; peer times out; paced bodies already `RESPONSE_TRY_AGAIN` |

---

## 5. Expected behaviour after flash

- Done Paying + activate + sale persist with 2 portal phones: **no** `sales-sd-read` −8 KB DMA cliff.
- May still briefly see `ETH_DMA_LOW` under Admin SPA load — soft fail, not reboot.
- No StoreProhibited / LoadProhibited from W5500 bounce-buffer starvation during RecordSale.
