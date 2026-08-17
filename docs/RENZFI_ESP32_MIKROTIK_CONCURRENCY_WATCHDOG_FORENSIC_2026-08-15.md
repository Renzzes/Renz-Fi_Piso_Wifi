# Renz-Fi — ESP32 + MikroTik Concurrency / Watchdog Forensic

**Date:** 2026-08-15  
**Mode:** SOURCE (authoritative for current behavior) + operator-supplied serial excerpts.  
**Status:** **NO CODE CHANGES. NO FLASH. NO ROUTEROS/CONFIG/PORTAL CHANGE.**

**Paste file:** `Pasted text(20260815-135513).txt` was **not present** in the workspace, Downloads, or Cursor project attachments at investigation time. Timelines below use **quoted hardware evidence from the operator prompt** plus source. Where a claim needs the missing full file (exact handler `elapsedMs` immediately before abort), it is labeled **NOT PROVEN**.

**Product state:** Operational (coin → Activate → Active). This pass is concurrency/stability of a **supported** workload: 1+ paid sessions + 2 phones + captive portal + Admin Dashboard + RouterOS API.

**Preserve prior findings:** sessionGeneration, one Activate per Done Paying, session clock, Connected-after-success, Model B, no `active/set`, voucher wall clock, no idle ROS poll, one RouterWorker, health FSM, portal/heartbeat ≠ ROS, no parallel API storm, heap not exhausted, W5500 not proven failed, `10.20.0.1` is the real API host, unknown-host-IP is Class B (separate), MikroTik 100% is real but WinBox/Profile confounds `management`, firmware is not the sole proven 100% CPU cause.

---

## 1. Executive verdict

There are **two interacting bottlenecks**, not one common root cause.

| Bottleneck | What | Who starves |
|---|---|---|
| **A. RouterOS control plane** | hAP lite (ROS 7.20.7, 1×650 MHz, 32 MiB) services `/login` slowly (4.0 s … 8.4 s) under HotSpot + API + WinBox. Firmware still does **one connect+login per job**. | MikroTik CPU / API latency |
| **B. ESP32 `async_tcp` TWDT** | HTTP/SSE/static callbacks run **on** the `async_tcp` task (CPU 1). The new abort shows **CPU 1 running `async_tcp`**, not `loopTask`. That means `async_tcp` was **executing a callback that did not return to the AsyncTCP loop** (which feeds TWDT) in time. | ESP32 reboot |

**Causal chain (defensible):**

```
Supported workload
  Admin Dashboard (10.20.0.246)  +  2 portal phones  +  Activate
        │                                    │
        │ HTTP/SSE/static                    │ RouterWorker (yields during ROS wait)
        ▼                                    ▼
  async_tcp CPU1                      RouterOS /login 4–8 s
  long handlers (esp. /api/status     hAP lite CPU high
  → 3× SD sales.json read)            (WinBox confounds meter)
        │                                    │
        ▼                                    ▼
  TWDT: async_tcp did not reset       API slow / 8 s timeout class
  Abort → Reboot                      (independent unless they overlap)
        │
        ▼
  Boot with eth_ip=0.0.0.0 while persisted session retries Activate
  → "Host is unreachable"  (SEPARATE race)
```

**Do not say:** “Admin caused the crash” as a sole cause.  
**Do not say:** “MikroTik caused the ESP32 reboot.” RouterWorker **yields** (`vTaskDelay`) during the 8 s login; CPU 0 was **IDLE0** at abort.  
**Do not say:** “ESP32 caused 100% MikroTik CPU” as sole cause.  
**Do say:** the **supported concurrent workload** stresses **both** sides; the **watchdog is an ESP32 `async_tcp` callback-duration failure**; the **API slowness is RouterOS-side**.

---

## 2. Hardware evidence

