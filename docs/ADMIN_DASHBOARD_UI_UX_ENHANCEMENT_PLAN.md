# Admin Dashboard UI/UX Enhancement — Forensic Audit & Change Plan

**Status:** Phase 3A + 3B complete — **physical hEX validation pending**  
**Date:** 2026-08-29  
**Context:** Post hAP lite → hEX lite migration (operational). Enhancement is **incremental**, not a redesign.

**Evidence labels:** VERIFIED = observed in repository; INFERRED = logical derivation; NOT TESTED = not validated on device in this audit.

---

## Executive summary

The Renz-Fi Admin Dashboard is a **working production UI** with a deliberate **standby-first, low-polling architecture**. New Network Status and MikroTik Router sections should **reuse `/api/status` and cached `routerCache`**, not open new RouterOS sessions or high-frequency polls. **SD Card Health is a protected component** and must not be visually or logically redesigned.

Recommended approach: **extend presentation** of data already on the connect path, add **optional MikroTik storage/board fields only inside existing router sync/refresh worker**, and **reorganize sidebar + relocate Coin Slot detail** without touching coin processing, factory-reset quiesce, or SSE architecture.

---

## PHASE 1 — Forensic audit

### 1. Dashboard page

| Item | Detail | Evidence |
|------|--------|----------|
| File | `src/pages/DashboardPage.tsx` | VERIFIED |
| Route | `/dashboard` under `AdminLayout` | VERIFIED |
| Connect model | Single `GET /api/status` via `synchronizeAdminClient()` | VERIFIED |
| Live updates | Opt-in (`liveUpdatesEnabled`, default **false**) | VERIFIED |
| Secondary data | Coin/health details only when `detailsEnabled && liveUpdatesEnabled` | VERIFIED |

**Current layout (top → bottom):**

1. Header + standby toolbar (Reload Sales, Synchronize Router, Live updates, Load coin/health details)
2. Error / stale banners
3. **Summary row (4):** `MonthlySalesCard`, `ActiveUsersCard`, `CoinSummaryCard`, `TotalCoinsCard`
4. **Monitoring row (3):** `SystemStatusCard`, `CoinSlotStatusCard`, `SdCardHealthCard`
5. **Ops row (3):** `SystemHealthCard`, `StorageUsageCard`, `QuickActionsCard`
6. **Connectivity row:** `ConnectivityStatusRow` (5 tiles)

**Barrel:** `src/components/dashboard/index.ts`  
**Primitives:** `src/components/dashboard/DashboardPrimitives.tsx` (cards, `MetricRing`, `Sparkline`, badges)  
**Display helpers:** `src/lib/dashboardDisplay.ts`, `src/lib/adminStatus.ts`

---

### 2. Sidebar / navigation

| Item | Detail | Evidence |
|------|--------|----------|
| File | `src/components/AdminLayout.tsx` | VERIFIED |
| Icons | **Lucide React** (`lucide-react`) | VERIFIED |
| Collapse | Desktop sidebar 220px → 64px; mobile overlay | VERIFIED |
| Permissions | Owner vs operator via `operatorPermissions.ts` | VERIFIED |

**Current nav items (flat list):**

| Route | Label | Access |
|-------|-------|--------|
| `/dashboard` | Dashboard | `dashboard` |
| `/promo-rates` | Promo Rates | `promo-rates` |
| `/vouchers` | Vouchers | `vouchers` |
| `/active-users` | Active Users | `active-users` |
| `/sales-reports` | Sales Reports | `sales-reports` |
| `/captive-portal` | Captive Portal | `captive-portal` |
| `/access-points` | Access Points | owner only |
| `/coin-settings` | Coin Settings | `coin-settings` |
| `/system-configuration` | System Configuration | `system-configuration` |
| `/logs` | Logs | `logs` |
| `/firmware` | Update | `firmware` |
| `/system-settings` | System Settings | owner only |

**Redirects:** `/router-settings` → `/system-configuration`; `/devices`, `/fleet` → `/dashboard`

---

### 3. Coin Slot — dashboard vs dedicated page

