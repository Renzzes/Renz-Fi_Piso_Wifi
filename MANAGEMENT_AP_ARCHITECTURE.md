# Management Access Point — Architecture (Phase 7C.1)

> **Status:** Implemented (foundation) — lifecycle in [PHASE_7C2_MANAGEMENT_AP_LIFECYCLE.md](./PHASE_7C2_MANAGEMENT_AP_LIFECYCLE.md)  
> **Date:** 2026-06-30  
> **Scope:** Dedicated installer/owner Wi-Fi AP + unified network status model.  
> **Not in scope:** Android integration, automatic AP disable after setup (Phase 7C.2).
>
> **Phase 8 update (see [PHASE_8_NETWORK_ONBOARDING.md](./PHASE_8_NETWORK_ONBOARDING.md)):** the SSID below (`RenzFi-Setup-RF-XXXXXX`, prefix + device ID) has been replaced with a single fixed SSID: **`Renz-Fi Setup`** (no MAC/serial suffix). Ethernet is also now DHCP-first by default (was static-only). All other architecture described here — AP IP, dedicated `ManagementApManager`, no NAT/no Internet sharing, always-on AP — is unchanged.

---

## Purpose

The Renz-Fi appliance exposes **two completely separate network interfaces**:

| Interface | Hardware | Audience | Traffic |
|-----------|----------|----------|---------|
| **Management AP** | ESP32 built-in Wi-Fi (soft-AP) | Installers, owners | Setup, admin, diagnostics, maintenance, firmware update |
| **Customer Network** | W5500 Ethernet | End customers | Internet, captive portal, coin sessions, voucher sessions |

The Management AP exists so installers and owners can reach the appliance **without knowing its Ethernet IP address**. It does **not** carry customer traffic, does **not** share Internet with connected clients, and is **not** mixed into `PortalServer` or captive portal logic.

---

## Lifecycle

```
Power ON
  ↓
W5500 driver init (EthernetManager)
  ↓
Subsystems init (storage, auth, installation, provisioning, …)
  ↓
ManagementApManager.begin()
  ↓
Management AP starts (open network, 192.168.4.1)
  ↓
HTTP server starts when AP is running OR Ethernet has IP
  ↓
Installer connects to "Renz-Fi Setup" (fixed SSID, Phase 8)
  ↓
http://192.168.4.1 → existing React SPA (admin + setup wizard)
  ↓
Setup completes → Management AP **remains enabled** (Phase 7C.1)
  ↓
[Phase 7C.2] Optional automatic disable after installation
```

### Phase 7C.1 policy

- Management AP **starts on every boot**.
- It **stays enabled** through and after installation.
- Automatic disable after setup is **deferred to Phase 7C.2**.

---

## Boot Flow

```
┌─────────────────────────────────────────────────────────────────┐
│ Phase 0  RecoveryManager                                        │
│ Phase 1  EthernetManager.begin()  — W5500 driver + static IP    │
│ Phase 2  SPIFFS mount  — React SPA assets                       │
│ Phase 3  StorageManager  — SD card                              │
│ Phase 4  Subsystems (auth, installation, provisioning, …)     │
│          ManagementApManager.begin()  — soft-AP @ 192.168.4.1   │
│          startNetworkServices() if eth ready OR mgmt AP running │
└─────────────────────────────────────────────────────────────────┘
```

**Key change from pre-7C.1:** The HTTP server no longer waits exclusively for Ethernet link-up. If only the Management AP is available (factory unit, no cable), port 80 still serves the SPA.

---

## Network Topology

```
                    ┌──────────────────────────────┐
                    │      Renz-Fi Appliance       │
                    │                              │
  Installer phone   │  ┌────────────────────────┐  │     Customer LAN
  (Wi-Fi client)──────│ Management AP (Wi-Fi)  │  │
                    │  │ 192.168.4.1            │  │
                    │  │ SSID: "Renz-Fi Setup"  │  │
                    │  │ OPEN / DHCP            │  │
                    │  └────────────────────────┘  │
                    │                              │
                    │  ┌────────────────────────┐  │──── Ethernet (W5500)
                    │  │ Customer Network       │  │     VLAN40 / portal /
                    │  │ 10.40.0.2 (static)     │  │     coin / vouchers
                    │  └────────────────────────┘  │
                    │                              │
                    │  AsyncWebServer :80          │
                    │  (shared — both interfaces)  │
                    └──────────────────────────────┘
```

