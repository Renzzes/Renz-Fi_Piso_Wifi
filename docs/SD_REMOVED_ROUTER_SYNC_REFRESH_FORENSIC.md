# Synchronize / Refresh Router during SD-removed (degraded) — Forensic

**Date:** 2026-08-22  
**Scope:** Admin **Synchronize Router** and **Refresh Router Information** while SD is pulled.

---

## Verdict (data sources)

| Action | Live data | Credentials | Result cache shown in UI | Durable persist |
|---|---|---|---|---|
| **Synchronize Router** (`POST /api/router/cache/sync`) | **Live MikroTik RouterOS API** (configuration collect on `router_worker`) | `/config/router.json` (SD → SPIFFS fallback → last RAM driver cache) | **RAM** `RouterCacheManager::_doc` | `router-cache.json` on SD, or SPIFFS when degraded |
| **Refresh Router Information** (`POST /api/router/cache/refresh`) | **Live MikroTik RouterOS API** (telemetry collect) | Same as above | **RAM** `_doc` | Same persist path |
| Dashboard “CPU / Memory / Uptime” under MikroTik | From last **cached** `routerOs` block (filled by sync/refresh) | — | RAM / last persist | Not a live poll each Dashboard paint |

Neither button reads “cached only” as the primary source of truth for the **operation**. They open a MikroTik session, then **update** the local cache. The Admin UI then reads that local cache (RAM first).

---

## Why Sync/Refresh broke after hot-unplug (your serial)

Proven sequence:

1. `Card Failed! cmd: 0x00` while lifecycle still reported `SD_READY`.
2. `readJsonFromSd` mis-classified failures as `path_absent` and **did not** call `tripSdMediaMissing`.
3. Firmware kept doing dead SD SPI under `STORAGE_LOCK` → Admin HTTP saw `Timed out waiting for storage transaction lock`.
4. Credentials (`router.json`) and cache persist could not complete → Sync/Refresh failed even though Ethernet and MikroTik were fine.

This is **not** “Sync reads from SD instead of MikroTik.” It is “Sync cannot obtain credentials / finish cache persist because storage stayed falsely healthy.”

---

## Fixes applied

1. **Hot-unplug detection** in `readJsonFromSd` — fail streak / unresponsive card → `tripSdMediaMissing` → true degraded / SPIFFS fallback.
2. **`router-cache.json` fallback-eligible** so Sync/Refresh can persist to SPIFFS when SD is gone.
3. **RAM keep on persist defer** — if MikroTik collect succeeded but durable write fails in degraded mode, Admin still gets the live snapshot from RAM.
4. **MikroTik credential RAM cache** — last successful `router.json` load reused when storage read fails mid-session.
5. **Credential reconcile** — if SD write fails, force degraded + retry SPIFFS write from setup-verified credentials.

---

## Dashboard ESP32 health (added)

`GET /api/system/health` now includes:

- `esp32.cpuFreqMHz`, `esp32.chipTempC` (internal die temp via `temperatureRead()`, not ambient), `chipTempAvailable`
- `memory.psram` / `psramSize`

Dashboard **System Health** panel shows ESP32 CPU MHz, chip temp, heap KB, PSRAM free.

Note: the Status panel **CPU** row remains **MikroTik** CPU load from router cache (unchanged).
