# Router Sync Phase 3 — Pre-Implementation Checkpoint

**Phase:** 3A — Checkpoint only  
**Date:** 2026-08-16  
**Status:** COMPLETE — **NO FIRMWARE / ADMIN CODE CHANGED IN PHASE 3A**

This document is the implementation gate for Phase 3B. Code changes must not begin until this checkpoint is accepted.

---

## 1. Verified baseline

| Item | Value |
|------|--------|
| Git tip (HEAD) | `8695882` (`869588220a6421413fcaefb35c16720e9cea1e71`) |
| Branch | `main` (tracking `origin/main`) |
| Firmware version | `0.5.0-w5500` |
| Phase 0 | `docs/ROUTER_SYNC_PRE_CHANGE_BASELINE.md` |
| Phase 1 | `docs/ROUTER_SYNC_DATA_CLASSIFICATION.md` |
| Phase 2 | `docs/ROUTER_SYNC_FLOW_AUDIT.md` |
| Admin isolation | `docs/ADMIN_CORE_ISOLATION.md`, `.cursor/rules/admin-core-isolation.mdc` |

**Working tree note:** Unrelated prior uncommitted changes may exist in the workspace. Phase 3 must not overwrite, revert, or reformat them. Phase 3 edits must be scoped to the Sync/Refresh optimization only.

**Authoritative architecture:** Phase 2 flow audit. Do not re-audit from scratch; implement the documented separation.

---

## 2. Current architecture (freeze)

```
HTTP (async_tcp): auth + enqueue only → 202 + jobId
        ↓
RouterProvisioningWorker (single FreeRTOS task, queue depth 1)
        ↓
RouterPlatform
        ↓
MikroTikDriver + one RouterOsClient session per job
        ↓
RouterCacheManager::applyLiveSnapshot → save() on worker
```

**Frozen (do not redesign):** RouterProvisioningWorker, RouterPlatform, MikroTikDriver, RouterOsClient, RouterCacheManager, InstallationStateManager, StorageManager, Portal*, Coin*, Session/Sales/SSE, W5500, FactoryResetWorker, Setup Wizard, TWDT, async_tcp safety contracts.

---

## 3. Current routes (preserve names)

| Method | Path | Today |
|--------|------|--------|
| POST | `/api/router/cache/sync` | `enqueueAdminSyncCache("Router synchronized")` |
| POST | `/api/router/cache/refresh` | `enqueueAdminSyncCache("Router information refreshed")` |
| GET | `/api/router/cache` | RAM cache read |
| GET | `/api/router/jobs/{id}` | Job poll |

**Do not** create `/api/admin/sync`. **Do not** rename routes.

---

## 4. Current call chains

### Synchronize Router

```
SystemConfigurationPage / DashboardPage
  → routerApi.syncRouter()
  → POST /api/router/cache/sync
  → enqueueAdminSyncCache
  → OpType::AdminSyncCache
  → synchronizeRouterCache(false) ≡ refreshRouterCache(false)
  → MikroTikDriver::collectCacheSnapshot()
  → applyLiveSnapshot + save
```

### Refresh Router Information

```
SystemConfigurationPage
  → routerApi.refreshCache()
  → POST /api/router/cache/refresh
  → enqueueAdminSyncCache   ← SAME op
  → SAME collectCacheSnapshot (full)
```

### Login (stale cache)

```
auth → GET /api/status → if stale → syncRouter() → SAME full AdminSyncCache
```

---

## 5. Current worker / session architecture

| Item | Value |
|------|--------|
| Op type | `OpType::AdminSyncCache` only for both buttons |
| Queue | Depth 1 |
| Job budget | `ROUTER_WORKER_JOB_TIMEOUT_MS = 20000` — **do not increase** |
| Session | One open/close per job inside `collectCacheSnapshot` |
| Callers of `collectCacheSnapshot` | Only via `RouterPlatform::refreshRouterCache` ← AdminSyncCache |
| Callers of `synchronizeRouterCache` | Only AdminSyncCache worker case |
| Default `IRouterDriver::collectCacheSnapshot` | Stub/fallback; MikroTik overrides |

