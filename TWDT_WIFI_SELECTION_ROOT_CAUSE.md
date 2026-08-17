# Forensic Report — TWDT on `POST /api/setup/router/wifi/selection`

**Mode:** Forensic investigation only — **no code changes**.  
**Date:** 2026-08-11  
**Appliance:** Production Renz-Fi ESP32-S3  
**Primary symptom:** Task Watchdog on `async_tcp` (CPU 1) during router Wi-Fi selection / finishing the Wi-Fi setup step  

---

## 0. Executive verdict

### Classification (required)

**A. Proven endpoint-specific blocking operation**  
with **B. Proven shared blocking infrastructure** (StorageManager transactional SD write + continuous SPIFFS checkpoint) as the amplifier.

Not C (lock wait), not D (synchronous worker wait), not E (concurrent-request proven), not F (ELF/backtrace unresolvable).

### One-sentence answer

**Clicking Continue on Wi-Fi Configuration posts `POST /api/setup/router/wifi/selection`, which runs `RouterProvisioningManager::saveWifiSelection` → `persist()` → `StorageManager::writeJson(/config/router-provisioning.json)` entirely on `async_tcp`; after a transactional SD write, the continuous SPIFFS checkpoint blocks in `SPIFFS.exists` / `esp_flash_read` long enough to trip the task WDT. The previous owner TWDT fix did not prevent this because it only deferred durable work for `POST /api/setup/owner` and never audited or hardened this still-synchronous setup endpoint.**

---

## 1. ELF identity (mandatory gate)

| Item | Value |
|---|---|
| Hardware ELF SHA256 prefix | `bafccbb34` |
| Workspace `firmware.elf` SHA256 | `BAFCCBB34ED606EF117528C2BD7365DBCC24DB4A5706AC72C2805E47CFFE79B6` |
| Path | `ESP32_S3_Firmware/.pio/build/freenove_esp32_s3_wroom/firmware.elf` |
| Match | **YES — backtrace resolution is authoritative** |

Tool: `xtensa-esp32s3-elf-addr2line -pfiaC -e firmware.elf <addrs>`

Core-dump flash corruption / “No core dump partition” messages after reboot are **secondary** and not used as root-cause evidence.

---

## 2. Exact endpoint and handler

| Field | Value |
|---|---|
| Method / path | `POST /api/setup/router/wifi/selection` |
| Plane | setup (Management AP `192.168.4.1`, client `192.168.4.2`) |
| Registration | `SetupServer.cpp` ~1250–1302 |
| Boot log label | `[web] POST /api/setup/router/wifi/selection (sync)` — **explicitly synchronous** |
| Handler timer | `WebRequestDiagnostics::RequestTimer(..., "SetupServer/wifi/selection")` |
| Business call | `_routerProvisioning->saveWifiSelection(...)` at **line 1284** |
| Worker? | **No** — not enqueued to `RouterProvisioningWorker` |

---

## 3. Proven call chain (source + backtrace)

```text
async_tcp (_async_service_task)
  → AsyncWebServerRequest middleware / handleRequest
  → SetupServer::registerRoutes lambda #22   SetupServer.cpp:1284
  → RouterProvisioningManager::saveWifiSelection   RouterProvisioningManager.cpp:1825
  → RouterProvisioningManager::persist             RouterProvisioningManager.cpp:1202
  → StorageManager::writeJson                      StorageManager.cpp:1267
      (ScopedStorageLock acquired here)
      → writeJsonToSdSerialized  (transactional SD — completed before tip)
      → checkpointToSpiffs                         StorageManager.cpp:1208
      → spiffsWriteFile                            StorageManager.cpp:994
      → recoverSpiffsTransaction                   StorageManager.cpp:1058
      → readSpiffsPayload                          StorageManager.cpp:834
      → FS::exists / SPIFFSImpl::exists
      → esp_vfs_stat → SPIFFS_stat → …
      → esp_partition_read → esp_flash_read        ← TWDT tip (resolved)
```

### First application-level frame (after AsyncTCP / ESP-IDF)

`SetupServer::registerRoutes … lambda … SetupServer.cpp:1284`  
(`0x420ca79d`)

