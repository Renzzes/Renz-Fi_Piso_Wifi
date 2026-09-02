# MikroTik Hotspot — Production Captive Portal Deployment



This directory holds the **production** customer-facing captive portal for a

Renz-Fi Piso WiFi deployment: a generated bundle uploaded to the MikroTik hAP

lite's Hotspot storage. The ESP32-S3 appliance serves `/api/portal/*` JSON,

`/api/portal/assets/*` media, provisioning/setup pages, the management AP, and

the owner admin dashboard — not the production customer HTML.



Guest / Hotspot LAN target: **`10.10.10.0/24`** (gateway **`10.10.10.1`**).



See [`MIGRATION_192_TO_10_10_10.md`](MIGRATION_192_TO_10_10_10.md) when moving

from `192.168.88.0/24`.



## Files in this directory



| File | Source | Committed to git? |

|------|--------|--------------------|

| `README.md` | Hand-written | Yes |

| `MIGRATION_192_TO_10_10_10.md` | Hand-written | Yes |

| `upload-hotspot-files.rsc` | Hand-written template | Yes |

| `admin.html` | Generated from `portal/admin.html` — redirects to ESP32 `/admin` | **No** (gitignored) |

| `login.html` | Generated from `portal/login.html` | **No** (gitignored) |

| `renzfi-app.js` | Generated — `RENZFI_APPLIANCE_BASE_URL` substituted | **No** |

| `renzfi-style.css` | Generated from `portal/renzfi-style.css` | **No** |

| `md5.js` | Generated from `portal/md5.js` | **No** |

| `favicon.ico` | Generated from `portal/favicon.ico` | **No** |

| `Default-Banner.png` | Generated from `portal/Default-Banner.png` | **No** |

| `bg_music.mp3` | Copied from `portal/` when present | **No** |

| `coin.mp3` | Copied from `portal/` when present | **No** |

| `success.mp3` | Copied from `portal/` when present | **No** |



**Never hand-edit generated files.** Edit `portal/*`, then rebuild or run
`scripts\export-captive-portal.bat` and upload **`C:\Captive_Portal_BAT\`** only.

See `docs/PORTAL_SOURCE_OF_TRUTH_AND_EXPORT.md`.




## 1. Configure the appliance base URL (required)



The MikroTik-hosted portal calls the ESP32 API on a **different origin**, so the

build **must** inject the ESP32's current guest-LAN address (typically a DHCP

reservation on `10.10.10.0/24`):



```bash

RENZFI_APPLIANCE_BASE_URL=http://10.10.10.2 npm run build:mikrotik-portal

```



If `RENZFI_APPLIANCE_BASE_URL` is unset, the build **fails** — there is no silent

default to `192.168.88.2` or `10.40.0.2`.



The build validates generated output:



- No `__RENZFI_APPLIANCE_BASE_URL__` placeholder in the declaration line

- No stale `10.40.0.2` or `192.168.88.2`

- RouterOS `$(...)` tokens preserved in `login.html`



Run resolver tests: `npm run test:portal`



## 2. Default portal assets (MikroTik local fallbacks)



Place these under `portal/` before building (recommended for production):



| File | Role |

|------|------|

| `Default-Banner.png` | Initial banner (auto-generated from favicon if missing) |

| `bg_music.mp3` | Coin modal background music |

| `coin.mp3` | Short sound on each accepted coin pulse |

| `success.mp3` | Sound after Done Paying confirms activation |



If custom banner/music exist on the ESP32 (owner admin upload), the portal uses

`/api/portal/branding` URLs and falls back to these local files on load failure.



## 3. Upload to the MikroTik hAP lite



1. Open **Winbox** → **Files**.

2. Upload all generated files to **`hotspot/`** (overwrite stock RouterOS templates).

3. Ensure **`login.html`**, **`status.html`**, **`renzfi-app.js`**, **`renzfi-style.css`**, **`md5.js`**,

   **`Default-Banner.png`**, and audio files are present.



## 4. Post-upload RouterOS configuration



**Prerequisite:** RouterOS **hotspot** package enabled.



1. Edit `renzfiApplianceIp` in `upload-hotspot-files.rsc` if not `10.10.10.2`.

2. Upload the `.rsc` file to the router.

3. Import:



```

/import file-name=upload-hotspot-files.rsc

```



This sets `html-directory=hotspot` and replaces any prior **Renz-Fi ESP32 appliance API**

walled-garden rule with the configured appliance IP.



Manual equivalent:



```

/ip hotspot profile set [find] html-directory=hotspot

/ip hotspot walled-garden ip add action=accept dst-address=10.10.10.2/32 comment="Renz-Fi ESP32 appliance API"

```



## 5. Verify the deployment



1. Join the guest SSID **without** authenticating.

2. Open **`http://10.10.10.1/login`** if the OS captive assistant does not appear.

3. Confirm session timer/credits populate (`GET /api/portal/session` via walled garden).

4. **Owner Admin launcher:** open **`http://10.20.0.1/admin`** or **`http://10.20.0.1/admin.html`**
   (RouterOS may require the `.html` suffix — validate on hardware). Browser should redirect to
   `http://10.10.10.2/admin` without authorizing Hotspot Internet.

5. **Insert Coin** opens the payment modal; countdown syncs with ESP32.

6. **Pause / Resume** updates after API confirmation.

7. With active time, button reads **ADD ADDITIONAL TIME**.

8. Custom banner/music from admin when uploaded; otherwise local defaults.

9. Disconnect ESP32 briefly — service notice appears; voucher form still works.



## Troubleshooting



| Symptom | Likely cause |

|---------|----------------|

| Default RouterOS login page | Files not in `hotspot/` or wrong `html-directory` |

| Timer/credits never populate | Walled-garden IP wrong; rebuild with correct `RENZFI_APPLIANCE_BASE_URL` |

| CORS errors on `/api/portal/*` | Reflash ESP32 firmware with CORS headers (`WebServerManager`) |

| `$(mac)` shown literally | Corrupt `login.html` — rebuild and re-upload |

| Insert Coin does nothing | Wrong appliance URL in generated JS; MAC unavailable from Hotspot template |



## Captive portal notes



Auto pop-up is **not guaranteed** on all phones. HTTP captive checks redirect;

HTTPS may not intercept. Manual URL: **`http://10.10.10.1/login`**.

