# Router Sync — Pre-Change Baseline

**Document purpose:** Rollback / reference point describing the system **as it exists before** the Router Synchronization optimization pass.

**Status:** Phase 0 — baseline only. **No behavior changes** are described as “done” here.

**Baseline git tip (working tree may have unrelated local mods):** `8695882` — `Fix CoinManager null pointer crash when disabled`  
**Branch:** `main` (tracking `origin/main`)  
**Firmware version constant:** `RenzFiConfig::FIRMWARE_VERSION` = `0.5.0-w5500`  
**Date of baseline capture:** 2026-08-16

**Important:** The Renz-Fi Core (coin, portal, sales, setup finish, production networking) is already operational on hardware. This baseline does **not** claim the product was broken. It documents how Admin router sync works today so a future optimization can be compared and rolled back.

---

## 1. Current architecture (high level)

```
MikroTik RouterOS  ←—— single API session / worker ——→  ESP32-S3 Renz-Fi Core
                                                          |
                    +-------------------------------------+-------------------------------------+
                    |                   |                 |                   |                 |
                 CoinManager      Portal/Sessions      Sales/Storage    InstallationState   RouterPlatform
                    |                   |                 |                   |                 |
                    v                   v                 v                   v                 v
                 Internet            Captive UI        SD/fallback      provisioned state   MikroTikDriver
                                                                                              |
                                                                                              v
                                                                                     RouterCacheManager
                                                                                              ^
Admin Dashboard (browser) —— REST/SSE —— ApiServer —— (202 enqueue) —— RouterProvisioningWorker
```

- **ESP32 = Core.** Coin, sessions, sales, portal, storage, installation state do not require Admin.
- **Admin = optional client.** Talks to Core via REST + SSE.
- **MikroTik = network engine.** RouterOS mutations and inventory for Admin go through **one** `RouterProvisioningWorker` FreeRTOS task (not `async_tcp`).

---

## 2. Current Admin → ESP32 API flow (router-related)

| UI action | Frontend | HTTP | HTTP response | Worker op |
|-----------|----------|------|---------------|-----------|
| Synchronize Router | `routerApi.syncRouter()` | `POST /api/router/cache/sync` | **202** + `jobId` | `OpType::AdminSyncCache` |
| Refresh Router Information | `routerApi.refreshCache()` | `POST /api/router/cache/refresh` | **202** + `jobId` | **Same** `AdminSyncCache` |
| Read cache | `routerApi.cache()` | `GET /api/router/cache` | 200 from RAM cache | none |
| Test Connection | `routerApi.test()` | `POST /api/router/test` | 202 | `AdminTestConnection` |
| Connect / login sync | `synchronizeAdminClient()` | `GET /api/status` then optional `syncRouter()` if `routerCache.stale` | status RAM; sync 202 | optional `AdminSyncCache` |

**Critical baseline fact:** UI labels distinguish “Synchronize” vs “Refresh,” but **both endpoints call the same** `enqueueAdminSyncCache()` → same `synchronizeRouterCache()` → same `MikroTikDriver::collectCacheSnapshot()`.

Files:

- `src/services/router.ts` — `syncRouter`, `refreshCache`
- `src/pages/SystemConfigurationPage.tsx` — buttons + toasts
- `src/pages/DashboardPage.tsx` — dashboard sync mutation
- `src/services/adminSync.ts` — post-login Core sync (status first; stale → worker)
- `ESP32_S3_Firmware/src/ApiServer.cpp` — routes ~2769–2798

---

## 3. Current ESP32 → RouterOS flow (Admin sync)

```
POST /api/router/cache/sync|refresh   [async_tcp]
  → requireOwnerAuth
  → RouterApiTransportGate::allowsAdminNonEssential()
  → RouterProvisioningWorker::enqueueAdminSyncCache(message)
  → sendAdminJobAccepted (202)

router_worker task  [FreeRTOS, affinity -1]
  → runOp(AdminSyncCache)
  → RouterPlatform::synchronizeRouterCache(false)
  → RouterPlatform::refreshRouterCache(false)
  → MikroTikDriver::collectCacheSnapshot(snap)
       openRouterSession
       … many /print (+ optional repair sets) …
       observeAndRepairWan
       closeRouterSession
  → RouterCacheManager::applyLiveSnapshot(snap)  [persist cache]
  → fillRouterCache → job HTTP 200 envelope
     OR fail → 503 ROUTER_CACHE_SYNC_FAILED

Admin UI polls GET /api/router/jobs/{id}
```

