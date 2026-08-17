# Admin RouterOS Async Worker — Production Hardening

## 1. Previous confirmed TWDT

Admin Dashboard **Test Connection** blocked `async_tcp` via synchronous
`RouterProvisioningWorker::dispatch()` while the worker ran RouterOS work and
(originally) a second `refreshRouterCache` connect + cooldown. Task WDT (5 s)
on `async_tcp` rebooted the appliance.

Immediate prior fix: Test no longer opens a second RouterOS session.

## 2. Architectural root cause

**AsyncTCP must never wait for unpredictable RouterOS network transactions.**

`dispatch()` holds the HTTP callback on `_doneSem` for the entire worker job.
Cooldown/`vTaskDelay` on `router_worker` is correct RouterOS protection — the defect
is coupling that wait to `async_tcp`.

## 3. RouterOS endpoint matrix (Admin / production plane)

| Endpoint | Method | Frontend | Handler | Worker? | Sync dispatch? | RouterOS cmds | Sessions | Cooldown risk | refreshRouterCache | TWDT risk | Action |
|----------|--------|----------|---------|---------|----------------|---------------|----------|---------------|--------------------|-----------|--------|
| `/api/router/test` | POST | SystemConfiguration Test | ApiServer | YES | **NO (202)** | identity/profile/resource | 1 | only if reconnect | NO (apply test fields) | mitigated | async job |
| `/api/router/settings` | GET | SystemConfiguration | ApiServer | NO | n/a | none (SD) | 0 | no | no | SAFE | sync local |
| `/api/router/settings` | PUT/POST | Save | ApiServer | YES | **NO (202)** | none (SD write) | 0 | no | **removed** | mitigated | async job |
| `/api/router/wireless` | GET | SystemConfiguration | ApiServer | NO | n/a | none (cache/local) | 0 | no | no | SAFE | sync local |
| `/api/router/wireless` | PUT/POST | Save wireless | ApiServer | YES | **NO (202)** | set + verify print | 1 | on connect | **applyWirelessFields** | mitigated | async job |
| `/api/router/profiles` | GET | SystemConfiguration | ApiServer | NO | n/a | cache only | 0 | no | no | SAFE | sync local |
| `/api/router/cache` | GET | SystemConfiguration | ApiServer | NO | n/a | cache only | 0 | no | no | SAFE | sync local |
| `/api/router/cache/sync` | POST | Synchronize | ApiServer | YES | **NO (202)** | full snapshot | 1 | yes | YES (required) | mitigated | async job |
| `/api/router/cache/refresh` | POST | Refresh | ApiServer | YES | **NO (202)** | full snapshot | 1 | yes | YES (required) | mitigated | async job |
| `/api/router/jobs/<id>` | GET | poll helper | ApiServer | poll only | no | none | 0 | no | no | SAFE | sync poll |
| `/api/health` | GET | monitor/App | ApiServer | NO | n/a | none | 0 | no | no | SAFE | sync |
| `/api/system/*` network/rgb/coin | * | various | ApiServer | NO | n/a | local | 0 | no | no | SAFE | sync |
| Hotspot activate/deauth | internal | PortalSession | tryEnqueue* | fire-forget | no wait | hotspot cmds | 1 | possible | no | already safe | keep |

Setup wizard enqueue paths were already async (unchanged).

## 4. Synchronous `dispatch()` callers (remaining)

| Caller | Context | Safe? |
|--------|---------|-------|
| `runApply` | SetupServer debug/legacy | Setup plane; not Admin Dashboard production path |
| `runTcpDiagnostic` | Setup diag (compile-gated off) | Disabled in production |
| `runApiProtocolDiagnostic` | Setup diag (compile-gated off) | Disabled in production |
| `runListWifiNetworks` | Used via tryRefresh background / enqueue path | Background / enqueue; not Admin HTTP sync |

**Admin production routes: zero `dispatchAdmin*` / sync wait.**

## 5. `refreshRouterCache` callers

| Caller | Class | Notes |
|--------|-------|-------|
| `RouterPlatform::save` | **E removed** | Local credential save only |
| `RouterPlatform::test` | **E** apply from test result | No live refresh |
| `RouterPlatform::saveWireless` | **E** `applyWirelessFields` | Same-session verify already done |
| `RouterPlatform::synchronizeRouterCache` / AdminSyncCache | **A** | Explicit user sync — required live |
| Finish / provisioning paths | unchanged | Out of Admin scope |

## 6–7. Save / Save Wireless traces

**Save:** UI → PUT `/api/router/settings` → enqueue → worker → `saveSettings` (SD only) → 202 poll → result. No RouterOS session.

**Save wireless:** UI → PUT `/api/router/wireless` → enqueue → open session → update → same-session read verify → close → `applyWirelessFields` → poll result. One session.

## 8. Task ownership

| Task | Role after fix |
|------|----------------|
| `async_tcp` | Validate + enqueue + 202 / poll JSON only |
| `router_worker` | All RouterOS + cooldown `vTaskDelay` |
| SSE | Publishes `setup.job` + `router.job` state only |

## 9–11. Async job architecture / API / frontend

```
POST /api/router/<op> → 202 { success, data: { jobId, state: "queued", type } }
GET  /api/router/jobs/<id> → { jobId, state, httpStatus?, result? }
SSE  event "router.job" (also "setup.job") { jobId, type, state }
```

Frontend `src/services/router.ts`: enqueue via `apiFetch`, poll every 1 s until terminal, unwrap worker envelope `data` to preserve prior UI contracts. Buttons already disable while `isPending`.

## 12–16. SSE / queue / disconnect / cache / session / cooldown

- SSE: lightweight emit only; job continues if browser disconnects (slot-owned JSON).
- Queue depth 1; busy → `503 ROUTER_WORKER_BUSY`.
- Request body copied into `_slot` before HTTP returns.
- Cooldown **preserved** (`waitUntilConnectAllowed` unchanged); async_tcp does not wait.
- Session continuity: wireless write+verify same session; sync still one dedicated session.

## 17–18. Memory / CPU

No new FreeRTOS task; single `_lastJob` slot; no extra RouterOS storm; cooldown Serial already rate-limited.

## 19. Files modified

- `RouterProvisioningWorker.h/.cpp`
- `ApiServer.h/.cpp`
- `router/RouterPlatform.cpp`
- `src/services/router.ts`
- `docs/ADMIN_ROUTEROS_ASYNC_WORKER_PRODUCTION_HARDENING.md`

## 20–22. Build / static / regression

See agent build output. Static: Admin routes use `enqueueAdmin*`; no `dispatchAdmin`. Setup Finish / production-network / portal / coin / voucher untouched.

## 23. Remaining risks

- Setup debug `runApply` still sync-dispatch (non-Admin).
- Single global job slot: Admin and Setup contend (by design).
- Explicit Synchronize can still take seconds on worker (correct).

## 24. Core dump recommendation

`partitions_custom.csv` has no coredump partition. Prefer **disable flash core dump** in sdkconfig/build flags unless a sized `coredump` partition is deliberately added after OTA/SPIFFS size review. Not part of this TWDT fix.