- **No NAT** between Management AP and Ethernet.
- **No customer traffic** routed through the AP.
- **No Internet sharing** to AP clients.

---

## Management AP Defaults

| Setting | Value |
|---------|-------|
| SSID | `Renz-Fi Setup` (fixed, exact — no MAC/serial suffix, Phase 8) |
| Security | OPEN (no password) |
| AP IP | `192.168.4.1` |
| Gateway | `192.168.4.1` |
| Subnet | `255.255.255.0` |
| DHCP | Enabled (ESP32 soft-AP default) |
| Max clients | 4 |
| Channel | 6 |
| Portal URL | `http://192.168.4.1` |

---

## Relationship with Ethernet

| Scenario | Management AP | Ethernet | HTTP server |
|----------|---------------|----------|-------------|
| Factory, no cable | Running | Link down | Serves via AP |
| Factory, cable connected | Running | Link up | Serves via both |
| Installed, cable connected | Running (7C.1) | Link up | Serves via both |
| Installed, cable disconnected | Running (7C.1) | Link down | Serves via AP |

Ethernet status (IP, gateway, subnet, DNS, MAC, link) is reported in the admin dashboard via `GET /api/system/network`. The Management AP is **never stopped** when Ethernet connects.

---

## Admin Access

When connected to the Management AP, `http://192.168.4.1` serves the **same React SPA** already on SPIFFS:

- Admin Dashboard (`/admin`)
- Setup Wizard (`/setup`)
- System configuration
- Diagnostics

No second frontend. No captive portal on the Management AP.

---

## Unified Network Status Model

**Endpoint:** `GET /api/system/network` (auth required)  
**Builder:** `NetworkStatusModel::fill()`  
**Backward-compat alias:** `GET /api/system/wifi`

### Response shape (additive)

```json
{
  "interfaces": {
    "managementAp": {
      "ssid": "Renz-Fi Setup",
      "ip": "192.168.4.1",
      "running": true,
      "clients": 1,
      "connectedClients": 1,
      "enabled": true,
      "mode": "factory",
      "uptimeSeconds": 327,
      "portalUrl": "http://192.168.4.1",
      "security": "open"
    },
    "ethernet": {
      "driverReady": true,
      "link": true,
      "linkUp": true,
      "hasIp": true,
      "mode": "dhcp",
      "ip": "10.40.0.2",
      "gateway": "10.40.0.1",
      "subnet": "255.255.255.0",
      "dns": "10.40.0.1",
      "mac": "02:AA:BB:10:40:02"
    }
  },
  "managementAp": {
    "ssid": "Renz-Fi Setup",
    "ip": "192.168.4.1",
    "running": true,
    "clients": 1,
    "connectedClients": 1,
    "enabled": true,
    "mode": "factory",
    "portalUrl": "http://192.168.4.1",
    "security": "open"
  },
  "ethernet": {
    "driverReady": true,
    "link": true,
    "linkUp": true,
    "hasIp": true,
    "mode": "dhcp",
    "ip": "10.40.0.2",
    "gateway": "10.40.0.1",
    "subnet": "255.255.255.0",
    "dns": "10.40.0.1",
    "mac": "02:AA:BB:10:40:02"
  },
  "mode": "ethernet",
  "modeLabel": "W5500 wired (link up)",
  "mdns": {
    "hostname": "renzfi.local",
    "adminUrl": "http://renzfi.local/admin"
  }
}
```

`interfaces` is the forward-compatible structure for future network interfaces
such as `wifiSta`, `cellular`, or `usbEthernet`. The top-level `managementAp`
and `ethernet` objects are preserved as aliases for existing clients.

`managementAp.mode` is derived from `InstallationStateManager`:

| Mode | Meaning |
|------|---------|
| `factory` | AP is active because installation still needs setup |
| `maintenance` | AP is active after setup for owner/installer maintenance |
| `disabled` | AP is not running or not enabled |

`uptimeSeconds` reports how long the Management AP has been running in the
current session (not whole-device uptime). Phase 7C.2 can use this with a
fixed maintenance timeout (e.g. 600s) so Browser/Android show remaining time
without client-side hidden timers:

`remaining = maintenanceTimeoutSeconds - managementAp.uptimeSeconds`

This model is designed for future consumption by:

- Browser admin (network settings page)
- Android Owner App
- Fleet health overlays

---

## Component Ownership