---

## 4. Current router worker architecture

| Item | Value / behavior |
|------|------------------|
| Class | `RouterProvisioningWorker` |
| Files | `RouterProvisioningWorker.cpp/.h` |
| Queue depth | `ROUTER_WORKER_QUEUE_DEPTH = 1` |
| Job timeout | `ROUTER_WORKER_JOB_TIMEOUT_MS = 20000` (20 s) |
| Job TTL | `ROUTER_WORKER_JOB_TTL_MS = 120000` |
| Stack | `RENZFI_ROUTER_WORKER_STACK_WORDS = 12288` (48 KB) |
| Core affinity | `ROUTER_WORKER_CORE_AFFINITY = -1` (unpinned) |
| Admin sync enqueue | `enqueueAdminSyncCache` — rejected if transport gate recovery blocks admin non-essential |
| Priority | Admin sync = **Normal** (not Critical) |
| Duplicate jobs | Single-slot queue; busy → enqueue rejected (`ROUTER_WORKER_BUSY`) |

Admin sync execution site: `RouterProvisioningWorker::runOp` case `AdminSyncCache` (~1462–1488).

---

## 5. Current `RouterCacheManager` behavior

| Function | File | Context | RouterOS | Storage | Blocks |
|----------|------|---------|----------|---------|--------|
| `isPopulated` | `RouterCacheManager.cpp` | any | no | no | no |
| `isStale` | same | any | no | no | no — age vs `ROUTER_CACHE_STALE_THRESHOLD_HOURS` (**24 h**) |
| `cacheAgeSeconds` | same | any | no | no | no |
| `applyLiveSnapshot` | same | **worker** after sync | no | **yes** (`writeJson` cache file) | yes (worker OK) |
| `fillPublic` / `fillCacheStatus` | same | HTTP GET / status | no | no | RAM only |
| Boot load | `begin` / load from `RouterCacheFile` | boot / loop | no | read | boot |

Stale rule: populated + ageSeconds > 24h → `stale=true`. Age 0 → not stale.

---

## 6. Current `/api/status` behavior

- File: `ApiServer.cpp` `GET /api/status`
- Context: **async_tcp**
- Auth: session required
- Data: RAM/cache snapshots — sales summaries, active users, mikrotik configured/host, `fillHealthStatus` / observation from **cache**, `fillRouterCacheStatus`, storage via `fillDashboardStatus` (snapshot)
- **Does not** open RouterOS
- **Does not** run `collectCacheSnapshot`

---

## 7. Current `/api/system/health` and `/api/health`

| Route | Behavior |
|-------|----------|
| `GET /api/health` | Storage snapshot + production handoff fields + optional router health from cache |
| `GET /api/system/health` | `SystemHealthService::fillHealth` — snapshot-based |
| `GET /api/storage/status` | `fillStorageStatus` from published snapshot |

Heavy SPIFFS walks (`fallbackTotalBytes`, spool/manifest) run in `StorageManager::refreshRuntimeSnapshot` on **loopTask**, throttled by `STORAGE_SNAPSHOT_HEAVY_INTERVAL_MS` (30 s). Status HTTP handlers must not re-walk.

---

## 8. Current “Refresh Router Information”

- UI: System Configuration — outline button “Refresh Router Information”
- API: `POST /api/router/cache/refresh` → **identical worker path** to Synchronize
- Toast: “Router information refreshed” / “Failed to refresh router information”
- **Baseline gap:** Label implies lightweight status refresh; implementation is full `collectCacheSnapshot` (+ WAN observe/repair).

---

## 9. Current “Synchronize Router”

- UI: “Synchronize Router” (Dashboard + System Configuration)
- API: `POST /api/router/cache/sync` → `AdminSyncCache`
- Success message string: `"Router synchronized"`
- Failure: job 503 / frontend toast **“Failed to synchronize router”**
- Failure is **all-or-nothing**: any `collectCacheSnapshot` false **or** `applyLiveSnapshot` false → entire sync failed. No `completed_with_warnings`.

---

## 10. Current RouterOS commands used by Admin cache sync

Source of truth: `MikroTikDriver::collectCacheSnapshot` (+ callees), file `MikroTikDriver.cpp` ~1912–2165.

