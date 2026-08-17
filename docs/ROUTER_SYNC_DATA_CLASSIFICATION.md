# Router Synchronization Data Classification

**Phase:** 1 — Documentation / audit only  
**Date:** 2026-08-16  
**Status:** COMPLETE — **NO CODE WAS CHANGED IN PHASE 1**

---

## 1. Purpose

This document classifies every Admin-visible RouterOS-related field and every RouterOS command executed by the current Admin cache synchronization path (`collectCacheSnapshot` + callees).

It exists because hardware showed:

- MikroTik online, HotSpot/production Wi-Fi healthy  
- Admin **Synchronize Router** → “Failed to synchronize router”  
- Worker budget **20 s**; observed sync ≈ **23.5 s** with slow `/ip/route/print`  

The goal is to answer: **which exact data does normal Admin synchronization actually need?** — without implementing any optimization yet.

---

## 2. Protected Baseline

| Item | Value |
|------|--------|
| Phase 0 baseline | [`docs/ROUTER_SYNC_PRE_CHANGE_BASELINE.md`](ROUTER_SYNC_PRE_CHANGE_BASELINE.md) |
| Git baseline | `8695882` |
| Firmware version | `0.5.0-w5500` (`RenzFiConfig::FIRMWARE_VERSION`) |

**Explicit statement:** Phase 1 produced **documentation only**. No `.cpp` / `.h` / `.ts` / `.tsx` / route / worker / UI behavior changes are authorized or performed in this phase.

Protected working systems (must remain untouched by later phases unless separately approved): Core coin/session/sales, Captive Portal, Setup, Factory Reset, W5500, StorageManager fallback, Admin/Core isolation, TWDT-safe deferred Wi-Fi persist, existing HTTP contracts, worker-based RouterOS mutations.

---

## 3. Current Synchronization Architecture

```
Admin UI
  "Synchronize Router"     → routerApi.syncRouter()
  "Refresh Router Information" → routerApi.refreshCache()
        │
        ▼
POST /api/router/cache/sync   OR   POST /api/router/cache/refresh
        │  (async_tcp: auth + enqueue only)
        ▼
HTTP 202 + jobId
        │
        ▼
RouterProvisioningWorker::enqueueAdminSyncCache
        │
        ▼
OpType::AdminSyncCache  (router_worker FreeRTOS task)
        │
        ▼
RouterPlatform::synchronizeRouterCache(false)
  ≡ RouterPlatform::refreshRouterCache(false)
        │
        ▼
MikroTikDriver::collectCacheSnapshot(snap)
  + observeAndRepairWan(observation)
  + (optional) RouterWireless::reconcileCaptiveHotspotPath
        │
        ▼
RouterCacheManager::applyLiveSnapshot(snap)   [persist on worker]
        │
        ▼
Job result polled via GET /api/router/jobs/{id}
```

**Phase 0 fact (unchanged):** Sync and Refresh UI buttons call **different HTTP paths** but the **same** `AdminSyncCache` worker op and the **same** `collectCacheSnapshot` body.

Login path (`synchronizeAdminClient`): `GET /api/status` first; only if `routerCache.stale` does it call `syncRouter()` (same full job).

---

## 4. Admin Router Data Inventory

Classification key:

| Code | Meaning |
|------|---------|
| **A** | Required for normal configuration synchronization |
| **B** | Refreshable observational / live telemetry |
| **C** | Diagnostic / on-demand |
| **D** | Optional / best-effort (should not block sync) |
| **E** | Local / cached / Core-known (no RouterOS needed for display) |

### 4.1 Dashboard (`DashboardPage.tsx` + `GET /api/status`)

