# Renz-Fi v1 Product Standardization

**Status:** Official hardware specification for Renz-Fi Version 1  
**Phase:** 7D — Product Standardization

---

## Official Appliance Package

The Renz-Fi v1 appliance is a complete, supported Piso WiFi deployment package with a defined hardware stack and clearly assigned responsibilities for each component.

---

## Product Branding (Installer UI)

Installer-facing software refers to the router as **Renz-Fi Gateway** — *Powered by MikroTik RouterOS* — rather than a specific hardware model.

| Context | Label |
|---------|-------|
| Setup wizard / Owner App | Renz-Fi Gateway |
| Subtitle | Powered by MikroTik RouterOS |
| Firmware driver | `MikroTikDriver` (unchanged) |

**Why:** Version 2 may ship different MikroTik SKUs (hEX, hAP ac², hAP ax lite, hAP ax²). Branding at the product level avoids UI and documentation redesign when the physical router changes.

The v1 reference hardware model (RB941-2nD-TC) is documented here for procurement and support — not hardcoded in installer UI.

---

## Hardware Component Registry

| Component | Official Status | v1 Reference |
|-----------|-----------------|--------------|
| ESP32-S3 + W5500 Ethernet | **Required** | Freenove ESP32-S3 WROOM |
| Renz-Fi Gateway (MikroTik RouterOS) | **Required** | hAP lite — RB941-2nD-TC |
| Android Owner App | **Required** | Renz-Fi Manager |
| Browser Admin | **Required** | Embedded Web UI |
| Wi-Fi Access Points | Optional (coverage only) | Any AP in Bridge/AP mode |

---

## Component Responsibilities

### ESP32-S3 Appliance

The appliance controller handles all business logic.

| Responsibility | Detail |
|---------------|--------|
| Coin detection | Physical pulse counting via GPIO |
| Captive portal | Login page served from SPIFFS |
| Session management | Time-limited Wi-Fi sessions |
| Voucher redemption | Code validation and time allocation |
| Promo management | Rate tables and time multipliers |
| Fleet telemetry | Health, uptime, session metrics |
| Provisioning API | REST API for setup wizard and Owner App |
| Asset hosting | Custom banners, music, and logos |
| Management AP | Open setup network `RenzFi-Setup-{DeviceId}` |

### Renz-Fi Gateway — Required Router

The Renz-Fi Gateway is the **only router** in the official Renz-Fi deployment. It is powered by MikroTik RouterOS and handles everything at Layer 3.

| Responsibility | Detail |
|---------------|--------|
| Internet gateway | WAN-to-LAN NAT and routing |
| Hotspot engine | Captive portal redirect and enforcement |
| DHCP server | IP assignment for all LAN clients |
| Bandwidth control | Per-user rate profiles |
| Voucher enforcement | Coordinated with ESP32 via RouterOS API |
| User synchronization | Login/logout events via MikroTikDriver |
| Firewall | Traffic isolation and client control |

**Why MikroTik RouterOS?**  
RouterOS provides a stable, programmable hotspot API that enables real-time user authorization, bandwidth profile assignment, and disconnect-on-session-expiry. These features cannot be replicated with consumer routers.

**v1 reference hardware:** MikroTik hAP lite (RB941-2nD-TC)

### Optional Wi-Fi Access Points

Access points extend physical Wi-Fi coverage only. They are **not routers** and perform no Layer 3 functions.

The important requirement is **not the brand** — it is that the device operates in Bridge or Access Point mode, leaving routing, DHCP, hotspot, and enforcement to the Renz-Fi Gateway.

| What they do | What they do NOT do |
|-------------|---------------------|
| ✓ Extend Wi-Fi coverage | ✗ DHCP |
| ✓ Bridge traffic to gateway | ✗ NAT |
| ✓ Increase client capacity | ✗ Firewall |
| | ✗ Hotspot enforcement |
| | ✗ Captive portal |

**Example brands:** TP-Link, COMFAST, Ruijie, Omada, UniFi, ASUS (AP mode)

**Configuration for each AP:**
- Mode: Access Point or Bridge (not Router mode)
- DHCP: Disabled
- NAT: Disabled
- Firewall: Disabled
- Uplink: Connected to Renz-Fi Gateway LAN port

### Android Owner App

Renz-Fi Manager provides onboarding and fleet management.

