# RENZ-FI POST-FINISH FORENSIC REPORT

Investigation of three post-Finish anomalies after a successful real-hardware
Finish Setup run (production-network SUCCESS, install=provisioned,
ProductionReady, no Guru Meditation).

**Baseline preserved:** production-network wireless fix (`monitor-once`,
tri-state running, session reuse) was not reopened and is not implicated.

---

## 1. EXECUTIVE SUMMARY

| Issue | Verdict |
|-------|---------|
| Issue 1 — Filesystem “NO TIMEOUT” | **CONFIRMED** (diagnostic gap + real hang risk) |
| Issue 2 — Management AP / netstack 12308 | **BENIGN** (ESP-IDF teardown artifact + mislabeled ETH events) |
| Issue 3 — `roscpu=255` | **BENIGN** (intentional UNKNOWN sentinel after sample age-out) |

---

## 2. FILESYSTEM FORENSICS

### Exact call chains

**`/config/router-cache.json` (write during Finish)**

```
RouterProvisioningWorker::runOp(FinishSetupProvisioning)
  -> RouterProvisioningEngine::runFinishPipeline()
     -> RouterPlatform::persistFinishRouterCache()
        -> RouterCacheManager::applyProductionNetworkVerification() / markProvisioned()
           -> RouterCacheManager::save()
              -> StorageManager::writeJson()
                 -> writeJsonToSdSerialized()   // FinishTrace storageWriteOp timeoutMs=0
                    -> SD.open(path+".tmp") / print / flush / close
                    -> SD.remove(path) / SD.rename(tmp, path)
```

**`/config/router-provisioning.json` (read+write)**

```
runFinishPipeline finalize
  -> StorageManager::readJson(RouterProvisioningFile)   // optional FinishTrace at call sites
  -> StorageManager::writeJson(..., force=true)
     -> writeJsonToSdSerialized()   // timeoutMs=0
```

Also: `persistWirelessSelection()` wraps read/write with `storageReadOp` /
`storageWriteOp` (both `timeoutMs=0`).

**`/config/installation.json`**

```
runFinishPipeline
  -> commitFinishInstallationState()
     -> InstallationStateManager::setState(Provisioned)
        -> touchSession() -> persist()
        -> persist()
           -> StorageManager::writeJson(InstallationFile)
              -> writeJsonToSdSerialized()   // timeoutMs=0
```

`setState` / `persist` also push FinishTrace scopes with `timeoutMs=0`.

**`/logs/logs.json`**

```
InstallationStateManager::setState() success path
  -> Logger::info("Installation state -> provisioned")
     -> StorageManager::appendJsonArrayItem(LOGS_FILE)
        -> readJson + writeJson
           -> writeJsonToSdSerialized("/logs/logs.json")  // timeoutMs=0
```

### Where `timeoutMs` becomes 0

`FinishTrace::storageWriteOp` / `storageReadOp` hardcode `timeoutMs=0`
(`FinishTrace.cpp`). That value is **diagnostic metadata only**.

`BlockingOpScope` / heartbeat log WAITING; they **never cancel**, never abort
the SD call, and never feed a watchdog from `timeoutMs`.

Contrast: RouterOS ops use `SETUP_ROUTER_IO_TIMEOUT_MS` etc. as real I/O
budgets inside RouterOS client paths — FS helpers do not.

### Timeout implementation answers (Task 1B)

| Question | Answer |
|----------|--------|
| 1. Is `timeoutMs` only diagnostic? | **Yes** for FS ops |
| 2. Does any watchdog enforce it? | **No** |
| 3. Can writeJson/readJson be cancelled? | **No** |
| 4. Arduino SD/File bounded? | **No** — sync; can stall on bad SPI/card |
| 5. Worker boundary for safe timeout? | Finish runs on **router worker**; job deadline exists but **does not interrupt** mid-SD |
| 6. Would assigning 8000ms alone protect? | **Zero actual protection** |

### SPI relationship (Task 1C)

| Device | Host | Pins |
|--------|------|------|
| SD | **FSPI / SPI2** (`SPIClass g_sdSpi(FSPI)`) | CS=18 SCK=7 MISO=5 MOSI=6 |
| W5500 | **SPI3_HOST** | CS=10 SCK=12 MISO=13 MOSI=11 |

