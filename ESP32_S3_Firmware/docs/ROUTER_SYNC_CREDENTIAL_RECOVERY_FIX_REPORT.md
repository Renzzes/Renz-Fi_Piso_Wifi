# IMPLEMENTATION REPORT
## Router Sync Credential Loading + Recovery-Gate Fix

**Date:** 2026-08-17  
**Status:** Code fix complete. **Physical validation is still required.**  
**SD baseline (do not regress):** `docs/SD_HOTUNPLUG_STABLE_BASELINE.md`

This report does **not** claim physical validation of the router-sync fix.

---

## 1. Observed physical failure

SD hot-unplug is now stable (Ethernet UP, ping 10.10.10.1, HTTP responsive, no WDT, no Guru, no `emac_w5500_task` DMA panic, remount → `SD_READY`).

A **separate** router synchronization failure then appeared:

```
[router-refresh] mode=telemetry opening session
[WARN] router: Router telemetry refresh failed: RouterOS API username is not configured
[router-refresh] ok=no stage=admin-refresh-cache reason=telemetry-or-persist-failed
[ros-health] state=DEGRADED reason=job_failed failures=1
[router-worker] finished type=admin-refresh-cache ok=no http=503
```

After remount (`SD_READY`, storage `HEALTHY`):

```
[router-worker] admin job deferred reason=recovery
```

Admin UI continued posting `POST /api/router/cache/refresh` and `POST /api/router/cache/sync`.

Device state at the time: `lifecycle=ProductionReady`, `eth_link=up`, `eth_ip=10.10.10.2`, `install=provisioned`.

Refresh Router Information and Synchronize Router both enqueue the same worker family (`AdminRefreshCache` / `AdminSyncCache`) and both reach `MikroTikDriver::collectCacheSnapshot()`.

---

## 2. Credential loading root cause

Proven from source, not guessed.

### Two existing files (not two new stores)

| File | Role | Contents | Reader |
|------|------|----------|--------|
| `/config/router-connection.json` | Setup-verified encrypted credentials | `username` + `passwordProtected` (`CredentialProtector`) | `SetupRouterConnectionManager` |
| `/config/router.json` (`RenzFiConfig::ROUTER_FILE`) | Production RouterOS API settings | plaintext `host` / `username` / `password` | `MikroTikDriver::loadRouterCredentials()` |

Seeded default `kDefaultRouter` has **empty username and password**.

Production telemetry/sync path:

```
POST /api/router/cache/refresh
  → enqueueAdminRefreshCache
  → RouterPlatform::refreshRouterTelemetry()
  → MikroTikDriver::collectCacheSnapshot(Telemetry)
  → loadRouterCredentials()          // reads /config/router.json only
  → openRouterSession()
       if username empty → "RouterOS API username is not configured"
```

Sync uses `synchronizeRouterCache(false)` → `refreshRouterCache(false)` → `collectCacheSnapshot(Configuration)` → the same `loadRouterCredentials()`.

Admin Router Settings already writes `/config/router.json` via `MikroTikDriver::saveSettings()`. That is the same object MikroTikDriver reads. Refresh/Sync do **not** send the Admin form username; they use stored `router.json`.

Reconcile already existed: `RouterProvisioningEngine::ensureProductionRouterCredentials()` copies setup-verified credentials into `/config/router.json` via `syncProductionRouterCredentials()`. Called at boot and by hotspot jobs.

### Sticky-cache bug (this is the username failure)

```cpp
if (_productionCredentialsOk) return true;  // never re-read disk
```

After SD hot-unplug/remount, `readJson` can serve a dirty SPIFFS fallback of `router.json` (fallback-eligible, seeded with empty username). `_productionCredentialsOk` stayed `true` from boot, so reconcile was skipped. `MikroTikDriver` then read an empty username.

The log was **not** `"Router settings not available"` (file missing). It was `"RouterOS API username is not configured"`, which only fires when the file **was read** and `username` was empty.

The Admin UI previously substituted `username: raw.username?.trim() || "admin"` for display only. That hid an empty stored username and was **not** sent on Refresh.

No username `"admin"` was hardcoded in firmware. No authentication bypass was added.

---

## 3. Recovery-state root cause

`RouterApiTransportGate::allowsAdminNonEssential()` returned true **only** for ROS health `Healthy` or `Unknown`.

