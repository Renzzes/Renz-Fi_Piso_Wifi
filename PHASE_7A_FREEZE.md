# Phase 7A Freeze

Phase 7A (Device Registry & Device Switching) is **complete and frozen**.

## In scope (delivered)

- Client-side Device Registry (browser `localStorage`, Android DataStore)
- LAN discovery via `GET /api/health` → frozen [DeviceProfile](./ESP32_S3_Firmware/docs/DEVICE_PROFILE_CONTRACT.md)
- Runtime API base URL switching (fleet mode) without changing ProvisioningClient
- Per-appliance offline status in registry
- Capability flags in DeviceProfile for client UI adaptation

## Explicitly out of scope — do not add to Phase 7A

| Feature | Target phase |
|---------|----------------|
| Shared vouchers across appliances | 7B / 8 |
| Shared database / SQLite sync | 7B / 8 |
| Shared SD / shared assets | 7B / 8 |
| Remote command execution | 7B / 8 |
| Fleet-wide OTA orchestration | 7B / 8 |
| Broadcast commands to all devices | 7B / 8 |
| ESP32-to-ESP32 communication | Never (appliance architecture) |
| **FleetHealth** aggregated status UI | **7B** |

## Architecture rules (unchanged)

1. Every appliance remains **fully autonomous**.
2. Only the **client** knows about multiple devices.
3. Registry is keyed by **`deviceId`**, not IP.
4. `DeviceProfile` field names are **frozen** — extend only via `capabilities` and new optional keys per contract.

## Phase 7B preview

See [PHASE_7B_FLEET_HEALTH.md](./PHASE_7B_FLEET_HEALTH.md) for the planned fleet status layer (not the per-appliance dashboard).
