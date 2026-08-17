# Phase 7C.2 — Management AP Lifecycle

> **Status:** Implemented  
> **Date:** 2026-06-30  
> **Builds on:** [MANAGEMENT_AP_ARCHITECTURE.md](./MANAGEMENT_AP_ARCHITECTURE.md) (Phase 7C.1)

---

## Objective

Define **when** the Management AP starts and stops, without redesigning networking, provisioning, router platform, or portal logic.

---

## Lifecycle Overview

```
┌─────────────┐
│  Power ON   │
└──────┬──────┘
       │
       ▼
┌─────────────────────┐     needsSetup() == true
│  Factory / Setup    │────────────────────────────► mode = factory
│  Management AP ON   │                              (never times out)
└──────────┬──────────┘
           │ installation complete (Complete screen)
           ▼
    ┌──────────────┐
    │ User choice  │
    └───┬──────┬───┘
        │      │
 disable│      │keep enabled
        │      │
        ▼      ▼
   mode=     mode=
  disabled  maintenance
   AP OFF    AP ON
        │      │
        │      └──► 600s inactivity after last admin session
        │                    │
        │                    ▼
        │              AP stops (mode=disabled)
        │
        └──► on reboot: AP stays off (unless maintenance API used)

Post-setup reboot with keep_enabled preference:
  ► AP starts in maintenance mode on boot
```

---

## State Transitions

| From | Event | To | AP running |
|------|-------|-----|------------|
| Boot (factory) | `applyBootPolicy()` | `factory` | Yes |
| `factory` | Finish + disable after setup | `disabled` | No |
| `factory` | Finish + keep enabled | `maintenance` | Yes |
| `disabled` | `POST …/management-ap/start` | `maintenance` | Yes |
| `maintenance` | `POST …/management-ap/stop` | `disabled` | No |
| `maintenance` | 600s inactivity, no admin sessions | `disabled` | No |
| Boot (ready, keep_enabled=true) | `applyBootPolicy()` | `maintenance` | Yes |
| Boot (ready, keep_enabled=false) | `applyBootPolicy()` | `disabled` | No |

`managementAp.mode` is derived from installation state + runtime AP state:

- **factory** — installation needs setup and AP is running
- **maintenance** — installation complete, AP running
- **disabled** — AP not running

---

## Persistence

Stored in existing `NetworkSettingsManager` (`renz-network` NVS + `settings.json` → `network.managementAp.keepEnabledAfterSetup`).

| Field | Default | Meaning |
|-------|---------|---------|
| `keepEnabledAfterSetup` | `false` | Disable AP after setup (recommended) |

Set by `POST /api/system/management-ap/post-setup` from the Complete screen.

No new persistence layer. No changes to `ProvisioningEngine` or installation workflow files.

---

## APIs (`/api/system` only)

| Method | Path | Auth | Action |
|--------|------|------|--------|
| `GET` | `/api/system/network` | Yes | Unified network model (unchanged path, extended fields) |
| `POST` | `/api/system/management-ap/post-setup` | Yes | `{ "keepEnabled": bool }` — apply Complete screen choice |
| `POST` | `/api/system/management-ap/start` | Yes | Start maintenance AP (default 600s timeout) |
| `POST` | `/api/system/management-ap/temporary` | Yes | `{ "durationSeconds": 600 }` — temporary maintenance (Owner App / Android) |
| `POST` | `/api/system/management-ap/stop` | Yes | Stop maintenance AP |

All routes use `ManagementApLifecycle` → `ManagementApManager::start()` / `stop()`. Ethernet and customer sessions are never modified.

---

## Network Model (`GET /api/system/network`)

```json
{
  "interfaces": {
    "managementAp": {
      "mode": "maintenance",
      "running": true,
      "enabled": true,
      "ssid": "RenzFi-Setup-RF-FE1191",
      "ip": "192.168.4.1",
      "clients": 1,
      "uptimeSeconds": 327,
      "timeoutSeconds": 600
    },
    "ethernet": { "link": true, "ip": "10.40.0.2", "...": "..." }
  },
  "managementAp": { "...alias..." : "..." },
  "ethernet": { "...alias..." : "..." }
}
```

- `timeoutSeconds` = **600** in maintenance mode only (firmware-owned constant)
- `timeoutSeconds` = `null` in factory and disabled modes
- Clients compute: `remaining = timeoutSeconds - uptimeSeconds`

---

## Timeout Behavior

| Mode | Timeout |
|------|---------|
| **factory** | Never — AP stays on until setup completes |
| **maintenance** | 600 seconds after **all** admin sessions end |
| **disabled** | N/A |

Rules (implemented in `ManagementApLifecycle::loop()`):