| Item | Value |
|---|---|
| ESP32 | S3 + W5500; `10.10.10.2` |
| Admin client | `10.20.0.246` |
| Customers | `10.20.0.251`, `10.20.0.247` |
| API / HotSpot GW | `10.20.0.1` |
| Ping GW | `10.10.10.1` |
| Watchdog | `E (270893) task_wdt` — `async_tcp (CPU 1)` — **CPU 0: IDLE0, CPU 1: async_tcp** — Abort — Rebooting |
| Activate DA:77:0A:31:67:B5 | `ros_login=4030 ok=1`, 5 commands, 1 session, **10639 ms**, Active `*A1400F7`, 300 s — **SUCCESS** |
| Heavier Activate | `ros_login=8395`, `ros_auth=2229`, `total_esp=13593` |
| Heap (prior run class) | ~8.4 MB — not exhaustion |
| Stack logs | `[STACK] activation entry free=11380`; `createHotspotUser entry free=9812` |

---

## 3. Exact watchdog timeline

From quoted serial (millis `270893` ≈ 4.5 min uptime):

```
… production HTTP + portal + admin + (likely) RouterWorker activity …
E (270893) task_wdt: Task watchdog got triggered.
  did not reset: async_tcp (CPU 1)
  running: CPU 0 IDLE0 / CPU 1 async_tcp
task_wdt: Aborting.
task_wdt: Print CPU 1 backtrace
Rebooting...
ESP-ROM:esp32s3-20210327
```

**Interpretation of “currently running” (ESP-IDF TWDT):**

| Prior incident (Admin Test, documented) | **This incident** |
|---|---|
| Failed: `async_tcp`; **running CPU 1: `loopTask`** | Failed: `async_tcp`; **running CPU 1: `async_tcp`** |
| `async_tcp` was **blocked** (not on CPU) — historically `dispatch()` wait on `_doneSem` | `async_tcp` was **on CPU** — stuck **inside a callback** (no yield to the loop that feeds TWDT) |

**PROVEN:** this reset is **not** the same failure mode as blocking `RouterProvisioningWorker::dispatch()` from HTTP (Admin Test). Current Admin router test uses **enqueue** + HTTP 202 (`ApiServer.cpp` `enqueueAdminTest`).

**NOT PROVEN:** the exact C function at PC of the missing backtrace (paste file absent; ELF not decoded here).

TWDT period is **not** set in `platformio.ini`. Prior Renz-Fi forensics and Arduino-ESP32 defaults: **5 s**. Label: **HIGH-CONFIDENCE 5 s**, not proven from an in-tree `sdkconfig`.

---

## 4. Exact activation timeline

### 4.1 Success — MAC `DA:77:0A:31:67:B5` (preserve)

```
router_worker started activate-hotspot-user
tcp_connect = 13 ms
login_rx = ros_login = 4030 ms  ok=1
user/print  173 ms
user/add   1430 ms
active/print 239 ms
active/login 137 ms
active/print 239 ms
router-budget commands=5 session=1 elapsed=10639 ms
active_id=*A1400F7 session_time_left=300
```

**PROVEN:** workflow functional; **1 session**; serialized; login **4.0 s** of the 10.6 s wall time.

### 4.2 Slow success (quoted)

`ros_login=8395`, `ros_auth=2229`, `total_esp=13593`. Login ≈ **8.4 s**, still under `ROUTEROS_IO_TIMEOUT_MS=8000`? **8395 > 8000** — if this is the same `ros_login` printf as `millis()-_loginIoStartMs` including challenge/`ros_auth`, it can exceed 8000 and still `ok=1` if the **sentence** wait reset per read. **NOT PROVEN** without the surrounding lines. It **does** prove login in the **8 s class**.

### 4.3 What firmware does during `ros_login`

`RouterOsClient::loginWithPassword` → `sendLoginSentence` → `drainLoginResult` → `readByte`: wait up to 8000 ms with **`vTaskDelay(1)`** on **`router_worker`**, not on `async_tcp`.

**PROVEN:** an 8 s login **does not by itself** hold `async_tcp`. It **does** occupy the **only** RouterWorker and the W5500/lwIP path for that socket.

