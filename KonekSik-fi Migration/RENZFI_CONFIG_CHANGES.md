# Renz-Fi / KonekSik-Fi — Config & Software Changes (hEX)

## Firmware

| Question | Answer |
|----------|--------|
| New ESP32 firmware build? | **No** |
| OTA update required? | **No** (unless you want unrelated fixes) |
| Same Admin SPA? | **Yes** |

---

## What you change on the ESP32 / Admin

### 1. Router credentials (`/config/router.json`)

Update if hEX management IP or password differs:

| Field | Typical change |
|-------|----------------|
| `host` | hEX bridge IP (e.g. `10.20.0.1`) |
| `username` | Same API user as hAP |
| `password` | Same or new |

**Where:** Admin → System Configuration → Router / Gateway  
**Or:** Setup Step 2 (Router Connection) if re-running partial setup

### 2. Router cache refresh

After credentials are correct:

- Admin → **Synchronize Router** (or router cache sync job)
- Refreshes `/config/router-cache.json`

### 3. Captive portal rebuild & upload

Phones load HTML from **MikroTik**; JS calls **ESP32 API**.

1. Set `RENZFI_APPLIANCE_BASE_URL` to ESP32 reachable URL.
2. `npm run build:mikrotik-portal`
3. Upload to **hEX** HotSpot directory (not hAP).

### 4. External AP registry (`/config/access-points.json`)

**New step for hEX:**

- Admin → **Access Points**
- Add each AP: name, management IP, optional notes
- Renz-Fi monitors reachability only — **does not configure AP**

### 5. Provisioning JSON (`/config/router-provisioning.json`)

After migration, fields may differ:

| Field | hAP lite | hEX |
|-------|----------|-----|
| `wifiInterfaceId` | `wlan1` | Empty or bridge name |
| `selectedWirelessInterface` | wlan | **bridge** member context |
| Network mode | existing / new SSID | **existing network on bridge** |

Prefer **Synchronize Router** + manual RouterOS over forcing wizard Finish.

---

## Setup wizard — what works on hEX

| Step | Usable on hEX? |
|------|----------------|
| 1 Owner Information | Yes |
| 2 Router Connection | Yes |
| 3 Router Scan | Yes (detects bridge + HotSpot) |
| 4 Wi‑Fi Configuration | **No** — no MikroTik radio |
| 5 Administrator & Operator | Yes |
| 6 Installation Summary | Yes (after manual hEX prep) |

**Step 4:** Configure customer SSID on **external AP**, not Renz-Fi wizard.

**Finish / production activation:** May fail if it requires `/interface/wireless`. Use manual hEX HotSpot + Admin sync instead.

---

## Admin pages — behavior changes

| Page | hAP lite | hEX |
|------|----------|-----|
| Dashboard | Normal | Normal |
| Sales / Sessions | Normal | Normal |
| System Configuration → Router | Normal | Update IP/credentials |
| Wireless SSID (router) | Edits MikroTik Wi‑Fi | **N/A** — use AP UI |
| Access Points | Optional | **Required** for Wi‑Fi sites |
| Storage / SD | Normal | Normal |

---

## Files on SD card — expected changes

| File | Change after migration |
|------|------------------------|
| `router.json` | Host/credentials if new |
| `router-cache.json` | Refreshed on sync |
| `router-provisioning.json` | May show bridge-only topology |
| `settings.json` | Usually unchanged |
| `sales.json` | Unchanged |
| `portal_sessions.json` | Unchanged unless reset |

**No factory reset required** if you only swap gateway and update router.json + portal on MikroTik.

---

## Optional: document site-specific values

Fill in after migration:

```
MikroTik model:     hEX refresh E50UG
MikroTik IP:        
ESP32 IP:           
Guest subnet:       
Portal base URL:    
External AP model:  
External AP mgmt IP:
Customer SSID:      
Migration date:     
```