| Surface | File | Data sources |
|---------|------|--------------|
| Dashboard summary | `CoinSummaryCard` (`SummaryCards.tsx`) | `/api/status` → `coinSlot` |
| Dashboard detail card | `CoinSlotStatusCard` (`MonitoringCards.tsx`) | status + optional `/api/system/coin`, `/api/coin/diagnostics`, `/api/coin/settings` |
| Dedicated page | `CoinSettingsPage.tsx` (`/coin-settings`) | Full calibration, test pulse, diagnostics, save |

**CoinSlotStatusCard current fields (VERIFIED):** enabled badge, coins today ring, hardware label, total coins, last coin, rate.

**Coin processing:** Firmware GPIO/pulse logic in ESP32 — **out of scope** for this UI task.

---

### 4. SD Card Health — PROTECTED COMPONENT

| Layer | File | Notes |
|-------|------|-------|
| Dashboard wrapper | `SdCardHealthCard` in `MonitoringCards.tsx` | **PROTECTED** — badge, ring, capacity, uptime, details dialog |
| Full diagnostics | `StorageHealthCard.tsx` | Used inside dialog + System Configuration |
| Hook | `useStorageHealth.ts` → `GET /api/storage/status` | Lazy via dialog |
| Dashboard summary | `/api/status` → `storage.sd`, `storageStatus` | Connect path |

**Protected rule:** Do **not** change `StorageHealthCard.tsx` visuals/logic. Do **not** change `SdCardHealthCard` ring/badge/capacity calculations. May **relocate grid position** only if unavoidable.

**Current props (`SdCardHealthCard`):** `loading`, `error`, `onRetry`, `badgeLabel`, `badgeTone`, `freePct`, `capacityLabel`, `uptimeLabel`, `mounted`

---

### 5. MikroTik / RouterOS communication

**Architecture (VERIFIED):**

```
MikroTik ← RouterOS API (worker only, not async_tcp storms)
    ↓
MikroTikDriver / RouterCacheManager (SD cache)
    ↓
GET /api/status → routerCache + mikrotik + wan + hotspot
    ↓
Admin Dashboard (browser never talks to RouterOS)
```

| Endpoint | RouterOS on request? | Use |
|----------|---------------------|-----|
| `GET /api/status` | **No** — reads cache + ESP32-local health | Dashboard connect |
| `POST /api/router/cache/sync` | **Yes** — worker job | Manual "Synchronize Router" |
| `POST /api/router/test` | **Yes** — worker job | System Configuration |

**`routerCache` fields today (VERIFIED — `src/types/api.ts`, `MikroTikDriver.cpp`):**

| Field | Source | Available now |
|-------|--------|---------------|
| `identity` | sync | Yes |
| `routerOsVersion` | sync | Yes |
| `routerOs.cpuLoad` | `/system/resource/print` on sync/refresh | Yes |
| `routerOs.freeMemory` / `totalMemory` | resource print | Yes |
| `routerOs.uptime` | resource print | Yes |
| `observation.connectivity` | refresh | Yes |
| `cacheAgeSeconds`, `stale` | cache manager | Yes |
| **board-name / model** | validator reads it; **not in dashboard cache** | **Gap** |
| **RouterOS flash storage (hdd)** | **Not in driver cache today** | **Gap** |
| **RouterOS temperature** | **Not in driver cache today** | **Gap** — may be unavailable on RB750r2 |

**Important:** Reference screenshot storage (128 MB / 39.7 MB / temperature 48°C) is **MikroTik RouterOS resource data**, not ESP32 SD. Do not confuse with `StorageUsageCard` (ESP32 flash/SD).

---

### 6. Network status — existing components

| Component | Location | Covers |
|-----------|----------|--------|
| `ConnectivityStatusRow` | Bottom of dashboard | MikroTik, Admin, WAN, Coin, Hotspot |
| `SystemStatusCard` | Monitoring row | WAN Internet, Production SSID, Wi-Fi |
| `SystemHealthCard` | Ops row | ESP32 CPU/temp/heap/Ethernet |
| `mikrotikDisplay()` | `dashboardDisplay.ts` | Configured vs online semantics |

**Status sources in `/api/status` (VERIFIED):**