### Deepest relevant application frame

`StorageManager::readSpiffsPayload` at **StorageManager.cpp:834**  
(`0x4209f92a`) — specifically `SPIFFS.exists(path)` during SPIFFS transaction recovery that precedes a checkpoint write.

### Exact blocking class at tip

**SPI flash read** (`esp_flash_read`) while servicing SPIFFS VFS `stat`/`exists` on the continuous-checkpoint path for `/config/router-provisioning.json` → `/fb/router-provisioning.json`.

This is **not** a RouterOS wait and **not** a FreeRTOS mutex wait frame in the resolved stack.

---

## 4. Resolved backtrace (useful frames)

| Address | Function | File:line |
|---|---|---|
| `0x40382ea5` | `vPortTaskWrapper` | FreeRTOS port |
| `0x420071c1` | `_async_service_task` | AsyncTCP.cpp:328 |
| `0x4200fdad` … `0x4200d646` | AsyncWebServer request/middleware | ESPAsyncWebServer |
| `0x420ca79d` | SetupServer wifi/selection lambda | SetupServer.cpp:**1284** |
| `0x4207ab62` | `saveWifiSelection` | RouterProvisioningManager.cpp:**1825** |
| `0x42079811` | `persist` | RouterProvisioningManager.cpp:**1202** |
| `0x420a4ff9` | `StorageManager::writeJson` | StorageManager.cpp:**1267** |
| `0x420a4801` | `checkpointToSpiffs` | StorageManager.cpp:**1208** |
| `0x420a3145` | `spiffsWriteFile` | StorageManager.cpp:**994** |
| `0x420a2e67` | `recoverSpiffsTransaction` | StorageManager.cpp:**1058** |
| `0x4209f92a` | `readSpiffsPayload` | StorageManager.cpp:**834** |
| `0x4200779b` … `0x42120654` | `FS::exists` → `vfs_spiffs_stat` | Arduino FS / SPIFFS |
| `0x403896ff` | `esp_flash_read` | esp_flash_api.c:972 |
| Top ISR/cache frames | `spi_flash_*` / `vTaskSuspendAll` | ESP-IDF SPI flash |

Unresolved: `0x400559dd` → `??` (ROM/low-level; not required once app frames resolve).

---

## 5. What `saveWifiSelection` does (and does not do)

From `RouterProvisioningManager.cpp:1800–1838`:

| Operation | On this path? | Where |
|---|---|---|
| Parse Wi-Fi selection JSON | Yes | RAM (`RouterWireless::parseWifiSelection`) |
| Update in-memory flags (`_wifiSelectionConfigured`, mode, SSID, iface) | Yes | RAM |
| `persist()` → write `/config/router-provisioning.json` | **Yes — sync on async_tcp** | SD + SPIFFS checkpoint |
| Write `router-connection.json` | No | — |
| Write `provisioning.json` / installation advance | No | — |
| RouterOS commands | **No** | — |
| Wi-Fi scan / DHCP / network connect | **No** | — |
| `RouterProvisioningWorker` enqueue / wait | **No** | — |
| Logger durable history | No on this function | — |

**RouterOS is not involved in this watchdog failure.**

After a successful save, the HTTP handler also calls `_provisioning->fillSetupStatus(...)` and serializes the response (`SetupServer.cpp:1292–1300`). The crash stack shows the fault **inside** `saveWifiSelection`/`persist`/`writeJson`, so status fill did not execute for this abort.

---

## 6. Storage / lock / task ownership

### Task ownership

| Task | Role in this failure |
|---|---|
| `async_tcp` (CPU 1) | **Runs the entire handler + durable write** — TWDT victim |
| `loopTask` | Not on the resolved stack for this abort |
| `router_worker` | Not used by this endpoint |

### Storage lock

- `writeJson` takes `ScopedStorageLock` (`StorageManager.cpp:1247–1248`).
- Lock timeout is `STORAGE_LOCK_TIMEOUT_MS = 5000` (`Config.h`) — equal to the TWDT budget class.
- **Proven for this incident:** lock was **held and I/O was progressing** (SPIFFS flash read), not stuck in `xSemaphoreTakeRecursive`.
- **Not proven:** another task already held the lock and starved this call. No lock-timeout serial line is in the supplied evidence.

