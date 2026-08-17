# Captive Portal Auto-Redirect — Hotspot Interception Forensics

## Expected production path (from firmware source)

```
Phone
  → production SSID (wireless / renzfi-wifi)
  → bridge port on guest bridge (bridge-renzfi or adopted)
  → guest L3: /ip/address + DHCP on bridge
  → Hotspot server on THE BRIDGE (not wireless slave alone)
  → Hotspot profile (html-directory=hotspot)
  → hotspot/login.html
  → browser JS → ESP32 /api/portal/* (walled-garden)
```

## Source-proven topology (provisioning)

| Layer | Source binding |
|-------|----------------|
| Foundation | Bridge + IP + pool + DHCP on guest bridge (`RouterProvisioningManager`) |
| Wi-Fi apply | Wireless as bridge port → `ensureHotspotOnInterface` |
| Hotspot create | Prefer bridge when WLAN is a bridge member |
| Finish | Verify Hotspot on wireless **or** bridge; optional portal file check; walled-garden |

Firmware defaults for **create_new** guest LAN remain `10.20.20.0/24`. Production docs / many field units use `10.10.10.0/24` (adopted or migrated). Hotspot create now prefers the **live** `/ip/address` on the required interface when present.

## Root cause (source) — Hotspot not forced onto bridge captive path

### Defect

Previous `ensureHotspotOnInterface` treated Hotspot on the **wireless interface alone** as success, even when:

- Guest DHCP / gateway IP lived on the **bridge**
- Wireless was only a **bridge port**

MikroTik requires Hotspot on the **bridge** in that topology. Hotspot on the wireless slave leaves unauthenticated clients outside normal Hotspot host/intercept machinery: portal files under `Files/hotspot/` can exist while **no** auto-redirect occurs.

Secondary gaps (same family):

1. Managed Hotspot name reuse without verifying interface matched the guest path.
2. Profile `hsprof-renzfi` was referenced on create but **never created** by firmware.
3. `html-directory=hotspot` was written only in portal mode `MANAGED` (wizard default is `skipped`).
4. Hotspot `=address=` used firmware default CIDR even when the live bridge address differed.

### Not the first-failure layer

| Layer | Role |
|-------|------|
| `login.html` / CSS | Portal **render** after redirect |
| `__RENZFI_APPLIANCE_BASE_URL__` | Portal **API** after page opens (MikroTik origin ≠ ESP32) |
| ESP32 `CaptivePortalDetectionServer` | Management AP (`192.168.4.1`) only — not production guests |

Files present under `hotspot/` do **not** prove interception works; they only prove storage.

## Fix applied

`RouterWireless::reconcileCaptiveHotspotPath` (used by Wi-Fi ensure + Admin **Synchronize Router**):

1. If WLAN ∈ guest bridge → required Hotspot interface = **bridge**.
2. Hotspot on wireless only → `/ip/hotspot/set =interface=<bridge> =disabled=no`.
3. Disabled Hotspot on required iface → enable.
4. Ensure Hotspot profile exists; set `html-directory=hotspot` only when wrong.
5. Create Hotspot only when missing; address from live bridge IP when available.

Finish always runs idempotent `html-directory` ensure (write only if not already `hotspot`).

No continuous polling. Idle RouterOS rate remains 0 commands/min.

## Existing install migration

No factory reset. Operator runs **Synchronize Router** (or re-runs Wi-Fi / Finish paths that call ensure). Repair is idempotent: correct units perform print/compare only; wrong units get minimal `set`/`add`.

## Portal API origin (second stage — separate from redirect)

Official build:

```bash
RENZFI_APPLIANCE_BASE_URL=http://10.10.10.2 npm run build:mikrotik-portal
```

If `renzfi-app.js` still contains `__RENZFI_APPLIANCE_BASE_URL__`, the page falls back to `window.location.origin` (MikroTik) and `/api/portal/*` fails **after** redirect. Confirm substituted JS on the router; keep walled-garden for the ESP32 `/32`.

## Hardware validation (mandatory)

Do not claim PASS until an unauthenticated phone:

1. Joins production SSID and gets DHCP from MikroTik guest LAN
2. Appears in `/ip/hotspot/host`
3. Plain HTTP is redirected to Hotspot login
4. OS captive sheet may appear (separate from HTTP redirect)
5. `login.html` loads; CSS/JS load; ESP32 portal API reachable; auth works

Mark until then: **HARDWARE VALIDATION REQUIRED**.