---

## 5. Admin Dashboard request timeline

**Client:** `10.20.0.246` (quoted).

| Source | Endpoint | Cadence (source) |
|---|---|---|
| `useAdminApiMonitor.ts` | `GET /api/health` | **Every 5 s while dashboard logged in** (`HEALTH_POLL_MS=5000`), **independent of SSE** |
| `useDashboardEvents.ts` | `/api/status`, users, storage, health, coin, rgb, logs | **`false` (no interval) if SSE connected**; else **30 s** |
| `SystemConfigurationPage` | `/api/status`, related | **30 s** while that page mounted |
| SSE `coin.accepted` / `sessions.changed` / `users.active` / `system.status` | invalidates **status + health + coin + users** | **Burst on pay/activate** (500 ms key throttle, **multiple keys**) |

**PROVEN:** opening Admin **always** produces `/api/health` at 5 s.  
**PROVEN:** a coin/activation with SSE connected produces a **burst** of GETs, including `/api/status`.  
**NOT PROVEN:** exact request timestamps in the missing paste. Operator states those paths were served.

---

## 6. Portal request timeline

| Client | Path | Cadence |
|---|---|---|
| Each phone | `POST /api/portal/heartbeat` then `GET /api/portal/session` | **10 s** (`HEARTBEAT_MS`) — heartbeat **also** syncs session |
| Coin modal | extra `GET /api/portal/session` | **2 s** (`COIN_POLL_MS`) while inserting |
| Done Paying | `POST /api/portal/done-paying` | event |
| Branding | `GET /api/portal/branding` | load |

**2 phones Connected (no coin modal):** ≈ **12 heartbeat + 12 session / minute** total.  
**With coin poll on one phone:** +30 session/min on that phone.

Handlers: RAM + `lockState()` (`portMAX_DELAY`). **No RouterOS.** `donePaying` **enqueues** Activate (`tryEnqueueActivateHotspotUser`), does not wait for ROS.

**PROVEN:** portal contributes **HTTP pressure on `async_tcp`**, not ROS polling.  
**NOT PROVEN:** portal alone exceeded TWDT (rates are modest if handlers stay short).

---

## 7. RouterOS API timeline

Unchanged architecture: **one worker, one session gate, connect+login+commands+disconnect, no `/quit`.**

| Job | Logins | Commands (typical) |
|---|---|---|
| Activate | 1 | 5 (print/add-or-set/print/login/print) |
| Verify | 1 | 1 print; **one MAC / 60 s**; HEALTHY only; 120 s trust |
| HealthProbe | 1 | identity/print if login ok |
| Pause/Deauth | 1 | 2–4 |

Idle, no sessions: **0**. No parallel sessions.

**Remaining avoidable ROS:** (1) per-job login; (2) Connected-only HealthProbe after login fail (prior forensic); (3) Verify = full login for one print.

---

## 8. CPU-pressure analysis

**MikroTik 100%:** still **PARTIALLY PROVEN** as in `docs/RENZFI_CPU_SPIKE_ROOT_CAUSE_FORENSIC_2026-08-15.md`. WinBox/Profile inflates `management`. Firmware API is a **contributor**, not sole proven cause. 1–2 phones do **not** linearly scale Verify (O(1)/60 s).

**ESP32 CPU 1:** `async_tcp` **and** Arduino `loopTask` share CPU 1 (`CONFIG_ASYNC_TCP_RUNNING_CORE=1`). `loop()` runs portal tick, storage snapshot **every 2 s**, coin, RGB, EventBus heartbeat.

At WDT abort, **CPU 0 IDLE0** ⇒ `tiT`/lwIP and `router_worker` (if on 0) were **not runnable** (blocked/yielded). **CPU 1 `async_tcp`** ⇒ the starved task was **busy in its own callback**.