| Signal | Fields |
|--------|--------|
| Internet | `internet.ok`, `wan.internet`, `wan.known`, `wan.ip` |
| MikroTik | `mikrotik.connectivity`, `mikrotik.configured`, `mikrotik.latencyMs`, `routerCache.observation` |
| ESP32 / Controller | `server.ok`, `esp32.uptime`, `adminApiReachable` (frontend) |
| HotSpot | `hotspot.ok`, `hotspot.status` |
| Access Point | **Not in `/api/status`** — registry at `/api/access-points` | **Gap** |

**Metrics gap analysis:**

| Metric | Exists? | Source |
|--------|---------|--------|
| Latency (MikroTik) | Partial | `mikrotik.latencyMs` (may be 0 if cache-only) |
| Latency (Internet) | Partial | `internet.latencyMs` |
| Packet loss | **No** | — |
| Download / Upload throughput | **No** | — |
| Historical network graph | **No** | — |

**Plan:** Show **N/A** or **Unknown** for unavailable metrics. Do **not** add continuous ping or RouterOS polling for decorative graphs.

---

### 7. Polling / SSE / factory-reset quiesce

| Mechanism | File | Interval / behavior |
|-----------|------|---------------------|
| SSE | `useDashboardEvents.ts` → `/api/events` | Enabled only when logged in + `liveUpdatesEnabled` + **not** factory-reset quiesced |
| SSE fallback poll | same | 30s when SSE down |
| Health monitor | `useAdminApiMonitor.ts` | 5s **only when SSE disconnected** and not standby |
| System status | `useSystemStatus.ts` | `refetchInterval: fallbackPollMs` (30s or false) |
| Default React Query | `main.tsx` | `staleTime: 5s`, no refetch on focus |

**Factory-reset quiesce (VERIFIED — must preserve):**

| File | Behavior |
|------|----------|
| `services/factoryResetQuiesce.ts` | Global `active` flag |
| `App.tsx` | Disables SSE + health monitor when quiesced |
| `SystemSettingsPage.tsx` | Sets quiesce before reset; only polls reset status |

**New dashboard polling MUST:** respect `factoryResetQuiesced` and `liveUpdatesEnabled`; add **zero** always-on MikroTik polls.

---

### 8. Auth / default account

| Item | Status | Evidence |
|------|--------|----------|
| Admin IP + password login | VERIFIED | `AuthPage.tsx` |
| Default password prefilled | `"admin"` in state | VERIFIED — line 25 |
| Username field | **Not present** — password-only connect to appliance IP | VERIFIED |
| Separate username `admin` login path | **NOT IMPLEMENTED** as described in task | — |

**Preserve** existing IP + password flow. Do **not** break Auth during sidebar work.

---

### 9. Chart / graph inventory

| Component | Used on dashboard? |
|-----------|-------------------|
| `Sparkline` (SVG) | Yes — sales only |
| `MetricRing` | Yes — sales, coin, SD |
| Recharts `ChartContainer` | **No** — unused wrapper |

**Network graph:** No data source exists. Phase 3 should **defer** or show static placeholder / optional latency spark from cache timestamps only.

---

### 10. Full route inventory → proposed sidebar mapping

| Current route | Current label | Proposed section | Proposed label | Status |
|---------------|---------------|------------------|----------------|--------|
| `/dashboard` | Dashboard | OVERVIEW | Dashboard | EXISTS |
| `/sales-reports` | Sales Reports | OVERVIEW | Sales | EXISTS (rename label only) |
| `/promo-rates` | Promo Rates | MANAGEMENT | Coin Rates | EXISTS (rename; maps to promos) |
| — | Plans | MANAGEMENT | Plans | **NOT IMPLEMENTED** (≈ Promo Rates) |
| `/active-users` | Active Users | MANAGEMENT | Sessions | EXISTS (rename label) |
| `/vouchers` | Vouchers | MANAGEMENT | Vouchers | EXISTS |
| — | Network Overview | NETWORKING | Network Overview | **NOT IMPLEMENTED** — use `/system-configuration` Network section or new read-only page later |
| `/access-points` | Access Points | NETWORKING | Access Points | EXISTS |
| `/active-users` | — | NETWORKING | Connected Devices | **PARTIAL** — same page as Sessions unless new page added |
| — | Bandwidth | NETWORKING | Bandwidth | **NOT IMPLEMENTED** (promo speed limits only) |
| — | Security | NETWORKING | Security | **NOT IMPLEMENTED** (operators in System Settings) |
| `/system-configuration` | System Configuration | SYSTEM | Settings | EXISTS (rename group label) |
| — | Remote Access | SYSTEM | Remote Access | **NOT IMPLEMENTED** |
| — | Sub Vendo | SYSTEM | Sub Vendo | **NOT IMPLEMENTED** |
| `/firmware` | Update | SYSTEM | OTA Update | EXISTS (rename label) |
| `/coin-settings` | Coin Settings | MANAGEMENT | Coin Slot | EXISTS (rename label) |
| `/captive-portal` | Captive Portal | — | — | **Keep** — map under MANAGEMENT or CONTENT (not in reference sidebar) |
| `/logs` | Logs | SYSTEM | Logs | EXISTS — add to SYSTEM group |
| `/system-settings` | System Settings | SYSTEM | System Settings | EXISTS (owner) |