**Safe to mode-split:** No Setup/finish caller of `collectCacheSnapshot` found. Setup uses other worker ops / engines.

---

## 6. Current Synchronize / Refresh behavior

Both run the **full** snapshot including:

- identity, resource (version + CPU/mem/uptime)
- wireless + security profiles
- **`reconcileCaptiveHotspotPath` (may mutate)**
- hotspot print, profile html, user profiles
- **`observeAndRepairWan`** (DHCP/route/ping/repair)

---

## 7. Current expensive operations (must leave Sync/Refresh)

| Operation | Action in Phase 3B |
|-----------|-------------------|
| `/ip/route/print` (+ retry) | Stop invoking from Sync/Refresh |
| `/ip/route/remove` | Stop invoking from Sync/Refresh |
| DHCP repair set/add | Stop invoking from Sync/Refresh |
| `/ping` (WAN) | Stop invoking from Sync/Refresh |
| `observeAndRepairWan()` entire call | Stop invoking from Sync/Refresh |
| `reconcileCaptiveHotspotPath()` | Stop invoking from Sync/Refresh |

**Do not delete** `observeAndRepairWan` or `reconcileCaptiveHotspotPath` functions — retain for future Diagnostics/Repair.

---

## 8. Current timeout behavior

- Soft deadline 20s via `RouterApiTransportGate::beginJob`
- Mid-command may abort with `job_deadline_exceeded`
- Soft post-check only if `!ok && body.empty()`
- Late success not overwritten
- **Phase 3 fix = less work, not larger timeout**

---

## 9. Current cache persistence

- Worker: `applyLiveSnapshot` → `RouterCacheManager::save()` → `writeJson(RouterCacheFile)`
- Must remain worker-side
- Refresh must not corrupt configuration cache if telemetry fails

---

## 10. Current failure semantics

- Binary: success vs `ROUTER_CACHE_SYNC_FAILED` / timeout codes
- Coarse: optional/diagnostic work can inflate duration → false fail while ROS online
- Phase 3: Sync fails only on required config; Refresh fails softly without wiping config

---

## 11. Target behavior (Phase 3B)

### A. Synchronize (`/cache/sync`) — Configuration mode

1. Credentials + open session  
2. Identity  
3. Resource (version; telemetry may be collected opportunistically but Sync purpose is config)  
4. Wireless iface / SSID / band (+ security profile when needed)  
5. Light `/ip/hotspot/print` (read-only)  
6. User profiles + rate-limit  
7. `applyLiveSnapshot` + save  
8. Close session  

**Not run:** route/WAN repair/ping/captive reconcile/html-directory chase beyond necessity.

### B. Refresh (`/cache/refresh`) — Telemetry mode

1. Open session  
2. Resource → CPU / memory / uptime (+ version)  
3. Optional light hotspot status (read-only)  
4. Patch telemetry/observation into cache **without** mutating RouterOS  
5. Persist if safe; on failure keep prior config  

**Not run:** route, WAN repair, captive reconcile, profile full inventory (unless already trivial — prefer not).

### C. Diagnostics / Repair

Not implemented as new UI in Phase 3. Keep functions available internally.

### D. Login stale path

Use **configuration** minimal sync (same as Synchronize), not full snapshot.

---

## 12. Planned implementation method (smallest)

Preferred: enum/mode on snapshot collection, e.g.:

```text
enum class AdminCacheSyncMode { Configuration, Telemetry };
```

- `enqueueAdminSyncCache` → split into `enqueueAdminSyncCache` (config) + `enqueueAdminRefreshCache` (telemetry), **or** same enqueue with mode flag / separate `OpType::AdminRefreshCache`
- `RouterPlatform::synchronizeRouterCache` / new `refreshRouterTelemetry` OR mode arg into `refreshRouterCache`
- `MikroTikDriver::collectCacheSnapshot(out, mode)` — Configuration vs Telemetry branches; shared session open/close
- `ApiServer`: sync route → config enqueue; refresh route → telemetry enqueue
- `adminSync.ts` stale → config sync only
- UI labels/messages clarified; buttons unchanged