| Component | Responsibility |
|-----------|----------------|
| `ManagementApManager` | Start/stop AP, SSID, IP, client count, status JSON, captive DNS |
| `ManagementApConfig` | Constants (IP, fixed SSID, channel) |
| `NetworkStatusModel` | Unified `managementAp` + `ethernet` JSON envelope |
| `EthernetManager` | W5500 link, static IP, DNS (unchanged role) |
| `WebServerManager` | HTTP :80 on all interfaces (unchanged) |
| `PortalServer` | Customer captive portal only (unchanged) |

`SetupModeController` is a legacy stub; Phase 7C.1 supersedes it with `ManagementApManager`.

---

## Installation Flow (unchanged logic)

```
Factory
  ↓  Management AP enabled
Installer connects to AP
  ↓
Setup Wizard (existing ProvisioningEngine workflow)
  ↓
Complete
  ↓
Management AP remains enabled (7C.1)
```

No changes to `ProvisioningEngine`, `ProvisioningServer`, `ProvisioningClient`, or installation state machine.

---

## Future Android Flow (documentation only — not implemented)

```
Owner App
  ↓
User joins Wi-Fi: "Renz-Fi Setup" (open, fixed SSID)
  ↓
App opens http://192.168.4.1/setup (WebView or deep link)
  ↓
Existing Setup Wizard SPA
  ↓
On completion, app returns to fleet list
```

Android will reuse `GET /api/health` for discovery when on Ethernet, and `GET /api/system/network` for Management AP status when connected locally.

---

## Future Maintenance Mode (Phase 7C.2+)

Planned capabilities (not implemented in 7C.1):

| Feature | Description |
|---------|-------------|
| Auto-disable AP | Turn off Management AP after installation completes |
| Owner re-enable | Button in admin to temporarily re-enable AP for field service |
| Timed AP | Auto-disable after N minutes of inactivity |
| Password option | Optional WPA2 for high-security deployments |

`ManagementApManager::stop()` and `isEnabled()` are in place for these future hooks.

---

## Files Added

| File | Purpose |
|------|---------|
| `ESP32_S3_Firmware/src/ManagementApConfig.h` | AP constants |
| `ESP32_S3_Firmware/src/ManagementApManager.h` | Manager interface |
| `ESP32_S3_Firmware/src/ManagementApManager.cpp` | soft-AP start/stop/status |
| `ESP32_S3_Firmware/src/NetworkStatusModel.h` | Unified network JSON model |
| `ESP32_S3_Firmware/src/NetworkStatusModel.cpp` | Model builder |
| `MANAGEMENT_AP_ARCHITECTURE.md` | This document |

## Files Modified

| File | Change |
|------|--------|
| `ESP32_S3_Firmware/src/FirmwareApp.h` | Added `_mgmtAp` member |
| `ESP32_S3_Firmware/src/FirmwareApp.cpp` | Init AP; start HTTP when AP or ETH ready |
| `ESP32_S3_Firmware/src/EthernetManager.h` | Added `dns()` accessor |
| `ESP32_S3_Firmware/src/ApiServer.h` | `ManagementApManager*` dependency |
| `ESP32_S3_Firmware/src/ApiServer.cpp` | Extended `/api/system/network` via `NetworkStatusModel` |
| `ESP32_S3_Firmware/src/SetupModeController.h` | Comment → superseded by `ManagementApManager` |

## Files NOT Modified (frozen)

- RouterPlatform, IRouterDriver, GenericAPDriver, MikroTikDriver
- ProvisioningEngine, ProvisioningServer, ProvisioningClient
- Installation Workflow state machine
- HTTP Route Contract document (additive JSON fields only at runtime)
- Device Profile contract (`GET /api/health` `data.device` unchanged)
- Fleet (browser + Android)
- Captive portal logic (`PortalServer`)
- Customer session logic
- Setup Wizard UI (React)
- Android Owner App code

---

## Success Criteria Verification

| Criteria | Status |
|----------|--------|
| Factory unit broadcasts `Renz-Fi Setup` (fixed SSID) | ✅ `ManagementApManager::buildSsid()` |
| Open network, no password | ✅ `WiFi.softAP(ssid, nullptr, …)` |
| `http://192.168.4.1` serves existing SPA | ✅ HTTP starts when AP running |
| Setup Wizard works unchanged | ✅ Same routes, no UI changes |
| Ethernet can connect later without stopping AP | ✅ Independent lifecycles |
| No customer traffic through AP | ✅ Separate interface, no NAT |
| No provisioning behavior changes | ✅ Frozen components untouched |
| Dedicated component (not PortalServer) | ✅ `ManagementApManager` |
