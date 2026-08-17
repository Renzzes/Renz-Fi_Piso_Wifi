# Phase 7B — FleetHealth

> **Status:** Implemented — see [PHASE_7B_IMPLEMENTATION_REPORT.md](./PHASE_7B_IMPLEMENTATION_REPORT.md)

## Purpose

**FleetHealth** is a **fleet-level status view** — not the per-appliance admin dashboard.

It answers: *"How is my entire estate doing right now?"*

## Example UI

```
Reception     🟢  2 active users
Hall          🟡  Router reconnecting
Dormitory     🔴  Offline
```

## Distinction

| Surface | Scope | Phase |
|---------|-------|-------|
| Admin Dashboard | Single appliance (sales, users, promos, …) | Existing |
| Device Registry / Selector | Pick target appliance | 7A |
| **FleetHealth** | Aggregated health across all registered appliances | **7B** |

## Data sources (7B design sketch)

FleetHealth is **client-computed** from lightweight probes — same autonomy rule as 7A:

- `GET /api/health` per appliance (online + DeviceProfile)
- Optional `GET /api/status` when authenticated (active user count, router state)
- No central server required for LAN-only fleet view
- Cloud aggregation optional later (7B+)

## Status levels (proposed)

| Level | Meaning | Example trigger |
|-------|---------|-----------------|
| 🟢 Healthy | Online, core services OK | `device.online === true`, router OK |
| 🟡 Degraded | Reachable but partial failure | Router reconnecting, SD fallback |
| 🔴 Offline | Unreachable | Health probe timeout |

## Out of scope for 7B FleetHealth spec

- Shared vouchers / shared database
- Remote execution / broadcast OTA
- ESP32 clustering

These remain Phase 7B cloud features or Phase 8 if ever needed.

## Dependencies

- Frozen [DeviceProfile contract](./ESP32_S3_Firmware/docs/DEVICE_PROFILE_CONTRACT.md) (Phase 7A)
- Client Device Registry (Phase 7A)