| UI Field | API Property | Backend Source | RouterOS Command (when last synced) | Class | Live ROS required for display? | Failure Severity (for *normal sync*) |
|----------|--------------|----------------|--------------------------------------|-------|--------------------------------|--------------------------------------|
| MikroTik Router (Configured) | `mikrotik.configured` / `host` | Router settings / Core RAM | none (local credentials presence) | **E** | No | N/A |
| Connectivity | `mikrotik.connectivity` / `routerCache.observation.connectivity` | Last observation in cache | session success → `"online"` in snapshot | **B** | No (cached) | D — do not block if only connectivity stale |
| Router Identity | `routerCache.identity` | Cache / provisioning seed | `/system/identity/print` | **A** | No (cached OK) | A — missing identity weakens config model |
| RouterOS Version | `routerCache.routerOs.version` / `routerOsVersion` | Cache | `/system/resource/print` (`version`) | **A** | No (cached OK) | D if version missing but identity/SSID present |
| CPU | `routerCache.routerOs.cpuLoad` | Cache (from resource print) | `/system/resource/print` | **B** | Prefer refresh path | **D** — must not block config sync |
| Memory | `routerCache.routerOs.freeMemory` / `totalMemory` | Cache | `/system/resource/print` | **B** | Prefer refresh | **D** |
| Uptime | `routerCache.routerOs.uptime` | Cache | `/system/resource/print` | **B** | Prefer refresh | **D** |
| Production SSID | `routerCache.productionNetwork.ssid` / `ssid` | Cache + production verify | wireless print / canonical | **A** | No (cached OK) | A for config integrity |
| Hotspot | `hotspot.status` / `ok` | Status from cache observation | `/ip/hotspot/print` (+ reconcile) | **A**/`B` | No (cached) | D if unknown; A if Admin must show HS availability |
| WAN Internet | `internet` / `wan.*` | Status observation (from last sync/test) | WAN observe (`interface`/`dhcp`/`route`/`ping`) | **B** | Prefer refresh | **D** for config sync |
| Admin Connection | browser ↔ Core | Client-side | none | **E** | No | N/A |
| Sales / Active Users / Coin / Storage / ESP32 uptime / Database / API | Core status | RAM/snapshots | none | **E** | No | N/A |
| Pending Sync | storage / Core | Storage snapshot | none | **E** | No | N/A |

### 4.2 System Configuration — Wireless Configuration summary (`WirelessConfigurationSummary.tsx`)

| UI Field | API Property | Backend Source | RouterOS Command | Class | Live ROS? | Failure Severity |
|----------|--------------|----------------|------------------|-------|-----------|------------------|
| Source | `wifiMode` | `GET /api/router/wireless` ← cache / provisioning canonical | none for mode (local) | **E** | No | N/A |
| Configured Interface | `interface` | Wireless API / cache `wirelessInterface` | wireless print / canonical | **A** | No (cached) | A |
| Production SSID | `ssid` | Wireless API / cache | wireless print | **A** | No (cached) | A |
| Band | `band` (+ `productionNetwork.frequency` fallback) | Cache / production verify | wireless `band`/`frequency` | **A**/`D` | No | D if band unknown |
| Status (Configured) | `configured` | Canonical wireless config on ESP32 | none | **E** | No | N/A |

### 4.3 System Configuration — Wireless Settings (editable)

| UI Field | API Property | Backend Source | RouterOS | Class | Notes |
|----------|--------------|----------------|----------|-------|-------|
| SSID (edit) | wireless form / save | Cache + `PUT /api/router/wireless` worker | SSID set (mutation) | **A** (config) | Mutation is separate worker job, not cache sync |
| Password display | security open/closed | Cache `security` | security-profiles print | **A**/`D` | Needed to know Open vs protected; not rate-limit |

### 4.4 System Configuration — Network

| UI Field | API Property | Backend Source | RouterOS | Class | Notes |
|----------|--------------|----------------|----------|-------|-------|
| Mode (DHCP/Static) | `wifiConfig.addressMode` | `GET /api/system/wifi/config` / NetworkSettings | **ESP32 local** | **E** | Not MikroTik WAN |
| IP / Gateway / Mask / DNS (DHCP view) | `wifiConfig.current.*` or `systemHealth.ethernet.*` | W5500 / health snapshot | **not** Admin sync | **E**/`B` | ESP32 Ethernet plane |
| MAC | ethernet mac | health / wifiConfig | none | **E** | |
| Static fields (edit) | saved via wifi config POST | Storage / NVS | none | **E** | |