**PROVEN interaction model:** ROS wait **yields**; WDT is **async_tcp callback**. They **coincide** in the supported workload; ROS wait is **not** the TWDT mechanism.

---

## 9. AsyncTCP architecture analysis

| Item | Current source |
|---|---|
| Task | `async_tcp`, pinned **CPU 1** (`platformio.ini`) |
| Stack | `CONFIG_ASYNC_TCP_STACK_SIZE=16384` |
| Queue | `CONFIG_ASYNC_TCP_QUEUE_SIZE=128` |
| Library | ESP32Async/AsyncTCP 3.4.10 + ESPAsyncWebServer 3.9.4 |
| Handlers | **Synchronous lambdas on `async_tcp`** (`ApiServer.cpp`, static, SSE) |
| RouterOS from HTTP | Production HotSpot/admin **enqueue**, not `dispatch()` wait — **except** setup-plane `dispatch()` still exists for wizard jobs |

**Every path that can prevent TWDT feed:** any handler that runs **> TWDT** without returning to AsyncTCP’s loop: SD `readJson`, SPIFFS `exists`/open, `serializeJson` of large docs, `portMAX_DELAY` mutex (blocked → other-task-on-CPU pattern), `AsyncEventSource::send` from another task, W5500 TX from `req->send()`.

**PROVEN:** callbacks are on `async_tcp`.  
**PROVEN for this abort shape:** not blocked-off-CPU (that would show `loopTask`/`IDLE1`).

---

## 10. RouterWorker analysis

| Question | Answer |
|---|---|
| Isolated from HTTP? | **HotSpot yes** (`tryEnqueue*` / `enqueueAdmin*`). Setup `dispatch()` still blocks caller up to `kDispatchTimeoutMs` (20 s+5 s) — **not** the production Admin test path. |
| HTTP heartbeat/session → ROS? | **No** |
| SSE → ROS? | **No** |
| Worker affinity | `ROUTER_WORKER_CORE_AFFINITY = -1` (unpinned) |
| During 8 s login | `vTaskDelay(1)` — **yields** |
| Involved in this WDT? | **NOT PROVEN as the blocking waiter.** **HIGH-CONFIDENCE** it was **concurrent** (activation logs in same run class). |

`EventBus::emit` from **worker** calls `AsyncEventSource::send` (`EventBus.cpp`). That is **cross-task SSE** during Activate success. **POSSIBLE CONTRIBUTOR** (lock/contention with `async_tcp`); **NOT PROVEN** as the abort PC.

---

## 11. SD / SPI analysis

| Bus | Host | Pins | Role |
|---|---|---|---|
| W5500 | **SPI3_HOST** | SCK12 MISO13 MOSI11 CS10 RST14 | Ethernet |
| SD | **FSPI** | SCK7 MISO5 MOSI6 CS18 | `StorageManager` |

**PROVEN isolated SPI controllers** (`SdSpi.cpp` comments + `W5500Config.h` / `Config.h`). **No shared SPI bus.**

**Shared software lock:** `STORAGE_LOCK_TIMEOUT_MS = 5000` — **same order as typical TWDT**.

`loopTask` every 2 s: `refreshRuntimeSnapshot()` → lock → `getSdTotalBytes` / `getSdUsedBytes` (SD ioctl) or SPIFFS size.

HTTP `/api/health`, `/api/storage/status`, `/api/system/health` call `fillStorageStatus` → **same lock** (snapshot copy if lock acquired).

**HIGH-CONFIDENCE:** SD lock contention can **block** `async_tcp` up to 5 s (WDT-shaped). This abort **showed `async_tcp` running**, so the final slice was **running work**, not only a wait — e.g. **`readJson(SALES_FILE)`** on `/api/status`.

`GET /api/status` (`ApiServer.cpp`): **`salesToday` + `salesWeek` + `salesMonth`** → **three** `aggregateSales` → **three** `readJson` of `/sales` into **24 KB** docs (`JSON_DOC_LARGE=24576`) **on `async_tcp`.**

