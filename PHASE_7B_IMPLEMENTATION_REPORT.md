# Phase 7B — Fleet Health & Monitoring Implementation Report

**Date:** 2026-06-30  
**Status:** Complete  
**Depends on:** Phase 7A (frozen) — Device Registry, Device Profile, Fleet/Direct mode

---

## 1. Summary

Phase 7B delivers **client-computed Fleet Health** across browser and Android. Each registered appliance is probed via **`GET /api/health`** (no new protocol). The firmware returns **facts only**; health level, score, and warnings are computed on the owner client.

**Product rules preserved:**

- Fleet is an **owner/client** feature — not an appliance feature
- Each ESP32 remains **fully autonomous**
- Fleet **observes** and **switches** only — no shared sessions, vouchers, sales, SD, or ESP32-to-ESP32 communication

---

## 2. Architecture

```
                    Owner Client (Browser / Android)
                              │
                    Device Registry (Phase 7A)
                              │
              ┌───────────────┼───────────────┐
              │               │               │
         GET /api/health  GET /api/health  GET /api/health
              │               │               │
         ESP32 #1         ESP32 #2         ESP32 #3
         (autonomous)     (autonomous)     (autonomous)
```

| Layer | Responsibility |
|-------|----------------|
| **Firmware** | Expose appliance facts on `/api/health` (backward-compatible extensions) |
| **Client probe** | Poll each registered IP; parse health envelope |
| **Client compute** | Derive `healthy` / `warning` / `offline`, score, warnings |
| **Fleet Dashboard** | Display cards; click → switch active appliance (Phase 7A) |
| **SSE (active only)** | Live refresh for currently selected appliance |

No fleet registry on ESP32. No server-side health score.

---

## 3. Health Model

### 3.1 Data sources (per appliance)

| Field group | Source | Auth required |
|-------------|--------|---------------|
| Device identity | `data.device` (frozen DeviceProfile) | No |
| Build metadata | `data.build` | No |
| Storage | `data.storage` (extended) | No |
| Installation | `data.installation` (new optional block) | No |
| Router | `data.router` (new optional block) | No |
| Portal | `data.portal` (new optional block) | No |
| Coin | `data.coin` (new optional block) | No |
| Uptime / time | `data.uptimeSeconds`, `data.serverTimeMs` | No |
| Admin session hint | `data.session.authenticated` | No (cookie reflected when present) |
| Last seen | Client timestamp on successful probe | Client-only |

### 3.2 Firmware `/api/health` extensions (backward compatible)

**File:** `ESP32_S3_Firmware/src/ApiServer.cpp`

Added optional keys under existing `data` envelope — **no changes** to frozen `data.device` DeviceProfile fields:

- `storage` — merged `ok` + `StorageManager::fillStorageStatus()` + `spiffsReady`
- `installation` — `InstallationStateManager::fillStatus()`
- `router` — `configured`, `driverId`
- `portal` — `revision`, `hasBanner`, `hasMusic`
- `coin` — `enabled` + `CoinManager::fillCoinStatus()`
- `uptimeSeconds`, `serverTimeMs`

Dev simulator updated: `server/index.ts`

### 3.3 Client health levels

| Level | Emoji | Rules (client-side) |
|-------|-------|---------------------|
| **Offline** | 🔴 | Probe timeout, HTTP error, or `!device.online` |
| **Warning** | 🟡 | Reachable but degraded (see below) |
| **Healthy** | 🟢 | Online with no warnings |

**Warning triggers:**

- Storage not OK or SD fallback active
- SPIFFS not ready
- Installation needs setup (`needsSetup && !ready`)
- Router driver selected but not configured
- Portal incomplete after ready (no banner, revision 0)
- Coin enabled but fault / not ready

**Score:** Client-only 0–100 (100 healthy, 0 offline, warning scaled by warning count).

---

## 4. Health Calculation

**Browser:** `src/services/fleetHealthService.ts` — `computeFleetHealth()`, `buildFleetApplianceHealth()`

**Android:** `RenzFi-Owner-App/.../model/FleetHealth.kt` — `FleetHealthCalculator.compute()`

Rules are intentionally mirrored between platforms. No firmware score.

---

## 5. Fleet Dashboard (Browser)

**Route:** `/fleet`  
**Nav:** Admin sidebar → **Fleet Health**

| Feature | Implementation |
|---------|----------------|
| Summary tiles | Healthy / Warning / Offline counts |
| Appliance cards | `src/components/fleet/FleetApplianceCard.tsx` |
| Health colors + score | Client-computed |
| Expandable details | Build metadata, contracts, storage, installation, router, portal, coin |
| Click card / Switch | `DeviceRegistryContext.switchDevice()` → `/dashboard` |
| Discovery | Reuses Phase 7A subnet scan |
| Manual refresh | `refreshFleetHealth()` |
| Auto refresh | 15s / 30s / 60s (`localStorage` preference) |
| SSE live updates | `useFleetActiveDeviceEvents` — active appliance only |

**Registry integration:** `DeviceRegistryContext` extended with `fleetHealthByDevice`, `pollIntervalMs`, `refreshFleetHealth()`.

**Legacy:** `/devices` (Device Manager) retained for registry CRUD; links to Fleet Health.

---

