# Router Synchronization Flow Audit

**Phase:** 2 — Flow audit / documentation only  
**Date:** 2026-08-16  
**Status:** COMPLETE — **NO CODE WAS CHANGED IN PHASE 2**

---

## 1. Purpose

Trace the **existing** Admin router synchronization path in exact execution order. Identify where time, RouterOS CPU pressure, and failure risk originate. Propose a **conceptual** separation of Synchronize / Refresh / Diagnostics for Phase 3+ — **without implementing any of it**.

This is not a claim that Core is broken. Core is operational. This audit targets Admin sync efficiency and resilience only.

---

## 2. Protected Baseline

| Artifact | Reference |
|----------|-----------|
| Phase 0 | [`docs/ROUTER_SYNC_PRE_CHANGE_BASELINE.md`](ROUTER_SYNC_PRE_CHANGE_BASELINE.md) |
| Phase 1 | [`docs/ROUTER_SYNC_DATA_CLASSIFICATION.md`](ROUTER_SYNC_DATA_CLASSIFICATION.md) |
| Git tip | `8695882` |
| Firmware | `0.5.0-w5500` |

Phase 1 classifications (A/B/C/D/E) are treated as authoritative unless this audit finds contradictory source evidence.

**NO CODE CHANGED IN PHASE 2.**

---

## 3. Current End-to-End Flow

```
Admin browser
  │
  ├─ Synchronize Router ──────────┐
  │                               │
  └─ Refresh Router Information ──┤
                                  ▼
                    routerApi.syncRouter()  OR  routerApi.refreshCache()
                                  │
                                  ▼
         POST /api/router/cache/sync   OR   POST /api/router/cache/refresh
                                  │  async_tcp: auth + enqueue only
                                  ▼
                         HTTP 202 + jobId
                                  │
                                  ▼
              RouterProvisioningWorker::enqueueAdminSyncCache(message)
                                  │
                                  ▼
                     OpType::AdminSyncCache  (router_worker)
                                  │
                                  ▼
              RouterPlatform::synchronizeRouterCache(false)
                ≡ refreshRouterCache(false)
                                  │
                                  ▼
              MikroTikDriver::collectCacheSnapshot(snap)
                ├─ session
                ├─ identity / resource / wireless / security
                ├─ reconcileCaptiveHotspotPath (optional mutate)
                ├─ hotspot / profile / user profiles
                └─ observeAndRepairWan (observe + repair + route + ping)
                                  │
                                  ▼
              RouterCacheManager::applyLiveSnapshot(snap)
                └─ save() → StorageManager::writeJson(RouterCacheFile)
                                  │
                                  ▼
              Job result → GET /api/router/jobs/{id} → Admin toast
```

---

## 4. Synchronize Router Flow

| Step | Location | Action |
|------|----------|--------|
| 1 | `SystemConfigurationPage.tsx` / `DashboardPage.tsx` | Button → `syncRouterMutation.mutate()` |
| 2 | `src/services/router.ts` | `syncRouter()` → `enqueueAdminRouterJob(.../cache/sync)` |
| 3 | `ApiServer.cpp` | `POST /api/router/cache/sync` → `routerCacheSynchronize(req, "Router synchronized")` |
| 4 | Same | `requireOwnerAuth` → `enqueueAdminSyncCache("Router synchronized")` |
| 5 | `RouterProvisioningWorker` | Queue depth 1; reject if busy / recovery gate |
| 6 | Worker `runOp(AdminSyncCache)` | `synchronizeRouterCache(false)` |
| 7 | On success | Envelope message = `"Router synchronized"` (or custom) |
| 8 | On failure | `503` body `ROUTER_CACHE_SYNC_FAILED` / `"Unable to synchronize router information from RouterOS"` |
| 9 | Frontend | Poll job; toast success `"Router synchronized"` or error `"Failed to synchronize router"` |

---

## 5. Refresh Router Information Flow

| Step | Location | Action |
|------|----------|--------|
| 1 | `SystemConfigurationPage.tsx` | Outline button → `handleRouterCacheRefresh` → `refreshCacheMutation` |
| 2 | `src/services/router.ts` | `refreshCache()` → `enqueueAdminRouterJob(.../cache/refresh)` |
| 3 | `ApiServer.cpp` | `POST /api/router/cache/refresh` → `routerCacheSynchronize(req, "Router information refreshed")` |
| 4 | Same | **Identical** `enqueueAdminSyncCache(...)` |
| 5–8 | Worker | **Same** `AdminSyncCache` → **same** `collectCacheSnapshot` |
| 9 | Frontend | Toast `"Router information refreshed"` / `"Failed to refresh router information"` |