### Durable work classification (forensic only)

| Step | Purpose | Timing today | Notes |
|---|---|---|---|
| Transactional SD write of `router-provisioning.json` | Persist Wi-Fi selection | Sync on `async_tcp` | Required for durability; unsafe on TWDT-subscribed HTTP task when combined with checkpoint |
| Continuous SPIFFS checkpoint | Last-known-good / fallback | Sync immediately after SD success | Tip of resolved stack; SD-hardening amplifier |
| SPIFFS transaction recover/`exists` | Safety before SPIFFS write | Sync | Flash cache disable / `esp_flash_read` |

Do **not** remove transactional SD or checkpoint architecture as a “fix”; the unsafe part is **execution on `async_tcp`**.

---

## 7. Frontend / Finish relationship (proven sequence)

User language “Finish” on this step maps to **finishing Wi-Fi Configuration (Step 4 Continue)**, not necessarily `POST /api/setup/finish`.

Proven UI sequence (`SetupWizardPageHtml.h`):

```text
wifiNextBtn click
  → saveWifiSelectionAndContinue()
  → POST /api/setup/router/wifi/selection     ← TWDT here (this incident)
  → on HTTP success: finishWifiStepAndApply()
  → executeAdoption()
  → POST /api/setup/router/existing-network/configure  (202 + RouterWorker — NOT reached if selection crashes)
```

Later wizard Finish (`/api/setup/finish`) is a **separate** later step and is already worker-queued when installation is not ready.

**Concurrent requests:** for this abort, selection never returned success, so the subsequent configure request was not started by this success path. Concurrent contention is **possible in other scenarios** but **unproven** as the cause of this stack.

---

## 8. Previous TWDT fixes — regression comparison

| Prior incident | Proven pattern | Fix scope | Covers wifi/selection? |
|---|---|---|---|
| `POST /api/setup/owner` (`TWDT_OWNER_ENDPOINT_ROOT_CAUSE.md`, `OWNER_SETUP_TWDT_IMPLEMENTATION_REPORT.md`) | Durable SD / history / persist on `async_tcp` | Owner-only: skip empty session rewrite, `infoLocal`, defer `provisioning.json`/`installation.json` to `loop()` | **No** |
| Admin Dashboard Test Connection (`ADMIN_DASHBOARD_ASYNCTCP_WATCHDOG_FORENSIC.md`) | `async_tcp` blocked waiting on RouterWorker / RouterOS | Admin path moved off blocking wait | **No** — different endpoint class |

### Same class?

**Yes — same unsafe execution class:** durable filesystem work on the TWDT-subscribed `async_tcp` task.

### Same endpoint / same fix applied?

**No.** Owner fix was endpoint-scoped.  
`wifi/selection` remains labeled `(sync)` and still calls `persist()` → `writeJson` inline.

### Did the owner TWDT fix regress / break wifi/selection?

**No evidence.** Owner changes do not alter `saveWifiSelection` or StorageManager checkpoint eligibility. Owner fix **remains intact** and simply **does not cover** this route.

### Did SD hardening contribute?

**Yes, as a duration amplifier (contributing factor):**

- Transactional SD write (`writeJsonToSdSerialized` / stage-backup-rename-verify).
- Continuous SPIFFS checkpoint for `StoragePaths::RouterProvisioningFile` (`isContinuousCheckpointEligible`).
- SPIFFS transactional recover + multiple `exists`/read/verify/flush operations.

Hardening is correct for durability; it makes **synchronous** use on `async_tcp` more likely to exceed the WDT budget.

### Blank SD relevance?

**Not the tip.** Tip is SPIFFS flash read during checkpoint. Blank/factory SD can make SD transaction work heavier on first writes, but the resolved tip is SPIFFS checkpoint I/O after SD success.

### MikroTik CPU relevance?

**None for this abort.** Zero RouterOS commands on this path.

---

## 9. PROVEN ROOT CAUSE vs contributing vs unproven

### PROVEN ROOT CAUSE