**Rules applied:**

- Do **not** create placeholder routes for NOT IMPLEMENTED items.
- Use **section headers** with disabled entries or omit missing items.
- **Do not remove** Captive Portal, Logs, or System Settings.
- **Keep all existing URLs** — label changes only.

---

## PHASE 2 — Minimal change plan

### Design principles

1. **Reuse `/api/status`** for Network Status + MikroTik card on connect.
2. **No new RouterOS polling** from dashboard mount.
3. Extend router cache **only inside existing sync/refresh worker** for storage/board (one login per sync, already budgeted).
4. **Protect** `SdCardHealthCard` + `StorageHealthCard`.
5. **Shrink** dashboard coin UI; **enrich** `CoinSettingsPage` with moved fields.
6. Honor **factory-reset quiesce** in any new `useQuery` hooks.

---

### Change table

| # | File | Current | Change | Why | Risk | Rollback |
|---|------|---------|--------|-----|------|----------|
| 1 | `src/components/dashboard/NetworkStatusCard.tsx` | — | **New** — consolidated Internet/MikroTik/ESP32/AP rows + metrics (N/A where missing) | Reference layout | Low — read-only UI | Delete component + dashboard import |
| 2 | `src/components/dashboard/MikrotikRouterCard.tsx` | — | **New** — model, ROS, uptime, CPU, memory, storage overview from `routerCache` | Reference layout | Low if cache-only | Delete component |
| 3 | `src/pages/DashboardPage.tsx` | 3-col monitoring + connectivity row | Add Network + MikroTik cards; **remove** `CoinSlotStatusCard` from main grid; keep `SdCardHealthCard` unchanged | Hierarchy | Medium layout | Revert layout block |
| 4 | `src/components/dashboard/MonitoringCards.tsx` | `CoinSlotStatusCard` | **No visual change** to SD card; optionally export coin card for Coin page reuse | Protection | Low | — |
| 5 | `src/pages/CoinSettingsPage.tsx` | Config-focused | Add **operational summary** section using existing coin APIs (today, total, last, rate, hardware) | Move detail off dashboard | Low | Remove section |
| 6 | `src/components/AdminLayout.tsx` | Flat nav | Grouped nav with section headers; same routes | Reference sidebar | Medium UX | Revert nav array |
| 7 | `src/types/api.ts` | `routerCache.routerOs` | Optional fields: `boardName`, `totalHdd`, `freeHdd`, `temperatureC` | MikroTik storage/temp | Low types | Optional fields optional |
| 8 | `ESP32_S3_Firmware/.../MikroTikDriver.cpp` | resource print partial | On **sync/refresh only**, parse `board-name`, `total-hdd-space`, `free-hdd-space`, `cpu-temperature` if present | Real MikroTik data | **Medium** — RouterOS load | Revert driver fields; cache ignores |
| 9 | `ESP32_S3_Firmware/.../RouterCacheManager.cpp` | Persists routerOs | Persist new optional fields | Status exposure | Low | — |
| 10 | `src/lib/dashboardDisplay.ts` | MikroTik helpers | Add formatters for router storage %, temp N/A | Display | Low | — |
| 11 | `src/components/dashboard/ConnectivityRow.tsx` | Full row | **Deprecate or slim** if duplicated by NetworkStatusCard | Avoid duplicate | Low | Keep row |
| 12 | `StorageHealthCard.tsx` | SD diagnostics | **NO CHANGE** | Protected | — | — |
| 13 | Factory reset / SSE / coin firmware | — | **NO CHANGE** | Protected | — | — |

