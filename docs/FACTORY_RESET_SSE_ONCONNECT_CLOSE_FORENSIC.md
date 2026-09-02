# Factory-Reset SSE Rejection NULL Dereference — Forensic Incident

**Date:** 2026-08-21  
**ELF SHA256 prefix:** `4d27f8641` (**MATCHES** physical dump `4d27f8641`)  
**ELF path:** `ESP32_S3_Firmware/.pio/build/waveshare_esp32_s3_eth/firmware.elf`  
**Status:** Root cause **PROVEN**. Minimal §14 fix **APPLIED** (pre-construction `authorizeConnect`; `onConnect` no longer calls `client->close()`). **NOT PHYSICALLY VALIDATED.**

---

## 1. CRASH CLASSIFICATION

| Claim | Status |
|-------|--------|
| W5500 DMA exhaustion / `dma_largest` insufficient | **REJECTED** for this incident (`largest=9716` ≫ ~1490) |
| Historical Admin DMA Guru class | Still **PROVEN** historically; **not** this crash |
| NULL dereference (`LoadProhibited`, `EXCVADDR=0`) | **PROVEN** |
| Crash inside SSE client construction after Priority-1 quiesce rejection | **PROVEN** |
| Subsystem | **AsyncTCP `AsyncClient` + ESPAsyncWebServer `AsyncEventSourceClient`**, triggered by **EventBus Priority-1 `onConnect` → `client->close()`** |

**Class name:** Priority-1 SSE reject-from-`onConnect` lifetime violation (synchronous disconnect during constructor).

---

## 2. SYMBOLICATED BACKTRACE

Tool: `xtensa-esp32s3-elf-addr2line` against ELF `4d27f86413f64026…`

| Address | Function | File:Line |
|---------|----------|-----------|
| `0x42196274` / PC `0x42196277` | `AsyncClient::setNoDelay(bool) const` | `AsyncTCP/src/AsyncTCP.cpp:1169–1170` |
| `0x4200b60a` | `AsyncEventSourceClient::AsyncEventSourceClient(...)` | `ESPAsyncWebServer/src/AsyncEventSource.cpp:188` |
| `0x4200b697` | `AsyncEventSourceResponse::_switchClient()` | `AsyncEventSource.cpp:506` |
| `0x4200b6bd` | onAck lambda → `_switchClient` | `AsyncEventSource.cpp:495` |
| `0x42006483` | `std::function::operator()` | libstdc++ `std_function.h:591` |
| `0x42006e0f` | `AsyncTCP_detail::handle_async_event(...)` | `AsyncTCP.cpp:300` |
| `0x40382eb5` | `vPortTaskWrapper` | FreeRTOS `port.c:139` |

**Registers:** `A2=0x00000000` = `this` for `AsyncClient::setNoDelay` (null `AsyncClient*`).

**Disassembly confirmation (same ELF):** after `call8 AsyncEventSource::_addClient`, ctor does `l32i a10,[this+0]` (`_client`) then `callx8 setNoDelay` at `0x4200b60a`.

---

## 3. FIRST FAILURE

**PROVEN first failure:** Priority-1 `EventBus` `onConnect` callback, while factory reset busy, calls `client->close()` **inside** `AsyncEventSourceClient` construction, before line 188 `setNoDelay`.

Exact app code:

```cpp
_source->onConnect([](AsyncEventSourceClient *client) {
  if (!client) return;
  if (HttpPlaneGate::isFactoryResetBusy()) {
    Serial.println("[http-quiesce] rejected SSE connect (factory reset busy)");
    client->close();   // ← FIRST FAILURE
    return;
  }
  ...
});
```

Serial proves this path ran immediately before Guru.

---

## 4. SECONDARY FAILURE

**PROVEN secondary failure:** After `_addClient` returns, constructor executes `_client->setNoDelay(true)` with `_client == nullptr` → LoadProhibited.

Library order (`AsyncEventSource.cpp:187–188`):

```text
_server->_addClient(this);   // invokes onConnect → close → sync disconnect
_client->setNoDelay(true);   // SECONDARY / fatal NULL call
```

---

## 5. DMA STATUS AT FAILURE

| Metric | Value | Interpretation |
|--------|-------|----------------|
| Last `[dma] … largest=` before crash | **9716** | Healthy contiguous DMA |
| Typical W5500 TX need | ~1490 | `9716 >> 1490` |
| `[dma-alloc-fail]` in this sequence | **Absent** | Not a DMA-starve Guru |

