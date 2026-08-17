# Renz-Fi — ESP32 + MikroTik Stability Remediation

**Date:** 2026-08-15  
**Mode:** Source remediation only. **Do not flash. Do not change MikroTik configuration.**  
**Status:** Firmware compiled successfully. **Hardware stability is not claimed until hardware validation passes.**

**Product state:** The appliance is already operational. This pass is a narrow stability/performance remediation of proven `async_tcp` watchdog and unnecessary RouterOS login pressure. Customer session/payment/clock contracts are frozen.

---

## 1. Problem

Under the supported concurrent workload (Admin Dashboard open, 1+ paid customers, 2+ phones, captive portal, HotSpot Active, RouterOS API during activation/verify, SSE, portal heartbeat), the ESP32 aborted:

```
E (220403) task_wdt: Task watchdog got triggered.
E (220403) task_wdt:  - async_tcp (CPU 1)
E (220403) task_wdt: CPU 0: IDLE0
E (220403) task_wdt: CPU 1: async_tcp
```

This is **not** the old Admin-Test `dispatch()` wait (that showed `loopTask` on CPU 1). Production Admin test is already enqueue+202. `async_tcp` itself was executing a callback long enough to miss TWDT servicing.

Separately:

- Connected-only sessions after a Verify `/login` RX timeout kept `needsRouterOsWork() == true`, which re-armed HealthProbe → another RouterOS `/login`.
- Boot recovery could enqueue Activate while `eth_ip=0.0.0.0` (`Host is unreachable`), then `ETH_GOT_IP` arrived afterward.

TWDT was **not** changed. Watchdog feeding inside callbacks was **not** added. The callback must stay bounded.

---

## 2. Hardware evidence

| Item | Value |
|---|---|
| Last printed HTTP before abort | `GET /api/system/rgb` from Admin `10.20.0.246` |
| Last app log before abort | `[sales] today=13 week=20 month=20` |
| Watchdog | `E (220403) task_wdt` — `async_tcp` CPU 1; CPU 0 `IDLE0` |
| Crash ELF SHA256 prefix | `7839121b5` |
| Matching workspace ELF (pre-remediation) | `ESP32_S3_Firmware/.pio/build/freenove_esp32_s3_wroom/firmware.elf` |
| Full SHA256 of crash ELF | `7839121B5EC78CD5732DAC710DB4AE4506F9A9020037903DE54F8A39945E7697` |
| Post-remediation ELF SHA256 | `6F8E510CED2EB5463BFD34AABEE23D53798D19DFA33DBCC0C096BCD6DD4CEE03` |
| Core dump boot error | `esp_core_dump_flash: Core dump flash config is corrupted! CRC=0x7bd5c66f instead of 0x0` |

**Last printed request ≠ crashing callback.** `/api/system/rgb` is O(1) RAM (`RgbController::fillStatus`). The sales log is printed from the **same** `/api/status` handler that the backtrace proves was still inside SD `exists`/`f_stat`.

---

## 3. Proven root causes

### 3.1 Watchdog PC (decoded against ELF `7839121b5`)

Decoded with `xtensa-esp32s3-elf-addr2line -pfia` against the exact matching ELF. **Confidence: high (exact SHA prefix match).**

```
_async_service_task                         AsyncTCP.cpp:328
  → AsyncWebServerRequest::_parseLine       WebRequest.cpp:687
  → ApiServer::registerProductionRoutes     ApiServer.cpp:946   GET /api/status
  → RouterPlatform::load                    RouterPlatform.cpp:187
  → MikroTikDriver::loadSettings            MikroTikDriver.cpp:122
  → StorageManager::readJson                StorageManager.cpp:1242
  → readJsonFromSd                          StorageManager.cpp:921
  → recoverSdTransaction                    StorageManager.cpp:849
  → FS::exists → f_stat → ff_sd_read → spiTransferBytesNL
```

**Exact crashing function:** `StorageManager::recoverSdTransaction` (`SD.exists` on the transaction `.stage` file) called from `GET /api/status` via `_router->load()`.

**Blocking operation:** synchronous SD FAT `stat` / SPI transfer on the `async_tcp` task (CPU 1). CPU 0 was idle, so this is callback duration, not RouterWorker starvation.