**Finding:** Admin “Network” section is **ESP32 Ethernet configuration**, not MikroTik routing table. It does **not** consume `/ip/route/print`.

### 4.5 System Configuration — Hotspot

| UI Field | API Property | Backend Source | RouterOS | Class | Failure Severity |
|----------|--------------|----------------|----------|-------|------------------|
| Username | router settings | `GET /api/router/settings` (no password) | none | **E** | N/A |
| Password (write-only) | save settings worker | Storage/NVS encrypted | none on GET | **E** | N/A |
| Default Profile | `form.profile` / cache `hotspotProfile` | Settings + cache | none to *display* saved default | **A** | A |
| Available Profiles + Rate Limit | `profiles` / `profileDetails` | `GET /api/router/profiles` (cache) or refresh worker | `/ip/hotspot/user/profile/print` | **A** | A for list; D for individual rate blank |
| Profile Status “Available” | UI static when listed | cache | user profile print | **A** | D |
| Test Connection steps | test job result | `AdminTestConnection` | identity + profiles + hotspot (+ resource) | separate from sync | — |

### 4.6 System Configuration — Status panel

| UI Field | API Property | Source | RouterOS | Class | Failure Severity |
|----------|--------------|--------|----------|-------|------------------|
| RouterOS Connectivity | status / observation | cache | session | **B** | D |
| Hotspot Service | `hotspot` / observation | cache | hotspot print | **A**/`B` | D |
| Internet | `internet` / `wan` | observation | WAN observe | **B** | D |
| Ethernet | system status eth | W5500 | none | **E** | N/A |
| Router Identity | cache | identity print | **A** | A |
| RouterOS Version | cache | resource print | **A** | D |
| Last Synchronization | `lastSynchronizedAt` / millis | RouterCacheManager | none | **E** | N/A |
| Cache Age | `cacheAgeSeconds` | RouterCacheManager | none | **E** | N/A |
| Provision Status | `provisionStatus` | cache / Installation | none (Admin sync passes `markProvisioned=false`) | **E** | N/A |
| Production Wi-Fi | `productionNetwork` | finish verify cache | setup/finish (not Admin sync invent) | **E**/`A` display | D for sync |
| Production SSID | cache ssid / productionNetwork | wireless | **A** | A |
| Hotspot Profile | cache / form | settings | **A** | A |
| CPU / Memory / Uptime | `routerOs.*` | resource print | **B** | **D** |

### 4.7 Connection status list (System Configuration)

Same as Status connectivity/Hotspot/Internet/Ethernet rows — **B** or **E** as above.

### 4.8 Promo Rates page (profile names)

| UI Field | Source | RouterOS | Class |
|----------|--------|----------|-------|
| MikroTik profile names for speed | cached profiles | user profile print (via sync or profiles refresh) | **A** (shared cache) |

---

## 5. RouterOS Command Inventory

Scope: commands executed on the **AdminSyncCache** path (`collectCacheSnapshot` → `observeAndRepairWan` → `reconcileCaptiveHotspotPath`), not Setup discovery or coin authorize.

