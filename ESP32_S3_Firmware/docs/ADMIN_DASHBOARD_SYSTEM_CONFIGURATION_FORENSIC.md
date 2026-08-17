# Admin Dashboard — System Status & System Configuration Forensic Audit

**MODE:** Investigation only. **NO CODE CHANGED.**

**Date context:** Post Admin async-worker hardening (enqueueAdmin* / HTTP 202).  
This document re-verifies current source; it does not trust prior docs blindly.

---

## 1. EXECUTIVE RESULT

| Area | Verdict |
|------|---------|
| Admin RouterOS mutations async (test/save/wireless/sync) | **CONFIRMED** — `enqueueAdmin*` + 202 + poll |
| Opening System Configuration RouterOS storm | **NONE on mount** — GETs cache/local; status poll is ESP-local |
| Dashboard “MikroTik Router: Configured” | **NOT live reachability** — host present + `mikrotik.ok` (credentials/cache-biased) |
| Test Connection → all profiles appear | **NOT IMPLEMENTED** — Test verifies one named profile only; does not fill profiles cache |
| Default Profiles speed limits / “Not set yet” | **NOT IMPLEMENTED** — UI is name-only select; cache stores name strings only |
| Wireless Admin GET | Cache + canonical SD; no live RouterOS |
| Wireless Save | One session set + same-session verify; `applyWirelessFields` |
| Hotspot status on `/api/status` | Cache/credential heuristic — **not** live hotspot/bridge topology check |
| Idle MikroTik CPU from System Configuration | **Low** if SSE up (config GETs once); `/api/status` every 30s is ESP-only |
| AsyncTCP TWDT on Admin mutations | **Mitigated** for Admin routes; residual risk = Setup debug `dispatch` only |

**RELEASE VERDICT: HARDWARE VALIDATION REQUIRED**  
Static audit finds **confirmed semantic/UX gaps** (Configured ≠ reachable; Test ≠ profile inventory; no rate-limit UI) but **no confirmed remaining Admin async_tcp→RouterOS blocking path**. Hardware must still prove TWDT-free under Tests A–J style load.

---

## 2. SYSTEM STATUS FORENSIC — “Configured / Not Configured”

### UI surfaces (two different labels)

| Location | Component | Label when host set | Label when no host |
|----------|-----------|---------------------|--------------------|
| Dashboard → System Status | `DashboardPage.mikrotikDisplay` | `mikrotik.ok ? "Configured" : "Unavailable"` | `"Not Configured"` |
| System Configuration → Status | `mikrotikApiStatusDisplay` | `mikrotik.ok ? "Connected" : "Disconnected"` | `"Not Configured"` |

### Backend `/api/status` (`ApiServer.cpp`)

```text
load ROUTER_FILE → routerHost, routerConfigured = (host.length() > 0)
mikrotik.ok   = routerConfigured
mikrotik.host = routerHost
if cachePopulated():
  fillHealthStatus(mikrotik)   // adds configured, status, cache metadata
  mikrotik.ok = true           // FORCES ok when cache populated
hotspot.ok = cachePopulated() ? true : routerConfigured
```

### Exact meaning of Dashboard “Configured”

**Boolean chain (Dashboard):**

1. `loading` → “Loading…”
2. `!mikrotik` → “—”
3. `!host.trim()` → **“Not Configured”**
4. else → **“Configured”** iff `mikrotik.ok`, else **“Unavailable”**

**What `mikrotik.ok` means today:**

- Primarily: **host string exists in ESP32 router settings (SD)** (`routerConfigured`).
- If router cache is populated: **`ok` is forced `true`** regardless of live ping/login.
- **Does NOT** mean: live TCP to MikroTik, correct password, hotspot present, or wlan1 running.

`fillHealthStatus` sets `configured = _healthConfigured` where `_healthConfigured` is also **host length > 0** (`refreshHealthCache`), and `status = configured ? "connected" : "detected"` — again **not** a live probe.

### Case matrix

