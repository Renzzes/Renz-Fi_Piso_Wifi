# Safe Stabilization + Legacy ESP32 Portal Removal

**Verdict:** SOURCE IMPLEMENTED — HARDWARE VALIDATION REQUIRED  
**Full `portal/` SPIFFS removal:** BLOCKED — LEGACY PORTAL STILL REQUIRED (required HTML/JS/CSS)

## 1. Forensic verification (Phase 0)

| Question | Result |
|----------|--------|
| Where does `portal/` come from? | Repo root `portal/` — source for MikroTik Hotspot build **and** ESP32 recovery/setup staging |
| Does `stage-esp32-data.mjs` copy it? | Yes — `PORTAL_REQUIRED` + `PORTAL_RECOMMENDED` → `ESP32_S3_Firmware/data/portal/` |
| Does uploadfs put it in SPIFFS? | Yes — PlatformIO `data/` → SPIFFS |
| Production captive UI? | MikroTik Hotspot `Files/hotspot/` at `http://10.20.0.1/login` |
| ESP32 Admin? | `http://10.10.10.2/admin` (Admin SPA from SPIFFS `index.html` + `assets/`) |
| MikroTik independent of ESP32 static portal? | Yes for production UI/media. MikroTik may call ESP32 `/api/portal/*` (API ≠ static files) |

### Why full `portal/` removal is BLOCKED

Still required on SPIFFS:

- Setup Finish provisioning (`/api/setup/provisioning/portal/*` reads SPIFFS portal objects)
- `BootDiagnostics` / `PortalSpiffsLayout::kRequiredPortalAssets`
- Management recovery routes in `PortalServer` (`/portal`, flat aliases)
- Small `Default-Banner.png` fallback via `AssetResolver`

### Safe SPIFFS recovery (implemented)

Stop staging large **MikroTik-only** audio into ESP32 `data/`:

- `bg_music.mp3` (~915 KB)
- `coin.mp3`
- `success.mp3`

Kept on SPIFFS: required HTML/JS/CSS + favicon + `Default-Banner.png`.  
Source `portal/` and `deployment/mikrotik-hotspot/` unchanged for MikroTik uploads.

## 2–6. Staging / SPIFFS (measured)

| Metric | Bytes |
|--------|------:|
| `data/` BEFORE | 2,080,746 |
| `data/` AFTER | 1,143,859 |
| Recovered | 936,887 (**45%**) |
| `data/portal/` BEFORE | 1,023,270 |
| `data/portal/` AFTER | 84,940 |
| Audio removed from SPIFFS staging | `bg_music.mp3` + `coin.mp3` + `success.mp3` (~938 KB) |

Staged `data/portal/` after build: login.html, renzfi-app.js, renzfi-style.css, md5.js, favicon.ico, Default-Banner.png only.

## 7–8. WAN root cause + fix

**Cause:** `/ip/route/print` read failure → firmware set `defaultRoute=unavailable` and `internet=offline`.

**Fix (`MikroTikDriver::observeAndRepairWan`):**

- Successful query + active default → `available`
- Successful query + no active default → `unavailable`
- Failed/timeout query → `unknown` (one bounded retry)
- Observation failure ≠ internet offline; ping API failure → `unknown`
- Frontend: `Unable to verify` when route/internet unknown (not “No default route”)

## 9. 1970 last-sync fix

- `isoTimestampNow()` returns empty if wall clock &lt; 2020-01-01 UTC
- `stampSynchronized()` stores `lastSynchronizedMillis` always; ISO only when wall clock valid
- Frontend `routerCacheLastSyncLabel` prefers relative age (`Just now` / `53s ago` / `2m ago`) when clock invalid

## 10. Memory telemetry

`/api/status` exposes `internalHeap`, `psram`, `dma` separately. Dashboard labels **Internal Heap** (not “RAM nearly full”).

## 11. RouterOS command dedupe

`collectCacheSnapshot` already skips the full `/interface/wireless/print` list when `RouterWireless::readInterface` succeeds (`wirelessCached`). No further dedupe — equivalence across Hotspot reconcile paths not proven safe.

## 12. Files changed

- `scripts/esp32-staging-manifest.mjs`, `scripts/stage-esp32-data.mjs`
- `ESP32_S3_Firmware/src/router/drivers/MikroTikDriver.cpp`
- `ESP32_S3_Firmware/src/RouterCacheManager.cpp`
- `ESP32_S3_Firmware/src/ApiServer.cpp`
- `src/lib/adminStatus.ts`, `src/lib/routerCacheStatus.ts`, `src/lib/systemConfigurationStatus.ts`
- `src/types/api.ts`, `src/pages/DashboardPage.tsx`

## Do-not-change confirmation

HttpPlaneGate SPA policy, Auth/RBAC, Hotspot topology, WAN DHCP/NAT, coin/promo/session, `router_worker`, W5500, MikroTik deployment upload bundle, `/api/portal/*` APIs — preserved.

## Hardware validation

Required before RELEASE PASS — checklist A–S in task brief.