### Why they behave identically

Both routes share one lambda (`routerCacheSynchronize`) that only differs by the **success message string** passed into `enqueueAdminSyncCache`. There is **no** separate op type, **no** lighter snapshot path, and **no** flag skipping WAN/reconcile.

---

## 6. Exact `collectCacheSnapshot` Call Graph

Source: `MikroTikDriver::collectCacheSnapshot` (`MikroTikDriver.cpp` ~1912–2165).

| # | Operation | RouterOS command(s) | Data | Cat | Required? | Mutation? | Retry? | Failure | UI consumer |
|---|-----------|---------------------|------|-----|-----------|-----------|--------|---------|-------------|
| 1 | Load credentials | none (storage) | host/user/pass/profile | E | Yes | No | No | **return false** | — |
| 2 | Read provisioning hints | none (storage read) | bridge, selectedWireless, seeded identity/version | E | No | No | No | continue | seeds cache |
| 3 | Set routerIp + hotspotProfile | none | host, profileName | A/E | Yes | No | No | — | cache |
| 4 | `openRouterSession` | TCP + login | session | A | **Yes** | No | Gate/backoff | **return false** | — |
| 5 | Identity | `/system/identity/print` | identity | A | Prefer yes | No | No | best-effort | Dashboard / Status |
| 6 | Resource | `/system/resource/print` | version + cpu/mem/uptime | A+B | version yes; telemetry no | No | No | best-effort | Version A; CPU/Mem/Uptime B |
| 7a | Canonical wireless read | targeted wireless print via `RouterWireless::readInterface` | iface/ssid/security/band | A | Yes if configured | No | No | fall through 7b | Wireless summary |
| 7b | Wireless inventory | `/interface/wireless/print` | iface/ssid/band/sec-profile | A | If 7a miss | No | No | best-effort | Wireless |
| 8 | Security profile | `/interface/wireless/security-profiles/print` | auth types → security | A/D | Prefer | No | No | `unknown` | Open vs password UI |
| 9 | Bridge hint copy | none | bridge from provisioning | E | No | No | No | — | cache bridge |
| 10 | Captive reconcile | see §8 | hotspotServer/bridge/html + **possible repairs** | C+repair | **No** for display-only | **Yes** | internal | log error; continue | Hotspot name/iface |
| 11 | Hotspot list (if empty) | `/ip/hotspot/print` | hotspotServer, iface | A/B | Prefer | No | No | best-effort | Hotspot status |
| 12 | Hotspot profile html (if empty) | `/ip/hotspot/profile/print` | htmlDirectory | D | No | No | No | best-effort | rarely shown |
| 13 | User profiles | `/ip/hotspot/user/profile/print` | profiles + rate-limit | A | Yes for Hotspot/Promo UI | No | No | best-effort empty | Hotspot profiles |
| 14 | Observation base | none | connectivity=online, hotspotStatus | B | No | No | No | — | Status list |
| 15 | `observeAndRepairWan` | see §7 | wan.* | B+C | **No** for config | **Yes** possible | route×2 | best-effort | WAN/Internet |
| 16 | `closeRouterSession` | disconnect | — | — | — | No | No | — | — |
| 17 | return | — | — | — | — | — | — | **true** unless #1/#4 failed | — |

**Critical code fact:** After a successful session open, nearly all subsequent failures are **best-effort**. `collectCacheSnapshot` still **returns true** and proceeds to `applyLiveSnapshot`.

---

## 7. `observeAndRepairWan` Call Graph

Called unconditionally at end of `collectCacheSnapshot` (same open session). Comment: Sync/Test only; ~3–6 targeted commands intended.