**Shared bus = NO.** No SPI bus mutex between SD and W5500 is required or
present. Historical Guru Meditation from SD code touching W5500 CS is documented
in `SdSpi.cpp` and is a **pin ownership** bug, not shared-bus contention.

### Blocking behavior

| Property | Proven answer |
|----------|---------------|
| A. Sync on Finish worker | **Yes** |
| B. Async FS jobs | **No** |
| C. Mutex on SD | **No** SD-specific mutex |
| D/E. Same SPI as W5500 | **No** (separate hosts) |
| F. Block networking task | Not directly; blocks **router worker**. ETH SPI is separate host |
| G. Block WDT feed | No app WDT feed around SD; prolonged stall can starve worker progress |

### Failure / atomicity (Task 1D)

| Operation | Failure return | Finish abort? | Atomic write? | Previous preserved? | Boot recovery |
|-----------|----------------|---------------|---------------|---------------------|---------------|
| router-cache | `false` | **Yes** (`ROUTER_CACHE_REFRESH_FAILED`) | tmp→flush→rename | **Risk**: `remove` then failed `rename` leaves gap | Cache may be stale; Finish fails before provisioned if fail before commit |
| router-provisioning finalize | ignored | **No** | same pattern | same risk | Hotspot flags may be stale; install may still provisioned |
| installation.json | `false` from `setState` | **Yes** | same pattern | same risk | `load()` reloads file; in-RAM may briefly be Provisioned if persist fails after assign |
| logs.json | ignored by Logger | **No** | same pattern | same risk | Logs loss only |

Writes use **temporary file → flush → close → remove dest → rename**. Not a
blind overwrite of the live file contents, but **not crash-safe** across the
remove/rename window on FAT/SD.

### Root cause (Issue 1)

1. FinishTrace warns because FS helpers intentionally set `timeoutMs=0`.
2. That warning correctly signals a **real** risk: synchronous SD I/O has no
   interruptible timeout.
3. Setting a non-zero `timeoutMs` without an enforcement mechanism would be a
   **false sense of safety**.

---

## 3. MANAGEMENT AP FORENSICS

### Shutdown call chain (matches hardware log)

```
loop → ManagementApLifecycle::processSetupCompletion()
  // requires isReady() (Provisioned|Ready) && mgmtAp->isRunning()
  -> log "setup complete (provisioned/ready) — stopping..."
  -> SetupDnsPolicy::restoreProductionDns()
  -> ManagementApManager::stop()
       -> captive DNS stop
       -> WiFi.softAPdisconnect(true)   // AP.clear() then AP.end()
       -> log "Management AP stopped"
  -> salesTimeBegin()
```

Secondary owner (after UI Finish acknowledge):

```
POST /api/setup/complete
  -> completeSetupAfterFinishSuccess()
     -> completeSetupProvisioning()
        -> stop() if still running (idempotent if _running==false)
```

### SoftAP Stop → Start → Stop owner

**Not Renz-Fi `ManagementApManager::start()`.**

Evidence:

1. Hardware sequence places SoftAP Start/Stop **between** DNS restore and
   `"[mgmt-ap] Management AP stopped"` — i.e. **inside** `softAPdisconnect(true)`.
2. `start()` always logs `"[mgmt-ap] Management AP started"` — absent in that
   window on hardware.
3. Arduino `WiFi.softAPdisconnect(true)` → `AP.clear()` + `AP.end()`; community
   logs show the **identical** AP_STOP → `netstack cb reg failed 12308` →
   AP_START → AP_STOP pattern during this teardown.

### ESP-IDF error 12308

| Field | Value |
|-------|-------|
| decimal | 12308 |
| hex | **0x3014** |
| symbolic | **`ESP_ERR_WIFI_STOP_STATE`** |
| meaning | Returned when Wi-Fi is stopping; overlapping Wi-Fi API / netstack cb registration refused |

Source: ESP-IDF error-codes reference; Espressif IDFGH-14107 confirms
`ESP_ERR_WIFI_STOP_STATE /* 12308 0x3014 */`.

Generated from `wifi_init_default` when `esp_wifi_internal_reg_netstack_buf_cb`
fails during teardown/re-init races — **not** a second Renz-Fi `WiFi.softAP()`.

### Event callback mapping

- `WiFi.onEvent` → correct SoftAP labels.
- `Network.onEvent(onEthArduinoEvent)` receives **all** network events;
  `ethEventName` default → **`ETH_OTHER`** for Wi-Fi IDs.