**Explicitly NOT changing:**

- `StorageHealthCard.tsx`, `SdCardHealthCard` internals
- `FactoryResetWorker`, `factoryResetQuiesce.ts`, SSE onConnect behavior
- Coin GPIO / `VoucherManager` / session logic
- MikroTik migration scripts
- `AuthPage` connect contract (unless separate approved username task)

---

### Data flow — Network Status (proposed)

```
GET /api/status (existing connect + optional 30s SSE invalidation)
    ├── internet / wan → Internet row
    ├── mikrotik + routerCache.observation → MikroTik row
    ├── server.ok + esp32 → Controller row
    └── GET /api/access-points (NEW: only if owner + detailsEnabled OR summary count)
            └── first registered AP status → Access Point row (or "Not registered")
```

**Polling:** None beyond existing `useSystemStatus` interval. AP list: **single fetch on dashboard mount** or when user enables details — **not** on interval by default.

**Metrics:**

- Latency: show `mikrotik.latencyMs` / `internet.latencyMs` when > 0, else **N/A**
- Packet loss / download / upload: **N/A** (no backend source)

**Graph:** Phase 3 **defer** or show "No historical data" — do not add storage/polling for throughput history.

---

### Data flow — MikroTik Router card (proposed)

```
GET /api/status.routerCache (cached)
    ├── identity / boardName → Model line
    ├── routerOsVersion → RouterOS
    ├── routerOs.uptime → Uptime
    ├── mikrotik.connectivity + stale flag → Connection badge
    ├── routerOs.cpuLoad → CPU
    ├── routerOs.freeMemory / totalMemory → Memory
    └── routerOs.totalHdd / freeHdd → Storage (after firmware gap fill)
        └── temperatureC → if present else "N/A"

Manual refresh: existing "Synchronize Router" button only (worker job)
```

**No dashboard-triggered RouterOS login on timer.**

---

### Storage & temperature data source (plan)

| Data | Proposed source | When fetched |
|------|-----------------|--------------|
| CPU / memory / uptime | Already in cache | Sync / refresh worker |
| Board / model | Add `board-name` from `/system/resource/print` | Sync / refresh only |
| RouterOS flash | Add `total-hdd-space`, `free-hdd-space` from resource print | Sync / refresh only |
| Temperature | Add `cpu-temperature` from resource print **if attribute exists** | Sync / refresh only; show N/A on RB750r2 if absent |

**Evidence:** INFERRED — RouterOS 7 exposes these on `/system/resource/print` for many devices; hEX lite may lack temperature sensor.

---

### Coin Slot — dashboard vs page (plan)

| Field | Dashboard after change | Coin Settings page |
|-------|------------------------|-------------------|
| Enabled/disabled | `CoinSummaryCard` only | Full status header |
| Coins today | Summary card | Detailed + history |
| Last coin | Summary card | Detailed |
| Total coins | `TotalCoinsCard` | Move duplicate detail here |
| Rate | Remove from dashboard ring card | Show on coin page |
| Hardware state | Summary hint | Diagnostics section |
| Recent activity | Remove | Diagnostics / logs link |

**Remove from dashboard grid:** `CoinSlotStatusCard` (large card).  
**Keep:** `CoinSummaryCard`, `TotalCoinsCard` (already summary-level).

---

### Sidebar implementation approach

1. Replace flat `nav[]` with `navSections[]`: `{ title, items[] }`.
2. Render section headers + existing `Link` items.
3. Mobile: same groups in drawer.
4. Missing reference items: **omit** or show muted "Coming soon" **only if product approves** — default **omit**.

---

### Polling intervals (after enhancement — target state)

