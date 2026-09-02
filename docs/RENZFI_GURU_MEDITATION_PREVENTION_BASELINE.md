# Renz-Fi Guru Meditation / TWDT Prevention Baseline

**Date:** 2026-08-11  
**Status:** FROZEN REGRESSION BASELINE  
**Mode:** Documentation only — do not weaken these protections while investigating unrelated functional issues.

---

## Binding statement

These protections are the production regression baseline and must not be weakened, bypassed, removed, or redesigned while investigating unrelated functional issues.

---

## Baseline failure classes (proven)

### CLASS 1 — Admin HTTP / `async_tcp` synchronous blocking

An AsyncTCP HTTP callback can synchronously wait on RouterOS-related work (including `_doneSem`) while `router_worker` performs RouterOS operations / connect cooldown.

**Production-safe correction:** preserve worker architecture; Admin HTTP must enqueue-and-return quickly (HTTP 202 / job pattern where applicable).

### CLASS 2 — Wi-Fi selection / HTTP callback filesystem blocking

`POST /api/setup/router/wifi/selection` previously ran durable `persist()` → `writeJson` (SD + SPIFFS checkpoint) on `async_tcp`, tipping at SPIFFS flash read (`esp_flash_read`).

**Production-safe correction:** validate + RAM update on `async_tcp`; return `202` with `QUEUED` / `PERSISTING` / `PERSISTED` / `FAILED`; durable commit on `loopTask`.

### CLASS 3 — `loopTask` / SPIFFS telemetry starvation of `async_tcp`

Exact-ELF backtrace (`8f93b74f9`) proved:

```text
FirmwareApp::loop
 → FirmwareApp::refreshHealthSnapshots
 → StorageManager::refreshRuntimeSnapshot
 → StorageManager::fallbackTotalBytes
 → StorageManager::spiffsFileSize
 → SPIFFS.exists
 → esp_flash_read
```

Failed TWDT task: `async_tcp` (CPU1) while CPU1 was executing `loopTask`.

**Production-safe correction:**

- Heavy SPIFFS snapshot work throttled (~30 s) via `STORAGE_SNAPSHOT_HEAVY_INTERVAL_MS`
- Lightweight snapshot fields still update frequently; **capacity probes** throttled (~10 s) via `STORAGE_SNAPSHOT_CAPACITY_INTERVAL_MS`
- Router health cache + device friendly-name SD reads throttled (~30 s) — see `docs/ADMIN_LOGIN_FAILED_ASYNCTCP_TWDT_FORENSIC.md` (ELF `260f442c4`)
- `STORAGE_LOCK` wait for task `async_tcp` capped at `STORAGE_LOCK_ASYNC_TCP_WAIT_MS` (never the full 5 s TWDT window)
- No durability weakening
- No TWDT timeout / disable / feed
- No architectural redesign of StorageManager durability

Regression audit notes retained:

- `/api/storage/status` reads cached snapshot fields; does not call `refreshRuntimeSnapshot()`
- Periodic heavy path is guarded inside `refreshRuntimeSnapshot()`
- Latent bypass risk: `fillStorageHealth()` still calls `fallbackTotalBytes()` directly (no current high-frequency route found)
- Write-path `appendHistory` / `checkQuota` still use `fallbackTotalBytes()` by design

---

## Investigation rules (mandatory)

- Do not move RouterOS work back into AsyncTCP callbacks.
- Do not introduce synchronous unpredictable RouterOS operations into HTTP handlers.
- Do not remove worker queue/backpressure protections.
- Do not remove RouterOS cooldown/session protections.
- Do not disable the watchdog.
- Do not feed the watchdog as a workaround.
- Do not increase watchdog timeout as a workaround.
- Do not blindly add delays.
- Do not disable SSE merely to hide instability.
- Do not reduce logging merely to hide timing.
- Do not blame the ESP32 without tracing the exact execution path.
- Do not blame MikroTik without proving the RouterOS state/command behavior.
- Do not blame Android without proving the server/network state independently.

---

## CLASS 4 — Finish false-fail after successful production activation (2026-08-11)

**Symptom:** Finish completed production-network / INSTALLATION READY markers, then failed at `router-cache` → worker finish gate → HTTP 400, leaving installation uncommitted.

**Root cause:** `runFinishPipeline` treated local `persistFinishRouterCache()` / SD cache write failure as a hard fatal stage after RouterOS production activation had already succeeded.

**Production-safe correction:** Keep `persistFinishRouterCache` (no extra RouterOS session). Treat cache persist as **non-blocking** after production success; still require `commitFinishInstallationState` + `verifyProvisionedPersisted` for terminal success.

**Related persistence hardening (same incident window):** Dirty SPIFFS fallback must not permanently override healthy SD after RESET; successful SD writes clear dirty manifest entries; verified router-connection may be reconstructed only when protected credentials unprotect successfully.

**Must not regress:** Owner/wifi-selection async_tcp deferral, SPIFFS telemetry throttle, RouterOS worker stack limits, Done Paying activation path.

---

## Required HTTP / loop patterns

```text
HTTP async_tcp:
  validate → update RAM / enqueue → return quickly

loopTask / dedicated deferred path:
  durable SD/SPIFFS work (bounded/throttled)

router_worker:
  RouterOS only — never block async_tcp waiting for completion
```

---

## References

- `TWDT_OWNER_ENDPOINT_ROOT_CAUSE.md`
- `OWNER_SETUP_TWDT_IMPLEMENTATION_REPORT.md`
- `TWDT_WIFI_SELECTION_ROOT_CAUSE.md`
- `WIFI_SELECTION_TWDT_IMPLEMENTATION_REPORT.md`
- `SETUP_ASYNCTCP_TWDT_PREVENTION.md`
- `ADMIN_LOGIN_TWDT_ROOT_CAUSE.md`
- `ESP32_S3_Firmware/docs/ADMIN_DASHBOARD_ASYNCTCP_WATCHDOG_FORENSIC.md`
