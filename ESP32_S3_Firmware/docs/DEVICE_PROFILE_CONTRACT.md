# Renz-Fi Device Profile Contract (Frozen — Phase 7A)

This document is the **permanent appliance identity contract** for Renz-Fi. It sits alongside the other frozen architecture documents:

- [STORAGE_ARCHITECTURE.md](./STORAGE_ARCHITECTURE.md)
- [HTTP_ROUTE_CONTRACT.md](./HTTP_ROUTE_CONTRACT.md)
- [INSTALLATION_WORKFLOW.md](./INSTALLATION_WORKFLOW.md)

**Authoritative runtime:** `DeviceIdentity::fillProfile()` → `GET /api/health` → `data.device`  
**Status:** **Frozen.** Field names and semantics are stable public interfaces. Do not rename or remove fields without an explicit contract revision.

---

## 1. Purpose

`DeviceProfile` is the **canonical permanent identity** of a Renz-Fi appliance. Clients (browser admin, Android Owner App, future cloud gateway) use it to:

- Register appliances in a **client-side Device Registry** (keyed by `deviceId`, not IP)
- Discover appliances on the LAN (`GET /api/health`)
- Adapt UI via **capability flags** (coin slot, vouchers, router driver, etc.)

ESP32 appliances **never** store fleet registry data. Each appliance only exposes its own profile.

---

## 2. Transport

| Field | Value |
|-------|-------|
| **Endpoint** | `GET /api/health` |
| **Authentication** | None |
| **Envelope** | `{ "success": true, "data": { …, "device": { … } } }` |
| **Legacy aliases** | Top-level `data.deviceId`, `data.deviceName`, `data.version` (discovery clients) |

---

## 3. DeviceProfile schema (frozen core fields)

All fields live under `data.device` unless noted as legacy top-level aliases.

| Field | Type | Required | Semantics |
|-------|------|----------|-----------|
| `deviceId` | string | **Yes** | Permanent ID. Format: `RF-` + last 6 hex digits of MAC (uppercase). Never IP-based. |
| `serialNumber` | string | Yes | Factory identity; today equals MAC address. |
| `friendlyName` | string | Yes | Human label from `settings.json` → `device.name`. |
| `deviceName` | string | Yes | **Alias** of `friendlyName` (legacy clients). Same value. |
| `firmwareVersion` | string | Yes | Running firmware version string. |
| `version` | string | Yes | **Alias** of `firmwareVersion` (legacy clients). |
| `hardwareRevision` | string | Yes | Board SKU (e.g. `ESP32-S3-W5500-N8R8`). |
| `macAddress` | string | Yes | Ethernet MAC (`AA:BB:CC:DD:EE:FF`). |
| `ipAddress` | string | Yes | Current LAN IP (may change with DHCP). |
| `routerDriver` | string \| null | Yes | Active router driver id (e.g. `mikrotik`, `tplink`). Null if none. |
| `online` | boolean | Yes | `true` when network service is ready (driver + IP). |
| `capabilities` | object | Yes | Feature flags — see §4. **Extension-only** after Phase 7A. |

### Legacy top-level aliases (frozen)

| Field | Maps to |
|-------|---------|
| `data.deviceId` | `data.device.deviceId` |
| `data.deviceName` | `data.device.friendlyName` |
| `data.version` | `data.device.firmwareVersion` |

---

## 4. Capabilities object (frozen — extension-only)

`capabilities` tells clients which features this appliance supports. **New keys may be added** in future firmware; clients must ignore unknown keys.

| Key | Type | Phase 7A | Semantics |
|-----|------|----------|-----------|
| `coin` | boolean | Yes | Universal coin slot hardware enabled (`ENABLE_COIN_MANAGER`). |
| `voucher` | boolean | Yes | Voucher redemption supported. |
| `assetUpload` | boolean | Yes | Portal banner/music upload supported. |
| `router` | string \| null | Yes | Active router driver id (mirrors `routerDriver`). |
| `fleet` | boolean | Yes | Appliance exposes this DeviceProfile for client fleet registry (Phase 7A). Does **not** imply cloud fleet management. |

### Example

```json
{
  "deviceId": "RF-00EF01",
  "friendlyName": "Reception",
  "firmwareVersion": "0.5.0-w5500",
  "hardwareRevision": "ESP32-S3-W5500-N8R8",
  "macAddress": "DE:AD:BE:EF:00:01",
  "ipAddress": "192.168.88.101",
  "routerDriver": "mikrotik",
  "online": true,
  "capabilities": {
    "coin": true,
    "voucher": true,
    "assetUpload": true,
    "router": "mikrotik",
    "fleet": true
  }
}
```

### Future capability keys (reserved — do not implement in Phase 7A)

Examples for TP-Link, Ruijie, LTE, cloud gateway SKUs:

- `lte`, `wireguard`, `cloudSync`, `otaRemote` — add only when hardware/software exists.

---

## 5. Change policy

| Allowed | Not allowed |
|---------|-------------|
| Add **new optional** keys under `capabilities` | Rename or remove core `device.*` fields |
| Add **new optional** top-level fields under `device` with contract revision | Change `deviceId` format without migration doc |
| Bump `hardwareRevision` for new boards | Store fleet registry on ESP32 |
| | Merge DeviceProfile with RouterProfile |

**Rule:** Treat `DeviceProfile` like `StoragePaths` — extend, never casually reshape.

---

## 6. Implementation map

| Layer | Location |
|-------|----------|
| Firmware fill | `src/DeviceIdentity.cpp` |
| Health route | `src/ApiServer.cpp` — `GET /api/health` |
| Dev simulator | `server/index.ts` |
| Browser types | `src/types/deviceProfile.ts` |
| Android types | `RenzFi-Owner-App/.../HealthResponse.kt` |
| Zod validation | `server/contracts/deviceProfile.ts` |

---

## 7. Related (not DeviceProfile)

| Concept | Phase | Notes |
|---------|-------|-------|
| **FleetHealth** | 7B | Aggregated fleet status UI (online/degraded/offline per site). Not the admin dashboard. See [PHASE_7B_FLEET_HEALTH.md](../../PHASE_7B_FLEET_HEALTH.md). |
| **Cloud fleet sync** | 7B+ | Remote registry, not appliance-local. |
| **Shared vouchers / DB / SD** | 7B / 8 | Explicitly out of Phase 7A scope. |

---

**This document is frozen for Phase 7A.** Changes require explicit contract revision and cross-client updates.