The same `/api/status` handler also called `salesToday/Week/Month` → **three** `sales.json` reads on `async_tcp` (the `[sales] today=13 week=20 month=20` line). That work is also illegal on AsyncTCP even though the abort PC was in `load()`, not `aggregateSales`.

### 3.2 Connected-only HealthProbe after Verify login timeout

```
Connected → Verify → RouterOS /login RX timeout
  → DEGRADED → needsRouterOsWork() true because Connected
  → HealthProbe → another /login
```

Customer entitlement must stay Connected. Recovery must wait for real required work (Activate / Pause / Deauth / cleanup), not an endless Connected-only probe loop.

### 3.3 Activate with `eth_ip=0.0.0.0`

Persisted session recovery queued Activate before Ethernet had a local IP. `tryEnqueueActivateHotspotUser` checked the health FSM but not `linkUp() && hasIp() && ip != 0.0.0.0`.

---

## 4. High-confidence contributors (not changed in this pass)

| Contributor | Classification | Why not changed |
|---|---|---|
| `/api/status` `fileSizeBytes(LOGS_FILE)` + `fillSdStatus` live `SD.usedBytes()` | POTENTIALLY BLOCKING | Same handler, later than the abort PC. Snapshot exists; left as remaining risk rather than a second storage redesign. |
| `SessionManager::appendActiveUsers` → `users.json` from `/api/status` | POTENTIALLY BLOCKING | Small file; not on the abort PC. |
| `/api/health` `ProductionHandoff::evaluate` → `SPIFFS.exists("/index.html")` | POTENTIALLY BLOCKING | Production provisioned path skips `router->load()`. SPIFFS stat is typically short. |
| `EventBus::emit` → `AsyncEventSource::send` from RouterWorker | POTENTIALLY BLOCKING | Abort CPU was `async_tcp` inside `/api/status` SD, not SSE send. SSE kept. |
| Admin `/api/health` every 5 s | SAFE if snapshot-only | Health payload uses `fillHealthStatus` + `fillStorageStatus` RAM snapshot. Not disabled. |
| `/api/system/rgb` | SAFE | O(1) RAM. Not modified. |
| RouterOS `/login` 4–8 s under load | RouterOS-side | No timeout increase, no extra polling, no second worker. |
| Core dump CRC mismatch | Separate from WDT | No `coredump` partition. See §10. |

---

## 5. Changes implemented

### 5.1 Sales + router snapshots off AsyncTCP

| File | Functions |
|---|---|
| `SessionManager.cpp/.h` | `aggregateAllSales` (one SD pass), `refreshSalesSummarySnapshot` (loopTask), `salesToday/Week/Month` copy RAM only, dirty flag on `invalidateSalesChartCache` |
| `FirmwareApp.cpp` | `warmHealthSnapshots` / `refreshHealthSnapshots` (~2 s existing cadence) call sales refresh |
| `RouterPlatform.cpp/.h` | `refreshHealthCache` stores `_healthHost` / `_healthConfigured`; `cachedRouterHost()` / `cachedRouterConfigured()` |
| `ApiServer.cpp` | `GET /api/status` uses sales RAM helpers + cached router host; **no** `_router->load()` |

HTTP returns the last valid snapshot if a refresh cannot take `SalesLock`. Calendar semantics (today/week/month, undated uptime markers) are unchanged. `sales.json` remains the source of truth.

### 5.2 Connected-only HealthProbe suppression

| File | Functions |
|---|---|
| `PortalSessionManager.cpp/.h` | `needsHealthRecoveryProbe()` — pending Activate/Pause/Deauth/cleanup/Activating/ActivationError/Expiring only. **Not** Connected-only. |
| `PortalSessionManager::loop` | HealthProbe requires `needsHealthRecoveryProbe()`, not `needsRouterOsWork()`. |
| `drainHotspotOutcomes` | Verify `ok=false` still leaves Connected; logs `verify-login-timeout connected=1 probe_suppressed=1`. |

`needsRouterOsWork()` still treats Connected as “has router work” for the idle log and Verify cadence. Critical jobs still follow the existing health FSM (`allowsHotspotActivate` includes DEGRADED). One RouterWorker. No new poll timer. 120 s Verify trust window and 60 s Verify cadence unchanged.

