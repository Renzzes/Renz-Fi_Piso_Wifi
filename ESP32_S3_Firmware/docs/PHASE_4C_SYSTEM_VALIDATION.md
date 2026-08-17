# Phase 4C – System Validation

**Firmware:** `0.5.0-w5500`  
**Status:** In progress — validation gate before Phase 5  
**Prerequisite:** Phase 4A (Web Platform), Phase 4B (Portal Hosting) complete  
**Next phase (blocked until sign-off):** Phase 5 — Router Adapter Layer

**Purpose:** The architecture is complete. Phase 4C validates that the appliance behaves identically to the pre-migration baseline across portal, admin, assets, web platform, network, and reliability scenarios. No new features. No Router Adapter work until this checklist passes.

**Related docs:**
- [HTTP_ROUTE_CONTRACT.md](./HTTP_ROUTE_CONTRACT.md)
- [PHASE_4B_IMPLEMENTATION_REPORT.md](./PHASE_4B_IMPLEMENTATION_REPORT.md)
- [STORAGE_ARCHITECTURE.md](./STORAGE_ARCHITECTURE.md)
- [ASSET_LIFECYCLE.md](./ASSET_LIFECYCLE.md)
- [BOOT_DIAGNOSTICS.md](../../docs/BOOT_DIAGNOSTICS.md)

---

## Validation environment

| Item | Requirement |
|------|-------------|
| Hardware | ESP32-S3 + W5500 + SD card + coin slot (if coin tests) |
| Firmware | Latest build flashed (`pio run -t upload`) |
| SPIFFS | Portal + admin uploaded (`pio run -t uploadfs`) — see **Boot Validation** below |
| Network | VLAN40 — ESP32 `10.40.0.2`, router gateway `10.40.0.1` |
| Router | MikroTik hotspot with redirect to ESP32 portal URL |
| Baseline | Capture screenshots / HAR / serial logs before marking pass |

**Sign-off rule:** Every checkbox must be **Pass** or explicitly **N/A (documented)** before Phase 5 begins.

---

## Boot validation (pre-field checklist)

Run after `upload` + `uploadfs`. Capture serial log from cold boot.

| # | Check | Pass | Expected serial output |
|---|-------|:----:|------------------------|
| B.1 | Production summary | ⬜ | `RENZ-FI APPLIANCE READY` block with Device ID, firmware, IP, Admin URL |
| B.2 | Portal asset validation | ⬜ | `[portal] Portal assets verified` with ✓ lines for login.html, renzfi-app.js, renzfi-style.css, md5.js |
| B.3 | Admin dashboard staged | ⬜ | Summary shows `Admin Dashboard : Ready` |
| B.4 | No SPIFFS inventory (production) | ⬜ | Full file listing **absent** on `freenove_esp32_s3_wroom` build |
| B.5 | SPIFFS inventory (installer build) | ⬜ | Optional: `pio run -e renzfi_installer` prints `[boot] SPIFFS inventory (debug):` |

### Portal asset validation

| # | Check | Pass | Procedure |
|---|-------|:----:|-----------|
| P.1 | Required files on SPIFFS | ⬜ | `npm run build:esp32` stages portal + admin; uploadfs blocked if incomplete |
| P.2 | Missing-file warning | ⬜ | If login.html omitted, serial shows `[portal] WARNING` + `Captive Portal will NOT function` — boot continues |
| P.3 | Path consistency | ⬜ | Boot check uses same paths as PortalServer (`/portal/login.html` per PortalSpiffsLayout) |

### Boot summary verification

| Field | Verify against |
|-------|----------------|
| Device Name | `settings.json` → `device.name` |
| Device ID | `RF-` + MAC suffix (matches `/api/health`) |
| Installation | Persisted installation state file |
| Router Driver | Active driver in router config |
| Storage | SD mounted vs SPIFFS fallback |
| Portal | All four required portal assets on SPIFFS |

### SPIFFS debug logging

| Build | Flags | Inventory at boot |
|-------|-------|-------------------|
| Production `freenove_esp32_s3_wroom` | all `RENZFI_DEBUG_*=0` | Off |
| Installer `renzfi_installer` | `DEBUG_BOOT`, `DEBUG_SPIFFS`, `DEBUG_PORTAL`=1 | On |
| Developer `renzfi_developer` | all `RENZFI_DEBUG_*=1` | On |

See [BOOT_DIAGNOSTICS.md](../../docs/BOOT_DIAGNOSTICS.md).

### Installer boot checklist

1. From repo root: `npm run deploy:esp32` (or `npm run build:esp32` then `pio run -t upload` + `uploadfs`)
2. For verbose boot logs, use `PIO_ENV=renzfi_installer`
3. Cold boot → confirm **B.1–B.4** in serial log
4. Open `http://<ip>/admin` and `http://<ip>/` (portal)

See [ESP32_STAGING.md](../../docs/ESP32_STAGING.md).

---

## 1. Portal