Failed telemetry called `endJob(false)` → `noteJobFailure("job_failed")` → ROS health **DEGRADED**.

Every later `enqueueAdmin*` then logged `"admin job deferred reason=recovery"` even when:

- SD lifecycle was already `SD_READY`
- Storage health was `HEALTHY`
- ROS health was **DEGRADED**, not `Recovering`

Admin jobs therefore stayed blocked after SD recovery. Health probe also needs credentials, so it could fail again and keep the worker permanently unusable from the Admin UI. The UI retried Refresh/Sync, which all returned generic `503 ROUTER_WORKER_BUSY`.

This was a **gate mix-up**: ROS job-failure health was treated as “storage recovery”.

---

## 4. Implementation changes

No SD DMA, W5500 DMA, SPI bus, pin, WDT, StorageManager conflict policy, or RouterOsClient stack hardening changes.

### Credential path

- `ensureProductionRouterCredentials()` always re-reads `/config/router.json`.
- Incomplete host/username/password → reconcile from encrypted `router-connection.json` into `router.json` (existing `syncProductionRouterCredentials()`).
- Sticky `_productionCredentialsOk` short-circuit removed.
- Hotspot credential helper no longer trusts the in-memory OK flag.
- Admin Refresh/Sync/HealthProbe call `ensureProductionRouterCredentials()` on the worker **before** RouterOS I/O.
- Safe diagnostics only:

```
[router-config] host=… port=8728 usernameConfigured=no|yes usernameLength=N
                passwordConfigured=no|yes passwordLength=N source=<source>
```

Sources: `router.json`, `reconciled-from-setup`, `production-router-json`. Never password, encrypted blob, or token.

Canonical chain (unchanged files, fixed load/reconcile):

```
Admin Router Settings
  → /config/router.json
  → MikroTikDriver::loadRouterCredentials()
  → RouterOsClient (production-router-json)

Setup-verified /config/router-connection.json
  → ensureProductionRouterCredentials() when router.json is incomplete
  → same /config/router.json
```

No parallel credential store was created.

### Recovery gate

Admin enqueue is gated on **SD lifecycle only**:

- Defer: `Mounting`, `Remounting`, `Syncing`, `Degraded`
- Release: `Ready` (and other non-recovery states)

ROS health is **not** auto-set HEALTHY because SD recovered.

After the gate releases, Refresh/Sync run normal telemetry. Health mapping:

| Condition | ROS health | reason |
|-----------|------------|--------|
| Credentials missing | DEGRADED | `credentials_missing` |
| Auth success | HEALTHY | (existing success path) |
| Router unreachable | DEGRADED | `router_unreachable` |
| Auth failed | DEGRADED | `auth_failed` |

### HTTP / storm control

When the storage recovery gate is active, enqueue is **rejected** (not queued) with:

- HTTP 503
- `code`: `ROUTER_RECOVERY_IN_PROGRESS`
- `message`: `Router operations are temporarily unavailable while recovery is in progress.`

If the single-slot worker is already running, duplicates are rejected with `503 ROUTER_WORKER_BUSY`.

Routes unchanged:

- `POST /api/router/cache/refresh`
- `POST /api/router/cache/sync`
- `GET /api/router/jobs/{id}`

Architecture unchanged: HTTP callback → enqueue → `RouterProvisioningWorker` → RouterOS → job result → poll.

### Admin UI

- Refresh/Sync toasts show `err.message` (structured backend error).
- Production settings form no longer fills empty username with `"admin"`.

---

## 5. HTTP behavior

| Situation | Status | code | Enqueued? |
|-----------|--------|------|-----------|
| SD recovering (`Degraded`/`Remounting`/`Syncing`/`Mounting`) | 503 | `ROUTER_RECOVERY_IN_PROGRESS` | No |
| Worker already running | 503 | `ROUTER_WORKER_BUSY` | No |
| Job accepted | 202 | (existing job envelope) | Yes |
| Job: credentials missing | 503 (job body) | `ROUTER_CREDENTIALS_MISSING` | Finished |
| Job: unreachable | 503 (job body) | `ROUTER_UNREACHABLE` | Finished |
| Job: auth failed | 503 (job body) | `ROUTER_AUTH_FAILED` | Finished |
| Job: other collect failure | 503 (job body) | `ROUTER_CACHE_REFRESH_FAILED` or `ROUTER_CACHE_SYNC_FAILED` | Finished |