**PROVEN expensive HTTP path on `async_tcp`.**  
**NOT PROVEN** it was the handler at t=270893.

---

## 12. W5500 analysis

Poll mode (`PIN_INT=-1`). Link/IP proven in other logs. TCP to ROS 13 ms in the successful Activate.

**RULED OUT** as primary WDT cause (no link-down in the quoted abort).  
**POSSIBLE CONTRIBUTOR:** `req->send` / ROS socket share lwIP + W5500 SPI3. CPU 0 idle at abort **weakens** “lwIP spun CPU0” as the WDT mechanism.

---

## 13. Memory analysis

Heap ~8.4 MB, largest ~8.25 MB class. **RULED OUT** as WDT/CPU cause.

---

## 14. Stack analysis

`[STACK] … free=N bytes` is `uxTaskGetStackHighWaterMark * sizeof(StackType_t)` = **minimum remaining stack on that task so far**, not heap.

| Log | Meaning |
|---|---|
| activation entry free=11380 | ~11 KB still unused on **that** task — **safe** |
| createHotspotUser free=9812 | ~9.8 KB remaining on **router_worker** — **safe** for that snapshot |

`async_tcp` stack is **16 KB** compile-time. **NOT PROVEN** overflow (no canary in this abort quote). Do not infer `async_tcp` stack from worker STACK lines.

---

## 15. Health FSM analysis

Unchanged from login-stability forensic:

Connected + job login fail → DEGRADED → `needsRouterOsWork()==true` (Connected counts) → **HealthProbe** (another login).

**Still an avoidable feedback loop.** **HIGH-CONFIDENCE CONTRIBUTOR** to RouterOS pressure. **NOT PROVEN** as the ESP32 TWDT cause (probe runs on `router_worker`).

---

## 16. Admin polling analysis

| Endpoint | Client | Frequency | Handler cost | SD? | RouterOS? | Blocking? | Safe on async_tcp? |
|---|---|---|---|---|---|---|---|
| `/api/health` | Admin 5 s | Always | MEDIUM JSON + storage lock + handoff | Lock/snapshot; not full sales scan | **No** (fillHealthStatus = cache) | Storage lock ≤5 s | **Borderline** if snapshot/lock stalls |
| `/api/status` | SSE burst / 30 s fallback | **HIGH** | **3× sales.json read+parse** + active-user merge | **Yes ×3** | No | SD + mutex | **UNSAFE if sales file large or SD slow** |
| `/api/storage/status` | SSE / 30 s | Snapshot copy | Lock | No | Lock ≤5 s | Usually OK |
| `/api/system/health` | SSE burst | Snapshot + coin/rgb | Lock | No | Lock | Usually OK |
| `/api/system/coin` | SSE burst | RAM | No | No | Short | Yes |
| `/api/system/rgb` | SSE / page | RAM | No | No | Short | Yes |
| `/api/coin/diagnostics` | SSE `coin.*` | RAM/diag | No | No | Short | Yes |
| `POST /api/router/test` | Button | Enqueue 202 | No on HTTP | Worker later | HTTP **not** wait | Yes (HTTP) |

**PROVEN:** Admin is **not** a ROS poller.  
**PROVEN:** Admin **is** an `async_tcp` poller (`/api/health` 5 s) plus **activation-triggered `/api/status` SD work**.  
**NOT PROVEN:** Admin sole WDT cause.

---

## 17. Portal polling analysis

| Endpoint | Freq (2 phones) | Duration | SD | ROS | Blocks |
|---|---|---|---|---|---|
| `/api/portal/session` | ~12/min + coin 2 s | RAM + mutex | No | No | `lockState` forever if loopTask holds lock during long tick |
| `/api/portal/heartbeat` | ~12/min | RAM `lastSeen` | No | No | same mutex |
| `/api/portal/branding` | load | small | No | No | short |
| `/api/portal/done-paying` | event | RAM + enqueue | deferred save | enqueue only | should be short |
| SSE | Admin, not portal | — | — | — | — |