| Case | EXPECTED (reachable vs configured) | CURRENT | RESULT |
|------|--------------------------------------|---------|--------|
| 1. Configured + MikroTik reachable | Configured / Connected | Host set → Configured/Connected | **PASS** (coincides) |
| 2. Configured + MikroTik unplugged | Should show unreachable if live | Still Configured/Connected if host/cache | **STALE-RISK / FAIL** vs live semantics |
| 3. Creds stored, wrong password | Unreachable / auth fail | Still Configured if host set | **STALE-RISK** |
| 4. Correct IP, wrong password | Auth fail | Same | **STALE-RISK** |
| 5. Reachable, Hotspot deleted | Hotspot bad; router may still “configured” | `hotspot.ok` true if cache/host | **FAIL** for hotspot |
| 6. wlan1 deleted/disabled | Wireless bad | Status doesn’t live-check wireless | **STALE-RISK** |
| 7. ESP SD OK, MikroTik factory reset | Stale config | Still Configured | **STALE-RISK** |
| 8. MikroTik OK, ESP SD erased | Not Configured | No host → Not Configured | **PASS** |
| 9. Stale cache says ok, offline | Misleading | `ok=true` if cache populated | **CONFIRMED STALE-RISK** |
| 10. Reboot after install | Configured if host+cache persist | Host/cache from SD | **PASS** for “credentials exist” |

**Conclusion:** Treat **Configured** and **currently reachable** as separate concepts. Current UI conflates them toward “credentials/cache exist.”

---

## 3. SYSTEM CONFIGURATION PAGE LOAD TRACE

```text
SystemConfigurationPage mount
  → useQuery ["router","settings"]     GET /api/router/settings     (local SD)
  → useQuery ["router","cache"]        GET /api/router/cache        (cache RAM/SD)
  → useQuery ["router","wireless"]     GET /api/router/wireless     (cache + canonical SD)
  → useQuery ["system","status"]       GET /api/status              (ESP local; 30s interval)
  → useQuery ["system","wifiConfig"]   GET /api/system/wifi/config  (ESP network settings)
  → useQuery ["router","profiles"]     GET /api/router/profiles     (cache names only)
```

`CONFIG_QUERY_OPTIONS`: `staleTime: Infinity`, `refetchOnMount: false` — config GETs **once per visit** (until invalidate).

`system/status` on this page: `refetchInterval: 30_000` **always** (independent of SSE).

**On mount RouterOS sessions = 0, RouterOS commands = 0** (proven: handlers use fillPublicSettings / fillRouterCache / fillWireless / listProfiles-from-cache / status without RouterOsClient).

---

## 4. FRONTEND REQUEST MATRIX (System Configuration)

| Endpoint | Trigger | Query key | staleTime | refetchInterval | SSE invalidation | RouterOS? |
|----------|---------|-----------|-----------|-----------------|------------------|-----------|
| GET `/api/router/settings` | mount | router/settings | ∞ | none | no | No |
| GET `/api/router/cache` | mount | router/cache | ∞ | none | no | No |
| GET `/api/router/wireless` | mount | router/wireless | ∞ | none | no | No |
| GET `/api/router/profiles` | mount | router/profiles | ∞ | none | no | No |
| GET `/api/system/wifi/config` | mount | system/wifiConfig | ∞ | none | no | No |
| GET `/api/status` | mount + timer | system/status | default | **30s** | sales/coin/etc. (not router.job) | No |
| POST `/api/router/test` | button | mutation + job poll 1s | n/a | while pending | router.job unused by map | **Yes (worker)** |
| PUT settings/wireless | button | mutation + poll | n/a | while pending | no | wireless: Yes; settings: No |
| POST cache sync/refresh | button | mutation + poll | n/a | while pending | no | **Yes** |

Dashboard (separate page): `useSystemStatus` uses `fallbackPollMs` → **false when SSE connected**.

`EVENT_QUERY_MAP` does **not** include `router.job` — Admin job completion relies on **HTTP poll**, not SSE invalidation.

### Idle frequency estimate (System Configuration open, SSE up)

| Window | HTTP | RouterOS cmds | RouterOS sessions |
|--------|------|---------------|-------------------|
| 5 s | mount burst (~6) + maybe 0–1 status | 0 | 0 |
| 30 s | +1 `/api/status` | 0 | 0 |
| 5 min | ~10 `/api/status` | 0 | 0 |

---