Avoid large abstraction hierarchies.

---

## 13. Files expected to change (Phase 3B)

| File | Why |
|------|-----|
| `MikroTikDriver.h` / `.cpp` | Mode-aware snapshot; skip WAN/reconcile on Sync/Refresh |
| `IRouterDriver.h` / possibly `.cpp` | Virtual signature if mode added (minimal) |
| `RouterPlatform.h` / `.cpp` | Pass mode / separate refresh telemetry entry |
| `RouterProvisioningWorker.h` / `.cpp` | Distinct op or mode for sync vs refresh |
| `ApiServer.cpp` | Wire sync→config, refresh→telemetry |
| `src/services/router.ts` | Only if messages/polling need clarity (optional) |
| `SystemConfigurationPage.tsx` / `DashboardPage.tsx` | Toast copy only if needed |
| `src/services/adminSync.ts` | Ensure stale uses config sync |
| `tools/*contract*.mjs` | New/extended Sync≠Refresh contracts |
| `docs/ROUTER_SYNC_PHASE3_IMPLEMENTATION_REPORT.md` | After 3B |

Only modify what is necessary.

---

## 14. Files / systems explicitly forbidden

- CoinManager, PortalServer, PortalSessionManager (architecture/behavior)
- SessionManager / sales SSE architecture
- Setup Wizard / SetupServer / finish engine redesign
- FactoryResetWorker
- RouterProvisioningManager deferred Wi-Fi persist design
- InstallationStateManager redesign
- W5500
- StorageManager fallback redesign
- TWDT configuration
- Captive Portal UX
- Production network finish gates
- Increasing `ROUTER_WORKER_JOB_TIMEOUT_MS`
- Creating `/api/admin/sync` or second RouterOS client

**Stop conditions:** If implementation appears to require any of the above → STOP and report.

---

## 15. Exact rollback strategy

1. Revert Phase 3B commits / file diffs for the files listed in §13.  
2. Leave Phase 0/1/2 docs intact.  
3. Restore prior behavior: both buttons → `enqueueAdminSyncCache` → full `collectCacheSnapshot`.  
4. Re-run contract suite + `pio run -e freenove_esp32_s3_wroom`.  
5. Do not revert unrelated pre-existing working-tree changes.

---

## 16. Phase 3 acceptance criteria

| # | Criterion |
|---|-----------|
| 1 | Routes `/cache/sync` and `/cache/refresh` unchanged |
| 2 | Sync = configuration-only RouterOS reads |
| 3 | Refresh = telemetry-only, read-only (no ROS mutation) |
| 4 | Sync/Refresh do **not** call `/ip/route/print`, `observeAndRepairWan`, or `reconcileCaptiveHotspotPath` |
| 5 | Those functions remain in tree for future diagnostics |
| 6 | Worker + single session architecture preserved |
| 7 | Cache save remains on worker |
| 8 | No RouterOS / durable writeJson / durable Logger on async_tcp |
| 9 | Login stale path uses config sync only |
| 10 | Core (coin/portal/sales/setup/factory) untouched |
| 11 | Contract tests updated and pass |
| 12 | Firmware build succeeds |
| 13 | **Not claimed fixed on hardware until flashed and matrix run** |

---

## 17. Contract / build plan (Phase 3B)

- Extend/add router sync mode contracts (sync≠refresh; forbidden commands absent on those paths)
- Run: `admin-core-isolation`, `async-tcp-blocking-audit`, `factory-reset`, `setup-unlock`, setup-wizard python checks
- Build: `pio run -e freenove_esp32_s3_wroom`
- Do not claim hardware success from compile alone

---

## 18. Phase 3A gate

**STOP.** Phase 3B implementation must wait for explicit approval after this checkpoint.

No firmware source was modified to create this document.