| Command | Called from | Purpose today | Notes |
|---------|-------------|---------------|-------|
| session connect/login | `openRouterSession` | API session | Required for any sync |
| `/system/identity/print` | collectCacheSnapshot | identity | Config |
| `/system/resource/print` | collectCacheSnapshot | version, **cpu-load**, memory, uptime | Mixes config + telemetry |
| `/interface/wireless/print` | collectCacheSnapshot (if not canonical read) | iface, SSID, band, security-profile | Config |
| targeted wireless read | `RouterWireless::readInterface` | preferred when canonical iface known | Config |
| `/interface/wireless/security-profiles/print` | collectCacheSnapshot | security auth types | Config-ish |
| `RouterWireless::reconcileCaptiveHotspotPath` | collectCacheSnapshot | Hotspot captive path repair | May use `/interface/bridge/port/print`, hotspot profile get/set | **Mutating + diagnostic** |
| `/ip/hotspot/print` | if hotspot server empty | hotspot name/interface | Config |
| `/ip/hotspot/profile/print` | if html-directory empty | html-directory | Config |
| `/ip/hotspot/user/profile/print` | always | profiles + rate-limit | Config (UI) |
| `/interface/print` | `observeAndRepairWan` | ether1-WAN link | Telemetry / WAN |
| `/ip/dhcp-client/print` (+ optional set/add) | `observeAndRepairWan` | WAN DHCP | Telemetry + **possible repair** |
| `/ip/route/print` (`?dst-address=0.0.0.0/0`) | `observeAndRepairWan` | default route / temp route | Telemetry + **possible remove**; **known slow**; **one retry** |

Hardware observation (pre-optimization): full admin sync ~**23.5 s**; `/ip/route/print` particularly slow. Worker budget is **20 s** → timeout / failure is plausible even when RouterOS is reachable.

---

## 11. Current worker queue behavior

- One outstanding work slot (`QUEUE_DEPTH = 1`).
- Admin non-essential jobs deferred when `RouterApiTransportGate` is in recovery.
- Coin/portal Critical ops (activate/pause/deauth) share the same worker — Admin sync must not starve them by design of priority + gate, but a long Normal sync still occupies the single slot for its full duration.

---

## 12. Current timeout behavior

| Constant | Value | Role |
|----------|-------|------|
| `ROUTER_WORKER_JOB_TIMEOUT_MS` | 20000 | Soft deadline for job |
| `ROUTER_API_SENTENCE_TIMEOUT_MS` | 2000 | Per-sentence |
| `SETUP_ROUTER_CONNECT_TIMEOUT_MS` | 5000 | Connect (setup-oriented) |
| `SETUP_ROUTER_IO_TIMEOUT_MS` | 8000 | I/O budget |
| CPU pacing tiers | 100–500 ms delays | Between commands when CPU high |

If job exceeds deadline with empty failure body → `timeoutResult()`. Observed 23.5 s sync exceeds 20 s budget.

---

## 13. Current cache freshness behavior

- Fresh (< 24 h age): Admin connect does **not** enqueue sync (`adminSync.ts`).
- Stale: optional `POST …/cache/sync` from connect flow.
- Explicit button: always enqueues full sync (both buttons).
- Cache persist path: `applyLiveSnapshot` → durable `RouterCacheFile` via StorageManager (on **worker**, not async_tcp).

---

## 14. Current SSE sales behavior

- Persist sale first (`SessionManager::upsertSale` → `saveSalesBoundedLocked`).
- Then `EventBus::emit("sale.created" / "sales.changed")`.
- `EventBus::emit` no-ops when `_source->count() == 0`.
- Admin UI: targeted `setQueryData` on `sale.created` (`useDashboardEvents.ts`).
- Coin path does **not** call Admin sync APIs.

---

## 15. Current StorageManager fallback

- SD preferred; SPIFFS fallback when SD absent/unhealthy.
- Sales/history/config dual-path via existing `writeJson` / spool.
- Admin is a reader of Core APIs; does not own a second sales store.

---

## 16. Current TWDT protections (relevant)

| Protection | Status before this optimization task |
|------------|--------------------------------------|
| Admin login tip log | `AuthManager::login` uses `infoLocal` / `warnLocal` / `errorLocal` |
| Broader HTTP tip logs | Many ApiServer/portal tip paths use `*Local` |
| Durable `Logger::info/warn/error` | Still used on **loopTask/worker** (coin pulse, router worker, factory reset) |
| Heavy storage snapshot | Throttled 30 s on loopTask |
| Admin RouterOS | Worker only (202) — **must remain** |
| TWDT config itself | Unchanged; not a workaround lever |

Proven historical tip: `Login successful` → `appendHistory` → `flush` on async_tcp.