| RouterOS Command | Purpose | Consumer (Admin / cache) | Class | Cost | Required for Normal Sync? | Failure Behavior today |
|------------------|---------|--------------------------|-------|------|---------------------------|------------------------|
| TCP connect + login | Open API session | all | A | MODERATE | **Yes** | Snapshot returns **false** → sync fails |
| `/system/identity/print` | Router identity | Dashboard / Status identity | A | LOW | **Yes** (config) | Best-effort fill; does not alone fail snapshot |
| `/system/resource/print` | version + **cpu/mem/uptime** | Version (**A**) + telemetry (**B**) | A+B | LOW–MODERATE | Version yes; telemetry **no** | Best-effort; does not alone fail snapshot |
| `RouterWireless::readInterface` (targeted wireless print) | iface/SSID/band/security-profile | Wireless summary / SSID | A | LOW–MODERATE | **Yes** if canonical iface known | Falls through to full wireless print |
| `/interface/wireless/print` | Inventory if no canonical read | Wireless fields | A | MODERATE | **Yes** when needed | Best-effort |
| `/interface/wireless/security-profiles/print` | Auth types → security label | Open vs protected UI | A/D | MODERATE | Prefer yes for security label | Sets `unknown` |
| `reconcileCaptiveHotspotPath` (may call `/interface/bridge/port/print`, hotspot get/set, profile get/set, `/ip/address/print`, etc.) | Repair captive path on WLAN/bridge | `hotspotServer` / bridge / htmlDirectory; **mutations** | C + repair | **HIGH** | **No** for *read-only* Admin config sync | Errors stored; sync continues |
| `/ip/hotspot/print` | Hotspot server name/iface | Hotspot available / observation | A/B | LOW–MODERATE | Prefer yes | Best-effort |
| `/ip/hotspot/profile/print` | html-directory | cache `htmlDirectory` | D | LOW–MODERATE | **No** for typical Admin UI | Best-effort |
| `/ip/hotspot/user/profile/print` | names + rate-limit | Hotspot profiles UI / promos | A | MODERATE | **Yes** | Best-effort array; empty list bad for UX |
| `/interface/print` (`?name=ether1-WAN`) | WAN link | `observation.wan.link` | B | LOW | **No** for config sync | Best-effort |
| `/ip/dhcp-client/print` (+ optional set/add) | WAN DHCP observe/repair | `wan.dhcp` / gateway / IP | B + repair | MODERATE | **No** for config sync | Best-effort; may **mutate** |
| `/ip/route/print` (`?dst-address=0.0.0.0/0`) + **one retry** | Default route / temp route | `wan.defaultRoute` / gateway; repair remove | **C**/B | **HIGH** (HW ~slow) | **No** for config sync | Sets `unknown`; does **not** return false from snapshot |
| `/ip/route/remove` (temp comment only) | Remove stale temp default | WAN repair | C + repair | MODERATE | **No** | Best-effort |
| `/ip/address/print` (WAN observe path) | Address observe | wan fields | B | LOW–MODERATE | **No** | Best-effort |
| `/ping` (bounded, budget-gated) | Internet reachability | `wan.internet` | B | MODERATE | **No**; skipped if budget &lt; 3s | Prefer skip vs timeout |

**Not part of Admin sync (other jobs):** hotspot active/login/user for coin; setup wifiwave2 discovery; profiles/op set; wireless SSID save; Test Connection (similar but separate op).

---

## 6. Required Synchronization Data (Category A — proven)

These are needed for Admin to represent **configured** Renz-Fi router state (cached):

1. **Session/credentials valid** (connect+login)  
2. **Router identity**  
3. **RouterOS version** (from resource; not CPU/mem/uptime)  
4. **Production wireless interface + SSID** (+ band when available)  
5. **Wireless security classification** (open vs auth types) when UI depends on it  
6. **Configured HotSpot profile name** (from ESP settings; verify existence optional)  
7. **HotSpot user profile list + rate-limit** (Hotspot + Promo UI)  
8. **HotSpot server presence** (name) for “Hotspot available” — preferably lightweight  

**Not** proven required for *normal* Admin config sync: full routing table, WAN repair, bridge inventory, html-directory, ICMP ping, CPU/memory/uptime.

---

## 7. Refreshable Information (Category B)

Should be independent of full configuration sync (conceptually — **not implemented yet**):

| Data | Current source |
|------|----------------|
| CPU / Memory / Uptime | `/system/resource/print` fields inside same sync |
| Connectivity online/offline | observation from last sync/test |
| Hotspot available/unavailable | observation |
| WAN link / DHCP / defaultRoute / internet | `observeAndRepairWan` |
| ESP32 Ethernet IP/GW/DNS/MAC | local health / wifiConfig (**already independent**) |

---

## 8. Diagnostic / On-Demand Information (Category C)