## 5. ADMIN API ENDPOINT MATRIX (re-verified)

| Endpoint | Sync HTTP? | Worker | RouterOS |
|----------|------------|--------|----------|
| GET settings/cache/wireless/profiles | Yes, local | No | No |
| POST test | **202 enqueueAdminTest** | Yes | 1 session |
| PUT/POST settings | **202 enqueueAdminSaveSettings** | Yes | SD only |
| PUT/POST wireless | **202 enqueueAdminSaveWireless** | Yes | 1 session |
| POST cache/sync\|refresh | **202 enqueueAdminSyncCache** | Yes | 1 full snapshot |
| GET `/api/router/jobs/*` | Yes poll | No | No |

No Admin route calls `dispatchAdmin*` (removed). Residual sync `dispatch()`: Setup `runApply` / compile-gated diags only.

---

## 6. ROUTEROS COMMAND MATRIX (Admin-triggered)

| Operation | Commands (current source) | Sessions |
|-----------|---------------------------|----------|
| Test | `/system/identity/print`; `/ip/hotspot/user/profile/print` **`?name=<configured>`**; `/system/resource/print` | 1 |
| Wireless save | wireless print/set + security read path via `updateInterface`/`readInterface` | 1 |
| Synchronize | identity; resource; wireless (targeted or full print); optional security-profiles; `/ip/hotspot/print`; `/ip/hotspot/profile/print ?name=`; `/ip/hotspot/user/profile/print` (all names) | 1 |
| Page open GETs | none | 0 |

---

## 7. TEST CONNECTION TRACE

```text
SystemConfigurationPage testMutation
  → toRouterTestPayload(form)  // password only if non-empty typed
  → routerApi.test → POST /api/router/test → 202 {jobId}
  → poll GET /api/router/jobs/<id> @ 1s
  → worker AdminTestConnection
  → RouterPlatform::test → MikroTikDriver::testSettings
       mergeSettings(override)  // in-memory only for this op
       openRouterSession
       /system/identity/print
       /ip/hotspot/user/profile/print ?name=<profile>
       /system/resource/print
       closeRouterSession
  → applyLiveSnapshot(identity + routerOs only)  // NOT profiles array
  → emit Connected / profileUpdated SSE events
```

| Question | Answer |
|----------|--------|
| Password copied before HTTP returns? | Yes — body into `_slot.requestJson` at enqueue |
| Persisted before validation? | **No** — Save persists; Test merges override for session only |
| Wrong password? | openRouterSession/login fails → error in job result |
| Sessions? | **1** |
| refreshRouterCache? | **No** |
| Cooldown? | Only if connect throttle from prior connect within 5s |
| async_tcp wait? | **No** (202) |
| Duplicate clicks? | Second enqueue → `503 ROUTER_WORKER_BUSY` while busy |
| Profiles list updated? | **No** |
| System Status after success? | Host/`ok` already true if credentials exist; cache identity/cpu fields update; does **not** prove live status model change |

---

## 8–9. WIRELESS READ / WRITE

### Displayed / editable (System Configuration)

| UI | Source | Writable |
|----|--------|----------|
| SSID | cache/canonical | Yes |
| Password | form (not always returned) | Yes if non-empty |
| Security | cache/canonical (display) | Not direct edit |
| Summary band/freq | wireless + productionNetwork cache | Read-only summary |

### Read path (GET)

`RouterPlatform::fillWireless` → cache `fillWireless` + overlay `RouterWireless` canonical from SD. **No** `_active->fillWireless` live call on Admin GET.

### Write path

```text
one session → updateInterface (set ssid/security) → readInterface verify → close
→ applyWirelessFields(out)
```

**Not** disconnect → reconnect → full refreshRouterCache.

### Attribute truncation risk

`readInterface` / sync path use **`/interface/wireless/print` without `.proplist`**.  
`RouterOsClient::MAX_ATTRS = 24` — **POTENTIAL RISK** of `reply attr limit reached` on fat wireless rows (same class as prior production-network issue). Admin wireless save still works if needed attrs appear early; silent truncation possible for later attrs.

---

## 10. NETWORK SETTINGS TRACE