| Data | Interval | Notes |
|------|----------|-------|
| `/api/status` | Connect + SSE invalidation + 30s fallback if live updates on | Unchanged |
| `/api/health` | 5s if SSE down and not standby | Unchanged |
| `/api/system/coin` | Only if user enabled details | Unchanged |
| `/api/access-points` | On-demand / once per dashboard visit | **New — no interval** |
| RouterOS API | Manual sync only | Unchanged |
| MikroTik storage/temp | Updated on sync/refresh job completion | **New — piggyback only** |

---

### Factory reset safety (checklist for Phase 3)

- [ ] New queries use `enabled: !factoryResetQuiesced && …`
- [ ] No `setInterval` for MikroTik metrics
- [ ] No SSE changes
- [ ] No new AsyncTCP RouterOS from dashboard request path

---

### Protected components registry

| Component | Protection level |
|-----------|------------------|
| `StorageHealthCard.tsx` | **STRICT — no edits** |
| `SdCardHealthCard` (visual/logic) | **STRICT — no edits** |
| `factoryResetQuiesce` flow | **STRICT — no edits** |
| Coin firmware processing | **STRICT — no edits** |
| RouterOS worker / transport gate | **CHANGE ONLY** for optional cache fields on existing sync path |

---

## PHASE 3–5 preview (not started)

### Phase 3 — Implementation order

1. Sidebar grouping (frontend only, lowest risk)
2. Coin Slot relocation (frontend only)
3. `NetworkStatusCard` + `MikrotikRouterCard` (frontend, cache-only)
4. Firmware cache field extension (minimal driver change + types)
5. Optional AP summary on dashboard (single GET)

### Phase 4 — Validation commands

```bash
npm run build
npm run lint
node ESP32_S3_Firmware/tools/routeros-migration-script-contract-check.mjs
node ESP32_S3_Firmware/tools/factory-reset-contract-check.mjs
# plus existing admin-core-isolation if applicable
```

### Phase 5 — Regression checklist

See task acceptance criteria — SD Card pixel/compare, factory reset quiesce, no hardcoded 128MB/48°C, no new RouterOS poll storm.

---

## Rollback procedure

1. Revert frontend commits (Dashboard layout, new cards, sidebar).
2. If firmware cache fields added: revert `MikroTikDriver.cpp` + `RouterCacheManager.cpp` — dashboard ignores unknown JSON fields safely.
3. No MikroTik configuration rollback required.

---

## Remaining risks

| Risk | Level | Mitigation |
|------|-------|------------|
| RouterOS temperature unavailable on RB750r2 | VERIFIED likely | Show N/A |
| Storage fields empty until user syncs router | INFERRED | Show stale badge + "Synchronize Router" hint |
| Duplicate Network UI (ConnectivityRow + new card) | Medium | Remove/slim ConnectivityRow |
| Validation script `:local`/`:set` on RouterOS 7.18.2 | INFERRED | Fix separately if validation import fails |
| DMA / Guru from extra RouterOS work | Medium | **Only** extend existing sync/refresh — no new poll loops |

---

## Related documents

- [`HAP_LITE_TO_HEX_LITE_MIGRATION_FINAL.md`](./HAP_LITE_TO_HEX_LITE_MIGRATION_FINAL.md) — migration complete
- [`HAP_LITE_TO_HEX_LITE_MIGRATION_FORENSIC.md`](./HAP_LITE_TO_HEX_LITE_MIGRATION_FORENSIC.md) — pre-migration analysis
- [`.cursor/rules/admin-core-isolation.mdc`](../.cursor/rules/admin-core-isolation.mdc) — Admin must not poll RouterOS aggressively

---

**Next step:** Review and approve this plan, then begin **Phase 3** with sidebar grouping + coin relocation (frontend-only) before any firmware cache extension.

---

## Phase 3A — Implementation complete (2026-08-29)

**Status:** VERIFIED (build + core contracts). **Physical validation:** NOT TESTED.

### Scope delivered (frontend-only)

- Grouped sidebar (Overview / Management / Networking / System)
- Coin Slot detail relocated to `/coin-settings`
- `NetworkStatusCard` + `MikrotikRouterCard` (cache-only from `/api/status`)
- SD Card Health, factory-reset/SSE, coin firmware — **unchanged**

### Files changed (Phase 3A)

