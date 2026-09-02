# Backup Checklist — Before hEX Migration

Complete **all sections** while hAP lite is still live and working. Store files in:

```
KonekSik-fi Migration/backups/YYYY-MM-DD/
```

**Do not commit passwords or `.backup` binaries to git.**

---

## A. MikroTik hAP lite (RouterOS)

### A1. Binary backup (full restore to same model)

Winbox → **System → Backup → Backup**  
Save as: `haplite-YYYY-MM-DD.backup`

### A2. Text export (reference for hEX rebuild)

Terminal on hAP lite:

```routeros
/export file=haplite-reference-YYYY-MM-DD
```

Download from **Files** via Winbox/FTP.

### A3. Screenshots / notes

Capture or write down:

- [ ] **IP → Addresses** (WAN + LAN)
- [ ] **IP → Pool** (DHCP ranges)
- [ ] **IP → DHCP Server** (interface, network)
- [ ] **IP → Hotspot** (servers, profiles, users template)
- [ ] **IP → Hotspot → Server Profiles** (rate limits, login page)
- [ ] **Interface → Bridge** (ports, name)
- [ ] **Interface → Wireless** (SSID, security — reference only, not copied to hEX)
- [ ] **IP → Firewall → Filter/NAT** (guest rules)
- [ ] **System → Users** (Renz-Fi API account)

### A4. HotSpot HTML directory

Download hotspot folder from hAP (login.html, renzfi-app.js, etc.).

### A5. Identity

```routeros
/system identity print
/system resource print
/routerboard print
```

Save to `haplite-system-info.txt`.

---

## B. Renz-Fi ESP32 (SD card + Admin)

### B1. SD card file copy

| SD path | Purpose |
|---------|---------|
| `/config/router.json` | RouterOS API host, user, password |
| `/config/router-provisioning.json` | Bridge, SSID selection, network mode |
| `/config/router-cache.json` | Cached wireless/HotSpot snapshot |
| `/config/settings.json` | Coin, admin, device settings |
| `/sales/sales.json` | Sales history |
| `/sessions/portal_sessions.json` | Portal sessions |

### B2. Record live network values

| Field | Value |
|-------|-------|
| MikroTik API host | |
| ESP32 Ethernet IP | |
| Guest subnet | |
| Customer SSID (hAP lite) | |
| Firmware version | |
| HotSpot profile name(s) | |

### B3. Captive portal build record

Note **`RENZFI_APPLIANCE_BASE_URL`** from last portal build.

---

## C. Sign-off

| Item | Done | Date |
|------|------|------|
| RouterOS .backup | ☐ | |
| RouterOS /export | ☐ | |
| HotSpot files | ☐ | |
| ESP32 SD /config | ☐ | |
| Live values table | ☐ | |

**Completed by:** _________________ **Date:** _________________