**PROVEN:** lightweight **if** `lockState` is not held across SD. `PortalSessionManager::loop` can `enqueueSaveSessions` from **loopTask**; if a save holds session mutex while HTTP waits `portMAX_DELAY`, **async_tcp blocks** → typically **CPU 1 ≠ async_tcp**. This abort **≠ that pattern**. Portal is **HTTP load**, not the matching WDT signature.

---

## 18. Boot / readiness race

**SEPARATE finding.**

Quoted: `IP=0.0.0.0` → Activate starts → TCP **Host is unreachable** → fail → then `ETH_GOT_IP` `10.10.10.2`.

**Source:** `tryEnqueueActivateHotspotUser` checks **health FSM only**, **not** `EthernetManager::hasIp()`.  
`RouterProvisioningPreconditions::check` **does** require link+IP — used by **setup provisioning**, not production HotSpot Activate.

After **WDT reboot**, sessions persist; `retryPendingRouterWork` / tick can enqueue Activate **before** DHCP.

**PROVEN in source** that Activate **can** run without IP.  
**PROVEN in quoted log** that it happened.  
**Classification:** **PROVEN ROOT CAUSE** of that **post-reboot activation fail**, **not** of TWDT itself (TWDT caused the reboot that then raced).

---

## 19. Proven root causes

1. **ESP32 reboot:** TWDT on **`async_tcp` (CPU 1)** while that task was **running** — callback did not feed WDT in time.  
2. **RouterOS slowness:** `/login` 4.0–8.4 s with fast TCP; Activate still succeeds.  
3. **HTTP `/api/status` performs three SD sales aggregations on `async_tcp`.**  
4. **Admin `/api/health` every 5 s on `async_tcp`.**  
5. **Production Activate not gated on Ethernet IP** → post-reboot `0.0.0.0` Activate.  
6. **Buses isolated; one RouterWorker; portal ≠ ROS.**

---

## 20. High-confidence contributors

1. SSE invalidation **burst** of `/api/status` (and related) at coin/Activate.  
2. `STORAGE_LOCK_TIMEOUT_MS=5000` vs TWDT ~5 s; `refreshRuntimeSnapshot` every 2 s on `loopTask`.  
3. SPIFFS static `exists`/serve on `async_tcp`.  
4. Cross-task `EventBus::emit` → `AsyncEventSource::send`.  
5. hAP lite + WinBox + HotSpot + Renz-Fi API logins → MikroTik CPU.  
6. Connected-only HealthProbe extra login.  
7. Two portal clients × 10 s heartbeat+session (HTTP, not ROS).

---

## 21. Ruled-out causes

| Claim | Why |
|---|---|
| Portal/heartbeat **directly** query RouterOS | Source |
| Parallel ROS sessions / 100-deep queue | Worker depth 1 |
| Heap exhaustion | ~8.4 MB class |
| This WDT = blocking `dispatch()` Admin Test | CPU 1 was `async_tcp`; Admin test is enqueue |
| 8 s ROS login **is** the TWDT (no yield) | `vTaskDelay` on worker; CPU 0 idle |
| Wrong API host / password / Ethernet unplug for successful Activate | Same run succeeded |
| `spi` Profile % = W5500 | Different chip |
| Linear Verify × N phones | One MAC / 60 s |
| Need to disable/increase TWDT | Would hide callback duration |
| Worker STACK 9–11 KB remaining = danger | Those are **free** watermarks |

---

## 22. Remaining unknowns

1. Exact handler and `elapsedMs` at t=270893 (full paste + `[http] SLOW HANDLER` if enabled).  
2. TWDT seconds in the **flashed** image.  
3. Sales.json size on that SD.  
4. Whether WinBox Profile was open during 100% samples in **this** capture.  
5. Decoded CPU 1 backtrace (ELF match).  
6. Whether `AsyncEventSource::send` from `router_worker` ran at abort.