| Data / op | Why C |
|-----------|--------|
| `/ip/route/print` (+ retry) | Default-route diagnostics; Admin Network UI does not show MikroTik routing table; HW slow |
| Temp route remove | Repair, not inventory |
| Full bridge port inventory / reconcile mutations | Captive repair; heavy; can mutate |
| Full interface inventory | Only WAN-filtered print today; still observational |
| `/ping` | Reachability probe |
| Optional DHCP client add/set | Repair |

---

## 9. Optional / Best-Effort Data (Category D)

| Data | Why D |
|------|--------|
| `htmlDirectory` | Rarely shown; filled only if empty |
| Band if missing | Frequency fallback from productionNetwork |
| Security `unknown` | UI still works |
| Individual profile rate blank | “Not set yet” is valid UI |
| Version if identity+SSID present | Degraded but usable |
| CPU/Memory/Uptime missing | Show “—” |

**Today’s code:** most of these already do not flip `collectCacheSnapshot` to false; **timeout of the whole job** still fails sync.

---

## 10. Local / Cached Data (Category E)

| Data | Source |
|------|--------|
| Installation / provision status | InstallationStateManager / cache field |
| Last sync timestamp / cache age / stale | RouterCacheManager |
| Admin connection | Browser |
| Coin, sales, sessions, storage, ESP32 uptime | Core `/api/status` |
| Network Mode / static IP plan / ESP32 MAC | NetworkSettings / W5500 |
| wifiMode Source / configured flag | Router provisioning / wireless canonical on ESP32 |
| Router host + default profile name | Router settings file (no password in GET) |
| `productionNetwork` verify blob | Finish/setup verify (cached) |

These must **not** force RouterOS work merely to paint Admin.

---

## 11. WAN Observation/Repair Analysis (`observeAndRepairWan`)

| Question | Evidence |
|----------|----------|
| Why sync invokes it | Called unconditionally at end of `collectCacheSnapshot` (comment: same open session) |
| Admin configuration dependency | **Weak.** Feeds `observation.wan` / internet display; Network section uses **ESP32** ethernet, not MikroTik routes |
| Required for installation? | Setup/finish has **separate** production-network stages — not this Admin sync helper |
| Required for coin / portal / session? | **No.** Authorize/deauth use hotspot user/active APIs on Critical worker jobs |
| Required for production networking? | Production path is finish/engine; this is Admin Sync/Test side-effect |
| Observational? | **Yes** (link, dhcp, defaultRoute, internet) |
| Repair? | **Yes** — may ensure WAN DHCP client, remove temp default route, optional ping |
| Belongs in normal Synchronize? | **Classification: No** for *configuration* sync; **Yes today** as coupled code |
| Belongs in Refresh? | Conceptually **B** (status), without repair mutations |
| Belongs in Diagnostics? | Repair + route query → **C** |

---

## 12. `/ip/route/print` Analysis

| # | Question | Answer |
|---|----------|--------|
| 1 | Why requested? | Inside `observeAndRepairWan`: filtered default route `0.0.0.0/0` to set `wan.defaultRoute`, gateway, detect temp route comment |
| 2 | Which Admin field? | Status/Dashboard **WAN / Internet** observation — **not** Network Mode/IP/Gateway/Mask/DNS editors |
| 3 | On Dashboard? | Yes, as WAN Internet / related status (from last observation) |
| 4 | On System Configuration? | Connectivity list Internet/WAN — not routing table UI |
| 5 | Required to configure Renz-Fi? | **No** |
| 6 | Required for HotSpot? | **No** |
| 7 | Required for coin/session? | **No** |
| 8 | Required for production networking? | **No** in Admin sync (finish has own verification) |
| 9 | Purely diagnostic? | **Mostly yes** (+ repair side-effect) |
| 10 | Category C? | **Yes** — recommend classify as diagnostic/on-demand for future phases |

**Hardware:** Phase 0 noted this command as a major contributor to ~23.5 s sync vs 20 s job timeout.  
**Code:** Failure sets `defaultRoute=unknown`; **does not** make `collectCacheSnapshot` return false. Timeout of entire AdminSyncCache job still yields `ROUTER_CACHE_SYNC_FAILED`.

