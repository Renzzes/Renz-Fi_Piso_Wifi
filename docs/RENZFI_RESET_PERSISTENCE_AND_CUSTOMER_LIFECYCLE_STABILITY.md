# Renz-Fi RESET Persistence & Customer Lifecycle Stability

**Date:** 2026-08-11  
**Status:** Implementation + source-verified contracts (hardware soak still required)  
**Companion baseline:** `docs/RENZFI_GURU_MEDITATION_PREVENTION_BASELINE.md`

---

## Central product rules

1. **THE CUSTOMER PATH MUST NOT DEPEND ON THE ADMIN PATH.**  
   Captive portal shell = MikroTik Hotspot. Application API = ESP32 at `http://10.10.10.2`. Admin login / Setup Wizard completion must not be required to *serve* the portal shell.
2. **NORMAL ESP32 RESET MUST NOT DESTROY OR INVALIDATE VALID PERSISTED CONFIGURATION.**  
   RESET ≠ factory reset. Do not ask operators to erase the SD to recover.
3. **A SUCCESSFUL ROUTEROS PRODUCTION ACTIVATION MUST NOT BE REPORTED AS A FAILED INSTALLATION BECAUSE OF A NON-CRITICAL CACHE OPERATION.**  
   `router-cache` is regenerable metadata for Admin dashboards.
4. **NO FIX IS ACCEPTED IF IT CREATES A NEW ESP32 TWDT/GURU MEDITATION RISK OR A SUSTAINED MIKROTIK CPU SPIKE.**

---

## Incident fixed in this pass (source-proven)

### Finish HTTP 400 after production SUCCESS

**Runtime evidence:** Finish completed `PRODUCTION ACTIVATION = SUCCESS` / `INSTALLATION READY = yes`, then:

```text
[finish-stage] BEGIN router-cache … success=false
[finish-stage] BEGIN worker finish gate … success=false
[router-worker] finished type=finish-setup-provisioning ok=no http=400
```

**Root cause (source):** `RouterProvisioningEngine::runFinishPipeline` treated `persistFinishRouterCache()` failure as a **hard return** (`ROUTER_CACHE_REFRESH_FAILED`) *after* production activation and session close. Worker finish gate then required `result.success && lifecycleReady` → HTTP 400, InstallationState never committed.

**Fix:** Router-cache persist is **best-effort / non-blocking** after production activation. Failure logs a warning and Finish continues to `commitFinishInstallationState` / `verifyProvisionedPersisted`.

| File | Function | Change |
|------|----------|--------|
| `RouterProvisioningEngine.cpp` | `runFinishPipeline` router-cache stage | Non-fatal on `persistFinishRouterCache` failure |

**Preserved:** Still uses `persistFinishRouterCache` (no fourth RouterOS session / `refreshRouterCache(true)`). Mandatory gates (production-network, commit, verify) unchanged.

### Dirty SPIFFS fallback masking valid SD after RESET

**Root cause (source):** `StorageManager::readJson` preferred a **dirty SPIFFS manifest** entry over SD even when SD was healthy/writable. Successful SD `writeJson` did **not** clear the dirty entry (`checkpointToSpiffs` refuses to replace dirty). Stale SPIFFS could therefore mask SD credentials across reboot and, via `syncFallbackToSd`, risk writing stale fallback back onto SD.

**Fix:**

1. After successful SD write → `removeFromManifest(toFallbackPath(path))` then checkpoint.  
2. On read, if dirty exists but SD is writable and holds valid JSON → clear dirty and read SD.

| File | Function | Change |
|------|----------|--------|
| `StorageManager.cpp` | `writeJson` | Clear dirty on SD success |
| `StorageManager.cpp` | `readJson` | Prefer healthy SD over stale dirty SPIFFS |

**Preserved:** Fallback still used when SD unreadable/unwritable; transactional write semantics unchanged.

### `connectionVerified` not reconstructed after RESET

**Root cause (source):** `hasVerifiedConnection()` requires `_connectionVerified && host && passwordProtected`. Flag is persisted, but if it was false while credentials were still valid (or masked by stale fallback), `resolveRouterCredentials(Persisted)` returned **“Saved MikroTik connection is unavailable”** without attempting unprotect.