| # | Check | Pass | Procedure / expected result |
|---|-------|:----:|----------------------------|
| 1.1 | Loads from ESP32 | ⬜ | Connect to hotspot; browser lands on `http://10.40.0.2/` (or configured IP). View source shows portal HTML from ESP32, not router file store. Serial: `[web] PortalServer` route hit. |
| 1.2 | HTML identical | ⬜ | Compare rendered DOM / view-source to MikroTik-hosted baseline (or repo `login.html` + template vars filled). No missing sections, modals, or footer. |
| 1.3 | CSS identical | ⬜ | `GET /renzfi-style.css` returns 200; layout, colors, timer card, action buttons match baseline screenshot pixel-for-pixel at same viewport. |
| 1.4 | JavaScript identical | ⬜ | `GET /renzfi-app.js` returns 200; no console errors on load; `window.RenzFiPortalReady === true`. |
| 1.5 | CHAP authentication | ⬜ | Hotspot profile uses HTTP-CHAP; router passes `chap-id` / `chap-challenge` in redirect URL; voucher submit reaches router login; `md5.js` loads (200). |
| 1.6 | Voucher login | ⬜ | Enter valid voucher → internet access granted; invalid voucher → router error (same as before). |
| 1.7 | Coin login | ⬜ | Insert coin → modal → session starts; timer counts down; `POST /api/portal/start-coin-session` succeeds in network tab. |
| 1.8 | Logout | ⬜ | Session terminate / router logout clears access; portal returns to disconnected state. |
| 1.9 | Expiration redirect | ⬜ | Let session expire → captive redirect reappears; no stale “connected” UI. |
| 1.10 | Error messages | ⬜ | API failures show same user-visible behavior as baseline (rates modal, session errors); no raw JSON exposed to guest. |

---

## 2. Assets

| # | Check | Pass | Procedure / expected result |
|---|-------|:----:|----------------------------|
| 2.1 | Banner upload | ⬜ | Admin: upload banner → `POST /api/settings/portal/banner` 200; branding JSON shows `hasCustomBanner: true`. |
| 2.2 | Music upload | ⬜ | Admin: upload MP3 → `POST /api/settings/portal/music` 200; branding JSON shows `hasCustomMusic: true`. |
| 2.3 | Banner replacement | ⬜ | Upload second banner → portal shows new image; `?v=` revision increments. |
| 2.4 | AssetResolver | ⬜ | Serial log tier on `GET /api/portal/assets/banner`: `contract_sd` when file on SD contract path. |
| 2.5 | Legacy `/www` | ⬜ | With file at `/www/portal-banner.webp` only (no contract file) → banner still serves; tier `legacy_sd`. |
| 2.6 | SPIFFS fallback | ⬜ | Custom banner in SPIFFS custom tier serves when SD tiers absent. |
| 2.7 | Bundled fallback | ⬜ | Remove all custom tiers → `Default-Banner.png` / API default music path; tier `bundled`. |

---

## 3. Admin

| # | Check | Pass | Procedure / expected result |
|---|-------|:----:|----------------------------|
| 3.1 | Dashboard loads | ⬜ | `GET /admin` → React SPA; no blank page; `/assets/*` chunks 200. |
| 3.2 | Login | ⬜ | `POST /api/auth/login` → session cookie; dashboard data loads. |
| 3.3 | Upload banner | ⬜ | Same as 2.1 from admin UI. |
| 3.4 | Upload music | ⬜ | Same as 2.2 from admin UI. |
| 3.5 | Save settings | ⬜ | Change setting → `PUT /api/settings` 200; persists after refresh. |
| 3.6 | Reboot | ⬜ | `POST /api/system/reboot` → device reboots; services restore; settings retained. |
| 3.7 | Backup | ⬜ | `GET /api/settings/backup` downloads valid archive/JSON. |
| 3.8 | Restore | ⬜ | `POST /api/settings/restore` with backup → config restored; portal/admin still work. |

---

## 4. Web platform

| # | Check | Pass | Procedure / expected result |
|---|-------|:----:|----------------------------|
| 4.1 | RouteRegistry | ⬜ | Boot serial lists providers in order: StaticFileServer → AssetServer → EventBus → ApiServer → PortalServer → AdminServer → DownloadServer. |
| 4.2 | MIME | ⬜ | `Content-Type` on portal CSS/JS/banner matches `MimeResolver` (no `application/octet-stream` on known extensions). |
| 4.3 | Cache | ⬜ | Portal HTML: `Cache-Control: no-store`. Portal JS/CSS: short cache. Admin `/assets/*`: immutable. API JSON: no-store. |
| 4.4 | Error pages | ⬜ | `GET /api/nonexistent` → JSON 404 envelope. Missing portal static → plain 404. Admin `/admin/*` deep link → SPA shell. |
| 4.5 | Downloads | ⬜ | `/downloads/*` reserved — returns 404 until routes wired; `/api/logs/export` and `/api/sales/export` still work. |
| 4.6 | Static files | ⬜ | Admin bundles `/assets/*` 200; `/static/*` foundation responds (404 if empty dir OK). |

---

## 5. Network