**Conclusion:** This incident is **not** DMA-pool exhaustion.

---

## 6. FACTORY RESET TIMELINE

**STRONGLY INDICATED** from serial + source (same Core 1 `async_tcp` path):

```text
loopTask: FactoryResetWorker busy; SD wipe (“assets: music deleted”)
  dma_largest=9716 (healthy)

async_tcp: GET /api/system/factory-reset/status  (allow-listed; continues)

Browser still attempts SSE /api/events
  → AsyncEventSourceResponse::_switchClient
  → new AsyncEventSourceClient
  → _addClient → EventBus onConnect
  → [http-quiesce] rejected SSE connect
  → client->close()
  → AsyncClient::_close() SYNCHRONOUSLY runs onDisconnect (_discard_cb)
  → _onDisconnect: _client=nullptr; erase unique_ptr (destroy client mid-ctor)
  → onDisconnect also delete AsyncClient*
  → ctor resumes → setNoDelay on NULL → Guru
```

`/api/events` is **not** covered by `HttpPlaneGate` ensure* gates (separate `AsyncEventSource` handler). Quiesce only “rejects” SSE via this unsafe `onConnect` path.

---

## 7. SSE LIFETIME ANALYSIS

| Question | Answer |
|----------|--------|
| Object in connect callback? | Fully allocated `AsyncEventSourceClient*`, already `emplace`d into `_clients`, **constructor not finished** |
| Guaranteed non-null? | Yes at entry; **PROVEN** path then nulls `_client` |
| Busy rejection action? | Log + `client->close()` |
| close → AsyncTCP? | **Yes** — `AsyncClient::close` → `_close` → `_tcp_close` → **sync** `_discard_cb` |
| Concurrent closeAllClients vs onConnect? | Possible in principle; **not required** for this crash — single-threaded ctor reentrancy is enough |
| Collection mutation during callback? | **Yes** — `_handleDisconnect` erases `unique_ptr` while still in `_addClient` / ctor |
| EventBus object destroyed? | No — `EventBus` / `AsyncEventSource` remain; **client** is destroyed mid-ctor |
| Capture lifetime? | Lambda captures nothing; uses `client` arg; still unsafe to close mid-ctor |
| NULL possibility? | **PROVEN** `_client` null before `setNoDelay` |
| Callback async after handler return? | Construction itself is async relative to HTTP (onAck → `_switchClient`); **disconnect callback is synchronous inside `close()`** |

---

## 8. EVENTBUS CONCURRENCY ANALYSIS

- `closeAllClients()` / `heartbeat()` / `emit()` run from `loopTask`; SSE ctor/`onConnect` run on `async_tcp` (Core 1).
- Library protects `_clients` with `recursive_mutex` — re-entry from `_handleDisconnect` during `_addClient` is allowed and **still destroys the constructing object**.
- Crash does **not** require a second task racing; it is **reentrancy / lifetime** on the same `async_tcp` stack.

---

## 9. STATUS-POLL CONCURRENCY ANALYSIS

- `GET /api/system/factory-reset/status` is allow-listed and appears in the log before the SSE reject.
- Handler uses `fillSnapshot` / `poll` (RAM worker snapshot) — **not** implicated by symbolicated PC.
- Status poll is concurrent background traffic; **not** the NULL site.
- **UNKNOWN** whether status+SD wipe can cause a *different* future crash; **not** this stack.

---

## 10. OBJECT OWNERSHIP / LIFETIME

**PROVEN chain:**

1. `AsyncClient::_close()` (`AsyncTCP.cpp:991–998`) calls `_discard_cb` **synchronously** when close succeeds.
2. SSE `onDisconnect` (`AsyncEventSource.cpp:179–184`) calls `_onDisconnect()` then **`delete c`** (deletes `AsyncClient`).
3. `_onDisconnect` sets `_client = nullptr` and `_handleDisconnect` **erases** the `unique_ptr<AsyncEventSourceClient>`, destroying the object **before its constructor returns**.
4. Constructor continues and calls `_client->setNoDelay(true)` → null `this` in `setNoDelay`.

Priority-1 introduced the only Renz-Fi call that closes from `onConnect`.

---

## 11. PROVEN ROOT CAUSE

