# Renz-Fi MikroTik Captive Portal Release Bundle

> **GENERATED overlay.** Do not hand-edit files here.
> Edit `portal/`, then run `scripts\export-captive-portal.bat` and upload
> `C:\Captive_Portal_BAT\` to MikroTik.

This folder is a release copy produced by `npm run build:mikrotik-portal`.
The owner upload package is:

```text
C:\Captive_Portal_BAT\
```

## Important: overlay only

**Do not delete or replace the router's complete `hotspot/` directory.**

Upload the files from the export folder into the router's existing active Hotspot
directory and overwrite matching filenames only.

The live router's native servlet files must remain in place:

- `alogin.html`
- `redirect.html`
- `logout.html`
- `error.html`
- `errors.txt`
- `api.json`

**Overwrite `status.html`.** RouterOS `/status` is a customer portal page. The
Renz-Fi overlay `status.html` is the same app as `login.html`. Leaving an older
`status.html` on the router serves the previous light UI (or a broken mix of
new HTML and old CSS) after the client is authenticated.

Also preserve `rlogin.html`, `radvert.html`, `xml/`, and `css/style.css` when
WISPr or advertisement support is enabled.

## Upload destination

Preferred:

1. Run `scripts\export-captive-portal.bat`
2. Open **`C:\Captive_Portal_BAT\`**
3. In Winbox → **Files** → active Hotspot directory (`hotspot/`)
4. Upload overlay files only (see `CAPTIVE_PORTAL_BUILD_INFO.txt` in the export)

## Files to upload

| Local file | Router destination | Purpose |
|---|---|---|
| `login.html` | `hotspot/login.html` | Customer UI (unauthenticated `/login`) |
| `status.html` | `hotspot/status.html` | **Required overwrite** — customer UI for `/status` |
| `renzfi-app.js` | `hotspot/renzfi-app.js` | Portal logic (API base URL baked in) |
| `renzfi-style.css` | `hotspot/renzfi-style.css` | Styles (must upload with status.html) |
| `md5.js` | `hotspot/md5.js` | CHAP helper |
| `Default-Banner.png` | `hotspot/Default-Banner.png` | Default banner |
| `bg_music.mp3` | `hotspot/bg_music.mp3` | Optional audio |
| `coin.mp3` | `hotspot/coin.mp3` | Optional audio |
| `success.mp3` | `hotspot/success.mp3` | Optional audio |

See also: `docs/PORTAL_SOURCE_OF_TRUTH_AND_EXPORT.md`

## Current network contract

- Customer Hotspot gateway/login: `http://10.20.0.1/login`
- ESP32 appliance API: `http://10.10.10.2`
- ESP32 Admin Dashboard: `http://10.10.10.2/admin`

The bundled `renzfi-app.js` is already generated with:

```text
RENZFI_APPLIANCE_BASE_URL = "http://10.10.10.2"
```

Do not hand-edit the files in this release folder.

## Developer source and rebuild

Canonical editable source:

```text
portal/
```

Preferred owner rebuild + export:

```bat
scripts\export-captive-portal.bat
```

Manual rebuild:

```powershell
$env:RENZFI_APPLIANCE_BASE_URL = "http://10.10.10.2"
npm run build:mikrotik-portal
```

After rebuilding, prefer uploading from `C:\Captive_Portal_BAT\` (via the BAT).
This folder remains a generated twin of `deployment/mikrotik-hotspot/` upload files.

## Deprecated folders

The following locations are not owner upload sources:

- `portal/` — canonical developer source, not the generated RouterOS bundle.
- `deployment/mikrotik-hotspot/` — generated engineering/deployment workspace.
- `Captive Portal/` — deprecated legacy/reference tree.
- `ESP32_S3_Firmware/data/portal/` — generated ESP32 setup/recovery staging.

**Do not delete the deprecated or generated trees until hardware validation and
live-router file archival are complete.**

The legacy tree contains RouterOS servlet references that may be needed during
rollback analysis. It must not be copied over the live router as a complete
replacement.

## Validation after upload

1. Use a private/incognito browser session or clear the captive portal cache.
2. Connect an unauthenticated client to the guest Wi-Fi.
3. Confirm `/login` and `/status` both show the same Renz-Fi HUD (not the old light page).
4. Confirm captive redirect and CHAP voucher form behavior.
5. Confirm coin credits and purchased minutes.
6. Confirm Done Paying reaches Connected.
7. Confirm the 30-second and 15-second warnings.
8. Confirm expiry returns the UI to Disconnected and removes Internet access.
9. Confirm RouterOS idle command count remains zero.

## Rollback

Restore the complete pre-upload `hotspot/` backup and verify the active Hotspot
profile's `html-directory` value. No firewall, NAT, scheduler, proxy, or RouterOS
script change is required by this bundle.
