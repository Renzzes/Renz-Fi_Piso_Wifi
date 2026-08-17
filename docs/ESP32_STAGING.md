# ESP32 staging and deployment

Installers never copy files by hand. Staging is fully automated and validated before SPIFFS upload.

> **Note:** This pipeline stages the captive portal onto the ESP32's own SPIFFS for **development, factory setup, management-AP access, and field recovery only.** The production customer-facing portal is a separate generated bundle deployed to the **MikroTik hAP lite's Hotspot storage** — see [`deployment/mikrotik-hotspot/README.md`](../deployment/mikrotik-hotspot/README.md) and `npm run build:mikrotik-portal`. Both pipelines are generated from the same canonical `portal/` source; edit there once and regenerate both.

## Pipeline

```
npm run build:esp32
  → Vite build (admin dashboard, embedded asset names)
  → stage portal/ → ESP32_S3_Firmware/data/portal/
  → stage dist/   → ESP32_S3_Firmware/data/
  → validate required files
  → ready for uploadfs

npm run deploy:esp32
  → npm run build:esp32
  → pio run (firmware)
  → pio run -t upload
  → pio run -t uploadfs  (blocked if data/ incomplete)
```

## Source directories

| What | Edit here | Staged to |
|------|-----------|-----------|
| Admin dashboard | `src/` (Vite) | `ESP32_S3_Firmware/data/` |
| Captive portal | `portal/` | `ESP32_S3_Firmware/data/portal/` |

**Do not edit `ESP32_S3_Firmware/data/` manually.** It is regenerated on every `build:esp32` and listed in `.gitignore`.

## Required portal files

Staging fails (and PlatformIO `uploadfs` is blocked) if any of these are missing after staging:

- `login.html`
- `renzfi-app.js`
- `renzfi-style.css`
- `md5.js`
- `favicon.ico` (auto-generated from `public/favicon.svg` when absent in `portal/`)

Optional: `Default-Banner.png` (auto-generated), `bg_music.mp3`.

Manifest: `scripts/esp32-staging-manifest.mjs` (keep in sync with `PortalSpiffsLayout.h`).

## Build metadata

Every `npm run build:esp32` writes `ESP32_S3_Firmware/data/build-info.json`:

```json
{
  "firmwareVersion": "0.5.0-w5500",
  "adminBuild": "2026-06-30T15:22:14.000Z",
  "portalRevision": "ab91d2",
  "gitCommit": "8f4d9c1",
  "buildNumber": 124,
  "deviceProfileVersion": 1,
  "storageContractVersion": 1,
  "httpContractVersion": 1
}
```

At runtime the firmware loads this from SPIFFS, mirrors to `/config/build-info.json` on SD when mounted, and exposes it as:

- `GET /api/health` → `data.build` (no auth — field support)
- `GET /api/system/build` → `data.staged` + `data.runningFirmwareVersion`

Contract version numbers: `scripts/contract-versions.mjs` ↔ `ESP32_S3_Firmware/src/ContractVersions.h`.

## Example staging output

```
[stage:esp32] Checking portal assets...
  ✓ login.html
  ✓ renzfi-app.js
  ✓ renzfi-style.css
  ✓ md5.js
  ✓ favicon.ico
[stage:esp32] Portal assets verified
[stage:esp32] Build metadata: firmware=0.5.0-w5500 portal=ab91d2 git=8f4d9c1
[stage:esp32] Ready for uploadfs
```

## PlatformIO environments

| Environment | Use | Debug flags |
|-------------|-----|-------------|
| `freenove_esp32_s3_wroom` | Production | Off |
| `renzfi_installer` | Field install / validation | Boot, SPIFFS, portal |
| `renzfi_developer` | Engineering | All diagnostics |

```bash
# Installer firmware + deploy
PIO_ENV=renzfi_installer npm run deploy:esp32

# Windows PowerShell
$env:PIO_ENV="renzfi_installer"; npm run deploy:esp32
```

## Scripts

| npm script | Script file |
|------------|-------------|
| `build:esp32` | `scripts/stage-esp32-data.mjs` (after `npm run build`) |
| `stage:esp32` | Re-stage only (requires existing `dist/`) |
| `deploy:esp32` | `scripts/deploy-esp32.mjs` |
| `build:mikrotik-portal` | `scripts/build-mikrotik-portal.mjs` — generates the production portal bundle in `deployment/mikrotik-hotspot/` (see that directory's `README.md`) |

Pre-upload gate: `ESP32_S3_Firmware/extra_scripts/validate_spiffs_data.py`.

See also: [BOOT_DIAGNOSTICS.md](./BOOT_DIAGNOSTICS.md).