| # | Check | Pass | Procedure / expected result |
|---|-------|:----:|----------------------------|
| 5.1 | W5500 | ⬜ | Boot: link UP, IP `10.40.0.2`; `[boot] ETH` OK; admin reachable wired. |
| 5.2 | DHCP | ⬜ | Hotspot clients receive IP from MikroTik; can reach ESP32 HTTP. |
| 5.3 | Router redirect | ⬜ | Unauthenticated HTTP → redirect to ESP32 portal URL (not router-hosted HTML). |
| 5.4 | Session creation | ⬜ | Coin/voucher session → RouterOS hotspot user created (serial: `[activate]` / portal provision log). |
| 5.5 | Session removal | ⬜ | Terminate / expire → RouterOS user/session removed. |
| 5.6 | Multiple clients | ⬜ | ≥2 clients: independent sessions; no cross-MAC timer bleed. |
| 5.7 | Reboot recovery | ⬜ | Reboot ESP32 → portal, admin, API, ETH link recover without manual intervention. |

---

## 6. Reliability

| # | Check | Pass | Procedure / expected result |
|---|-------|:----:|----------------------------|
| 6.1 | SD removed | ⬜ | Boot or hot-remove → degraded mode; `/api/health` reports storage; portal still loads from SPIFFS. |
| 6.2 | SD reinserted | ⬜ | `POST /api/storage/retry-sd` or reboot → SD remounts; uploads work. |
| 6.3 | SPIFFS only | ⬜ | No SD: portal + admin shell load; API returns appropriate degraded errors for SD-only ops. |
| 6.4 | Asset missing | ⬜ | Delete banner tiers → bundled/default or 404 on asset GET; portal does not crash. |
| 6.5 | Factory reset | ⬜ | `POST /api/system/factory-reset` → clean state; portal defaults; admin password flow OK. |
| 6.6 | OTA | ⬜ | `POST /api/system/firmware` upload → device updates; portal/admin survive; version string updated. |
| 6.7 | Power interruption | ⬜ | Power-cycle during idle and during upload → filesystem consistent or recovery; no boot loop. |
| 6.8 | Memory leaks | ⬜ | Run portal + admin + SSE 30+ min; `ESP.getFreeHeap()` / min heap stable (no continuous downward drift). |

---

## Quick smoke test (minimum bar)

Run before full checklist in the field:

```
1. pio run -t upload && pio run -t uploadfs
2. GET http://10.40.0.2/api/health          → ok: true
3. GET http://10.40.0.2/                    → portal HTML
4. GET http://10.40.0.2/admin               → admin SPA
5. GET http://10.40.0.2/api/portal/branding → JSON success
6. Voucher OR coin session on one test phone
7. Admin login + banner upload
```

---

## Validation record

| Field | Value |
|-------|-------|
| Tester | |
| Date | |
| Firmware version | |
| SPIFFS upload date | |
| Router model / ROS version | |
| Hotspot profile name | |
| Portal URL configured on router | |
| Total checks passed | / 38 |
| Blockers | |

---

## Phase 4C exit criteria

Phase 4C is **complete** when:

1. All **Portal**, **Assets**, **Admin**, and **Web platform** checks pass (sections 1–4).
2. **Network** checks 5.1–5.7 pass with router redirect configured.
3. **Reliability** checks 6.1–6.7 pass; 6.8 documented (pass or known baseline).
4. No regressions vs. pre–Phase 4B behavior reported.
5. Validation record signed off.

Only then may **Phase 5 — Router Adapter Layer** begin.

---

## Out of scope for Phase 4C

- Router Adapter / automated MikroTik provisioning
- Plug-and-play wizard
- Portal UI redesign
- New REST endpoints
- Multi-router support

These belong to Phase 5+.

---

## Checklist summary (copy for tracking)

### Portal
- [ ] Loads from ESP32
- [ ] HTML identical
- [ ] CSS identical
- [ ] JavaScript identical
- [ ] CHAP authentication
- [ ] Voucher login
- [ ] Coin login
- [ ] Logout
- [ ] Expiration redirect
- [ ] Error messages

### Assets
- [ ] Banner upload
- [ ] Music upload
- [ ] Banner replacement
- [ ] AssetResolver
- [ ] Legacy `/www`
- [ ] SPIFFS fallback
- [ ] Bundled fallback

### Admin
- [ ] Dashboard loads
- [ ] Login
- [ ] Upload banner
- [ ] Upload music
- [ ] Save settings
- [ ] Reboot
- [ ] Backup
- [ ] Restore

### Web platform
- [ ] RouteRegistry
- [ ] MIME
- [ ] Cache
- [ ] Error pages
- [ ] Downloads
- [ ] Static files

### Network
- [ ] W5500
- [ ] DHCP
- [ ] Router redirect
- [ ] Session creation
- [ ] Session removal
- [ ] Multiple clients
- [ ] Reboot recovery

### Reliability
- [ ] SD removed
- [ ] SD reinserted
- [ ] SPIFFS only
- [ ] Asset missing
- [ ] Factory reset
- [ ] OTA
- [ ] Power interruption
- [ ] Memory leaks