---

## 23. Minimal safe remediation boundary

**Do not:** TWDT change, extra ROS poll, parallel ROS, persistent API session, architecture rewrite, session/clock/Model B/`active/set`/wizard.

**Smallest justified later changes (not this pass):**

1. **Do not enqueue HotSpot Activate unless `eth.hasIp()`** (fixes §18 only).  
2. **Do not read `sales.json` three times on `async_tcp`.** Cache totals on `loopTask`; `/api/status` returns snapshot.  
3. **Suppress Connected-only HealthProbe** after a login-timeout job (prior forensic).  
4. Optional: keep `/api/health` 5 s but guaranteed snapshot-only (already mostly so) — **status** is the expensive one.

---

## 24. Files / functions responsible

| Issue | Location |
|---|---|
| WDT task/core | `platformio.ini` `CONFIG_ASYNC_TCP_RUNNING_CORE=1` |
| HTTP on async_tcp | `ApiServer.cpp` all `_server->on` lambdas |
| 3× SD sales | `ApiServer.cpp` `/api/status` → `SessionManager::salesToday/Week/Month` → `aggregateSales` |
| Admin 5 s | `src/hooks/useAdminApiMonitor.ts` |
| SSE burst | `src/hooks/useDashboardEvents.ts` `EVENT_QUERY_MAP` |
| Portal 10 s / 2 s | `portal/renzfi-app.js` `HEARTBEAT_MS`, `COIN_POLL_MS` |
| Storage lock 5 s | `Config.h` `STORAGE_LOCK_TIMEOUT_MS`; `FirmwareApp::refreshHealthSnapshots` |
| Activate without IP | `RouterProvisioningWorker::tryEnqueueActivateHotspotUser` |
| Setup IP gate (unused here) | `RouterProvisioningPreconditions::check` |
| ROS login wait | `RouterOsClient::readByte` |
| Cross-task SSE | `EventBus::emit` |
| Health probe loop | `PortalSessionManager::loop` + `needsRouterOsWork` |

---

## 25. Evidence for every conclusion

Embedded in §§3–18. Primary serial: operator quotes. Primary source: files in §24. Missing paste ⇒ no handler-level WDT proof.

---

## 26. Regression risks

| Change | Risk |
|---|---|
| Cached sales | Stale dashboard ₱ until loopTask refresh — acceptable if ≤2 s |
| IP gate on Activate | Must not drop credits; defer like `router_unavailable` |
| Probe split | Recovery slower until next critical job — **desired** |
| Touching TWDT | Masks bugs — **forbidden** |
| Moving ROS onto HTTP | Recreates old `dispatch()` WDT — **forbidden** |

---

## 27. Recommended implementation order

**After this forensic is accepted — not now:**

1. Ethernet-IP gate on HotSpot Activate enqueue (boot race).  
2. Offload `/api/status` sales aggregation from `async_tcp`.  
3. Connected-only HealthProbe suppression.  
4. Hardware: Admin + 2 phones + ₱1 **without** WinBox Profile; confirm **no** `task_wdt`; `[http] SLOW HANDLER` if debug HTTP on; `ros_login` stays bounded.

---

## Required root-cause table