**Phase 1 action:** Document only — **do not remove**.

---

## 13. Failure Semantics

### What actually fails Admin sync today

1. Credentials missing / session open failure → snapshot false → **BLOCK**  
2. `applyLiveSnapshot` false → **BLOCK**  
3. Worker job timeout / empty failure → **BLOCK** (`ROUTER_CACHE_SYNC_FAILED`)  
4. Transport gate rejects enqueue → busy/deferred  

### What does *not* alone fail snapshot today

- Individual optional prints (identity/resource/wireless/hotspot/profiles/WAN/route) best-effort  
- `reconcileCaptiveHotspotPath` failure (logged / error field)  
- Route query failure  

### Recommended classification for *future* phases (not implemented)

| Failure | Should |
|---------|--------|
| Connect/login fail | **A — BLOCK** |
| Cannot read wireless SSID/iface when configured | **A — BLOCK** or warning with empty wireless |
| Cannot list user profiles | **A — BLOCK** or warning (UX needs profiles) |
| Identity/version missing | Prefer **warning** if other A fields OK |
| CPU/mem/uptime missing | **C — IGNORE** for sync success |
| WAN/route/ping/DHCP repair fail | **C — IGNORE** for sync success |
| Captive reconcile fail | **C — IGNORE** for sync success |
| Job timeout due to C ops | Treat as design bug to fix in Phase 3+ — **do not** raise timeout as “fix” |

---

## 14. Performance Risk

| Operation | Cost | Evidence |
|-----------|------|----------|
| `/ip/route/print` (+ retry) | **HIGH** | HW: slow; ~23.5 s total sync; timeout 20 s |
| `reconcileCaptiveHotspotPath` | **HIGH** | Multiple prints + possible sets |
| `observeAndRepairWan` bundle | **HIGH** | interface + dhcp + route + optional mutate + ping |
| `/ip/hotspot/user/profile/print` | MODERATE | Needed for UI; bounded proplist |
| `/interface/wireless/print` | MODERATE | Prefer targeted readInterface |
| `/system/identity/print` | LOW | — |
| `/system/resource/print` | LOW–MODERATE | Cheap but packs telemetry into sync |
| `/ping` | MODERATE | Budget-gated; skipped near deadline |

Not measured per-command in this phase beyond Phase 0 qualitative hardware notes.

---

## 15. Core Isolation

Admin sync RouterOS work is **unrelated** to runtime correctness of:

| Core area | Depends on Admin sync? |
|-----------|-------------------------|
| CoinManager / coin pulse | **No** |
| SessionManager / sales persist | **No** |
| Captive Portal / PortalServer | **No** |
| HotSpot authorize/deauth/pause | **No** (Critical worker ops) |
| Setup Wizard / finish engine | **No** (separate jobs) |
| InstallationStateManager | **No** (Admin sync `markProvisioned=false`) |
| FactoryResetWorker | **No** |
| W5500 Ethernet | **No** |
| TWDT / async_tcp | Sync already off async_tcp — must stay that way |
| StorageManager sales fallback | **No** |

**Rule for later phases:** Admin sync may degrade; **Piso WiFi Core must continue.**

---

## 16. Proposed Classification Summary

| Data Group | Normal Sync (desired) | Refresh (desired) | Diagnostics (desired) | Blocking? |
|------------|----------------------|-------------------|------------------------|-----------|
| Identity / version / SSID / iface / band / security | Yes | Optional | No | Identity/SSID/iface: prefer block; band/security: soft |
| HotSpot server name | Yes (light) | Optional | No | Soft preferred |
| User profiles + rate-limit | Yes | Via profiles refresh | No | Prefer block if empty when configured |
| CPU / mem / uptime | **No** | **Yes** | No | No |
| WAN / route / dhcp / ping | **No** | Light status **without repair** | Full repair/route | No |
| Captive path reconcile/mutate | **No** | No | On-demand repair | No |
| ESP32 network / sales / coin / install | Local only | Local | — | N/A |