### 5.3 Ethernet readiness gate on Activate

| File | Functions |
|---|---|
| `RouterProvisioningWorker.cpp/.h` | `ethernetReadyForHotspot()`, `ethernetIpLabel()`, `tryEnqueueActivateHotspotUser` refuses `!linkUp`, `!hasIp`, or `0.0.0.0` |
| `PortalSessionManager::retryPendingRouterWork` | Does not enqueue Activate while Ethernet is down; keeps `activationRetryPending` |
| `PortalSessionManager::loop` | Existing 1 s tick retries pending router work when the worker is idle (covers boot `ETH_GOT_IP` without a new poll loop) |

Failed enqueue still sets `activationRetryPending` and does **not** clear payment, `sessionGeneration`, or `secondsLeft`, and does **not** mark Connected. `alreadyAuthorizedThisGeneration` still prevents duplicate Activate.

---

## 6. Changes deliberately NOT implemented

- TWDT timeout / `esp_task_wdt_reset` in HTTP callbacks
- W5500 / SPI bus / AsyncTCP core pin changes
- Setup wizard steps or RouterOS / HotSpot profile changes
- Session clock, Model B, voucher expiry, `sessionGeneration`, payment math
- Optimistic Connected
- Second RouterWorker, persistent RouterOS session, idle `/identity` poll, extra recovery timer
- Increasing `STORAGE_LOCK_TIMEOUT_MS` or `ROUTEROS_IO_TIMEOUT_MS`
- Adding a `coredump` partition or enabling flash core dump
- Disabling Admin `/api/health` polling or removing SSE
- Rewriting every HTTP endpoint; `/api/system/rgb` left unchanged
- Portal `renzfi-app.js` session/clock semantics

---

## 7. AsyncTCP architecture (after this pass)

```
loopTask (~2 s refreshHealthSnapshots)
  → StorageManager::refreshRuntimeSnapshot     (STORAGE_LOCK, SD stats)
  → RouterPlatform::refreshHealthCache         (router.json load — OK here)
  → SessionManager::refreshSalesSummarySnapshot (one sales.json read — OK here)

GET /api/status  (async_tcp)
  → salesToday/Week/Month     RAM copy, no SalesLock, no SD
  → cachedRouterHost/Configured RAM copy, no load()
  → fillHealthStatus / fillRouterCacheStatus RAM

GET /api/health  (async_tcp)
  → fillHealthStatus + fillStorageStatus snapshot
  → ProductionHandoff (provisioned: no router.json load)

GET /api/system/rgb  (async_tcp)
  → RgbController::fillStatus  O(1) RAM
```

`recoverSdTransaction` / `SD.exists` remain required for real SD reads on **loopTask**. They must not run from production HTTP callbacks.

---

## 8. RouterOS workload reduction

| Before | After |
|---|---|
| Idle + no session | Still 0 API polls (`idle no-router-work`) |
| Connected-only after Verify login timeout | HealthProbe **suppressed**; no login loop |
| Activate / Pause / Terminate / required Deauth | Still allowed per existing health FSM |
| Verify | Unchanged cadence; HEALTHY-only; 120 s trust |
| Worker | Still one `router_worker`, one API session |

Log:

```
[router-health] verify-login-timeout connected=1 probe_suppressed=1
```

---

## 9. Ethernet readiness protection

Activate may enqueue only when:

1. Ethernet link is UP
2. Ethernet has a valid local IP (not `0.0.0.0`)
3. RouterOS health policy allows Activate

Otherwise:

```
[activate] deferred reason=ethernet_not_ready ip=0.0.0.0
```

(rate-limited to 5 s). Entitlement stays pending. The existing 1 s session tick + worker idle notify drain **one** Activate when IP appears. No busy loop, no new 100 ms timer.

---

## 10. Core dump finding

Boot:

```
E (1920) esp_core_dump_flash: Core dump flash config is corrupted!
CRC=0x7bd5c66f instead of 0x0
E (1928) esp_core_dump_elf: Elf write init failed!
```

