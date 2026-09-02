# Admin Login / Dashboard `async_tcp` TWDT Forensic (ELF `260f442c4`)

**Date:** 2026-08-22  
**Class:** CLASS 3 (loopTask starvation of `async_tcp` on shared CPU1)  
**Not DMA:** DMA free/`largest` stayed healthy (`19444` / `31732`) through the abort.

---

## Incident summary

Failed Admin login attempts (and later Dashboard use) triggered Guru Meditation:

```text
E task_wdt: Task watchdog got triggered
E task_wdt:  - async_tcp (CPU 1)
E task_wdt: CPU 1: loopTask
Aborting.
```

Serial showed `[WARN] auth: Failed login`, dense `/api/health` + portal traffic, and continuous `[dma] sd-readJson` interleaved with the abort. Reboot recovered; the cycle repeated on the next login/dashboard storm.

---

## What is *not* the root cause

| Suspect | Why ruled out |
|---|---|
| ETH DMA collapse / `ETH_DMA_LOW` | Logs show `dma largest=19444` / `31732`, `minimum=256` — not the SoftAP 1536 gate class |
| `AuthManager::login` failure path | Failed login uses `_logger->warnLocal` only (no durable SD history). SHA-256 is cheap. Sessions are RAM-only |
| Login as the blocking instruction | No auth frame required; login is a **temporal trigger** (health/portal/dashboard fan-out) |
| RouterOS on `async_tcp` | `jobs=0 queue=0`; no worker frames in the abort signature |

---

## Proven mechanism

1. `async_tcp` and Arduino `loopTask` both run on **CPU 1** (`CONFIG_ASYNC_TCP_RUNNING_CORE=1`).
2. When TWDT fires with `CPU 1: loopTask`, `async_tcp` is **blocked or starved** and cannot reset its TWDT (~5 s).
3. `FirmwareApp::refreshHealthSnapshots()` runs every **2 s** on `loopTask` and previously did **SD `readJson` under `STORAGE_LOCK` every cycle**:
   - `RouterPlatform::refreshHealthCache()` → `load()` → `readJson(ROUTER_FILE)` **every 2 s**
   - `DeviceIdentity::refreshRuntimeProfile()` → `readFriendlyName()` → `readJson(SETTINGS_FILE)` **every 2 s**
   - `StorageManager::refreshRuntimeSnapshot()` → `SD.totalBytes()` / `SD.usedBytes()` (and SPIFFS size) under the same lock **every 2 s**
4. Concurrent portal work (`portal-save` / `sd-writeJson` on `loopTask`) and Admin `/api/health` storms amplify lock hold time and CPU1 occupancy.
5. Any HTTP path that still takes `STORAGE_LOCK` with the default **5000 ms** wait equals the TWDT window → parking `async_tcp` on the mutex is fatal.

Heavy SPIFFS walks were already throttled (`STORAGE_SNAPSHOT_HEAVY_INTERVAL_MS`, `ADMIN_LOGIN_TWDT_ROOT_CAUSE.md`). This incident proves the **remaining every-2s SD readJson + capacity probe** path was still enough to tip TWDT under login/dashboard load.

---

## Exact fix (production-safe)

| Change | Why |
|---|---|
| Cache `DeviceIdentity` friendly name for 30 s; clear on `invalidateRuntimeProfile()` | Stops settings.json SD read every 2 s |
| Throttle `RouterPlatform::refreshHealthCache` SD load to 30 s; refresh RAM immediately on `save()` | Stops router.json SD read every 2 s |
| Throttle SD/SPIFFS capacity probes to 10 s inside `refreshRuntimeSnapshot` | Keeps lock short between capacity updates |
| `lockStorage()`: if task is `async_tcp`, wait at most `STORAGE_LOCK_ASYNC_TCP_WAIT_MS` (80 ms) | Fail fast instead of parking HTTP for 5 s (= TWDT) |
| `vTaskDelay(1)` between phases of `refreshHealthSnapshots` | Gives `async_tcp` a scheduling window on CPU1 |

**Intentionally not done:** disable/feed/increase TWDT; move Wi-Fi DMA to PSRAM; lower SoftAP DMA gates; put RouterOS on `async_tcp`; weaken SD durability.

---

## Files changed

- `ESP32_S3_Firmware/src/Config.h`
- `ESP32_S3_Firmware/src/StorageManager.h` / `.cpp`
- `ESP32_S3_Firmware/src/DeviceIdentity.cpp`
- `ESP32_S3_Firmware/src/router/RouterPlatform.h` / `.cpp`
- `ESP32_S3_Firmware/src/FirmwareApp.cpp`
- `docs/ADMIN_LOGIN_FAILED_ASYNCTCP_TWDT_FORENSIC.md` (this file)

---

## Validation checklist

1. Flash matching ELF; open Admin login on Ethernet.
2. Enter wrong password repeatedly while portal sessions/heartbeats run.
3. Open Dashboard after a successful login; idle with SSE/health traffic.
4. Expect: no `task_wdt` / `async_tcp` abort; `[dma] sd-readJson` rate drops (settings/router no longer every 2 s); DMA `largest` remains healthy.
5. After changing device name or router host, health still reflects new values within ≤30 s (or immediately after save + invalidate).