**Fix:** On `SetupRouterConnectionManager::load`, if flag is false but host + protected password **successfully unprotect**, set verified and persist repair. Does **not** trust file-exists alone or UI-masked `admin`.

| File | Function | Change |
|------|----------|--------|
| `SetupRouterConnectionManager.cpp` | `load` | Integrity-based reconstruction |

### Portal exact failure messaging

| File | Change |
|------|--------|
| `portal/renzfi-app.js` | Coin start / rates show backend `err.message` (fallback strings included) |
| Generated via `scripts/build-mikrotik-portal.mjs` → `Final_Build_Portal/` | `http://10.10.10.2` substituted |

---

## Persistence contracts

### NORMAL RESET vs FACTORY RESET

| Action | SD config | NVS auth | Installation | Portal Hotspot files on MikroTik |
|--------|-----------|----------|--------------|----------------------------------|
| RESET (button) | **Keep** | **Keep** | **Keep** | Untouched |
| Factory reset (explicit product action) | Wipe per RecoveryManager contract | May reset | Factory | Untouched by ESP alone |

### Files

| File | Source of truth role | Boot load | Must survive RESET |
|------|----------------------|-----------|--------------------|
| `/config/router.json` | **Production** MikroTikDriver credentials | Driver / `ensureProductionRouterCredentials` | Yes |
| `/config/router-connection.json` | **Setup** verified connection + protected password | `SetupRouterConnectionManager::load` | Yes |
| `/config/provisioning.json` | Owner meta + setup unlock hash | `SetupProvisioningManager::load` | Yes |
| `/config/installation.json` | Lifecycle state | `InstallationStateManager` | Yes |
| `/config/router-provisioning.json` | Adoption / Wi-Fi selection | RouterProvisioningManager | Yes |
| `/config/router-cache.json` | **Cache only** — regenerable | RouterCacheManager | Nice-to-have |
| NVS `renz-auth` | Owner/operator password hashes | AuthManager | Yes |

### Setup vs production credentials

```text
router-connection.json (setup, verified)
  → syncProductionRouterCredentials / ensureProductionRouterCredentials
  → router.json (production)
  → MikroTikDriver::loadRouterCredentials
  → openRouterSession / Hotspot authorize
```

Do not casually collapse these files.

### SPIFFS fallback precedence (updated)

1. SD unreadable / using fallback-only → SPIFFS.  
2. Dirty manifest + SD **not** healthy → SPIFFS.  
3. Dirty manifest + SD writable + valid SD JSON → **SD wins**, dirty cleared.  
4. Successful SD write → dirty cleared, then optional clean checkpoint.

### Setup unlock

- Hash stored in `provisioning.json` (`setupUnlockPasswordHash`).  
- Empty hash after load falls back to factory default string hash **only when hash field empty** — must not replace a custom hash when SD is read correctly.  
- Dirty-SPIFFS fix above protects custom unlock across RESET.

---

## Finish success invariant

After `production-network` / verify succeeds:

1. Best-effort `persistFinishRouterCache` (non-blocking).  
2. Update `router-provisioning.json` hotspot flags (best-effort existing path).  
3. **Mandatory:** `commitFinishInstallationState` → Provisioned/Ready.  
4. **Mandatory:** `verifyProvisionedPersisted`.  
5. Worker finish gate: `result.success && installation->isReady()` → HTTP 200.

---

## Captive portal independence

```text
Customer → Guest Wi-Fi → MikroTik Hotspot (login.html)
        → renzfi-app.js API base http://10.10.10.2
        → ESP32 /api/portal/*
        → RouterWorker → RouterOS authorize
```

- Shell does **not** require Admin login or `InstallationState == Ready`.  
- Backend activation requires production credentials (`router.json`) — separate from shell serving.  
- Manual vs automatic browser contexts may still differ (see `MANUAL_VS_AUTOMATIC_CAPTIVE_PORTAL_FORENSIC.md`); exact browser Network capture remains a hardware step. Portal now surfaces exact API errors.

---

## Forbidden / required operations

### Forbidden on `async_tcp`

- Durable SD/SPIFFS transactions  
- RouterOS API / `_doneSem` waits  
- Heavy SPIFFS telemetry walks  

### Allowed on `router_worker`

