# Router Sync Phase 3B — Implementation Report

**Phase:** 3B — Configuration Sync vs Telemetry Refresh  
**Date:** 2026-08-16  
**Status:** SOFTWARE VERIFIED (compile + contracts). **Not** hardware-verified.  
**Baseline checkpoint:** `docs/ROUTER_SYNC_PHASE3_PRE_IMPLEMENTATION.md` (Phase 3A approved)

---

## 1. Verdict

Admin **Synchronize Router** and **Refresh Router Information** no longer run the same broad RouterOS snapshot.

| Operation | Route | Mode | Worker op |
|-----------|-------|------|-----------|
| Synchronize Router | `POST /api/router/cache/sync` | `RouterCacheCollectMode::Configuration` | `OpType::AdminSyncCache` |
| Refresh Router Information | `POST /api/router/cache/refresh` | `RouterCacheCollectMode::Telemetry` | `OpType::AdminRefreshCache` |
| Login stale cache | `routerApi.syncRouter()` → sync route | Configuration | `AdminSyncCache` |

`observeAndRepairWan()` and `reconcileCaptiveHotspotPath()` remain in the tree for future Diagnostics/Repair. They are **not** invoked by Sync or Refresh.

**Do not claim:** Router CPU fixed, Guru Meditation fixed, or production Sync/Refresh timing proven on hardware until flashed and tested.

---

## 2. Exact files changed

| File | Change |
|------|--------|
| `ESP32_S3_Firmware/src/router/IRouterDriver.h` | `RouterCacheCollectMode` enum; mode param on `collectCacheSnapshot` (pre-wired in 3A start) |
| `ESP32_S3_Firmware/src/router/IRouterDriver.cpp` | Stub accepts mode |
| `ESP32_S3_Firmware/src/router/drivers/MikroTikDriver.h` | Override signature with mode |
| `ESP32_S3_Firmware/src/router/drivers/MikroTikDriver.cpp` | Shared `collectCacheSnapshot(out, mode)` — Configuration vs Telemetry branches |
| `ESP32_S3_Firmware/src/router/RouterPlatform.h` | `refreshRouterTelemetry()` declaration |
| `ESP32_S3_Firmware/src/router/RouterPlatform.cpp` | Config collect in `refreshRouterCache` / `synchronizeRouterCache`; new `refreshRouterTelemetry()` |
| `ESP32_S3_Firmware/src/RouterProvisioningWorker.h` | `OpType::AdminRefreshCache`; `enqueueAdminRefreshCache` |
| `ESP32_S3_Firmware/src/RouterProvisioningWorker.cpp` | Enqueue + `runOp` cases for sync vs refresh |
| `ESP32_S3_Firmware/src/ApiServer.cpp` | Sync → config enqueue; Refresh → telemetry enqueue |
| `ESP32_S3_Firmware/src/RouterCacheManager.cpp` | Observation merge uses `as<>` (preserve prior observation fields on telemetry patch) |
| `src/pages/SystemConfigurationPage.tsx` | Toast wording only |
| `ESP32_S3_Firmware/tools/router-sync-refresh-contract-check.mjs` | New Phase 3B contracts |
| `docs/ROUTER_SYNC_PHASE3_IMPLEMENTATION_REPORT.md` | This report |

**Not modified:** Coin*, Portal*, Setup*, FactoryReset*, W5500, TWDT config, StorageManager architecture, `ROUTER_WORKER_JOB_TIMEOUT_MS`, Phase 0/1/2 docs.

---

## 3. Exact functions changed / added

| Function | Role |
|----------|------|
| `MikroTikDriver::collectCacheSnapshot(out, mode)` | One session; branch by mode |
| `RouterPlatform::refreshRouterCache` | Configuration mode collect + `applyLiveSnapshot` |
| `RouterPlatform::synchronizeRouterCache` | Alias → `refreshRouterCache` |
| `RouterPlatform::refreshRouterTelemetry` | Telemetry mode collect + `applyLiveSnapshot`; fail without wiping prior config |
| `RouterProvisioningWorker::enqueueAdminSyncCache` | Unchanged purpose (config) |
| `RouterProvisioningWorker::enqueueAdminRefreshCache` | **New** — telemetry job |
| `RouterCacheManager::applyLiveSnapshot` | Observation merge no longer clears prior observation object |