So `EVENT ETH_OTHER` immediately after SoftAP events is **mislabeled Wi-Fi /
other non-ETH traffic**, not real Ethernet start/stop storms.

### Root cause (Issue 2)

One intentional stop (`processSetupCompletion` → `softAPdisconnect(true)`).
Transient SoftAP Start + error 12308 are ESP-IDF/Arduino AP.end() side effects.
Final `mgmt_ap=off` / ProductionReady is correct. **Not fatal.**

---

## 4. ROUTER CPU FORENSICS

### Where 255 originates

```
RouterApiTransportGate.cpp:
  uint8_t g_lastCpuLoadPercent = 255;  // 255 = unknown

lastObservedCpuLoadPercent():
  if percent == 255 → return 255
  if age > ROUTER_CPU_SAMPLE_MAX_AGE_MS (60000) → return 255

MemoryDiagnostics::periodicLog():
  roscpu=%u from lastObservedCpuLoadPercent()
```

Samples recorded only as a side effect of RouterOS `/system/resource` traffic
(`RouterWirelessAdapter` → `recordObservedCpuLoad`). Values `>100` rejected.
`255` is never stored as a parsed RouterOS reading.

### Why 47 → 255 after Finish

During Finish, RouterOS work observes cpu-load (~47%). After Finish,
`session.close()` and production path stop frequent RouterOS resource traffic.
Within ≤60s the sample ages out → getter returns **255 UNKNOWN**.

### UI impact

Admin `SystemConfigurationPage` shows
`routerOsSnapshot?.cpuLoad ? \`${cpuLoad}%\` : "—"`.
Cache/API `cpuLoad` is a **string from RouterOS/cache**, not the mem-log
uint8 sentinel. Mem log `roscpu=255` is **operator telemetry**, not proof the
dashboard shows `255%`. If cache still holds last string `"47"`, UI may show
47%; if empty, `—`. **Not a clamp candidate.**

### Root cause (Issue 3)

Intentional `uint8_t` UNKNOWN sentinel + 60s max age after RouterOS traffic
stops. Not 255% CPU.

---

## 5. ROOT CAUSE TABLE

| Issue | Root Cause | Evidence | Severity | Fix Required? |
|-------|------------|----------|----------|---------------|
| 1 | FS FinishTrace `timeoutMs=0` diagnostic only; sync SD unbounded | FinishTrace.cpp; StorageManager; Arduino SD | **P1 reliability** | Clarify warning now; real interruptible enforcement = follow-up design |
| 2 | `softAPdisconnect(true)`/AP.end teardown race; ETH_OTHER mislabel | ManagementApManager; ESP-IDF 0x3014; Network.onEvent | Low (benign) | Label filter yes; teardown change only with HW retest |
| 3 | 255 = UNKNOWN after sample age-out | RouterApiTransportGate; Config.h 60s | Cosmetic | Mem-log clarity only |

---

## 6. PERFORMANCE IMPACT

**Current (successful HW run):** Finish completes; DMA survived; no SPI shared-bus
storm; RouterOS session reused; FS writes sync on worker; AP ends with teardown
noise; mem log shows stale CPU as 255.

**After minimal fixes:** Same CPU/heap/DMA/SPI/RouterOS profile. Clearer logs
only. No extra polling, no global reply-limit change, no production-network
touch.

---

## 7. MINIMAL FIX PLAN

### Issue 1

- **Change:** FinishTrace warning states **DIAGNOSTIC ONLY / NOT ENFORCED**.
- **Do not** set fake `timeoutMs=8000`.
- **Follow-up (not this change):** worker-level hung-op policy or FS task with
  enforceable deadline (architecture decision).

### Issue 2

- **Change:** `onEthEvent` logs only real `ARDUINO_EVENT_ETH_*` IDs.
- **Do not** suppress ESP-IDF 12308 by hiding logs.
- **Do not** change `softAPdisconnect(true)` until dedicated HW validation
  (risk to ProductionReady handoff).

### Issue 3

- **Change:** `[mem]` prints `roscpu=unknown` when sentinel 255.
- **Do not** clamp to 100 or invent UI percent.

---

## 8. DO-NOT-CHANGE CONFIRMATION

MikroTik production-network wireless activation, `monitor-once`, session reuse,
portal-verify modes, coin/voucher/pause, captive portal branding/rates, SSE,
CORS, setup unlock, idempotent provisioning order — **untouched**.
