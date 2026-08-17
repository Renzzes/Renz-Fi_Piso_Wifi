# Renz-Fi Boot Diagnostics

**Scope:** Serial boot output, installer visibility, and SPIFFS validation.  
**Does not change:** boot order, subsystem initialization, HTTP contracts, or storage architecture.

**Related:**
- [DEVICE_PROFILE_CONTRACT.md](./ESP32_S3_Firmware/docs/DEVICE_PROFILE_CONTRACT.md)
- [PHASE_4C_SYSTEM_VALIDATION.md](./ESP32_S3_Firmware/docs/PHASE_4C_SYSTEM_VALIDATION.md)
- [PortalSpiffsLayout.h](./ESP32_S3_Firmware/src/web/PortalSpiffsLayout.h) — canonical portal SPIFFS paths

---

## Boot phases (unchanged order)

| Phase | Action |
|-------|--------|
| 0 | Recovery button check (`RecoveryManager`) |
| 1 | W5500 Ethernet (`EthernetManager::begin`) |
| 2 | SPIFFS mount → optional debug inventory → **portal asset validation** |
| 3 | SD card (`StorageManager::begin`) |
| 4 | Subsystems (auth, router, portal sessions, assets, provisioning, …) |
| 5 | Network services when link + IP ready (`WebServerManager`) |
| 6 | **Production boot summary** |

---

## Portal login.html detection — root cause

**Observed:** `[boot] /portal/login.html exists: no` while PortalServer registers `GET /` → `/portal/login.html`.

**Conclusion:** The runtime check and PortalServer use the **same canonical path** (`PortalSpiffsLayout::kLoginHtml` = `/portal/login.html`). This is **not a path bug**.

The file was **genuinely absent from the SPIFFS image** because portal sources were not staged before `uploadfs`.

**Fix for installers:** Run `npm run build:esp32` from the repo root (or `npm run deploy:esp32` for a full flash). This stages admin + portal into `ESP32_S3_Firmware/data/` and validates required files before upload. Do not copy into `data/` manually.

See [ESP32_STAGING.md](./ESP32_STAGING.md).

---

## Portal asset validation

Runs after SPIFFS mount (Phase 2). Uses `PortalSpiffsLayout::kRequiredPortalAssets`:

| SPIFFS path | Label |
|-------------|-------|
| `/portal/login.html` | login.html |
| `/portal/renzfi-app.js` | renzfi-app.js |
| `/portal/renzfi-style.css` | renzfi-style.css |
| `/portal/md5.js` | md5.js |

### Example outputs

**All present:**

```
[portal] Portal assets verified
  ✓ login.html
  ✓ renzfi-app.js
  ✓ renzfi-style.css
  ✓ md5.js
```

**Single missing:**

```
[portal] WARNING
Portal asset missing:
  login.html
Captive Portal will NOT function.
Admin Dashboard remains available.
```

**Multiple missing:**

```
[portal] WARNING
Missing portal assets:
  login.html
  renzfi-app.js
  md5.js
Captive Portal disabled.
Admin Dashboard remains available.
```

Boot **never stops** for missing portal files.

---

## Production boot summary

Printed at end of `FirmwareApp::begin()` using live manager state (no hardcoded values):

```
================================================
RENZ-FI APPLIANCE READY
------------------------------------------------
Device Name      : Reception
Device ID        : RF-00EF01
Serial Number    : DE:AD:BE:EF:00:01
Firmware         : 0.5.0-w5500
Hardware         : ESP32-S3-W5500-N8R8
Installation     : Factory
Router Driver    : MikroTik
------------------------------------------------
Ethernet Driver  : UP
Ethernet Link    : DOWN
IP Address       : 10.40.0.2
MAC Address      : DE:AD:BE:EF:00:01
------------------------------------------------
Storage          : SD Ready
Assets           : Ready
Portal           : Ready
Admin Dashboard  : Ready
Provisioning     : Ready
------------------------------------------------
Admin URL
http://10.40.0.2/admin
================================================
```

| Field | Source |
|-------|--------|
| Device Name | `DeviceIdentity::readFriendlyName` → `settings.json` |
| Device ID | `DeviceIdentity::formatDeviceId(MAC)` |
| Serial Number | Ethernet MAC |
| Firmware | `RenzFiConfig::FIRMWARE_VERSION` |
| Hardware | `RenzFiConfig::HARDWARE_REVISION` |
| Installation | `InstallationStateManager::current()` |
| Router Driver | `RouterPlatform::activeDriver()` |
| Storage | `StorageManager::healthy()` / fallback |
| Assets | `AssetManager::ready()` |
| Portal | Required SPIFFS portal files present |
| Admin Dashboard | `/index.html` + `/assets/*` bundles |
| Provisioning | Engine initialized (always Ready after Phase 4) |

---

## Debug flags (`RenzFiDebug.h`)

Set via PlatformIO `build_flags` (`-DRENFZFI_DEBUG_*=1`).

| Flag | Default (production) | Effect |
|------|----------------------|--------|
| `RENFZFI_DEBUG_BOOT` | 0 | Extra lines after production summary |
| `RENFZFI_DEBUG_SPIFFS` | 0 | Full SPIFFS file inventory at boot |
| `RENFZFI_DEBUG_STORAGE` | 0 | Reserved — storage domain logging |
| `RENFZFI_DEBUG_ROUTER` | 0 | Reserved — router domain logging |
| `RENFZFI_DEBUG_HTTP` | 0 | Reserved — HTTP domain logging |
| `RENFZFI_DEBUG_PORTAL` | 0 | Per-file portal path debug during validation |

### Build variants

| Environment | Command | Diagnostics |
|-------------|---------|-------------|
| Production | `pio run -e freenove_esp32_s3_wroom` | Summary + portal validation only |
| Installer | `pio run -e renzfi_installer` | + SPIFFS inventory + portal path debug |

---

## Installer interpretation guide

| Observation | Meaning | Action |
|-------------|---------|--------|
| `Portal assets verified` | Captive portal can serve | Proceed to hotspot redirect test |
| `Portal … Incomplete` in summary | One or more required portal files missing | Re-run `npm run build:esp32`, then `uploadfs` |
| `Admin Dashboard : Unavailable` | No React build in SPIFFS | Run `npm run build:esp32`, then `uploadfs` |
| `Storage : SPIFFS Fallback` | SD missing/degraded | Portal/admin may still work; fix SD for production |
| `Ethernet Link : DOWN` | No cable / VLAN issue | Expected on bench; link required for hotspot tests |
| Full SPIFFS listing | Only in `renzfi_installer` or `-DRENFZFI_DEBUG_SPIFFS=1` | Normal production builds omit this |

---

## Failure conditions

| Condition | Boot continues? | Captive portal? | Admin dashboard? |
|-----------|---------------|-----------------|------------------|
| SPIFFS mount fail | Yes | No | No |
| Portal files missing | Yes | No | Yes (if admin uploaded) |
| SD mount fail | Yes | Yes (if portal OK) | Yes (SPIFFS fallback) |
| ETH.begin fail | **No** (early return) | — | — |

---

## Implementation map

| Component | File |
|-----------|------|
| Debug flags | `src/RenzFiDebug.h` |
| Portal validation + summary | `src/BootDiagnostics.cpp` |
| SPIFFS inventory (debug) | `src/SpiffsHost.cpp` |
| Boot orchestration | `src/FirmwareApp.cpp` |
| Canonical portal paths | `src/web/PortalSpiffsLayout.h` |