---

## 4. Sync mode (Configuration)

**Purpose:** Bring Admin-required router configuration/state cache up to date.

**Shared:** credentials → `openRouterSession` → `/system/resource/print` → mode branch → `closeRouterSession` → platform `applyLiveSnapshot` + worker-side `save()`.

**Configuration branch collects:**

1. Identity (`/system/identity/print`)
2. Resource (version; CPU/mem/uptime also present from shared read)
3. Wireless interface / SSID / band / security (canonical read or `/interface/wireless/print` + security profile)
4. Light HotSpot (`/ip/hotspot/print` name/interface/disabled)
5. HotSpot user profiles + rate limits (`/ip/hotspot/user/profile/print`)
6. Light observation (connectivity + hotspot status)

Then: `RouterCacheManager::applyLiveSnapshot()` → worker `save()`.

---

## 5. Refresh mode (Telemetry)

**Purpose:** Lightweight, **read-only** RouterOS telemetry.

**Telemetry branch collects:**

1. Resource → CPU, memory, uptime, version
2. Optional light HotSpot status (`/ip/hotspot/print`)

No wireless inventory, no user-profile inventory, no mutations.

On collect failure: platform returns false **without** calling apply — prior configuration retained.

---

## 6. RouterOS commands removed from normal Sync/Refresh

| Command / operation | Sync | Refresh |
|---------------------|------|---------|
| `/ip/route/print` (+ retry) | Removed | Removed |
| `/ip/route/remove` | Removed | Removed |
| DHCP repair set/add | Removed | Removed |
| `/ping` (WAN) | Removed | Removed |
| `observeAndRepairWan()` | Not called | Not called |
| `reconcileCaptiveHotspotPath()` | Not called | Not called |
| HotSpot create / retarget | Not called | Not called |
| Bridge mutation | Not called | Not called |
| HotSpot profile `html-directory` chase | Removed | N/A |
| Full profile inventory | Kept on Sync | Not run on Refresh |

---

## 7. Functions retained for future Diagnostics / Repair

| Function | Location | Status |
|----------|----------|--------|
| `MikroTikDriver::observeAndRepairWan` | `MikroTikDriver.cpp` | Retained; still used by Admin **Test Connection** path |
| `RouterWireless::reconcileCaptiveHotspotPath` | `RouterWirelessAdapter.cpp` | Retained; not called from Sync/Refresh |

No new Diagnostics UI was added in this phase.

---

## 8. Before / after call chain

### Before

```
POST /cache/sync  ─┐
POST /cache/refresh┤→ enqueueAdminSyncCache → AdminSyncCache
                   → synchronizeRouterCache ≡ refreshRouterCache
                   → collectCacheSnapshot()  [full]
                      → reconcileCaptiveHotspotPath
                      → observeAndRepairWan (/ip/route/print, DHCP, ping, repair)
                   → applyLiveSnapshot + save
```

### After

```
POST /cache/sync
  → enqueueAdminSyncCache → AdminSyncCache
  → synchronizeRouterCache
  → collectCacheSnapshot(Configuration)
  → applyLiveSnapshot + save

POST /cache/refresh
  → enqueueAdminRefreshCache → AdminRefreshCache
  → refreshRouterTelemetry
  → collectCacheSnapshot(Telemetry)   [read-only]
  → applyLiveSnapshot + save (merge; no wipe on failure)

Login stale → routerApi.syncRouter() → Configuration path only
```

---

## 9. Before / after operation count (approximate)

| Path | Before (Phase 2 audit) | After Phase 3B |
|------|------------------------|----------------|
| Sync | ~identity + resource + wireless + captive reconcile (multi) + hotspot + html profile + user profiles + WAN observe/repair (route/DHCP/ping) — often **>20s** vs 20s budget | ~resource + identity + wireless (+sec) + hotspot + user profiles — **configuration-only** |
| Refresh | Same as Sync | ~resource + light hotspot — **2 command groups** |

Exact ROS command counts vary by wireless path (canonical vs print+security). Measurable win: WAN repair + captive reconcile + html chase removed from both buttons.

---

## 10. Timeout behavior

- `ROUTER_WORKER_JOB_TIMEOUT_MS = 20000` — **unchanged**
- Soft deadline via `RouterApiTransportGate::beginJob` — unchanged
- Fix strategy: less work, not a larger timeout