| # | Step | Command | Observation? | Repair? | Cat | Config sync required? | Core required? | Can delay? | Can raise ROS CPU? |
|---|------|---------|--------------|---------|-----|----------------------|----------------|------------|-------------------|
| 1 | WAN iface | `/interface/print` `?name=ether1-WAN` | Yes | No | B | No | No | Low–mod | Low–mod |
| 2 | DHCP clients | `/ip/dhcp-client/print` | Yes | No | B | No | No | Mod | Mod |
| 3 | DHCP ensure/set/add | `/ip/dhcp-client/set` or `/add` | No | **Yes** | C | No | No* | Mod | Mod |
| 4 | Address (as used in path) | `/ip/address/print` (when used) | Yes | No | B | No | No | Low–mod | Low |
| 5 | Default routes | `/ip/route/print` `?dst-address=0.0.0.0/0` | Yes | No | **C**/B | **No** | No | **HIGH** (HW) | **HIGH** possible |
| 6 | Route retry | same print once more | Yes | No | C | No | No | **HIGH** | **HIGH** |
| 7 | Remove temp route | `/ip/route/remove` | No | **Yes** | C | No | No | Mod | Mod |
| 8 | Budget check | `remainingJobBudgetMs()` | — | — | — | — | — | — | — |
| 9 | Ping 8.8.8.8 count=1 | `/ping` | Yes | No | B | No | No | Mod; **skipped** if budget &lt; 3s | Mod |

\*Core production networking uses Setup/finish paths, not this Admin Sync helper.

**Admin Network UI** (Mode/IP/GW/Mask/DNS/MAC) reads **ESP32** `wifiConfig` / health — **not** these MikroTik WAN fields.

---

## 8. `reconcileCaptiveHotspotPath` Call Graph

Entry: `RouterWireless::reconcileCaptiveHotspotPath` → `reconcileHotspotCaptivePathImpl`  
When: Inside `collectCacheSnapshot` when wireless iface (canonical or selected) is non-empty.

### Reads (typical)

| Command / helper | Purpose |
|------------------|---------|
| `/interface/bridge/port/print` (via `bridgeHasInterface`) | Detect bridged topology |
| `/ip/hotspot/print` (via `loadHotspotRows`) | Inventory hotspot servers |
| `/ip/hotspot/profile/print` (+ ensure html-directory) | Profile / html path |
| `/ip/address/print` (create path) | Guest address CIDR |

### Writes / mutations (conditional)

| Action | Commands | When |
|--------|----------|------|
| Enable hotspot | `/ip/hotspot/set` | Disabled on required iface |
| Move to bridge | `/ip/hotspot/set` interface=bridge | Bridged but HS on wireless slave |
| Retarget managed HS | `/ip/hotspot/set` | Managed name wrong iface |
| Ensure html-directory | profile set | html missing/wrong |
| Create hotspot | `/ip/hotspot/add` | No suitable server |

### Required?

| Consumer | Required on every Admin sync? |
|----------|-------------------------------|
| Admin display of hotspot name | Prefer light `/ip/hotspot/print` only |
| Coin / session authorize | **No** — Critical worker hotspot user/active APIs |
| Captive Portal after provisioned | Path should already be correct from **Setup/finish**; reconcile is **repair** |
| Production after setup complete | **Should not be required** for normal sync if finish succeeded |

**Risk:** Admin “Synchronize” can **mutate** HotSpot/bridge-related RouterOS state. That is powerful for recovery but **expensive** and inappropriate for a lightweight Refresh. Phase 3 must not remove repair capability from the product — only stop invoking it from normal Sync/Refresh without an explicit diagnostics/repair action (design only).

On failure: collectCacheSnapshot logs `hotspotCaptiveRepairError` and **continues** (does not return false).

---

## 9. `RouterCacheManager::applyLiveSnapshot` Flow

| Step | Behavior |
|------|----------|
| Null snap | return false |
| Merge string fields | routerIp, identity, version, wireless, ssid, security, band, bridge, hotspot*, provision* |
| Merge arrays | profiles, profileDetails |
| Merge `routerOs` object | includes CPU/mem/uptime |
| Merge `observation` | connectivity, hotspotStatus, **wan** object |
| `stampSynchronized()` | millis + optional wall-clock ISO |
| `save()` | `StorageManager::writeJson(RouterCacheFile, _doc, true)` on **worker** |
| Return | `save()` boolean |