1. If any authenticated admin session is active → **do not** stop AP; inactivity timer paused
2. When last session ends → inactivity countdown starts
3. After 600 seconds with no sessions → `ManagementApManager::stop()`
4. Active admin work on Ethernet does not affect customer portal sessions

---

## Factory Behavior

- On boot, if `InstallationStateManager::needsSetup()` → AP starts automatically
- SSID: `RenzFi-Setup-{DeviceId}`, open, `192.168.4.1`
- No timeout while installer is in the Setup Wizard
- HTTP server starts when AP is up even if Ethernet has no link

---

## Maintenance Behavior

- Entered when installer chooses **Keep enabled** on Complete screen, or via `POST …/start`
- On reboot, resumes automatically only if `keepEnabledAfterSetup` was persisted
- Stopped manually via `POST …/stop` or automatically after inactivity timeout
- Ethernet continues independently; captive portal unaffected

---

## Browser Changes

**Only** the Complete screen (`CompleteScreen.tsx`):

- Management Wi-Fi choice:
  - **Disable after setup** (recommended, default)
  - **Keep enabled**
- User clicks **Complete setup** → `POST /api/provisioning/finish` then `POST /api/system/management-ap/post-setup`
- No other setup screens modified

---

## Android (documentation only)

Future Owner App flow:

```
Owner App → Enable Maintenance
  ↓
POST /api/system/management-ap/temporary { "durationSeconds": 600 }
  ↓
AP starts (mode = maintenance)
  ↓
GET /api/system/network → uptimeSeconds, timeoutSeconds
  ↓
Show "Maintenance Mode — MM:SS remaining"
```

`POST /api/system/management-ap/start` remains available (same behavior, default 600s).
No Android code in Phase 7C.2.

---

## Future GPIO Button Integration (reserved)

A future PCB revision may add a dedicated GPIO button to enter maintenance mode without the admin UI:

```
Physical button press (future)
  ↓
ManagementApLifecycle::startMaintenance()
  ↓
mode = maintenance, AP on, 600s inactivity timer
```

Not implemented in this phase. `ManagementApLifecycle` is the intended hook point.

---

## Component Ownership

| Component | Role |
|-----------|------|
| `ManagementApManager` | Public API unchanged — `start()`, `stop()`, `fillStatus()` |
| `ManagementApLifecycle` | Boot policy, post-setup preference, timeout, API orchestration |
| `NetworkSettingsManager` | Persists `keepEnabledAfterSetup` |
| `AuthManager::hasActiveSessions()` | Blocks timeout while admin is logged in |
| `NetworkStatusModel` | Builds `interfaces` + top-level aliases |
| `CompleteScreen` | Installer choice UI |

---

## Files Added

| File |
|------|
| `ESP32_S3_Firmware/src/ManagementApLifecycle.h` |
| `ESP32_S3_Firmware/src/ManagementApLifecycle.cpp` |
| `PHASE_7C2_MANAGEMENT_AP_LIFECYCLE.md` |

## Files Modified

| File | Change |
|------|--------|
| `ManagementApConfig.h` | `MAINTENANCE_TIMEOUT_SECONDS = 600` |
| `ManagementApManager.cpp` | Boot start delegated to lifecycle; `start`/`stop` set `enabled` |
| `Models.h` | `managementApKeepEnabledAfterSetup` on `NetworkSettings` |
| `NetworkSettingsManager.cpp` | NVS + JSON persistence |
| `AuthManager.h/.cpp` | `hasActiveSessions()` |
| `NetworkStatusModel.h/.cpp` | `interfaces` envelope + lifecycle `timeoutSeconds` patch |
| `ApiServer.h/.cpp` | Management AP routes |
| `FirmwareApp.h/.cpp` | Wire lifecycle |
| `src/pages/setup/screens/CompleteScreen.tsx` | Management Wi-Fi choice |
| `src/services/system.ts` | API client methods |
| `server/routes/system.ts` | Simulator parity |

## Frozen (unchanged)

RouterPlatform, ProvisioningEngine/Server/Client, Device Profile, Fleet, Captive Portal, customer sessions, Setup Wizard structure (except Complete screen choice), `ManagementApManager` public method signatures.

---

## Success Criteria

| Scenario | Result |
|----------|--------|
| Factory boot | AP starts, `mode=factory` |
| Complete + disable | AP stops, `mode=disabled` |
| Complete + keep enabled | AP stays on, `mode=maintenance` |
| Maintenance inactivity 600s | AP stops automatically |
| Admin session active | AP not stopped |
| Ethernet connected | Unaffected |
| Customer portal | Unaffected |
| Provisioning workflow | Unchanged |