---

## 17. Current Admin / Core isolation rules

Documented in:

- `docs/ADMIN_CORE_ISOLATION.md`
- `.cursor/rules/admin-core-isolation.mdc`

Contract: `ESP32_S3_Firmware/tools/admin-core-isolation-contract-check.mjs`

Rules in force: no `/api/admin/sync`; no credentials to browser; connect = status + optional stale worker; Core independent of Admin.

---

## 18. Current Setup Wizard behavior (frozen)

Six steps; finish via worker `finish-setup-provisioning`. Deferred Wi-Fi persist:

`HTTP → RAM/QUEUED → FirmwareApp::loop → RouterProvisioningManager::loop → persist → PERSISTED`

Do not convert back to sync HTTP `writeJson`.

---

## 19. Current Factory Reset behavior

`POST /api/system/factory-reset` → 202 → `FactoryResetWorker` on loopTask; cancels provisioning persist; quiesces; clears files; Factory state; invalidate sessions; reboot.

---

## 20. Current Coin → Session → Sales flow

Coin interrupt → CoinManager → PortalSessionManager credit / done-paying → enqueue activation + enqueue sale → worker RouterOS authorize → StorageManager persist → SSE.

Independent of Admin open/closed.

---

## 21. Current Captive Portal flow

PortalServer / portal APIs / HotSpot captive path. Functional. Not part of this optimization scope.

---

## 22. Current W5500 behavior

Production Ethernet plane; Admin at `http://10.10.10.2/admin` when provisioned. Unchanged by this baseline.

---

## 23. Current InstallationStateManager

Authoritative installation lifecycle (`Factory` → … → `Provisioned`). Admin sync does not transition installation state. `markProvisioned` only when `refreshRouterCache(true)` (setup/finish paths), Admin sync passes `false`.

---

## 24. Current MikroTikDriver / RouterAdapter (RouterPlatform)

| Piece | Role |
|-------|------|
| `RouterPlatform` | Active driver facade + cache ops |
| `MikroTikDriver` | RouterOS API implementation |
| `IRouterDriver::collectCacheSnapshot` | Admin sync inventory entry |
| `RouterOsClient` | Single TCP API client used by worker jobs |
| No second Admin-only RouterOS client | |

---

## 25. Current HTTP route contracts (router Admin)

| Method | Path | Auth | Sync RouterOS? |
|--------|------|------|----------------|
| GET | `/api/router/settings` | owner | no (file/cache) |
| POST/PUT | `/api/router/settings` | owner | worker save |
| GET | `/api/router/profiles` | owner | cache |
| POST | `/api/router/profiles/refresh` | owner | worker |
| POST | `/api/router/profiles/op` | owner | worker |
| POST | `/api/router/test` | owner | worker |
| GET | `/api/router/wireless` | owner | cache |
| POST/PUT | `/api/router/wireless` | owner | worker |
| GET | `/api/router/cache` | owner | cache |
| POST | `/api/router/cache/sync` | owner | worker **full snapshot** |
| POST | `/api/router/cache/refresh` | owner | worker **same full snapshot** |
| GET | `/api/router/jobs/*` | owner | poll |

---

## 26. Existing automated contract tests

| Tool | Purpose |
|------|---------|
| `tools/admin-core-isolation-contract-check.mjs` | Admin isolation / sync semantics |
| `tools/async-tcp-blocking-audit-check.mjs` | Local logger / async_tcp tip safety |
| `tools/factory-reset-contract-check.mjs` | Factory reset 202/worker |
| `tools/setup-unlock-contract-check.mjs` | Setup unlock |
| `tools/setup-wizard-finish-provisioning-check.py` | Finish provisioning |
| `tools/setup-wizard-existing-network-check.py` | Existing network guards |
| `tools/esp32-mikrotik-stability-contract-check.mjs` | MikroTik stability |
| `tools/routeros-stability-contract-check.mjs` | RouterOS stability |
| (+ session/voucher/pause contracts) | Portal/session |

---

## 27. Current firmware build result (last known this workspace)

From recent `pio run -e freenove_esp32_s3_wroom` (prior session):

- **Result:** SUCCESS  
- **RAM:** ~32.8% (~107588 / 327680)  
- **Flash:** ~93.1% (~2440971 / 2621440)  

Re-measure after any future sync optimization; flash headroom is tight.

---

## 28. Current known residual risks