| Responsibility | Detail |
|---------------|--------|
| First-time setup | Native onboarding via provisioning APIs |
| Fleet monitoring | Health dashboard, per-appliance status |
| Management AP | Trigger maintenance mode remotely |
| Appliance discovery | LAN subnet scan |

### Browser Admin

Full administration dashboard served from the ESP32.

| Responsibility | Detail |
|---------------|--------|
| Promo management | Rates, voucher codes, schedules |
| Session monitoring | Active clients, session history |
| Asset management | Banners, music, logo upload |
| System settings | Gateway credentials, coin config, OTA |

---

## Recommended Network Topologies

### Single AP deployment

```
Internet
    │
 [Renz-Fi Gateway]     ← Hotspot, DHCP, enforcement
    │
 [ESP32 Appliance]     ← Controller (wired via W5500)
    │
 [Wi-Fi clients]       ← Connect to gateway Wi-Fi
```

Covers a single-room or small venue. No additional AP required.  
The gateway provides both routing and Wi-Fi coverage.

### Multi-AP deployment

```
Internet
    │
 [Renz-Fi Gateway]
    │
    ├── [ESP32 Appliance]        (wired)
    │
    ├── [Access Point #1]        (Bridge/AP mode, wired)
    │
    └── [Access Point #2]        (Bridge/AP mode, wired)
```

For larger venues: the Renz-Fi Gateway remains the sole router. Access points are transparent bridges — clients roam between them using the same DHCP lease and hotspot session managed by the gateway.

### Future expansion

The RouterPlatform layer inside the ESP32 firmware is driver-based. Additional router drivers can be added without modifying the ProvisioningEngine or any existing contracts:

| Future driver | Use case |
|--------------|---------|
| `UniFiDriver` | UniFi controller integration |
| `OpenWRTDriver` | OpenWRT LuCI API |
| `TPLinkDriver` | TP-Link Omada SDK |

`GenericAPDriver` remains compiled and registered as a reserved fallback for future product editions.

---

## Future: Gateway Telemetry (Planned)

Since every official appliance includes a Renz-Fi Gateway, the ESP32 exposes gateway product branding and feature capabilities via `GET /api/health` → `router.product`, `router.status` (`detected` | `connected` | `unavailable`), and `router.capabilities` (`hotspot`, `pauseResume`, `bandwidthControl`, `voucherSync`, `queueManagement`). Clients render gateway features dynamically from the reported flags — no hardcoded SKU assumptions.

**Proposed shape** (not yet implemented):

```json
{
  "gateway": {
    "model": "RB941-2nD-TC",
    "routerOsVersion": "7.18",
    "uptime": 86400,
    "cpuUsage": 18,
    "memoryUsage": 46,
    "wanOnline": true,
    "internetReachable": true
  }
}
```

**Fleet Dashboard vision:**

```
Store 1
  🟢 Appliance Healthy
  🟢 Gateway Healthy
  🟢 Internet Online
  🟢 SD Ready
  🟢 Portal Ready
```

Implementation would extend `GET /api/health` (or a nested fleet fact) via MikroTikDriver — no ProvisioningEngine or HTTP contract redesign required. Model-specific fields (`model`) would come from RouterOS identity, keeping UI branding generic while telemetry stays accurate.

---

## GenericAPDriver — Reserved Status

`GenericAPDriver` (driverId: `generic_ap`) is the passive mode driver that works with any router that provides DHCP and a default gateway without requiring a programmable API.

**Status:** Reserved — not removed, not selected by default in v1 installer.

It remains:
- Compiled into firmware
- Registered in RouterPlatform
- Accessible via direct API if needed

It is **not**:
- Presented in the installer UI
- Auto-selected by any runtime path
- Removed from the codebase

This preserves the option for future product editions or specialized deployments.

---

## Software Architecture Compatibility

All existing software contracts remain unchanged.

| Contract | Status |
|---------|--------|
| Provisioning API (`/api/provisioning/*`) | Frozen — unchanged |
| RouterPlatform / IRouterDriver | Unchanged |
| ProvisioningEngine | Unchanged |
| MikroTikDriver | Unchanged |
| GenericAPDriver | Unchanged (reserved) |
| HTTP contracts | Unchanged |
| Fleet API | Unchanged |
| Device Profile | Unchanged |
| Portal | Unchanged |
| Coin | Unchanged |
| Management AP | Unchanged |
| OTA | Unchanged |
| Storage | Unchanged |
| Android onboarding flow | Unchanged (wording updated only) |
| Browser Setup Wizard flow | Unchanged (wording updated only) |