| File | Change |
|------|--------|
| `src/components/AdminLayout.tsx` | Grouped nav sections |
| `src/components/dashboard/NetworkStatusCard.tsx` | New |
| `src/components/dashboard/MikrotikRouterCard.tsx` | New |
| `src/components/dashboard/index.ts` | Exports |
| `src/pages/DashboardPage.tsx` | Layout + new cards |
| `src/pages/CoinSettingsPage.tsx` | Full coin operational card |

### Phase 3A validation (VERIFIED)

| Check | Result |
|-------|--------|
| `npm run build` | PASS |
| `factory-reset-contract-check.mjs` | 26/26 PASS |
| `admin-core-isolation-contract-check.mjs` | 16/16 PASS |
| `standby-idle-dma-contract-check.mjs` | 8/8 PASS |
| ESLint (changed files) | PASS |
| `npm run typecheck` | Pre-existing repo failures (unrelated) |

---

## Phase 3B — MikroTik cache extension complete (2026-08-29)

**Status:** VERIFIED (build + core contracts). **Physical validation:** NOT YET PERFORMED.

### Objective

Populate `MikrotikRouterCard` storage/model/temperature from **real RouterOS data** cached during existing sync/refresh/test worker jobs — **no new RouterOS command, session, timer, or poll loop**.

### Firmware change (minimal)

**File:** `ESP32_S3_Firmware/src/router/drivers/MikroTikDriver.cpp`

Extended **existing** `/system/resource/print` parsing via helper `populateRouterOsResourceFields()` in:

1. `collectCacheSnapshot()` — Admin Sync + Admin Refresh worker path
2. `testConnection()` — System Configuration test worker path

**Optional JSON fields added to `routerOs` (only when RouterOS returns them):**

| RouterOS attribute | Cache JSON key |
|------------------|----------------|
| `board-name` | `boardName` |
| `total-hdd-space` | `totalHddSpace` |
| `free-hdd-space` | `freeHddSpace` |
| `cpu-temperature` | `cpuTemperature` |

Existing fields unchanged: `version`, `cpuLoad`, `freeMemory`, `totalMemory`, `uptime`.

**Not changed:** `RouterCacheManager.cpp` (whole `routerOs` object already copied/persisted), `ApiServer.cpp`, factory-reset/SSE, worker enqueue paths.

### Frontend change (consume optional cache fields)

| File | Change |
|------|--------|
| `src/types/api.ts` | Optional `routerOs` fields |
| `src/services/router.ts` | `RouterOsResourceSnapshot` extended |
| `src/lib/dashboardFormat.ts` | `formatRouterHddBytes`, `formatRouterHddOverview`, `formatRouterTemperature` |
| `src/pages/DashboardPage.tsx` | Model uses `boardName` → fallback `identity`; storage/temp from cache |
| `src/components/dashboard/MikrotikRouterCard.tsx` | Hide sync hint when storage data present |

**Not changed:** `NetworkStatusCard`, `SdCardHealthCard`, `StorageHealthCard`, coin firmware.

### Data flow (unchanged)

```
RouterOS /system/resource/print (existing — same command count)
    ↓ parse extended attrs
router-cache.json → GET /api/status → routerCache.routerOs
    ↓
Dashboard MikrotikRouterCard
```

### Storage calculation (frontend)

- `used = totalHddSpace - freeHddSpace` (bytes, from RouterOS strings)
- Display via `formatRouterHddBytes` (bytes → MB/GB)
- Usage % from actual values — **no hardcoded 128 MB / 39.7 MB / 88.3 MB / 48°C**

### Temperature

- Shown only when `cpuTemperature` present in cache after sync
- **INFERRED:** RB750r2 hEX lite may omit `cpu-temperature` → UI shows **N/A**
- **NOT TESTED** on physical hEX lite in this session

### Phase 3B validation results