Calling `AsyncEventSourceClient::close()` from `EventBus::onConnect` while factory reset is busy is unsafe: it triggers synchronous AsyncTCP disconnect teardown that nulls/destroys the client **before** the library constructor finishes `setNoDelay`, causing LoadProhibited.

DMA was healthy; this is a **lifecycle / NULL dereference** bug introduced by Priority-1 SSE rejection technique.

---

## 12. EVIDENCE

1. ELF SHA matches physical dump.  
2. addr2line + objdump pin PC to `setNoDelay` called from ctor line 188 after `_addClient`.  
3. Serial `[http-quiesce] rejected SSE connect` is only emitted immediately before `client->close()` in that callback.  
4. `AsyncClient::_close` source proves synchronous `_discard_cb`.  
5. `dma_largest=9716` rejects DMA starvation classification for this Guru.

---

## 13. HYPOTHESES NOT YET PROVEN

| Hypothesis | Status |
|------------|--------|
| Frontend reconnect storm necessary | **NOT REQUIRED** — one reject-with-close is enough; storm only increases probability |
| `closeAllClients()` concurrent with this ctor | **UNKNOWN / not needed** for proven path |
| Status endpoint races SD wipe | **UNKNOWN** — separate investigation if needed |
| `request->clientRelease()` returned null initially | **REJECTED** — earlier ctor calls `setRxTimeout` on `_client` before `_addClient`; crash is after reject path |

---

## 14. MINIMAL FIX (proposed — not applied)

**Do not call `client->close()` from `onConnect`.**

Smallest correct approach:

1. **Remove** `client->close()` from the busy branch in `EventBus::onConnect` (log + return only is still unsafe if later code assumes live client — prefer early reject).
2. **Prefer:** reject SSE **before** `AsyncEventSourceClient` construction:
   - `AsyncEventSource::authorizeConnect(...)` returning false when `HttpPlaneGate::isFactoryResetBusy()`, **or**
   - wrap/gate `/api/events` so busy yields `409 FACTORY_RESET_IN_PROGRESS` without upgrading to SSE.
3. Keep `closeAllClients()` on enqueue for **already constructed** clients (that path is library-supported).
4. Keep frontend quiesce (defense in depth); firmware must stay safe if browser reconnects.

---

## 15. FILES THAT WOULD CHANGE

- `ESP32_S3_Firmware/src/EventBus.cpp` (required)
- Optionally tiny helper if authorizeConnect / early HTTP reject needs shared busy check
- `docs/FACTORY_RESET_COMMUNICATION_QUIESCE.md` update after fix
- This forensic doc (status → fixed after hardware)

---

## 16. WHAT MUST NOT CHANGE

W5500 pins/SPI/8 MHz, WiFi/SD DMA strategy, `ETH_DMA_LOW` / 1536, AsyncTCP core/stack/queue flags, RouterOS, Setup Wizard, Portal business logic, factory-reset wipe semantics, allow-list for reset status, PSRAM JSON architecture.

---

## 17. HARDWARE VALIDATION PLAN

After minimal fix flash:

1. Admin open + SSE connected → Factory Reset → no Guru; status poll works; non-allow-list APIs 409.  
2. Force SSE reconnects during busy (DevTools / second tab) → no Guru; no LoadProhibited.  
3. Admin closed reset → completes; healthy boot.  
4. Confirm no `[dma-alloc-fail]` and `dma_largest` stays healthy (orthogonal check).  
5. Post-reset Setup + Admin + Portal smoke.

---

## 18. REGRESSION TESTS

- Extend `factory-reset-contract-check.mjs`: assert EventBus busy path **must not** contain `client->close()` inside `onConnect`; assert authorizeConnect / early reject present.  
- Keep existing 25 contracts.  
- Manual: SSE reconnect during reset.

---

## STOP

~~No firmware/frontend code was modified in this forensic pass.~~

**§14 implemented (2026-08-21):** `EventBus::begin` uses `AsyncEventSource::authorizeConnect` so factory-reset-busy SSE requests are rejected by middleware **before** `handleRequest` / `AsyncEventSourceClient` construction. `onConnect` no longer calls `client->close()`. `closeAllClients()` on enqueue retained. Library status for unauthorized connect is **401** (ESPAsyncWebServer `authorizeConnect` default) — EventSource clients treat any non-200 as failure; product HTTP API allow-list / 409 quiesce for JSON routes is unchanged.

Await physical Tests A–F.