1. **Sync ≡ Refresh implementation** — both run full `collectCacheSnapshot` + WAN observe/repair.
2. **Workload breadth** — telemetry + diagnostics + optional repairs inside “cache sync.”
3. **`/ip/route/print` cost** — filtered default-route query with retry; can dominate duration.
4. **20 s job timeout vs ~23 s observed sync** — can surface as “Failed to synchronize router” despite RouterOS online.
5. **All-or-nothing failure** — optional field / repair failure can fail entire sync.
6. **CPU 100% display** — UI may show green/healthy with high `cpuLoad` from last snapshot (presentation issue).
7. **Single worker slot** — long Admin sync occupies worker used by Critical hotspot ops.
8. **Sync path can mutate RouterOS** — captive hotspot reconcile + WAN DHCP/route repair on “sync,” not read-only inventory.
9. **Residual sync `writeJson` on some Admin mutation HTTP routes** (settings/promos/etc.) — separate from router sync; documented earlier.
10. **No core-dump partition** in `partitions_custom.csv` — diagnosability only.

---

## Important functions (inventory)

| File | Class / API | Function | Purpose | Typical caller | Context | RouterOS | Storage | Can block |
|------|-------------|----------|---------|----------------|---------|----------|---------|-----------|
| `ApiServer.cpp` | ApiServer | cache sync/refresh lambdas | 202 enqueue | Admin UI | async_tcp | no | no | no |
| `RouterProvisioningWorker.cpp` | Worker | `enqueueAdminSyncCache` | queue job | ApiServer | async_tcp | no | no | brief |
| `RouterProvisioningWorker.cpp` | Worker | `runOp(AdminSyncCache)` | execute sync | worker task | worker | yes | via cache | yes |
| `RouterPlatform.cpp` | RouterPlatform | `synchronizeRouterCache` | alias → refresh | worker | worker | yes | yes | yes |
| `RouterPlatform.cpp` | RouterPlatform | `refreshRouterCache` | snapshot + apply | worker | worker | yes | yes | yes |
| `MikroTikDriver.cpp` | MikroTikDriver | `collectCacheSnapshot` | full inventory | refreshRouterCache | worker | yes | read provis. | yes |
| `MikroTikDriver.cpp` | MikroTikDriver | `observeAndRepairWan` | WAN observe/repair | collectCacheSnapshot | worker | yes | no | yes |
| `RouterWirelessAdapter.cpp` | RouterWireless | `reconcileCaptiveHotspotPath` | HS path repair | collectCacheSnapshot | worker | yes | no | yes |
| `RouterCacheManager.cpp` | RouterCacheManager | `applyLiveSnapshot` | update + persist cache | refreshRouterCache | worker | no | write | yes |
| `RouterCacheManager.cpp` | RouterCacheManager | `isStale` / `fillPublic` | freshness / API | status/UI | any | no | no | no |
| `adminSync.ts` | — | `synchronizeAdminClient` | post-login Core sync | App.tsx | browser | via optional API | no | N/A |
| `router.ts` | — | `syncRouter` / `refreshCache` | enqueue + poll job | pages | browser | via API | no | N/A |

---

## UI fields currently fed by router cache / status (baseline list)

**System Configuration — Status panel (examples):** Router Identity, RouterOS Version, Last Synchronization, Cache Age, Provision Status, Production Wi-Fi, Production SSID, Hotspot Profile, CPU, Memory, Uptime (+ connection list items).

**Dashboard:** mikrotik connectivity/host, routerCache identity/production Wi-Fi, WAN/internet from status observation, sales/storage/coin from Core.

**Wireless / Hotspot sections:** SSID, interface, band, security, profiles/rate-limits from cache + profile APIs.

*(Full A/B/C/D classification is Phase 1 — `docs/ROUTER_SYNC_DATA_CLASSIFICATION.md` — not started in Phase 0.)*

---

## Explicit non-goals of this baseline

- Does **not** change code.
- Does **not** redefine Synchronize vs Refresh yet.
- Does **not** remove `/ip/route/print` yet.
- Does **not** claim hardware validation of a future optimization.

---

## Next phases (planned, not executed)

| Phase | Deliverable | Code edits? |
|-------|-------------|-------------|
| 0 | This document | **No** |
| 1 | `docs/ROUTER_SYNC_DATA_CLASSIFICATION.md` | No |
| 2 | `docs/ROUTER_SYNC_FLOW_AUDIT.md` | No |
| 3+ | Minimal sync optimization | Yes, after 0–2 |

**STOP after Phase 0.** Implementation must not begin until Phases 1–2 are complete and accepted.