## 6. Polling Strategy

| Mode | Behavior |
|------|----------|
| **Background poll** | All registered devices on configurable interval |
| **Manual refresh** | Immediate full fleet probe |
| **SSE** | Active appliance only — `installation.*`, `router.*`, `portal.changed`, `coin.*`, `storage.changed` |
| **Cross-origin** | Health probes use `credentials: omit` per device IP (Phase 7A) |

Default interval: **30 seconds**.

---

## 7. Browser Files

| File | Role |
|------|------|
| `src/types/fleetHealth.ts` | Types, poll intervals |
| `src/services/fleetHealthService.ts` | Probe parse + health rules |
| `src/services/fleetHealthPreferences.ts` | Poll interval persistence |
| `src/services/deviceHealth.ts` | `probeDeviceHealth()`, `refreshAllDeviceHealth()` |
| `src/contexts/DeviceRegistryContext.tsx` | Fleet health state |
| `src/pages/FleetDashboardPage.tsx` | Fleet Dashboard UI |
| `src/components/fleet/FleetApplianceCard.tsx` | Card + expandable details |
| `src/hooks/useFleetActiveDeviceEvents.ts` | SSE for active device |

---

## 8. Android

Device list screen is the **Fleet Dashboard** (multi-device entry point).

| File | Role |
|------|------|
| `model/FleetHealth.kt` | Types + calculator (mirrors browser) |
| `model/HealthResponse.kt` | Extended health JSON models |
| `data/repository/DeviceRepository.kt` | `refreshFleetHealth()` |
| `viewmodel/DeviceListViewModel.kt` | Fleet summary, poll interval, auto-refresh |
| `ui/components/FleetHealthBadge.kt` | 🟢🟡🔴 badge + score + last seen |
| `ui/components/DeviceCard.kt` | Fleet health on each card |
| `ui/screens/DeviceListScreen.kt` | Summary row + interval chips |
| `util/Constants.kt` | `FLEET_POLL_INTERVALS_MS` |

**Parity:** Same probe endpoint, same health rules, same poll options. Switch appliance → open WebView dashboard (Phase 7A).

---

## 9. Events

Browser subscribes to EventBus on **active appliance only** when Fleet Dashboard is mounted:

- `installation.state_changed`, `installation.completed`, `installation.aborted`
- `router.connected`, `router.disconnected`, `router.unavailable`
- `portal.changed` (firmware emits this for asset changes — not `asset.changed`)
- `coin.fault`, `coin.state.changed`, `coin.accepted`
- `storage.changed`

Other appliances continue polling on interval.

---

## 10. Frozen Contract Compliance

| Contract | Changed? |
|----------|----------|
| **DEVICE_PROFILE_CONTRACT** | **No** — `data.device` fields unchanged |
| **HTTP_ROUTE_CONTRACT** | **No** — same `GET /api/health` route |
| **INSTALLATION_WORKFLOW** | **No** |
| **PROVISIONING_API** | **No** |
| **SETUP_WIZARD_UI_CONTRACT** | **No** |
| **STORAGE_ARCHITECTURE** | **No** |
| **Phase 7A freeze** | **Honored** — registry, switching, autonomy |

Extensions are **additive optional keys** on the health envelope only — existing clients ignore unknown fields per contract extension rules.

---

## 11. Explicitly Not Implemented (per scope)

- OTA orchestration
- Remote commands / reboot / restart
- Remote configuration push
- Shared storage / vouchers / sessions
- ESP32-to-ESP32 communication
- Cloud fleet aggregation
- Authenticated per-device `/api/status` enrichment in fleet view (future optional)

---

## 12. Remaining Work for Phase 7C

| Item | Notes |
|------|-------|
| **Fleet trends / history** | Last-seen sparklines, downtime logs (client-local) |
| **Firmware version policy** | Configurable “expected firmware” → warning rule |
| **Authenticated enrichment** | Optional `/api/status` when admin session exists for cross-origin devices |
| **DeviceSelector header** | Show fleet health emoji in compact selector |
| **Android expandable details** | Match browser detail panel on card expand |
| **Export fleet report** | PDF/CSV owner report (client-generated) |
| **`/health` top-level alias** | Still reserved in HTTP contract — not required for 7B |

---

## 13. Verification Checklist

| Scenario | Expected |
|----------|----------|
| Single appliance, Direct Mode | Fleet page shows one card; health from same-origin health |
| Multi-appliance LAN | Each card probed independently; offline device → 🔴 |
| SD fallback | 🟡 with “SD fallback active” warning |
| Setup incomplete | 🟡 with installation state warning |
| Switch from fleet card | Active device changes; dashboard retargets API base |
| Active device SSE event | That card refreshes without waiting for poll |
| Android multi-device | Fleet Health list with badges; tap opens WebView |
| Factory-new appliance | Installation `factory` visible in expanded details |

---

## 14. Conclusion

Phase 7B completes the **owner-facing fleet monitoring layer** on top of frozen Phase 7A infrastructure. Appliances remain independent; the client aggregates health for visibility only. Frozen contracts are unchanged; `/api/health` remains the single probe endpoint with backward-compatible fact extensions.

**Ready for Phase 7C** enhancements without revisiting registry, profile, or provisioning architecture.