| Question | Answer |
|----------|--------|
| RAM updated? | Yes, `_doc` |
| Persist? | Yes, durable cache file (SD or SPIFFS fallback) |
| Blocking? | Yes — `writeJson` on worker (OK vs async_tcp) |
| Partial snap accepted? | Yes — `copyStringField` skips empties; optional fields may be absent |
| Optional field fail snapshot? | Individual missing ROS fields do **not** fail apply; **save()** failure does |
| Can produce ROUTER_CACHE_SYNC_FAILED? | Yes — if `applyLiveSnapshot` returns false (`synchronizeRouterCache` false) |
| `isPopulated` after apply | true if `routerIp` or `provisionStatus` non-empty (routerIp set early in snapshot) |

Worker durable `Logger::info`/`warn` after apply runs on **worker** (not async_tcp) — allowed.

---

## 10. Worker Timeout Interaction

| Item | Detail |
|------|--------|
| Constant | `RenzFiConfig::ROUTER_WORKER_JOB_TIMEOUT_MS = 20000` (`Config.h`) |
| Start | `runOp`: `deadline = millis() + 20000`; `RouterApiTransportGate::beginJob(opToken, deadline)` |
| Soft post-check | After op: if `millis() > deadline && !result.ok && result.body.isEmpty()` → `timeoutResult()` (`504` / `ROUTER_JOB_TIMEOUT`) |
| Hard abort? | **No cooperative cancel of whole op.** Job runs to completion of `runOp`. |
| Mid-command | `RouterOsClient` / gate honor **job deadline** on reads (`job_deadline_exceeded`) — in-flight sentences can fail when past deadline |
| Ping | Explicitly skips if `remainingJobBudgetMs() < 3000` |
| Late success | If snapshot+apply finish **after** 20s but `ok=true`, soft check does **not** overwrite success |
| Admin poll | Frontend polls up to **90s** (`ADMIN_JOB_MAX_POLLS`) — not the 20s failure source |
| Session cleanup | `closeRouterSession` at end of snapshot; gate `endJob` after `runOp` |

**Implication:** A ~23.5s wall-clock sync can still **succeed** if `syncOk` is true. Failures observed with long syncs are more likely **mid-flight deadline aborts**, **session/connect failure under CPU pressure**, or **`applyLiveSnapshot`/`save` failure** — not merely “clock hit 20s then force fail after success.” Soft timeout only fills empty failed bodies.

Do **not** raise the timeout as the optimization strategy.

---

## 11. Failure Model

| Code | Mechanism | Likelihood vs HW story |
|------|-----------|------------------------|
| **A** | Session/connect/auth fail (`collectCacheSnapshot` false) | High under ROS CPU / network stress |
| **B** | Soft `ROUTER_JOB_TIMEOUT` (empty failed body past deadline) | Lower if body already set |
| **C** | `applyLiveSnapshot` / `save` false | Possible (storage); not proven as HW root |
| **D** | Mid-job deadline → command failures → still often `return true` with partial data | Partial success silently possible |
| **E** | Enqueue reject (`ROUTER_WORKER_BUSY` / recovery gate) | Immediate 503; different toast path |
| **F** | Frontend poll 90s timeout | Unlikely at ~23s |

**Most consistent explanation with Phase 0 evidence (ROS online, slow `/ip/route/print`, ~23.5s, fail toast):**  
Broad sync spends budget on Category C WAN/route work → job deadline / sentence failures and/or session degradation → `synchronizeRouterCache` returns false **or** job reports failure envelope → Admin `"Failed to synchronize router"` **even though RouterOS is reachable**. Exact A vs deadline-induced A needs hardware logs in Phase 3 validation (`[router-sync] ok=no reason=...`, `[wan]`, job http status).

`ROUTER_CACHE_SYNC_FAILED` is **coarse**: it does not distinguish “required config OK, diagnostics timed out.”

---

## 12. RouterOS CPU Risk

