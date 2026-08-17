# Admin Dashboard — Status + Profile Remediation

**MODE:** IMPLEMENT + BUILD + STATIC VALIDATION + DOCUMENTATION  
**Date:** 2026-08-02  
**Firmware env:** `freenove_esp32_s3_wroom`  
**Follows:** `ADMIN_DASHBOARD_SYSTEM_CONFIGURATION_FORENSIC.md` (re-audited against current source before edits)

**RELEASE VERDICT: HARDWARE VALIDATION REQUIRED**

---

## 1. Original forensic defects (D1–D5)

| ID | Defect | Root cause |
|----|--------|------------|
| **D1** | Dashboard “Configured” / Config “Connected” conflated host/cache with live reachability | `/api/status` set `mikrotik.ok=true` whenever cache populated; UI treated `ok` as Connected |
| **D2** | Hotspot reported healthy from host/cache alone | `hotspot.ok = cachePopulated() ? true : routerConfigured` |
| **D3** | Test Connection verified one named profile; System Configuration could not list all profiles after Test | `?name=<profile>` only; no inventory; no cache profile fill from Test |
| **D4** | No rate-limit in cache/UI; empty rate-limit not shown as “Not set yet” | Cache stored `profiles: string[]` only |
| **D5** | Admin wireless reads used unbounded `/interface/wireless/print` | Full print risked `reply attr limit reached (max=24)` |

**Re-audit:** All five were still present in source before this remediation. No architecture rewrite was required.

---

## 2. Exact implementation

### D1 — Configured vs Online

**Configuration state:** `mikrotik.configured` / legacy `mikrotik.ok` = host credentials present (persisted router settings).

**Connectivity state:** `mikrotik.connectivity` = `online` | `offline` | `unknown` from cache `observation` only.

After reboot with configured host and no live RouterOS contact: **Configured + Unknown**.

Updated only when a worker RouterOS operation observes success/failure (Test, Sync, etc.). **No periodic MikroTik polling.**

### D2 — Hotspot health

`hotspot.status` = `available` | `unavailable` | `unknown` from observation.

Set `available` only after a successful same-session Hotspot observation (`/ip/hotspot/print` with `.proplist=name,interface,disabled`) finds an enabled server (any interface — bridge-aware). Not inferred from cache population or host presence.

### D3 / D4 — Test Connection + profiles

**One RouterOS session** per Test:

1. open session  
2. `/system/identity/print`  
3. `/system/resource/print`  
4. `/ip/hotspot/user/profile/print` `=.proplist=name,rate-limit`  
5. verify configured profile name exists in inventory  
6. `/ip/hotspot/print` `=.proplist=name,interface,disabled` (observation; does not fail Test alone)  
7. close session  
8. apply cache snapshot (identity, routerOs, profiles, profileDetails, observation)

**No** `refreshRouterCache()` / second reconnect after Test.

Worker result buffer bumped to `JSON_DOC_MEDIUM` so profileDetails fit.

### Rate-limit UX

- Non-empty RouterOS `rate-limit` → display as returned (e.g. `5M/5M`)  
- Empty / missing → **Not set yet**  
- Profile list status → **Available** (exists in latest successful inventory)

Real-hardware acceptance examples (dynamic, not hardcoded):

- `default` empty → Not set yet  
- `test1` → 5M/5M  

### D5 — Wireless `.proplist`

Admin/sync paths now bound prints (examples):

- find: `.id,name`  
- readInterface / updateInterface: filtered `?name=` / `?.id=` + needed fields  
- sync/cache wireless fallback: `.id,name,ssid,security-profile,band,radio-name,...`  
- `queryWirelessInterfaceState` already had proplist (unchanged)

`MAX_ATTRS` remains **24** (not increased).

Residual unbounded/optional wireless prints may remain in Setup-only inventory probes; Admin Test/Save/Sync paths are the remediation focus.

---

## 3. Router observation / freshness model

Cache schema **v2** (`RouterCacheManager::SCHEMA_VERSION = 2`):

```json
{
  "schemaVersion": 2,
  "profiles": ["default", "test1"],
  "profileDetails": [
    { "name": "default", "rateLimit": "" },
    { "name": "test1", "rateLimit": "5M/5M" }
  ],
  "observation": {
    "connectivity": "online|offline|unknown",
    "hotspotStatus": "available|unavailable|unknown",
    "lastSuccessfulContactAt": "...",
    "lastContactAttemptAt": "...",
    "lastContactError": "",
    "hotspotServer": "...",
    "hotspotInterface": "..."
  }
}
```

24-hour stale threshold unchanged. Stale cache may still display; refresh is explicit (Test / Sync / Refresh Profiles from cache) — never automatic MikroTik poll.

---

## 4. Test Connection — before / after

| | Before | After |
|--|--------|-------|
| Sessions | 1 (then historically a second refresh — already removed) | **1** |
| Commands | identity + `?name=` profile + resource | identity + resource + bounded profile inventory + bounded hotspot print |
| Profiles in UI | Not filled by Test | Filled via cache `profileDetails` |
| Second reconnect | No (post prior hardening) | **No** |

---

## 5. Idle System Configuration

Opening / leaving the page idle:

- RouterOS sessions/min = **0**  
- RouterOS commands/min = **0**  
- ESP-local `/api/status` / React Query polling = allowed  
- MikroTik polling = **none**