Job poll contract unchanged. Frontend already throws `ApiError` from `envelope.code` / `envelope.error`.

---

## 6. Security considerations

- RouterOS authentication is not bypassed.
- Username `"admin"` is not hardcoded in firmware.
- Encrypted setup store (`passwordProtected`) is unchanged.
- Production `router.json` still holds plaintext password as before (existing production file). Reconcile copies decrypted setup credentials into that existing file only.
- Serial logs never print password, encrypted password, token, or credential blob.
- Diagnostic logs print configured flags, lengths, host, API port (8728), and source label only.

---

## 7. Static validation

| Check | Result |
|-------|--------|
| Duplicate/parallel credential stores | None added. Two existing files remain; reconcile is the only bridge. |
| `RouterOsClient` outside worker path | Direct `new RouterOsClient` remains only in `RouterProvisioningWorker.cpp` (setup/diagnostic worker ops). Production telemetry uses `MikroTikDriver::_routerOs` invoked from the worker. |
| RouterOS I/O in AsyncWebServer callbacks | `ApiServer.cpp` has no `RouterOsClient` / `connect` / `login`. Cache refresh/sync only enqueue. |
| Password/credential blobs logged | No `printf` of password values. New logs are configured/length/source only. |
| HTTP routes | Unchanged. |
| SD/W5500/SPI/WDT/DMA | Unchanged. |

Router regression scripts:

- `tools/router-sync-refresh-contract-check.mjs` — **20/20 PASS**
- `tools/routeros-stability-contract-check.mjs` — **12/12 PASS** (admin deferral contract updated to storage recovery)
- `tools/admin-core-isolation-contract-check.mjs` — **16/16 PASS**

---

## 8. Build result

```
pio run -e freenove_esp32_s3_wroom
========================= [SUCCESS] Took 62.69 seconds =========================
freenove_esp32_s3_wroom  SUCCESS
```

RAM 32.9% / Flash 94.5% (link-time snapshot).

### Files changed

Firmware:

- `src/RouterProvisioningEngine.cpp` / `.h`
- `src/RouterProvisioningWorker.cpp` / `.h`
- `src/RouterApiTransportGate.cpp` / `.h`
- `src/router/RouterPlatform.cpp` / `.h`
- `src/router/drivers/MikroTikDriver.cpp`
- `src/ApiServer.cpp`

Admin UI:

- `src/lib/routerConfig.ts`
- `src/pages/SystemConfigurationPage.tsx`

Docs / contracts:

- `docs/SD_HOTUNPLUG_STABLE_BASELINE.md` (new frozen SD reference)
- `docs/ROUTER_SYNC_CREDENTIAL_RECOVERY_FIX_REPORT.md` (this file)
- `tools/routeros-stability-contract-check.mjs`

---

## 9. Required physical validation

Do **not** treat this section as completed.

A. Configure/verify RouterOS API username/password (Admin Router Settings or confirmed setup-verified credentials).  
B. Click **Refresh Router Information**.  
C. Verify RouterOS telemetry succeeds (serial: `usernameConfigured=yes`, job `ok=yes`).  
D. Click **Synchronize Router**.  
E. Verify the job completes successfully.  
F. Remove SD.  
G. Verify `SD_DEGRADED` without WDT. Refresh/Sync should return `ROUTER_RECOVERY_IN_PROGRESS` (no enqueue storm).  
H. Reinsert SD.  
I. Verify `SD_READY`.  
J. Verify router worker **exits** storage-recovery deferral (`admin job deferred reason=storage_recovery` stops). ROS health must not auto-flip HEALTHY solely because SD recovered.  
K. Click **Refresh Router Information** again.  
L. Click **Synchronize Router** again.  
M. Repeat SD removal/reinsert + router refresh/sync at least **10** cycles.  
N. Record DMA free/largest after each cycle.  
O. Confirm none of: Guru Meditation, Interrupt WDT, `emac_w5500_task` DMA allocation failure, Select Failed storm after `SD_DEGRADED`, permanently stuck router recovery state.

Expected serial after a successful refresh (credentials present):

```
[router-config] host=10.10.10.1 port=8728 usernameConfigured=yes usernameLength=… passwordConfigured=yes passwordLength=… source=…
[router-refresh] mode=telemetry opening session
[router-worker] finished type=admin-refresh-cache ok=yes http=200
```