- Finish pipeline RouterOS  
- Hotspot user authorize/deauthorize  
- Credential test/connect  

### Timer ownership

Firmware owns `secondsLeft`. Browser renders deadline. Do not reintroduce dual countdown ownership.

### Sales / Active Users

Preserve uptime-ms reporting attribution and entitlement-vs-heartbeat Active Users rules from prior implementation report.

---

## Regression tests required before future firmware changes

```text
py/platformio-python ESP32_S3_Firmware/tools/finish-router-cache-gate-check.py
py/platformio-python ESP32_S3_Firmware/tools/storage-dirty-sd-precedence-check.py
py/platformio-python ESP32_S3_Firmware/tools/router-cache-sync-check.py
node scripts/test-portal-session-lifecycle.mjs
node scripts/test-sales-uptime-aggregation.mjs
node scripts/test-active-users-entitlement.mjs
platformio run -e renzfi_developer
RENZFI_APPLIANCE_BASE_URL=http://10.10.10.2 npm run build:mikrotik-portal
```

---

## PASS/FAIL verification matrix (this pass)

### BUILD VERIFICATION

| Check | Result |
|-------|--------|
| Firmware build | **PASS** (`renzfi_developer`) |
| Portal / Admin build | **PASS** (`npm run build` + mikrotik portal build) |
| Firmware RAM | **PASS** 32.5% (106532 / 327680) |
| Firmware Flash | **PASS** 92.2% (2417687 / 2621440) |
| Static checks (finish-cache, dirty-SD, router-cache-sync) | **PASS** 3/3 |
| Portal lifecycle + sales + entitlement tests | **PASS** 22+14+6 |

### PERSISTENCE VERIFICATION

| Check | Result |
|-------|--------|
| Source: RESET ≠ factory | **PASS** |
| Source: dirty SPIFFS cannot mask healthy SD | **PASS** (code + check) |
| Source: connectionVerified reconstruct from unprotect | **PASS** |
| Hardware: router.json survives RESET | **NOT TESTED** |
| Hardware: unlock password survives RESET | **NOT TESTED** |
| Hardware: production credentials reconstruct | **NOT TESTED** |

### SETUP / FINISH

| Check | Result |
|-------|--------|
| Source: Finish does not hard-fail on router-cache after production SUCCESS | **PASS** |
| Hardware: Normal Finish → Provisioned + HTTP 200 | **NOT TESTED** (flash required) |
| Hardware: Skip Operator & Finish | **NOT TESTED** |

### CAPTIVE PORTAL / ACTIVATION / SESSION / SALES / ACTIVE USERS / TWDT

| Area | Result |
|------|--------|
| Source contracts preserved | **PASS** |
| Portal exact API errors in generated bundle | **PASS** |
| Hardware portal/rates/coin/activation/expiry | **NOT TESTED** |
| async_tcp audit of this diff | **PASS** (no new SD/RouterOS on async_tcp) |
| Guru Meditation / WDT / MikroTik CPU soak | **NOT TESTED** |

### FINAL GATE

| Gate | Result |
|------|--------|
| Source root causes proven for Finish 400 + dirty SD mask + verified reconstruct | **YES** |
| Required source fixes implemented | **YES** |
| Existing working contracts preserved (intent) | **YES** |
| Known regression introduced | **NO** (source) |
| Hardware validation required | **YES** |
| **Production readiness** | **NOT READY** |

---

## Hardware tests after flash (operator)

1. Flash `renzfi_developer` firmware (**do not** erase SD).  
2. RESET once → confirm setup unlock still works; Finish/status shows provisioned if previously finished.  
3. Run Finish (or re-Finish if needed) → must end **ok=yes http=200** even if serial shows router-cache WARNING.  
4. Guest automatic captive portal: rates, coin, Done Paying, Internet.  
5. Manual `http://10.20.0.1/login` → note exact on-screen error if any (should now be backend text).  
6. Re-upload `Final_Build_Portal/` to MikroTik Hotspot html-directory if portal JS on router is stale.  
7. 10+ minute Admin + coin soak: 0 TWDT / 0 Guru / no MikroTik 100% CPU.

---

## Automatic captive-portal regression note

> Automatic captive-portal flow is a known-good production path and must not be broken by future manual-login, portal, Hotspot, frontend, API, or router changes.