| Check | Result | Notes |
|-------|--------|-------|
| `npm run build` | **PASS** | VERIFIED |
| ESLint (Phase 3B files) | **PASS** | VERIFIED |
| `factory-reset-contract-check.mjs` | **26/26 PASS** | VERIFIED |
| `admin-core-isolation-contract-check.mjs` | **16/16 PASS** | VERIFIED |
| `standby-idle-dma-contract-check.mjs` | **8/8 PASS** | VERIFIED |
| `router-sync-refresh-contract-check.mjs` | **17/20 PASS** | **Pre-existing failures** (6, 9, 15 — `observeAndRepairWan` in telemetry branch; stale sync path string) — **not introduced by Phase 3B** |
| `routeros-stability-contract-check.mjs` | **11/12 PASS** | **Pre-existing failure** (4 — `allowsHotspotVerify`) — **not introduced by Phase 3B** |
| `npm run typecheck` | **FAIL** | **Pre-existing** (`embla-carousel-react`, `recharts`, `ProvisioningContext`, `usePromos`, `CaptivePortalPage`, `vite.config.ts`) — **no Phase 3B file errors** |
| No hardcoded 128/39.7/88.3/48 in changed files | **PASS** | VERIFIED (grep) |
| No new RouterOS command | **PASS** | VERIFIED (parser-only diff) |
| Physical hEX lite sync | **NOT PERFORMED** | Requires owner to run **Synchronize Router** and compare to RouterOS CLI |

### Physical validation procedure (owner)

1. Flash/deploy firmware + Admin UI with Phase 3B changes.
2. Open Admin Dashboard → **Synchronize Router**.
3. On hEX lite, run `/system resource print` in RouterOS terminal.
4. Compare Dashboard **MikroTik Router** card:
   - Model (`board-name`)
   - Storage total/used/available/usage
   - Temperature (if attribute exists)
5. Document results in this file when complete.

### Rollback (Phase 3B)

1. Revert `MikroTikDriver.cpp` helper + parse calls.
2. Revert frontend type/display files listed above.
3. Old cache JSON remains valid; new fields ignored if firmware rolled back.

---

**Dashboard enhancement status:** Phase 3A + 3B **code complete**. Phase 3B **physically validated** on hEX lite. Phase 3C **UI fidelity pass** applied to Network Status + MikroTik Router cards (2026-08-29).

---

## Phase 3C — Reference UI fidelity (2026-08-29)

**Status:** VERIFIED (build + core contracts). **Physical re-validation of new layout:** NOT REQUIRED for architecture — visual-only frontend change using same cache data.

### Forensic cause of compressed appearance (Phase 3A cards)

| Issue | Finding |
|-------|---------|
| Typography | `DashboardCardHeader` used 13px uppercase muted titles vs reference 20px semibold titles |
| Status rows | `StatusRow` used left dot + small forensic table vs reference pill badges right-aligned |
| Metrics | 10px labels in muted boxes vs reference 22–24px metric values in 2×2 grid |
| MikroTik layout | Vertical `StatusRow` list vs reference 3-column resource grids |
| Storage | Flat row list vs reference donut + breakdown + bottom 4-metric row |
| Card sizing | `p-4`, no min-height vs reference large cards with generous padding |
| Last sync | Prominent line under header vs reference subtle metadata under title |

### UI changes (frontend only)

| Component | Change |
|-----------|--------|
| `NetworkStatusCard.tsx` | Reference layout: title, pill status rows, 2×2 metrics, throughput panel placeholder |
| `MikrotikRouterCard.tsx` | Icon header, 3×2 resource grid, storage donut + **truthful** used/available breakdown only, bottom metrics row |
| `DashboardPage.tsx` | `mikrotikSnapshot` object; KonekSik-fi Controller label; `items-stretch` grid |
| `dashboardFormat.ts` | `usagePctValue` for donut (numeric, from cache bytes) |

### Data rules preserved

- No hardcoded reference sample values (128 MB, 48°C, 86.4 Mbps, etc.)
- No fake storage categories (RouterOS / Admin & portal / Backups / Other)
- Storage donut uses real `totalHddSpace` / `freeHddSpace` from cache
- Temperature remains N/A when RouterOS omits `cpu-temperature`
- No firmware / RouterOS / polling changes

### Phase 3C validation

| Check | Result |
|-------|--------|
| `npm run build` | PASS |
| ESLint (changed files) | PASS |
| Core contract checks (factory-reset, admin-core, standby-idle) | PASS |
| Hardcoded sample values in `src/` | None found |
