# Device Registry Architecture (Phase 7A — Frozen)

> **Phase 7A is frozen.** See [PHASE_7A_FREEZE.md](./PHASE_7A_FREEZE.md).  
> **DeviceProfile contract:** [ESP32_S3_Firmware/docs/DEVICE_PROFILE_CONTRACT.md](./ESP32_S3_Firmware/docs/DEVICE_PROFILE_CONTRACT.md)  
> **FleetHealth (7B):** [PHASE_7B_FLEET_HEALTH.md](./PHASE_7B_FLEET_HEALTH.md) — not implemented yet.

## Principle

Every Renz-Fi appliance remains **fully autonomous**. No clustering, no shared database, no master ESP32, and no inter-device communication.

The **only** component aware of multiple devices is the **client** (browser admin SPA or Android Owner App).

```
                 Browser / Mobile App
                         │
                  Device Registry
                         │
         ┌───────────────┼───────────────┐
         │               │               │
         ▼               ▼               ▼
    Reception       Second Floor      Cafeteria
 192.168.88.101   192.168.88.102   192.168.88.103
         │               │               │
         ▼               ▼               ▼
     ESP32 #1        ESP32 #2        ESP32 #3
```

ESP32s do not know each other exists.

## Device Profile (Firmware)

Each appliance exposes a permanent identity via `GET /api/health`:

```json
{
  "success": true,
  "data": {
    "ok": true,
    "deviceId": "RF-00EF01",
    "deviceName": "Reception",
    "version": "0.5.0-w5500",
    "device": {
      "deviceId": "RF-00EF01",
      "serialNumber": "DE:AD:BE:EF:00:01",
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
  }
}
```

- **deviceId** is derived from MAC (`RF-` + last 6 hex digits) — stable across DHCP IP changes.
- **friendlyName** comes from `settings.json` → `device.name`.
- **capabilities** tells clients which features to expose (coin, voucher, router driver, etc.).
- Legacy top-level `deviceId`, `deviceName`, and `version` fields support simple discovery clients.

Implementation: `ESP32_S3_Firmware/src/DeviceIdentity.cpp`.  
Contract: `ESP32_S3_Firmware/docs/DEVICE_PROFILE_CONTRACT.md` (**frozen**).

## Client Registry

| Client | Storage | Key |
|--------|---------|-----|
| Browser admin | `localStorage` | `deviceId` |
| Android Owner App | DataStore (JSON) | `applianceDeviceId` (+ legacy `id`) |

Registry entries are **never** stored on the ESP32.

## Browser Modes

### Direct Mode (default)

Same-origin SPA served from the appliance. `apiUrl()` resolves to relative paths. Behavior is identical to pre–Phase 7A.

### Fleet Mode

Activated when the user discovers or registers multiple devices. `setRuntimeApiBaseUrl("http://{ip}")` retargets all API calls. **ProvisioningClient is unchanged** — it uses `apiUrl()` indirectly.

Device switching:

1. Updates runtime base URL only.
2. Clears React Query cache.
3. Re-runs session bootstrap (re-login per appliance when cross-origin).
4. **Preserves the current route** (e.g. Dashboard stays Dashboard).

## Discovery

Both clients scan a configurable subnet (default `192.168.88.1–254`):

- `GET http://{ip}/api/health`
- Parse `device` profile
- Upsert registry by `deviceId`
- Update IP if DHCP changed

Browser: `src/services/deviceDiscovery.ts`  
Android: `DeviceRepository.discoverDevicesOnSubnet()`

## Offline Handling

Each registry entry tracks `isOnline` independently. One offline appliance does not affect others.

## Explicit Non-Goals (Phase 7A — frozen)

See [PHASE_7A_FREEZE.md](./PHASE_7A_FREEZE.md) for the full list. Summary:

- Shared SQLite / SD / assets / vouchers / sessions
- ESP32-to-ESP32 communication
- Cluster mode / leader election / sync engine

These belong to future cloud fleet services (Phase 7B+) if ever needed.

## Migration

Existing single-appliance deployments require **zero migration**. Direct Mode is the default. The current appliance auto-registers on first health check.