---

## 6. AsyncTCP / worker

Admin mutations remain:

`HTTP 202` → `enqueueAdmin*` → `router_worker` → `GET /api/router/jobs/<id>`

No `dispatchAdmin` / `xSemaphoreTake` RouterOS wait on async_tcp for Admin Test/Save/Wireless/Sync.

---

## 7. CPU / DMA / memory

- Prefer `.proplist` and filtered queries  
- Profile replies bounded by `MAX_REPLY_RECORDS` (32); truncation surfaced  
- Cache doc capacity 3072 for structured profiles  
- No MAX_ATTRS increase  
- No idle RouterOS storm  

---

## 8. Backward compatibility

Old `router-cache.json` with `profiles: ["default", ...]` migrates on load to:

- `profiles` name array  
- `profileDetails` with empty `rateLimit`  

API remains additive: `profiles` (names) + `profileDetails` (structured).

---

## 9. Files modified

**Firmware**

- `src/RouterCacheManager.h` / `.cpp`  
- `src/router/drivers/MikroTikDriver.h` / `.cpp`  
- `src/router/RouterPlatform.cpp`  
- `src/ApiServer.cpp`  
- `src/RouterWirelessAdapter.cpp`  
- `src/RouterProvisioningWorker.cpp`  

**Frontend**

- `src/types/api.ts`  
- `src/lib/systemConfigurationStatus.ts`  
- `src/services/router.ts`  
- `src/pages/DashboardPage.tsx`  
- `src/pages/SystemConfigurationPage.tsx`  

**Docs**

- this file  

---

## 10. Build result

```
platformio run -e freenove_esp32_s3_wroom → SUCCESS (~43s)
RAM:   31.8% (104140 / 327680)
Flash: 84.2% (2207239 / 2621440)
```

Pre-existing ArduinoJson `DynamicJsonDocument` deprecation warnings remain; not treated as task failures.

---

## 11. Static test matrix (static / code-level)

| Test | Result |
|------|--------|
| A Idle open System Configuration → 0 RouterOS | **PASS (static)** — GETs cache/local; status ESP-only |
| B Configured + no observation after boot → Unknown | **PASS (static)** — default connectivity unknown |
| C Successful Test → 1 session, profiles+rates, no second connect | **PASS (static)** — code path; needs hardware proof |
| D Wrong password | **PASS (static)** — offline observation; no password persist on failed test |
| E Unreachable MikroTik | **PASS (static)** — job fails on worker; HTTP non-blocking |
| F empty rate-limit → Not set yet | **PASS (static)** — UI `formatRateLimit` |
| G `5M/5M` display | **PASS (static)** |
| H Multiple arbitrary profiles | **PASS (static)** — dynamic inventory |
| I Hotspot deleted, router up | **PASS (static)** — online + hotspot unavailable |
| J Wireless disabled vs router online | **PASS (static)** — separate observations |
| K Wireless Save one session + proplist | **PASS (static)** — prior hardening + proplist |
| L Idle 15 min | **PASS (static)** — no MikroTik timer |
| M Rapid Test clicks | **PASS (static)** — button disabled + worker BUSY |
| N Test during cooldown | **PASS (static)** — worker waits; HTTP returns 202 |
| O Old string profiles cache | **PASS (static)** — `normalizeProfilesInDoc` |

---

## 12. Static search notes

| Pattern | Notes |
|---------|-------|
| `/interface/wireless/print` | Admin/sync paths use `.proplist` / filters; some Setup inventory probes may still be broader |
| `/ip/hotspot/user/profile/print` | Test/list/sync use `=.proplist=name,rate-limit` |
| `refreshRouterCache` | Not called after Test |
| `dispatchAdmin` / `xSemaphoreTake` | Not in Admin HTTP RouterOS path |
| `10.40.0.2` | Legacy defaults in Models/Backup/Storage templates only — not reintroduced as Admin production dependency |
| `MAX_ATTRS` | Unchanged at 24 |

---

## 13. Hardware tests still required

1. Flash `freenove_esp32_s3_wroom` + matching web assets.  
2. Boot configured unit → Dashboard shows Configured + Connectivity Unknown; Hotspot Unknown.  
3. Test Connection against real MikroTik with `default` (empty rate-limit) and `test1` (`5M/5M`) → UI matches; Serial shows **one** session.  
4. Wrong password / unplug Ethernet → Offline or failed job without TWDT / Guru Meditation.  
5. Idle System Configuration 15 minutes → zero RouterOS sessions.  
6. Wireless Save → one session, SSID updates, no second refresh.  
7. Confirm captive portal / coin / voucher / Setup Finish unchanged.

---

## 14. Regression checklist (intent)

| Area | Intent |
|------|--------|
| Setup Wizard / Finish | Unchanged flow |
| Production Network | Unchanged contract |
| Admin Dashboard | Status semantics + profile UI only |
| Captive Portal / Coin / Voucher / Pause | Untouched |
| Rates / Branding / Portal Verify / Unlock | Untouched |
| Idempotent provisioning | Untouched |
| W5500 / SPI | No large inventory reintroduced on Admin idle/Test |

---

## 15. Verdict

Implementation and firmware build succeeded. Behavior is observational, bounded, and async-worker safe by static analysis.

**HARDWARE VALIDATION REQUIRED** before production release.
