# Router Provisioning Worker — Implementation Report

**Environment:** `freenove_esp32_s3_wroom`  
**Build:** SUCCESS (42.3s) — Flash 68.2%, RAM 37.2%  
**Physical validation:** Not performed in this session (requires on-device acceptance sequence below).

## Problem

RouterOS TCP/API validation ran inside `async_tcp` AsyncWebServer callbacks. Large stack frames (`RouterSession`, `InspectionData`, `CommandResult`) and deferred request lifetimes caused post-response panics (`InstrFetchProhibited` PC `0x00000000`, stack canary, cache/MMU faults) on physical hardware during POST `/api/setup/router/test` and `/api/setup/router/save`.

## Solution

All RouterOS work is serialized in a dedicated FreeRTOS worker task with an explicit 24 KB stack. HTTP handlers only validate input, enqueue a job, and return HTTP 202 with a `jobId`. Clients poll job status until completion.

## Changed Files

| File | Change |
|------|--------|
| `src/RouterProvisioningWorker.h` | New — job types, queue API, snapshots |
| `src/RouterProvisioningWorker.cpp` | New — FreeRTOS worker task, job execution, diagnostics |
| `src/Config.h` | `ROUTER_WORKER_STACK_WORDS` (6144 = 24 KB), job timeout/TTL |
| `src/FirmwareApp.h/.cpp` | Worker `begin()`, `pollExpiredJobs()` in main loop, web deps |
| `src/web/WebServerManager.h/.cpp` | Pass `routerWorker` into setup plane |
| `src/web/SetupServer.h/.cpp` | Job enqueue handlers, poll route, wizard JS polling |
| `src/RouterOsClient.cpp` | Safer `disconnect()` flush/stop/delay |
| `src/SetupRouterValidator.cpp` | Heap-allocated `RouterOsClient` per validation |
| `tools/router-test-save-stability-check.py` | Guards worker isolation |

## Worker / Job Architecture

```
AsyncWebServer callback                RouterProvisioningWorker (core 1, 24 KB stack)
─────────────────────────              ─────────────────────────────────────────────
POST /api/setup/router/test     ──►    enqueueTest → queue → runJob → testConnection
POST /api/setup/router/save     ──►    enqueueSave → queue → runJob → saveConnection
GET  /api/setup/router-plan     ──►    enqueueBuildPlan → buildPlan
POST /api/setup/router-apply    ──►    enqueueApply → applyConfiguration
GET  /api/setup/router/jobs/:id ◄──    getJobSnapshot (queued/running/completed/failed)
```

- **Single queue, one job at a time** — FreeRTOS queue depth 4; worker processes serially.
- **12 job slots** — completed/failed jobs expire after 120 s (`pollExpiredJobs()`).
- **45 s job timeout** — logged if exceeded; cleanup always runs.
- **Job states:** `queued` → `running` → `completed` | `failed`.

### HTTP API (setup plane)

| Method | Path | Response |
|--------|------|----------|
| POST | `/api/setup/router/test` | 202 `{ success, data: { jobId, state: "queued" } }` |
| POST | `/api/setup/router/save` | 202 (same) |
| GET | `/api/setup/router-plan` | 202 (same) — read-only preview |
| POST | `/api/setup/router-plan` | 405 `METHOD_NOT_ALLOWED` |
| POST | `/api/setup/router-apply` | 202 (same) |
| GET | `/api/setup/router/jobs/<id>` | 200 with `data.state` and `data.result` when finished |

Setup wizard (`/admin/setup`) polls every 400 ms (max 120 attempts) via `pollRouterJob()`.

## Lifecycle Fixes

1. **No RouterOS in HTTP callbacks** — grep-verified: no `testConnection`, `saveConnection`, `buildPlan`, or `applyConfiguration` in `SetupServer.cpp`.
2. **`RequestTimer::finish()`** — detaches before `req->send()` to prevent use-after-free on async_tcp stack.
3. **`SetupRouterOwnedBodyStore`** — body copied off request; no `_tempObject` on test/save/apply.
4. **`RouterOsClient`** — synchronous `NetworkClient` (not AsyncClient); heap allocation in validator; member buffers for login to avoid large stack locals; explicit `disconnect()` cleanup.
5. **`RouterSession` / `InspectionData`** — remain heap-allocated in `RouterProvisioningManager` (prior router-plan fix).

## Diagnostics

Serial tags (no passwords or credential blobs):

```
[router-worker] queued type=test id=...
[router-worker] started type=... id=...
[router-worker] stack hwm=... stage=job-start|before-connect|after-validation|cleanup
[router-worker] heap free=... stage=...
[router-worker] login result=API_LOGIN_FAILED|ROUTER_VALIDATED|...
[router-worker] cleanup complete
[router-worker] finished type=... id=... state=... code=...
[router-test] job queued id=...
```

## Security / Behavior Preserved

- Test validates only; save persists only after successful validation.
- Blank password preserves saved credential.
- Encrypted credential storage unchanged.
- No MikroTik configuration changes during test/save/preview.
- Phase 3A apply scope unchanged (foundation only).
- Setup-plane gate enforced on all routes.

## Physical Validation Sequence

Flash `freenove_esp32_s3_wroom` build, connect to Management AP, open `/admin/setup`:

**A. Wrong password — Test:** Step 4 → Test Connection → inline `API_LOGIN_FAILED`, no reboot, `[router-worker] cleanup complete`, heartbeat continues.

**B. Wrong password — Save:** Step 4 → Save → `API_LOGIN_FAILED`, no reboot, saved credentials unchanged.

**C. Correct password:** Test and Save succeed without reboot; saved state displays correctly.

**D. Step 5 Preview:** Preview completes or returns structured router error; no reboot; no MikroTik changes.

**E. Repeat:** Wrong-password Test/Save ×10 each, Preview ×10 — no panic, watchdog, stack canary, cache/MMU fault, or heap degradation.

Monitor serial for `[router-worker]` lifecycle and stack high-water marks during each run.