**Important:** This table is **classification intent for Phase 2+**. Current code still puts B+C+repair inside the same AdminSyncCache job.

---

## 17. Phase 1 Findings

1. **Sync ≡ Refresh today** — both enqueue full `collectCacheSnapshot` including WAN observe/repair and hotspot reconcile.  
2. **Admin Network UI is ESP32 Ethernet**, not MikroTik `/ip/route/print`.  
3. **`/ip/route/print` is Category C** for Admin config — feeds WAN observation only; HW-expensive; contributes to timeout failures despite RouterOS being online.  
4. **`observeAndRepairWan` mixes B telemetry with C repair** and runs on every sync.  
5. **`collectCacheSnapshot` returns true** after best-effort fills; **job timeout** is the practical failure mode (~23.5 s vs 20 s).  
6. **Category A is relatively small** (identity, version, wireless, profiles, light hotspot).  
7. **CPU/mem/uptime are Category B** but collected via the same resource print as version — version can stay A while telemetry moves to Refresh conceptually.  
8. **Captive reconcile is mutation+diagnostic**, not required to *display* cached wireless/hotspot for Admin.  
9. **Core isolation is intact** — coin/portal/sales do not call Admin sync.  
10. Raising `ROUTER_WORKER_JOB_TIMEOUT_MS` is **not** an accepted Phase 1 or Phase 3 “fix.”

---

## 18. Phase 1 Recommendation (for Phase 2 only — no code)

Phase 2 (`docs/ROUTER_SYNC_FLOW_AUDIT.md`) should:

1. Trace exact call order, timings, and timeout interaction inside `collectCacheSnapshot` / `observeAndRepairWan` / reconcile.  
2. Confirm whether `applyLiveSnapshot` or timeout is the dominant failure on hardware when route print is slow.  
3. Propose (still no code until Phase 3) a **minimal command set** for “Synchronize Router Configuration” vs a **light** “Refresh Router Status” vs diagnostics.  
4. Preserve worker + routes; do not invent `/api/admin/sync`.  
5. Keep partial-success semantics design for Phase 4 — optional C failures must not equal “Failed to synchronize router.”

**STOP.** Do not implement Phase 2–8 without explicit approval.

---

## Appendix — Counts (Phase 1)

| Metric | Count |
|--------|------:|
| Admin Router-related UI fields audited (rows in §4) | **48** |
| RouterOS ops on Admin sync path audited (§5) | **16** |
| Category A (required config) primary items | **8** groups / ~18 field rows tagged A |
| Category B (refreshable) | ~12 field rows |
| Category C (diagnostic) | ~6 ops / groups |
| Category D (best-effort) | ~8 field rows |
| Category E (local/cached) | ~16 field rows |

*(Rows can carry dual tags A/B or A/D where one RouterOS response feeds mixed concerns.)*

---

## Appendix — Sources consulted

- `docs/ROUTER_SYNC_PRE_CHANGE_BASELINE.md`  
- `docs/ADMIN_CORE_ISOLATION.md`  
- `ESP32_S3_Firmware/src/router/drivers/MikroTikDriver.cpp` (`collectCacheSnapshot`, `observeAndRepairWan`)  
- `ESP32_S3_Firmware/src/router/RouterPlatform.cpp` (`synchronizeRouterCache` ≡ `refreshRouterCache`)  
- `ESP32_S3_Firmware/src/RouterProvisioningWorker.cpp` (`AdminSyncCache`)  
- `ESP32_S3_Firmware/src/RouterCacheManager.cpp`  
- `ESP32_S3_Firmware/src/RouterWirelessAdapter.cpp` (`reconcileCaptiveHotspotPath`)  
- `ESP32_S3_Firmware/src/ApiServer.cpp` (cache sync/refresh routes)  
- `src/pages/DashboardPage.tsx`, `SystemConfigurationPage.tsx`  
- `src/components/WirelessConfigurationSummary.tsx`  
- `src/services/router.ts`, `adminSync.ts`  
- `src/types/api.ts`
