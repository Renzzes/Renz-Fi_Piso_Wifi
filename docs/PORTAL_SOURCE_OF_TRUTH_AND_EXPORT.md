# Customer Captive Portal — Source of Truth & Export

## Rule

| Role | Path | Action |
|------|------|--------|
| **SOURCE** | `portal/` | **Only** place to edit customer HTML/CSS/JS/media |
| **GENERATED** | `deployment/mikrotik-hotspot/` | Do **not** hand-edit |
| **GENERATED** | `Final_Build_Portal/` | Do **not** hand-edit |
| **GENERATED** | `ESP32_S3_Firmware/data/portal/` | Do **not** hand-edit (SPIFFS stage) |
| **LEGACY** | `Captive Portal/` | Do **not** edit or delete (separate cleanup) |
| **EXPORT** | `C:\Captive_Portal_BAT\` | Upload **only** this folder to MikroTik |

## Owner workflow

1. Change files under `portal/` only.
2. Run:

```bat
scripts\export-captive-portal.bat
```

3. Open `C:\Captive_Portal_BAT\`
4. Upload those files as an **overlay** into the router’s existing `hotspot/` directory.

The BAT always builds first (`RENZFI_APPLIANCE_BASE_URL=http://10.10.10.2 npm run build:mikrotik-portal`). If the build fails, the previous export package is left untouched.

## Manual build (without export)

```bat
set RENZFI_APPLIANCE_BASE_URL=http://10.10.10.2
npm run build:mikrotik-portal
npm run check:captive-portal-sync
```

## Consistency check

```bat
npm run check:captive-portal-sync
```

Fails if generated `renzfi-app.js` still has `__RENZFI_APPLIANCE_BASE_URL__`, or if deployment / Final_Build do not match `portal/` after URL substitution.

## Tests

`npm run test:portal:lifecycle` refuses to run against a stale `Final_Build_Portal`.

## Related

- Forensic map: `docs/RENZFI_SOURCE_OF_TRUTH_FORENSIC.md`
- Deploy notes: `deployment/mikrotik-hotspot/README.md`