---

## 11. Cache behavior

- Persistence remains **worker-side** via `applyLiveSnapshot` → `save()` → `writeJson(RouterCacheFile)`
- No cache write from `async_tcp`
- Sync: full configuration field merge (existing `copyStringField` semantics)
- Refresh: telemetry/`routerOs` + light observation; string fields only overwrite when non-empty; observation merges without clearing prior object
- Refresh collect failure: no `applyLiveSnapshot` → prior valid configuration retained

---

## 12. Failure behavior

| Mode | Failure | Result |
|------|---------|--------|
| Sync | Required session / persist failure | Job fails (`ROUTER_CACHE_SYNC_FAILED`) |
| Sync | Optional fields missing (e.g. no wireless) | Still succeeds if session + cache populate |
| Refresh | Resource telemetry failure | Job fails (`ROUTER_CACHE_REFRESH_FAILED`); **cache not wiped** |
| Refresh | HotSpot status optional miss | Still succeeds if resource OK |

---

## 13. Contract results

| Suite | Result |
|-------|--------|
| `node tools/router-sync-refresh-contract-check.mjs` | **20/20 PASS** |
| `node tools/async-tcp-blocking-audit-check.mjs` | **10/10 PASS** |
| `node tools/admin-core-isolation-contract-check.mjs` | **16/16 PASS** |
| `node tools/factory-reset-contract-check.mjs` | **24/24 PASS** |
| `node tools/setup-unlock-contract-check.mjs` | **7/7 PASS** |

Covered explicitly: routes preserved; Configuration vs Telemetry; no route print / WAN repair / captive reconcile on Sync/Refresh; Refresh read-only mutators absent; worker intact; no `/api/admin/sync`; login stale → config sync; worker-side persistence; 20s timeout unchanged; repair helpers retained.

---

## 14. Build result

```
pio run -e freenove_esp32_s3_wroom
→ SUCCESS (~50s)
RAM ~32.8% / Flash ~93.1%
```

Compilation success ≠ hardware success.

---

## 15. Remaining risks

1. **Hardware timing** of Sync/Refresh not measured on device yet.
2. Sync no longer auto-repairs captive HotSpot path or WAN — intentional; repair requires future Diagnostics or existing Test path (Test still calls `observeAndRepairWan`).
3. Telemetry Refresh stamps `lastSynchronizedAt` via shared `applyLiveSnapshot` (pre-existing stamp behavior) — may mark cache “fresh” after telemetry-only update; stale login gate uses age threshold + credentials.
4. Flash utilization remains high (~93%); this phase did not grow architecture.

---

## 16. Hardware test plan

1. Flash `freenove_esp32_s3_wroom` firmware to appliance.
2. Admin login with **fresh** cache → Dashboard; confirm no unexpected worker job.
3. Admin login with **stale** cache + credentials → only Configuration Sync job; Serial shows `[router-sync]` not WAN/captive repair.
4. **Synchronize Router** → completes under 20s; identity/SSID/profiles update; no `/ip/route/print` / reconcile logs.
5. **Refresh Router Information** → CPU/mem/uptime update; Serial `[router-refresh]`; configuration fields preserved if briefly offline afterward and refresh fails.
6. Confirm Core coin/session/portal still operate with Admin closed.
7. Optional: force RouterOS slow/unavailable — Sync fails cleanly; Refresh fails without clearing SSID/profiles.

Only after steps 4–5 succeed on hardware may Sync/Refresh timing claims be elevated to **HARDWARE VERIFIED**.

---

## 17. Rollback instructions

1. Revert the files listed in §2 (git restore / reverse patch).
2. Rebuild `freenove_esp32_s3_wroom`.
3. Flash previous firmware binary if already deployed.
4. No schema migration was introduced; `router-cache.json` remains compatible.

---

## 18. Final principle check

- Same HTTP routes, same worker, one RouterOS session per job, `RouterCacheManager`, worker-side persistence, existing auth/UI buttons.
- Shared `collectCacheSnapshot` with mode — not two duplicated full snapshots.
- Core isolation boundaries untouched.
- Optimization only — no redesign of Setup / Portal / Coin / Factory Reset.

**SOFTWARE VERIFIED.** Stop here pending hardware flash and validation.