System Configuration “network” block uses **`/api/system/wifi/config`** → `NetworkSettingsManager` + live `EthernetManager` current IP/gw/mask/dns/mac.

**Authoritative source: ESP32** (saved address mode/static fields on SD/NVS path; `current.*` from W5500 runtime).

**Not** MikroTik `/ip/address` inventory.

Save → ESP settings only; may note reboot required. No RouterOS session.

---

## 11. HOTSPOT STATUS TRACE

`/api/status`:

```text
hotspot.ok = cachePopulated() ? true : routerConfigured
```

Comment in source: *“cached provision status — no live RouterOS probe.”*

**Not bridge-aware** against `hotspot-renzfi` / `bridgeGuest` / wlan1 member.  
**Inconsistency risk:** Setup Finish accepts bridge fallback; Admin status does not validate hotspot interface topology.

Cache sync stores `hotspotServer`, `bridge` (from hotspot interface or provisioning hint), `hotspotProfile`, `htmlDirectory`.

---

## 12–13. DEFAULT PROFILES & RATE-LIMIT

### UI

“Default Profile” = `<Select>` of **string names** from `GET /api/router/profiles` → `profiles: string[]`.

**No** Speed Limit column. **No** “Not set yet”. **No** per-profile status beyond select options / truncated warning.

### Data path

| Step | Behavior |
|------|----------|
| GET profiles | `RouterPlatform::listProfiles` → **cache only**; error if cache empty |
| Synchronize | `collectCacheSnapshot` → `/ip/hotspot/user/profile/print` → `profileNamesFromResult` extracts **`name` only** |
| Test Connection | `/ip/hotspot/user/profile/print ?name=<one>` — existence check only |

**rate-limit is never read into cache or UI.**

| Desired UX | Current |
|------------|---------|
| Test → profiles list appears | **FAIL** — need Synchronize (or prior sync) to populate names |
| Show rate-limit / “Not set yet” | **FAIL** — not implemented |

---

## 14. CACHE / FRESHNESS

| Item | Behavior |
|------|----------|
| Storage | `RouterCacheManager` document; `applyLiveSnapshot` / `save()` → SD `router-cache.json` |
| Populated | host or provisionStatus non-empty |
| Stale | `ROUTER_CACHE_STALE_THRESHOLD_HOURS = 24` via epoch age |
| Test updates | identity + routerOs fields |
| Wireless save | wireless fields |
| Sync | full snapshot including profiles **names** |
| Offline invalidation | **None** — no live fail clears `ok` |
| Survive reboot | Yes if SD healthy |

---

## 15. SYNCHRONIZE / REFRESH TRACE

Both POST `/api/router/cache/sync` and `/refresh` → same `enqueueAdminSyncCache` → `synchronizeRouterCache` → `collectCacheSnapshot` (commands in §6) → `applyLiveSnapshot`.

One session; may hit connect cooldown if recent connect. Unbounded wireless/user-profile prints → attr/reply limit **POTENTIAL RISK**.

---

## 16. ASYNCTCP / TWDT AUDIT

| Endpoint | async_tcp work | PASS/FAIL |
|----------|----------------|-----------|
| Admin GET * | JSON from RAM/SD | **PASS** |
| Admin mutations | enqueue + 202 | **PASS** |
| Job poll | read `_lastJob` | **PASS** |
| Setup `runApply` etc. | may still `dispatch()` | Out of Admin scope |

No Admin HTTP path found that calls `executeCommand` / `connect` / long `xSemaphoreTake(_doneSem)` on async_tcp.

---

## 17. MIKROTIK CPU LOAD AUDIT

| Source | Frequency | Session? | Impact |
|--------|-----------|----------|--------|
| System Configuration idle GETs | once | No | None |
| `/api/status` on Config page | 30s | No | None on MikroTik |
| Test / Save wireless / Sync | user | Yes | Bounded bursts |
| Duplicate clicks while busy | 503 | No extra | Protected |
| Dashboard with SSE | status poll off | No | Good |

**Highest-risk Admin op for MikroTik CPU:** Synchronize (multi-print).  
**Potential 100% CPU trigger from merely leaving System Configuration open:** **Not from RouterOS** (source-proven). ESP status poll only.

---

