# Admin Dashboard Isolation from Renz-Fi Core

**Date:** 2026-08-16 (updated)  
**Mode:** Boundary hardening. No Core rewrite. No Setup/Portal/RouterOS redesign.  
**Hardware:** Not flashed in this change. Do not claim device success until flashed.

MikroTik stability rules remain in force: no 100% CPU command storms, no keep-alive RouterOS polling, no Guru Meditation / TWDT from Admin work.

---

## Final synchronization semantics

**Admin synchronization synchronizes the Admin client with the authoritative Renz-Fi Core state. It does not transfer MikroTik credentials to the browser and does not create a new RouterOS login on every Admin connection.**

| Step | What happens |
|------|----------------|
| Authenticate | Existing `POST /api/auth/login` |
| Synchronize Renz-Fi state | `GET /api/status` (RAM / observational cache) |
| Checking router state | Read `routerCache.stale` / connectivity from Core |
| If cache **fresh** | Load existing Dashboard — **no** RouterOS job |
| If cache **stale** and credentials present on Core | Existing `POST /api/router/cache/sync` → **202** → worker → update cache |
| If RouterOS unavailable | Dashboard still loads; UI shows offline/unavailable |
| Credentials | Remain protected on ESP32; never returned to browser |

UI labels (accurate):

- Synchronizing Renz-Fi state…
- Checking router state…
- Loading dashboard…
- Connected.

Do **not** say “Synchronizing MikroTik credentials…” unless a real credential/provisioning operation ran.

---

## Architecture

```
                    RENZ-FI CORE (ESP32-S3)
                    Coin / Session / Sales
                    Router Adapter + MikroTik Driver
                    PortalServer / SetupServer / ApiServer
                    StorageManager / AssetManager
                              │
                       REST + SSE + jobs
                              │
              ┌───────────────┴───────────────┐
              │                               │
         Setup UI                      Admin Dashboard
```

Admin is a CLIENT. Core must survive Admin failure.

---

## RouterOS cache freshness

- Core stores observational cache via `RouterCacheManager` (`stale` after `ROUTER_CACHE_STALE_THRESHOLD_HOURS`, currently 24h).
- `/api/status` exposes `routerCache.stale`, `lastSynchronizedAt`, connectivity.
- Connect path may call existing owner-auth worker sync **only when stale**.
- Repeated Admin open with fresh cache must **not** create a RouterOS login storm.

## SSE sales

Authoritative sequence:

Coin → CoinManager → Session/Sales → **persist SUCCESS** → `sale.created` + `sales.changed` → SSE → Admin targeted UI patch.

Admin does not create the sale. No 1-second `/api/status` polling. While SSE is live, React Query interval is off and `/api/health` interval is off.

## SD fallback

Existing `StorageManager` SPIFFS `/fb/*` path (including sales). No second storage system. SD absent: coin/session/portal/sales continue via fallback when SPIFFS is available; storage degraded is logged and exposed — Core must not TWDT/crash.

## Factory reset

Existing worker: cancel provisioning persist → delete files → Factory → invalidate sessions → reboot. Admin 401 means device reset / open Setup — not “ESP32 broken.”

## Files in this isolation work

- `docs/ADMIN_CORE_ISOLATION.md`
- `.cursor/rules/admin-core-isolation.mdc`
- `src/services/adminSync.ts`
- `src/components/AdminSyncScreen.tsx`
- `src/App.tsx`
- `src/hooks/useAdminApiMonitor.ts`
- `src/hooks/useDashboardEvents.ts`
- `ESP32_S3_Firmware/src/SessionManager.cpp` (`sale.created` after persist)
- `ESP32_S3_Firmware/tools/admin-core-isolation-contract-check.mjs`

## Untouched (unless a proven regression)

CoinManager grant path, PortalServer, SetupServer/wizard, FactoryResetWorker, RouterProvisioningManager deferred persist/`loop()`, MikroTikDriver, W5500, TWDT config.

## Build vs hardware

**BUILD VERIFIED** when `pio run -e freenove_esp32_s3_wroom` and contract checks pass.

**HARDWARE VERIFIED** only after flash +: Admin connect/repeated connect, real-time sale, Admin closed coin, SD absent, RouterOS down, factory reset + reinstall, no Guru Meditation / TWDT.