**Cause:** `partitions_custom.csv` has **no `coredump` partition** (`nvs`, `otadata`, `app0`, `app1`, `spiffs` only). ESP-IDF still attempts a flash core dump after abort; the stored CRC does not match an empty/absent config (`0x0`). This is **stale/invalid partition data + configuration mismatch**, not the watchdog root cause.

The watchdog happened first. The core dump write then failed.

**Safe source-level choice:** leave partitions unchanged. Adding a coredump region would shrink OTA/SPIFFS and is not required for production. Do not make core dump a runtime dependency. No sdkconfig/TWDT change in this pass.

---

## 11. Test results

| Test | Result |
|---|---|
| `tools/esp32-mikrotik-stability-contract-check.mjs` | **12/12 PASS** (new) |
| `tools/routeros-stability-contract-check.mjs` | **12/12 PASS** (check 5+7 updated for ethernet gate + `needsHealthRecoveryProbe`) |
| `tools/session-sync-contract-check.mjs` | **15/15 PASS** |
| `tools/session-clock-sync-contract-check.mjs` | **21/21 PASS** (check 10 updated: Verify `ok=false` still `continue`s; suppression log added) |
| `tools/voucher-expiry-contract-check.mjs` | **12/12 PASS** |
| `tools/duplicate-activation-contract-check.mjs` | **12/12 PASS** |
| `scripts/test-portal-session-lifecycle.mjs` | **30/30 PASS** |

Check 10 documentation: the old assertion required the exact token `if (!outcome.ok) continue`. The new source still does not mutate Connected/secondsLeft on transport failure; it logs then `continue`. That is the intended architecture.

---

## 12. Build result

```
pio run -e freenove_esp32_s3_wroom
========================= [SUCCESS] Took 67.64 seconds =========================
RAM:   32.6%   Flash: 92.8%
```

**Not flashed. Not uploaded.**

Firmware compiled successfully, but hardware stability is not claimed until hardware validation passes.

---

## 13. Remaining risks

1. `/api/status` still calls `fileSizeBytes(LOGS_FILE)`, `getSpiffsUsedBytes()`, and `fillSdStatus()` (live SD/SPIFFS under `STORAGE_LOCK`, timeout 5 s). Not on the abort PC; still a stall vector if SD is slow **and** Admin keeps polling `/api/status`.
2. `/api/status` `mergedActiveUserStats` may read `users.json` via `SessionManager::appendActiveUsers`.
3. `fillStorageStatus` waits up to `STORAGE_LOCK_TIMEOUT_MS` (5 s) to copy a RAM snapshot — if loopTask holds the lock during a stalled SD read, `/api/health` could block `async_tcp`.
4. RouterOS `/login` can still take seconds on a loaded hAP lite; that is RouterOS-side and is not “fixed” by ESP32 callback shortening.
5. After Verify login timeout, health stays DEGRADED until a **critical** job succeeds. Connected customers keep local time; Verify (HEALTHY-only) will not run until recovery. This is intentional.
6. New firmware ELF hash differs from the crash ELF; post-flash backtraces must be decoded against the flashed build.

---

## 14. Hardware validation procedure

Do **not** claim success from compile/tests alone. On the bench, with the new firmware flashed by the operator:

1. Boot until `eth_ip` is a real address (not `0.0.0.0`). Confirm no Activate `Host is unreachable` caused by missing IP. Expect at most one `[activate] deferred reason=ethernet_not_ready` burst, then **one** Activate.
2. Insert coin → Done Paying → Activating → Connected only after RouterOS success. Timer synchronized. Add Time / pause / resume / terminate / voucher still work.
3. Leave Admin Dashboard open (`/api/health` 5 s + SSE) with 1 active customer, 2 phones, captive portal, HotSpot Active.
4. Confirm **no** Guru Meditation, **no** `task_wdt`, **no** reboot for a soak covering several `/api/status` cycles.
5. Serial: `[sales-cache] refresh` on loopTask, **not** three SD reads per `/api/status`. No `[router-health] … probe_suppressed` loop accompanied by HealthProbe logins while only Connected.
6. Idle, no session: `[router-worker] idle no-router-work` and 0 RouterOS API polls.

---

## 15. Explicit statement

**Firmware compiled successfully, but hardware stability is not claimed until hardware validation passes.**