| Operation | ROS CPU risk | ESP32 CPU risk | Blocking risk | Role | Normal Sync? (desired) |
|-----------|--------------|----------------|---------------|------|------------------------|
| identity | Low | Low | Worker | A | Yes |
| resource | Low–mod | Low | Worker | A+B | Version yes; telemetry → Refresh |
| wireless targeted | Mod | Low | Worker | A | Yes |
| wireless full print | Mod | Low | Worker | A | If needed |
| security-profiles | Mod | Low | Worker | A/D | Prefer yes |
| user profiles | Mod | Low | Worker | A | Yes |
| hotspot print | Low–mod | Low | Worker | A/B | Light yes |
| hotspot profile html | Low–mod | Low | Worker | D | No |
| captive reconcile | **High** | Mod | Worker + **mutate** | C | **No** (Diagnostics) |
| interface WAN | Low–mod | Low | Worker | B | Refresh/diag |
| dhcp print/repair | Mod | Low | Worker + mutate | B/C | **No** repair on sync |
| **route print + retry** | **High** (HW) | Low | Worker | C | **No** |
| route remove | Mod | Low | Mutate | C | Diagnostics |
| ping | Mod | Low | Worker | B | Refresh/diag; budget skip |
| applyLiveSnapshot/save | N/A | Mod (FS) | Worker storage | E | After success |
| HTTP enqueue | N/A | Low | async_tcp brief | — | Always |

Timings: only `/ip/route/print` has Phase 0 HW qualitative evidence (slow; ~23.5s total). Others: **Not measured.**

---

## 13. ESP32 / async_tcp / TWDT Risk

| Concern | Current state |
|---------|---------------|
| RouterOS on async_tcp | **No** — 202 + worker |
| SD write on async_tcp for sync | **No** — cache `save()` on worker |
| Login tip logs | `*Local` (prior hardening) |
| Long Admin sync occupying worker | **Yes** — single queue slot; Critical coin activate can wait |
| TWDT from sync itself | Low if kept on worker; risk rises if future code moves ROS/SD to HTTP |
| Raising TWDT timeout | Forbidden workaround |

---

## 14. Current vs Desired Operation Separation

### CURRENT

```
Synchronize Router  ─┐
                     ├──► AdminSyncCache ──► collectCacheSnapshot
Refresh Information ─┘         │
                               ├── Required configuration
                               ├── Telemetry (CPU/mem/uptime)
                               ├── WAN observation
                               ├── WAN repair
                               ├── Route print/retry
                               ├── Captive path reconciliation (mutate)
                               └── One job budget / one success-or-fail
```

### PROPOSED CONCEPT (documentation only)

```
Synchronize Router Configuration
        ↓
AdminSyncCache-scoped config job (same worker, same client)
        ↓
ONLY required configuration (Phase 1 Category A)
        ↓
applyLiveSnapshot (config fields)
        ↓
Success / Failed_Required

Refresh Router Status
        ↓
Light status job (same worker architecture)
        ↓
ONLY observational telemetry (Category B)
        ↓
Patch observation + routerOs telemetry in cache
        ↓
No repair / no route table / no captive mutate

Diagnostics / Repair (explicit Admin action — future)
        ↓
observeAndRepairWan + reconcileCaptiveHotspotPath
        ↓
Expensive / mutating operations on demand
```

---

## 15. Minimal Future Synchronization Set

**(Design only — do not implement in Phase 2.)**

1. Connect + login (one session)  
2. `/system/identity/print`  
3. `/system/resource/print` — use **version** (CPU/mem/uptime may be ignored or not requested if API allows; if one print is cheapest, still classify telemetry as non-blocking)  
4. Targeted wireless read (or bounded wireless print) → iface, SSID, band  
5. Security-profile print for configured profile (if UI needs Open vs protected)  
6. Light `/ip/hotspot/print` (name/iface) — **read-only**  
7. `/ip/hotspot/user/profile/print` (name, rate-limit)  
8. `applyLiveSnapshot` + save  

**Exclude:** route print/retry/remove, DHCP repair, ping, captive reconcile mutations, bridge repairs.

---

## 16. Minimal Future Refresh Set

**(Design only.)**

1. Connect + login (reuse worker / single client — no second architecture)  
2. `/system/resource/print` → CPU, memory, uptime (+ version refresh)  
3. Optional light hotspot print → available/unavailable  
4. Optional light connectivity stamp (`observation.connectivity`)  
5. Optional **read-only** WAN iface/DHCP status **without** repair/route/ping — or defer WAN entirely to Diagnostics  

**Must not:** mutate RouterOS; run captive reconcile; run `/ip/route/print`.

---

## 17. Diagnostic / Repair Set

**(Design only — explicit user action.)**