| Finding | Classification | Evidence | Responsible component | Confidence |
|---|---|---|---|---|
| ESP32 async_tcp watchdog | **A. PROVEN ROOT CAUSE** of reboot | `task_wdt` async_tcp CPU1; **running async_tcp**; Abort; Rebooting | AsyncTCP + HTTP callbacks | 95% (event); 70% (which handler) |
| MikroTik 100% CPU | **B. HIGH-CONFIDENCE** saturation; **not** sole firmware | Prior Profile samples; this run not required to repeat | ROS7 + hAP lite + WinBox confounder + API jobs | 85% platform; **not** sole Renz-Fi |
| RouterOS login latency | **A. PROVEN** for API slowness | 4030 / 8395 ms vs TCP 13 ms | RouterOS API `/login` + per-job login | 95% |
| Admin Dashboard load | **B. HIGH-CONFIDENCE** HTTP contributor | 5 s `/api/health`; SSE burst `/api/status` 3× SD | `useAdminApiMonitor`, `ApiServer` `/api/status` | 88% as load; **not** sole WDT |
| Portal load | **C. POSSIBLE** HTTP contributor | 10 s ×2 + coin 2 s; no ROS | `renzfi-app.js` | 70% load; **E** as ROS cause |
| RouterWorker | **E. RULED OUT** as this WDT wait; **B** as ROS load | Yields; enqueue not dispatch; CPU0 idle | `RouterProvisioningWorker` | 90% not WDT waiter |
| SD/SPI | **B. HIGH-CONFIDENCE** if `/api/status` in flight | 3× `readJson` sales; lock 5 s; buses isolated | `SessionManager::aggregateSales` | 85% mechanism; 60% this abort |
| W5500 | **E** as primary WDT; **D** as ROS delay | TCP 13 ms; CPU0 idle | Ethernet/lwIP | — |
| Heap | **E. RULED OUT** | ~8.4 MB | — | 95% |
| Stack | **E** for quoted worker watermarks; **D** for async_tcp | 9–11 KB **free** | `[STACK]` logs | 90% worker OK |
| Health FSM | **B** extra ROS login | Connected → probe | `needsRouterOsWork` | 90% |
| Boot readiness race | **A. PROVEN** for post-reboot Activate fail | 0.0.0.0 then ETH_GOT_IP; no `hasIp` on enqueue | `tryEnqueueActivateHotspotUser` | 92% |

---

## Watchdog Q&A (required)

1. **Why no WDT reset?** `async_tcp` stayed in a callback (or equivalent running work) longer than TWDT.  
2. **Path at time?** **NOT PROVEN** (no paste/backtrace). Leading candidate: `/api/status` sales SD or storage-locked health.  
3. **Blocked or starved?** **Running** (CPU 1 = async_tcp), not the blocked-`dispatch` pattern.  
4. **Mutex?** Possible for 5 s lock; that pattern usually shows another task on CPU 1. **NOT PROVEN** for this abort.  
5. **Filesystem?** **HIGH-CONFIDENCE** candidate (`aggregateSales`).  
6. **Multiple requests?** **HIGH-CONFIDENCE** (admin + 2 portals); handlers are **serialized on one task**.  
7. **Other task monopolize CPU 1?** At abort, **async_tcp itself** was on CPU 1.  
8. **RouterWorker?** Concurrent ROS yes; WDT waiter **no**.  
9. **SD/SPI?** Isolated buses; SD **from HTTP** is the issue.  
10. **W5500?** Not primary.  
11. **Admin?** Contributor (5 s health + status burst), not sole proven.  
12. **Portal?** HTTP contributor, not ROS.  
13. **ROS latency indirectly?** Occupies Ethernet/worker; **does not** explain CPU 1 running async_tcp. **Two bottlenecks.**  
14. **100% CPU caused by Renz-Fi?** **NOT PROVEN** sole.  
15. **Renz-Fi contributing workload?** **YES, PROVEN** (API jobs).  
16. **Two independent bottlenecks?** **YES.**  
17. **Smallest safe boundary?** IP-gate Activate; cache sales off `async_tcp`; no Connected-only probe; **never** touch TWDT.

---

## Hardware acceptance target

**NORMAL SUPPORTED STATE:** 1 active customer + Admin open + 2+ phones + captive portal + HotSpot Active + ROS API when required  

**Must:** ESP32 stays up; `async_tcp` responsive; **no TWDT**; no Guru; no reboot; W5500 up; portal/admin responsive; ROS reachable; no API storm; no parallel sessions; no idle poll; session sync preserved.

---

**DO NOT MODIFY CODE YET.**
)
</think>

The report is written. I'll give the user the verdict and table without implementing anything.