## 18. ESP32 MEMORY / DMA AUDIT

| Item | Class |
|------|-------|
| Admin GET handlers | BOUNDED / PROVEN SAFE for RouterOS |
| Job result String body | BOUNDED by job slot |
| Wireless full print attrs | **POTENTIAL RISK** (MAX_ATTRS=24) |
| User profile print many attrs | **POTENTIAL RISK** if many attrs/record; names-only parse mitigates functional need |
| No confirmed new DMA defect in Admin Config paths from source alone | — |

---

## 19. CROSS-MODULE CONSISTENCY

| Concept | Setup / Finish | Admin |
|---------|----------------|-------|
| Hotspot on bridgeGuest | Accepted | Status ignores topology |
| Wireless running semantics | monitor-once / tri-state | Not used in Admin status |
| “Connected” | Live session in Finish | Host/cache heuristic |
| Profiles | Inventory in sync/setup | Names via sync only; Test ≠ list |

---

## 20. POSSIBLE DEFECTS (no fixes)

| ID | Severity | Evidence | Impact | Confidence |
|----|----------|----------|--------|------------|
| D1 | Medium | `/api/status` forces `mikrotik.ok=true` if cache populated; UI “Configured”/“Connected” | Offline router still looks configured/connected | **CONFIRMED** |
| D2 | Medium | `hotspot.ok` = cache/host heuristic | False Online hotspot | **CONFIRMED** |
| D3 | High (UX) | Test does not populate `profiles[]`; only verifies one profile | Expected “Test → profiles appear” fails | **CONFIRMED** |
| D4 | Medium (UX) | `profileNamesFromResult` / UI name-only | No rate-limit / “Not set yet” | **CONFIRMED** |
| D5 | Low–Med | Wireless print without `.proplist` | Attr truncation risk | **HIGH** (risk) |
| D6 | Low | System Configuration polls `/api/status` every 30s even with SSE | Extra ESP HTTP; not MikroTik | **CONFIRMED** (non-MikroTik) |
| D7 | Info | `router.job` SSE not in EVENT_QUERY_MAP | Relies on poll; OK if poll works | **CONFIRMED** |

---

## 21. NON-ISSUES

1. Opening System Configuration does **not** create RouterOS command storms (GETs cache/local).
2. Admin mutations use **async enqueue** (prior TWDT root cause addressed for Admin).
3. Cooldown `waitUntilConnectAllowed` remains on worker with `vTaskDelay` — appropriate.
4. Router settings Save is local SD — does not need RouterOS for persistence.
5. Wireless Save uses one session + verify (no second refreshRouterCache).
6. Queue depth 1 + BUSY prevents Admin click storms to MikroTik.

---

## 22. HARDWARE VALIDATION REQUIRED (read-only first)

1. Idle System Configuration 5–15 min — MikroTik `/system resource print` + ESP `[mem]` — expect no RouterOS connect spam.
2. One Test Connection — identity/profile/resource only; no WDT.
3. Confirm profiles dropdown empty/stale until Synchronize.
4. One Synchronize — profiles names appear; observe CPU.
5. Unplug MikroTik — Dashboard still “Configured” if host/cache (confirms D1).
6. Wireless Save once — SSID updates; one session in logs.

---

## 23. RELEASE VERDICT

**HARDWARE VALIDATION REQUIRED**

Static Admin TWDT architecture for System Configuration mutations: **PASS WITH OBSERVATIONS**.  
Semantic/UX correctness of Status + Default Profiles vs product expectations: **FAIL — CONFIRMED DEFECTS (D1–D4)** pending product decision (not implementation in this task).

---

## FILES REVIEWED

`DashboardPage.tsx`, `SystemConfigurationPage.tsx`, `systemConfigurationStatus.ts`, `routerConfig.ts`, `router.ts`, `useSystemStatus.ts`, `useDashboardEvents.ts`, `ApiServer.cpp`, `RouterPlatform.cpp`, `MikroTikDriver.cpp`, `RouterCacheManager.cpp`, `RouterWirelessAdapter.cpp`, `RouterProvisioningWorker.*`, `RouterOsClient.h`, `Config.h`

**NO CODE CHANGED: YES**