- Full `observeAndRepairWan` (route, DHCP ensure, temp route remove, ping)  
- `reconcileCaptiveHotspotPath`  
- Any broader inventory  

---

## 18. Session Reuse / Worker Architecture

| Question | Answer |
|----------|--------|
| Second RouterOS client needed? | **No** |
| New `/api/admin/sync`? | **No** |
| How to scope ops? | New `OpType` values **or** flags on `AdminSyncCache` / separate enqueue helpers, still using `MikroTikDriver` + one worker + one `RouterOsClient` per job |
| Same session within a job? | Yes — today’s pattern (one open, many commands, one close) remains correct |
| Light refresh without second connection architecture? | Yes — shorter command list on existing worker job |

Safest direction: **keep** `RouterProvisioningWorker` + `RouterPlatform` + `MikroTikDriver`; **narrow** what each job type asks the driver to do.

---

## 19. Core Isolation Verification

| Core area | Affected by Sync/Refresh separation? |
|-----------|--------------------------------------|
| CoinManager / pulse | No |
| SessionManager / sales / SSE | No |
| Captive Portal / PortalServer | No (repair move to Diagnostics must remain available separately) |
| Setup / InstallationState / finish | No |
| FactoryResetWorker | No |
| W5500 | No |
| TWDT / async_tcp contracts | Must remain worker-only |
| StorageManager sales fallback | No |

**Rule:** Admin sync failure must never stop Piso WiFi Core.

---

## 20. Failure Semantics (future design — not implemented)

| Class | Examples | Future behavior |
|-------|----------|-----------------|
| REQUIRED | Session; essential wireless; profile list (when configured) | Fail sync |
| NON-BLOCKING | CPU/mem/uptime; WAN; route; ping; reconcile | Warning or omit |
| LOCAL | Cache age; ESP32 network; sales | Never fail sync |

---

## 21. Future Partial-Success Design (not implemented)

Possible job statuses (conceptual):

| Status | Meaning |
|--------|---------|
| `SUCCESS` | Required A fields collected + cache saved |
| `SUCCESS_WITH_WARNINGS` | Required OK; B/C incomplete |
| `FAILED_REQUIRED` | Session or required config missing |

Today only binary success vs `ROUTER_CACHE_SYNC_FAILED` / timeout codes — too coarse when C ops inflate duration.

---

## 22. Phase 3 Recommendation

1. Keep routes `POST /api/router/cache/sync` and `.../refresh` (compatibility).  
2. Differ **implementation**: sync = minimal config snapshot; refresh = light telemetry (no repair/route/reconcile).  
3. Do **not** increase `ROUTER_WORKER_JOB_TIMEOUT_MS` as the fix.  
4. Do **not** remove repair from the product — relocate to explicit diagnostics later if needed.  
5. Preserve worker / single client / no `/api/admin/sync`.  
6. Add contract tests that sync path does not call `/ip/route/print` / reconcile (after implementation).  
7. Hardware-validate: sync duration, no TWDT, coin with Admin open/closed, setup/factory unchanged.  

**Do not start Phase 3 without explicit approval.**

---

## 23. Open Questions / Hardware Validation

1. On a failing sync, is job `httpStatus` 503 (`ROUTER_CACHE_SYNC_FAILED`) or 504 (`ROUTER_JOB_TIMEOUT`)?  
2. Does `openRouterSession` fail under 100% ROS CPU, or do mid-command deadlines abort after open?  
3. Does `save()` ever fail on the observed hardware?  
4. After a “failed” sync, is partial cache still updated (silent true return)?  
5. Is captive reconcile ever **required** post-provision for a healthy site, or only when misconfigured?  
6. Can resource print be split so Refresh gets telemetry without Sync paying for WAN?

---

## Appendix — Login (unchanged)

```
Admin login → auth (*Local logs)
           → synchronizeAdminClient
           → GET /api/status (RAM/cache)
           → if stale && credentials → syncRouter() [today: FULL AdminSyncCache]
           → Dashboard
```

Future: stale path should enqueue **config-minimal** sync, not full C/repair bundle. Fresh cache: **no** RouterOS job.

---

## Appendix — Validation (Phase 2)

| Check | Result |
|-------|--------|
| Code modified? | **No** |
| Firmware build? | **Not run** (not authorized) |
| Flash? | **No** |
| Optimization complete? | **No** |