`POST /api/setup/router/wifi/selection` executes durable `StorageManager::writeJson` for `/config/router-provisioning.json` **synchronously on `async_tcp`**. The task WDT fires during the post-SD **SPIFFS continuous checkpoint** path (`checkpointToSpiffs` → `spiffsWriteFile` → `recoverSpiffsTransaction` → `readSpiffsPayload`/`SPIFFS.exists` → `esp_flash_read`), proven by ELF-matched addr2line on SHA `bafccbb34`.

### CONTRIBUTING FACTORS

1. Endpoint never moved to RouterWorker / deferred loop commit (unlike owner fix / configure).
2. SD production hardening increased per-write wall time (transaction + checkpoint).
3. `async_tcp` pinned to CPU 1 and subscribed to TWDT — long FS work cannot reset the watchdog.
4. Setup wizard Continue on Wi-Fi step always hits this sync path before adoption.

### POSSIBLE BUT UNPROVEN

1. Storage lock contention with `loopTask` / another writer.
2. Concurrent setup requests racing the same lock.
3. Pathologically slow SD hardware as the *only* cause (SD write completed far enough to reach checkpoint tip).

---

## 10. Recommended minimal implementation boundary (DO NOT IMPLEMENT HERE)

When implementation is later authorized, keep scope to this proven class only:

1. **Do not** disable/increase TWDT, change priorities, add busy-waits, or add RouterOS work.
2. **Do not** remove transactional SD writes or SPIFFS checkpoints.
3. **Do** stop running the durable `persist()`/`writeJson` for wifi selection on `async_tcp` — e.g. mirror the owner pattern (RAM commit + deferred durable flush on `loopTask`) **or** enqueue a fire-and-forget / job-style persist that returns quickly (without redesigning RouterWorker’s RouterOS role).
4. **Do** audit sibling sync setup endpoints that still call `writeJson`/`persist` on `async_tcp` (prevention gate below) so the class does not return on the next page.

---

## 11. Regression-prevention requirement (mandatory)

> **Every setup HTTP endpoint must be audited for synchronous SD/NVS/network/worker-wait operations on `async_tcp` before future implementation changes are accepted.**

Acceptance gate for any setup-route change:

1. Identify task that runs the handler (`async_tcp` vs worker vs `loopTask`).
2. List every `writeJson`, `appendHistory`, SPIFFS checkpoint, NVS burst, network connect, and worker `dispatch`/semaphore wait.
3. If any durable FS or multi-second wait remains on `async_tcp`, the change is **rejected** until deferred/offloaded.
4. Prefer proven patterns already used elsewhere (owner deferred commit; router save/test/configure/finish via worker) rather than inventing new architecture.

Companion checklist: `SETUP_ASYNCTCP_TWDT_PREVENTION.md`.

---

## 12. Why previous setup-page watchdog fix did not prevent this

| Question | Answer |
|---|---|
| Same Guru Meditation class? | Yes — FS durability on `async_tcp` |
| Same endpoint fixed? | No — owner only |
| Shared helper fixed globally? | No — StorageManager left intentional; callers must not run heavy writes on `async_tcp` |
| Why wifi/selection still fails? | Still `(sync)` + `persist()` + checkpoint-eligible `writeJson` on the HTTP task |

---

## 13. Evidence summary table

| Question | Finding |
|---|---|
| Exact endpoint | `POST /api/setup/router/wifi/selection` |
| Exact handler | SetupServer lambda; timer `SetupServer/wifi/selection` |
| Exact blocking function (tip) | `StorageManager::readSpiffsPayload` → `SPIFFS.exists` → `esp_flash_read` |
| Exact business function | `RouterProvisioningManager::saveWifiSelection` → `persist` |
| Exact file:line | `StorageManager.cpp:834` (tip); `RouterProvisioningManager.cpp:1825` (persist call); `SetupServer.cpp:1284` |
| RouterOS | Not involved |
| Worker wait | Not involved |
| Owner TWDT fix intact? | Yes; does not cover this route |
| SD hardening contributed? | Yes (duration amplifier) |
| Blank SD primary? | No |
| MikroTik CPU? | No |
