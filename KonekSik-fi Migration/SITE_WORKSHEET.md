# Site Worksheet — Fill Before Migration

Copy this file to `backups/YYYY-MM-DD/SITE_WORKSHEET-filled.md` with your values.

---

## Site information

| Field | Value |
|-------|-------|
| Business / site name | |
| Address | |
| Owner contact | |
| Planned migration date | |
| Planned cutover time (low traffic) | |
| Installer / tech | |

---

## Current hAP lite (from backup)

| Field | Value |
|-------|-------|
| Model | RB941-2nD-TC |
| RouterOS version | |
| Identity name | |
| WAN type | DHCP / PPPoE / Static |
| WAN IP (if static) | |
| Guest bridge name | |
| Guest subnet | e.g. `10.20.0.0/24` |
| Gateway IP | e.g. `10.20.0.1` |
| DHCP pool range | |
| HotSpot server name | |
| HotSpot profile(s) | e.g. `renzfi-speed-50m-50m` |
| HTML directory | e.g. `hotspot` |
| RouterOS API user | |
| API port | 8728 |
| Customer SSID (hAP Wi‑Fi) | |
| Wi‑Fi security | WPA2-PSK / other |

---

## Renz-Fi ESP32 appliance

| Field | Value |
|-------|-------|
| Board | ESP32-S3 + W5500 |
| Ethernet MAC | |
| ESP32 IP | e.g. `10.10.10.2` |
| Admin URL | e.g. `http://10.10.10.2/dashboard` |
| Firmware version | |
| SD card present | Yes / No |
| `RENZFI_APPLIANCE_BASE_URL` (portal build) | |

---

## New hEX refresh

| Field | Value |
|-------|-------|
| Model | E50UG (hEX refresh) |
| Serial / MAC | |
| ether1 (WAN) → | ISP device |
| ether2 → | ESP32 |
| ether3 → | External AP #1 |
| ether4 → | |
| ether5 → | |
| Planned gateway IP | (same as hAP guest gateway) |
| Power adapter | 24 V included |

---

## External AP(s)

### AP #1

| Field | Value |
|-------|-------|
| Brand / model | |
| Mode | AP / Bridge |
| Customer SSID | |
| Wi‑Fi password | (store securely, not in git) |
| Management IP | e.g. `10.20.0.10` |
| Uplink port | LAN → hEX ether3 |
| Mount location | |

### AP #2 (optional)

| Field | Value |
|-------|-------|
| Brand / model | |
| Management IP | |
| Uplink | |

---

## Backup file locations

| File | Path on disk |
|------|--------------|
| hAP `.backup` | |
| hAP `/export` | |
| ESP32 `router.json` | |
| HotSpot HTML bundle | |
| hEX final `/export` | |

---

## Sign-off (pre-migration)

| Role | Name | Date |
|------|------|------|
| Owner approved downtime window | | |
| Backup checklist complete | | |
| hEX + AP bench-tested | | |
