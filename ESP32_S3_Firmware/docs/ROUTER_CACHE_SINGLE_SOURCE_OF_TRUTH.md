# Router Cache Single Source of Truth

## Problem

After provisioning, serial proved RouterOS Test/Sync succeeded (`RouterOS test OK`, `Router cache synchronized from RouterOS`), while Admin Dashboard / System Configuration still showed:

- RouterOS API unreachable / Login Failed / Profile not found
- Empty Router Identity / CPU / Memory / Uptime
- Ethernet Disconnected despite heartbeat `eth_link=up`

## Root causes

1. **`JsonDocument::to<JsonObject>()` wipe in `applyLiveSnapshot` (primary remaining defect)** — Each `copyStringField(snap, key, _doc.to<JsonObject>())` cleared the entire cache document before writing one field. `routerIp` / `identity` / `ssid` were discarded; nested `routerOs` written afterward still showed Version/CPU/Memory. `isPopulated()` requires `routerIp` **or** `provisionStatus`, so persistence logged `cache-not-populated` while Test still returned `ok=yes` from live RouterOS.

2. **Router-cache document overflow (3072 bytes)** — Test/Sync snapshots with `profileDetails` + `routerOs` + observation silently truncated when applied. Identity and connectivity were discarded while the worker still logged success.

3. **Admin job result envelope double-copy** — Test result (MEDIUM) was `.set()` into another MEDIUM envelope, truncating nested `data`. The UI then treated incomplete results as failures and kept stale banners.

4. **Frontend metrics sourced from Test-only state** — System Configuration read CPU/Memory/Uptime from `testResult.routerOs` only, ignoring `router-cache` after Synchronize.

5. **Ethernet status ignored live health** — Connected only when `wifiConfig.current.ip` was set, not when `/api/system/health` reported link UP.

6. **Open wireless shown as raw/`—`, password editable**.

## Fixes (architecture preserved)

### Firmware

- **Use `_doc.as<JsonObject>()` once in `applyLiveSnapshot` / `applyWirelessFields`** — never `to<>` on the root when merging fields.
- Stamp `routerIp` / `hotspotProfile` onto the live Test result from the session host before applying the snapshot.
- Merge production SSID from local canonical wireless config into the Test snapshot (no extra RouterOS session).
- Capture wireless `band` (or derive from `frequency` MHz); do not treat `radio-name` as band.
- Enlarge `RouterCacheManager` `_doc` to `JSON_DOC_MEDIUM` (8192).
- Stamp `lastSuccessfulContactAt` when snapshot observation is online.
- Admin Test/Sync job envelopes use `serialized()` so full result bodies are not double-copied.
- Job poll capacity sized for nested result bodies.
- `/api/status` `routerCache` now includes `routerOs`, SSID, security, hotspot profile (same cache fields Dashboard/System Configuration share).

### Frontend

- `refreshProductionRouterViews()` invalidates + refetches router cache, profiles, wireless, status, health, wifiConfig after Test/Sync.
- Test Connection seeds `/api/router/profiles` React Query cache from the Test payload immediately.
- System Configuration metrics prefer `routerCache`; Test banners clear on Sync success; successful Test clears prior failure steps.
- Ethernet uses live health link/IP.
- Security displays **Open** for open networks; password disabled/read-only.
- Dashboard shows Identity / Version / CPU / Memory / Uptime / Production SSID from the same `status.routerCache`.
- Removed Admin nav entries: Router, Fleet Health, Devices (bookmark `/router-settings` still redirects to System Configuration).
- After installation `ready`/`provisioned`, `/setup` redirects to Dashboard and Setup Wizard nav is hidden (Setup Unlock can reopen).
- Production bootstrap never calls `/api/provisioning/installation/resume` (ESP32 production plane does not register that route; health-check exits to Dashboard instead).

## `isPopulated()` invariant

Requires non-empty **`routerIp` OR `provisionStatus`**. Do not weaken this — fix the snapshot so `routerIp` from the live Test session is persisted.

## Not changed

RouterOS scheduler, worker priority, CPU protection, session gate, provisioning workflow, wireless provisioning sequencing. No continuous RouterOS polling.

## Validation

- Firmware build: `freenove_esp32_s3_wroom`
- **HARDWARE VALIDATION REQUIRED** — flash and confirm Test Connection + Synchronize Router repaint Dashboard and System Configuration without reload.
- After Test, serial must show `[router-sync] ok=yes` (not `cache-not-populated`).
- After redeploying Admin SPIFFS, serial must no longer show `GET /api/provisioning/installation/resume` 404s on a provisioned appliance.
