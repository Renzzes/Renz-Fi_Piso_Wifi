# Renz-Fi MikroTik / RouterOS Stability Forensic — 2026-08-15

**Mode:** SOURCE-ONLY forensic / architecture audit  
**Status:** Root causes classified; **remediation implemented in source** — see `docs/RENZFI_MIKROTIK_ROUTEROS_STABILITY_REMEDIATION_2026-08-15.md`  
**Hardware validation:** not claimed here  
**Original constraints for this forensic pass:** NO firmware flash · NO portal upload · NO RouterOS config changes

**Non-negotiable product rule:** STABILITY > RECOVERY > DATA CONSISTENCY > SESSION CORRECTNESS > FEATURE PERFORMANCE.

---

## 1. Executive Summary

Renz-Fi already has a **single `router_worker` task**, a **one-session transport gate**, **connect backoff**, and **opportunistic CPU pacing**. That is real architecture — not zero protection.

However, the production HotSpot path still creates **RouterOS pressure** through:

1. **Per-job connect+login+disconnect** for every HotSpot operation (including a 60s Active verify that is a full API login for one `/ip/hotspot/active/print`).
2. **Activation command density** (≈5 RouterOS commands / ~15s wall time in the captured activation) inside one session.
3. **VerifyHotspotActive classified as Critical** — same priority tier as activate/deauth; never paused under high CPU.
4. **No RouterOS health state machine** — Ethernet/API failure does not globally STOP non-essential HotSpot work; portal **activation_error auto-retry** continues on a fixed schedule regardless of RouterOS availability.
5. **Observed 100% RouterOS CPU** cannot be attributed solely to Renz-Fi: Winbox **Profile** + **management/console** are **HIGH CONFIDENCE** confounders; **spi≈23%** is **NOT PROVEN** as Renz-Fi-caused.

The serial evidence of `ros_login≈8019ms ok=0` matches the firmware **IO timeout (8000ms)** under RouterOS unresponsiveness — consistent with overload/contention/boot, not with a “missing route.”

**Verdict:** Safe for *controlled* idle/single-client validation **with profiling caveats**. **Not GO** for aggressive multi-client / reboot-storm validation until Phase 1–6 stability remediations (design below) are implemented and reviewed.

---

## 2. Incident Description

During hardware testing with Renz-Fi active and RouterOS Profile/Winbox open, MikroTik reported **TOTAL CPU = 100%**, with **management ≈55.5%**, **spi ≈23%**, **console ≈7.5%**, **bridging ≈8%**, **hotspot ≈0%**.

Serial showed successful then failed `verify-hotspot-active` jobs, with login latency climbing to ~8s and `ROUTEROS_API_UNAVAILABLE`.

Separately, a ₱1 / 5-minute activation reported **commands=5, session=1, elapsed≈15256ms**.

---

## 3–5. Hardware / Serial / CPU Evidence

### Serial (operator)

| Observation | Source meaning |
|---|---|
| `dispatch type=verify-hotspot-active priority=critical` | Worker job enqueued; Critical priority |
| First verify: TCP connect ~12ms, `ros_login=884 ok=1`, `/ip/hotspot/active/print`, END ~4777ms, `present=yes` | Full connect+login+print+disconnect for one MAC filter |
| Later verify: TCP ~57ms, `ros_login=8019 ok=0`, `LOGIN FAILED` / `ROUTEROS_API_UNAVAILABLE` | Login waited ≈ IO timeout then failed |
| Activation budget ~15.2s / 5 commands / 1 session | Matches `createHotspotUser` + `loginHotspotActive` command set |

### RouterOS Profile categories (interpretation without over-claiming)

| Category | Observed | Interpretation |
|---|---|---|
| management | ~55.5% | **HIGH CONFIDENCE** inflated by Winbox + Profile itself; may also include API servicing — **split not proven** |
| console | ~7.5% | **HIGH CONFIDENCE** monitoring/console overhead |
| spi | ~23% | Hardware/SPI path — **NOT PROVEN** Renz-Fi API-caused |
| bridging | ~8% | Bridge/guest traffic — **SUSPECTED** normal under load; not API-specific |
| hotspot | ~0% | Profile sample does **not** show HotSpot subsystem as the CPU owner in that snapshot |
| networking | ~0.5% | Low in that sample |

**PROVEN:** A Profile screenshot taken *while profiling* is not a clean Renz-Fi causality proof.  
**PROVEN:** Renz-Fi *does* issue real API login + HotSpot commands during the same test window (serial).

---

## 6. RouterOS API Call Inventory (HotSpot / production path)

### Runtime HotSpot (via `RouterProvisioningWorker` → `MikroTikDriver`)

| Operation | OpType | RouterOS commands (typical) | Connection |
|---|---|---|---|
| Activate | `ActivateHotspotUser` | `user/print`, `user/add` **or** `user/set`, `active/print` (pre), `active/login` **or** `active/set`, `active/print` (post) | New session |
| Verify Active | `VerifyHotspotActive` | `active/print ?mac-address=` | New session |
| Pause | `PauseHotspotUser` | `active/remove`, `cookie/remove` (user kept) | New session |
| Deauth / expire | `DeauthorizeHotspotUser` | `active/remove`, `user/print`, `user/remove`, `cookie/remove` | New session |

### Admin / Setup / Provisioning (same worker, different OpTypes)

Includes (non-exhaustive): `/system/identity/print`, `/system/resource/print`, `/system/routerboard/print`, `/interface/wireless/*`, `/interface/wifi/*`, `/ip/hotspot/*` profile/walled-garden, `/ip/address`, `/ip/route`, `/ip/firewall/filter`, `/interface/bridge`, `/tool/fetch`, security-profiles, user-profile CRUD.

### Not RouterOS (portal HTTP)

| Endpoint | ROS? |
|---|---|
| `GET /api/portal/session` | **No** — RAM JSON |
| `POST /api/portal/heartbeat` | **No** — `lastSeen` only |
| `GET /api/portal/branding` | **No** |
| `POST /api/portal/start-coin-session` | **No** — opens coin window in RAM |
| `POST /api/portal/done-paying` | Enqueues Activate via worker (**yes**, later) |
| `POST /api/portal/cancel-modal` | **No** ROS |

**PROVEN:** Portal UI polling of session/heartbeat/branding does **not** itself login to RouterOS.

---

## 7. RouterWorker Architecture

```text
PortalSessionManager / ApiServer / Setup
        │  enqueue only
        ▼
RouterProvisioningWorker  (single FreeRTOS task "router_worker")
        │  queue depth = 1  (_running gate; reject if busy)
        │  job deadline = 20s
        ▼
RouterApiTransportGate
        │  acquireSession() — one API session system-wide
        │  min connect interval 5s + exponential backoff 10s→60s on failure
        │  waitBeforeCommand() CPU tier delays (opportunistic CPU sample)
        ▼
RouterOsClient / MikroTikDriver
        connect → login → command(s) → disconnect
```

**PROVEN:** Intended contract is single serialized worker + single session gate.  
**PROVEN:** `enqueueFireAndForget` refuses work if `_running` or unread hotspot outcome mailbox is full → natural backpressure (jobs dropped, not stacked unbounded).

Architectural gap: **no explicit ROUTER_HEALTHY / UNAVAILABLE state** that freezes HotSpot job classes during recovery.

---

## 8. Connection Lifecycle

**PROVEN pattern for HotSpot ops:**

```text
openRouterSession → connect() → login() → N× executeCommand → closeRouterSession/disconnect
```

There is **no** persistent production HotSpot API session reused across jobs.

**Implication:** Every VerifyActive pays full login cost (~0.9s healthy, up to ~8s on failure/timeout). That is expensive relative to the value of a single filtered `active/print`.

Persistent sessions: **evaluate later**; reliability risks (stale login, half-open TCP) may outweigh gains. Stability-first remediations (coalesce, demote verify, health FSM) come **before** session reuse.

---

## 9. Retry Analysis

| Path | Behavior | Storm risk |
|---|---|---|
| Transport connect/login failure | `recordFailure()` → backoff 10s→20s→…→60s; min connect interval 5s | **Mitigated** for *new connects* |
| Worker queue | Depth 1; reject if busy | **No 100-job burst** from queue |
| Activation `activation_error` | Retry every **20s**, up to **3**/burst, then **60s** cooldown, then **reset attempts forever** | **CRITICAL** during prolonged ROS outage — keeps enqueueing Activate |
| Deauth cleanup | Up to **3** cleanup retries on failure | Bounded |
| VerifyActive | At most **1 enqueue / 60s** per loop; only if Connected claim | Moderate; still full login each time |
| routerIdle → `retryPendingRouterWork` | Re-queues one pending activate/pause/cleanup when worker idle | Can chain after every failed job |

**PROVEN:** There is **no** `while (!login) connect()` tight loop in the HotSpot path.  
**PROVEN:** Activation auto-retry can still apply **sustained pressure** after ROS recovers (and during degradation, wasted 8s login attempts).

---

## 10. Polling Analysis

| Loop | Interval | Calls ROS? |
|---|---|---|
| `PortalSessionManager::loop` tick | 1s | Local expire/countdown; may **enqueue** VerifyActive |
| `maybeEnqueueActiveVerify` | ≥60s between enqueues | Yes, via worker |
| Portal heartbeat (browser) | ~10s | No |
| Portal session sync | UI-driven | No |
| Burn-in router probe | 60s TCP only | **Compiled out** (`RENZFI_BURN_IN_DIAG=0`) |

---

## 11. VerifyActive Analysis

**PROVEN necessity intent:** Keep portal `connected` honest vs RouterOS Active (session desync forensics).

**PROVEN cost:** Full API login + `active/print` + disconnect, Critical priority, ~5s healthy / ~8s+ on failure.

**PROVEN redundancy with activation:** `loginHotspotActive` already:

1. `active/print` before login (reuse path),  
2. `active/login` or `active/set`,  
3. `active/print` after login to confirm row.

Then within 60s of claiming Connected, VerifyActive may repeat a **new** login+print.

**Design note (not implementing):** After successful activate with `active_authorized=yes`, a short **trust window** (e.g. skip Verify for N minutes) is a stability candidate. Do not remove Verify entirely without Connected-truth regression tests.

**Priority issue (PROVEN):** Verify is `Critical` — same as activate/deauth. Under CPU pressure, Low jobs pause; Verify does **not**.

---

## 12. Activation Analysis (₱1 / ~15.2s)

**PROVEN command set** (`createHotspotUser` + `loginHotspotActive`):

| Step | Approx. operator timing | Role |
|---|---|---|
| `user/print` | ~2109 ms | Locate existing MAC user (Model B) |
| `user/add` or `user/set` | ~2620 ms | Create/update limit-uptime + profile |
| `active/print` | ~1782 ms | Detect existing Active (Add Time) |
| `active/login` | ~210 ms | Authorize |
| `active/print` | ~216 ms | Confirm Active row |

**PROVEN:** One TCP session for the whole activate job (good).  
**HIGH CONFIDENCE:** Large print/add latencies reflect **RouterOS responsiveness under load** (and/or Profile), not ESP32 CPU alone — ESP32 serial shows waiting on ROS replies.  
**NOT PROVEN:** Which single command is “unnecessary” without a clean idle baseline compare.  
**SUSPECTED optimization (post-stability):** post-login verify print might be coalesceable with a short trust window; pre-login print is needed for Add Time `active/set` path.

Do **not** remove Model B `user/print`/`user/set` without reopening uptime-limit forensics.

---

## 13–14. Queue / Concurrency

| Property | Value |
|---|---|
| Worker queue depth | **1** |
| Concurrent ROS sessions | Gate `acquireSession()` — **one** |
| Overlapping HotSpot jobs | **No** (busy reject) |
| Duplicate Verify coalesce | Implicit: cannot enqueue while busy; 60s throttle |
| Stale job cancel | **No** explicit cancel API |
| Admin + HotSpot | Serialized on same worker — admin can delay HotSpot and vice versa |

**PROVEN:** Parallel dual-login storms from two firmware tasks are **ruled out** for the gated path.  
**SUSPECTED residual:** Setup plane / diagnostics if ever invoked concurrently outside worker — production HotSpot path uses worker.

---

## 15. Timeout Analysis

| Timeout | Value | Match to evidence |
|---|---|---|
| Connect | 5000 ms | TCP connect observed small when link up |
| IO / login | **8000 ms** | **`ros_login=8019 ok=0` ≈ this budget** |
| Sentence | 2000 ms | Per-sentence |
| Worker job | 20000 ms | Bounds entire activate (~15s used a large fraction) |
| Min connect interval | 5000 ms | After connect attempts |
| Backoff | 10s → 60s | On connect/login failure |

**PROVEN:** Failed login does eventually give up (job/IO timeout), not infinite block.  
**PROVEN:** An 8s failed login still **occupies the only worker** for that duration — opportunity cost for activate/deauth.

---

## 16–17. Boot / Reboot / Recovery Analysis

### What exists today

| Question | Answer |
|---|---|
| Detects ROS failure? | Per-job connect/login/command failure codes |
| Global UNAVAILABLE state? | **No** |
| Distinguishes Ethernet up vs API ready vs HotSpot ready? | **No** dedicated FSM |
| Queued jobs during outage? | Mostly **not queued** (reject if busy); pending flags on sessions |
| Retries during outage? | Activation_error schedule + cleanup retries + verify every 60s if still Connected |
| New TCP+login per retry? | **Yes** |
| Overlap? | **No** (serialized) |
| Portal HTTP → ROS during recovery? | Indirectly only via enqueue activate/expire/verify |
| Gradual drain after recovery? | **No** — next successful enqueue runs immediately at Critical |
| Backlog of 100 verifies? | **Unlikely** (depth 1 + 60s throttle); backlog of **activation retries across many MACs** still possible over time |

### Desired vs actual (reboot OFF→ON)

```text
DESIRED: UNAVAILABLE → cooldown → probe → RECOVERING → HEALTHY → gradual resume
ACTUAL:  each job fails (~8s) → transport backoff → but activation_error still schedules retries
         → when ROS returns, Critical jobs resume without stabilization window
```

---

## 18–19. ESP32 Resource / Watchdog

| Signal | Interpretation |
|---|---|
| `jobs=1 queue=1 sse=1` | Matches single-worker design — **normal** |
| Heap variation around ROS | Expected with JSON/buffers — **not proven leak** from this evidence alone |
| Stack free ~12k activation / ~9.8k createHotspotUser | **HIGH CONFIDENCE** adequate for current path; not “dangerously low” by typical ESP32 margins — continue monitoring |
| TWDT / blocking | Worker uses `vTaskDelay` in gate waits; job deadline bounds work — **PROVEN** intent to avoid infinite wait |

---

## 20–21. Coin / Voucher during ROS outage

| Topic | Source behavior |
|---|---|
| Coin credit while ROS down | **Accepted in RAM** if coin window open |
| Done Paying | Enqueues Activate; on fail → `activation_error`, credits preserved, auto-retry |
| Double activate | Idempotent user reuse + Model B; outcome mailbox + pending guards — **designed** to avoid double Connected claim |
| False Connected | Activate outcome requires worker success; Verify clears Connected on `not_active` |
| Voucher absolute expiry | Local tick enqueues ExpireSession; deauth may fail offline → cleanup retries; reconnect gated by `serviceExpiresAt` |

**PROVEN:** Financial state can survive temporary ROS loss.  
**PROVEN:** System can still **attempt** ROS work during loss (retry), which fights the “REDUCE WORK when unhealthy” principle.

---

## 22. Portal Polling → Router Load

**PROVEN:** session / heartbeat / branding / start-coin-session do **not** call RouterOS directly.  
**PROVEN:** done-paying / pause / terminate / voucher activate paths enqueue worker jobs.

---

## 23. CPU Correlation Table (illustrative from provided evidence)

| Firmware event | ROS command | Latency | Concurrent? | CPU claim |
|---|---|---|---|---|
| verify-hotspot-active OK | login + active/print | ~4.8s end | solo | Contributes API load — **PROVEN activity**; % share **NOT PROVEN** |
| verify-hotspot-active FAIL | login timeout | ~8s | solo | ROS unresponsive — **PROVEN**; cause overload vs reboot **NOT PROVEN** |
| activate ₱1 | 5 cmds / 1 session | ~15.2s | solo | Heavy job — **PROVEN**; alone = 100% **NOT PROVEN** |
| Winbox Profile | N/A | continuous | concurrent with tests | management/console inflation — **HIGH CONFIDENCE** |

---

## 24. Proven Root Causes

1. **Every HotSpot job opens a fresh RouterOS API login** (including VerifyActive for one print).  
2. **VerifyActive is Critical** and runs on a **60s cadence** while Connected.  
3. **Activation performs multiple HotSpot prints/mutations** in one job (~15s under observed conditions).  
4. **No RouterOS health FSM** — unhealthy ROS does not globally suppress activation retries / verifies.  
5. **Activation_error auto-retry** continues indefinitely (burst+cooldown loop) independent of ROS health.  
6. **Failed login consumes ~IO timeout (8s)** and monopolizes the single worker for that window.  
7. **100% CPU screenshot alone does not prove Renz-Fi sole causation** (Profile/Winbox confounders).

---

## 25. High-Confidence / Suspected

### HIGH CONFIDENCE

1. Winbox + RouterOS Profile inflate **management/console** CPU during the capture.  
2. Observed activation/verify latencies are dominated by **RouterOS reply time**, not ESP32 compute.  
3. Demoting Verify / adding trust window after activate would reduce API login rate without changing Model B.

### SUSPECTED

1. Activation retries during ROS outage contribute to prolonged unresponsiveness once ROS is partially up.  
2. Admin sync / discovery jobs interleaved (if running) increase contention with HotSpot Critical work.  
3. SPI % related to board hardware / storage / wireless — not API login CPU.

---

## 26. Not Proven

1. Renz-Fi alone caused sustained 100% CPU.  
2. `spi≈23%` caused by Renz-Fi API.  
3. `management≈55%` fraction attributable to API vs Winbox Profile.  
4. That removing post-login `active/print` is safe in all HotSpot versions.  
5. That persistent API sessions are safer than connect-per-job on this hardware.

---

## 27. Ruled Out

1. Unbounded parallel RouterOS API sessions from multiple firmware HotSpot callers (gate + single worker).  
2. Portal GET session/heartbeat directly hammering RouterOS.  
3. Infinite `while (!login)` tight loop without timeout.  
4. Queue depth exploding to 100 simultaneous ROS logins (depth 1).  
5. Burn-in ROS login probe in production builds (`RENZFI_BURN_IN_DIAG=0`).

---

## 28. Architectural Risks (ranked)

| Rank | Risk |
|---|---|
| **CRITICAL** | No health FSM + perpetual activation retry under ROS outage/recovery |
| **CRITICAL** | VerifyActive = full login at Critical priority every 60s per Connected client (scales with “Connected” claims) |
| **HIGH** | Connect-per-job cost; 8s failed login blocks all other HotSpot enforcement |
| **HIGH** | Activation ~15s under load leaves little job-budget headroom (20s) |
| **MEDIUM** | Verify Critical vs informational priority mismatch |
| **MEDIUM** | No coalescing of “verify MAC A” beyond busy-reject |
| **LOW** | Stack HWM during activate (monitor only) |

---

## 29–32. Required Remediation (DESIGN ONLY — DO NOT IMPLEMENT YET)

### Phase order (stability-first)

1. **Prevent overload:** stop scheduling HotSpot jobs when ROS marked UNAVAILABLE/COOLDOWN.  
2. **Serialize:** keep single worker (already); freeze admin/discovery during HotSpot recovery.  
3. **Dedupe:** coalesce Verify by MAC; skip Verify inside post-activate trust window.  
4. **Health state:** implement FSM (below).  
5. **Backoff:** bind activation_error retries to health state (no retry while UNAVAILABLE).  
6. **Graceful recovery:** probe → RECOVERING dwell → drain Critical first (deauth/activate), then Normal.  
7. **Resume essential:** expire/deauth backlog coalesced per MAC.  
8. **Resume non-essential:** Verify, admin sync.  
9. **Optimize commands:** only after baselines.  
10. **Performance:** last.

### Proposed health FSM

```text
UNKNOWN → CONNECTING → HEALTHY
HEALTHY → (API fail) → DEGRADED → (N fails) → UNAVAILABLE
UNAVAILABLE → COOLDOWN → PROBING → (ok) → RECOVERING → (stable) → HEALTHY
```

Probe: lightweight TCP or single identity print — **not** full HotSpot inventory.  
Never: unhealthy → more Verify/Activate storms.

### Proposed queue policy

| Job | Keep offline? | On recovery |
|---|---|---|
| Deauth / expire | Keep (coalesce per MAC) | First |
| Activate (valid entitlement) | Keep one per MAC | After probe stable |
| Verify | Drop duplicates; pause while unhealthy | Last |
| Admin discovery | Pause | After HotSpot drain |
| Heartbeat/session | N/A (local) | — |

### Proposed RouterWorker rules

- Verify = **Normal** or **Low**, not Critical.  
- Critical = activate / deauth / pause only.  
- Reject Verify enqueue if last success for MAC < trust window.  
- If `cpuUnderPressure()` or health ≠ HEALTHY: refuse Verify and admin sync.

### Backoff

Keep transport 10s→60s. Add **application-level**: no Activate retry while UNAVAILABLE; after RECOVERING, stagger multi-MAC activates (e.g. one per few seconds) — **state machine, not `delay(5000)` in HTTP**.

---

## 33–34. Hardware Validation Plan & Acceptance

### Baseline methodology (required before blaming Renz-Fi)

1. Router idle, **no Winbox Profile**, wait 5–10 min, sample CPU.  
2. Renz-Fi connected, **no clients**, sample.  
3. One portal open (session/heartbeat only).  
4. One coin activate.  
5. Active session + Verify cadence.  
6. Voucher activate/expire.  
7. ROS API pull cable / disable API briefly.  
8. MikroTik power OFF→ON.  
9. Ethernet flap.  
10. Multi-client (only after Phase 1–6).

### Acceptance (stability)

- No sustained runaway CPU **attributable** to Renz-Fi after clean baseline.  
- No login storm; no concurrent sessions.  
- No Critical Verify during UNAVAILABLE.  
- Failed ROS → REDUCE WORK (activation retries paused).  
- Recovery → gradual drain, not burst.  
- Portal never claims Connected without activate success / Verify policy.

---

# FINAL RESPONSE BLOCK

## A. EXECUTIVE VERDICT

Observed instability is a **compound** of (1) **real Renz-Fi API load** — especially **full login-per-Verify** and **heavy activate command chains** under a **single but continuously retried** HotSpot worker — and (2) **HIGH CONFIDENCE measurement inflation** from Winbox/Profile. The ~8s failed login is the firmware **IO timeout** meeting an unresponsive RouterOS, not a missing feature. There is **no** dual-session storm, but there **is** a missing **health FSM**, so failure does not systematically REDUCE WORK.

## B. PROVEN ROOT CAUSES

1. Connect+login+disconnect per HotSpot job (including Verify).  
2. VerifyActive Critical + 60s cadence while Connected.  
3. Dense activate command sequence (~5 cmds, ~15s under load).  
4. No global RouterOS health state to suppress work.  
5. Perpetual activation_error retry schedule independent of ROS health.  
6. Failed login blocks the only worker for ~IO timeout.  
7. 100% Profile capture ≠ sole Renz-Fi proof.

## C. HIGH-CONFIDENCE ROOT CAUSES

1. Winbox/Profile inflate management/console CPU.  
2. Latencies are ROS-bound.  
3. Demoting/coalescing Verify would cut API logins materially.

## D. NOT PROVEN

1. Renz-Fi sole cause of 100% CPU.  
2. SPI 23% from Renz-Fi.  
3. Exact management % split API vs Winbox.  
4. Safety of dropping post-login active/print.  
5. Persistent session superiority.

## E. RULED OUT

1. Parallel multi-session HotSpot API from firmware.  
2. Portal session/heartbeat → ROS.  
3. Infinite login spin without timeout.  
4. 100-deep ROS login queue.  
5. Production burn-in ROS login poll.

## F. ROUTEROS API LOAD MAP

| Op | Frequency | Cost |
|---|---|---|
| Activate | Per purchase / retry | High (multi-cmd + login) |
| VerifyActive | ≤1/60s while Connected | Medium-High (login+print) |
| Pause/Deauth | On demand / expire | Medium |
| Admin/Setup | On demand | Variable, can be heavy |
| Portal poll | Continuous | **Zero ROS** |

## G. RETRY / BACKOFF MAP

Transport: 5s min interval + 10→60s backoff.  
App: activation 20s×3 then 60s cooldown forever; cleanup ≤3; verify 60s.  
Gap: app retries ignore transport health.

## H. ROUTERWORKER CONCURRENCY MAP

One worker · one session · busy-reject. Overlap of two ROS logins: **ruled out**. Serialization delay under 8s failed login: **proven**.

## I. ROUTEROS REBOOT RECOVERY TRACE (CURRENT)

```text
OFF → API fail (timeout~8s) → backoff connects
    → activation_error still schedules retries
    → Ethernet up ≠ API ready (not distinguished)
    → ON/API up → next Critical job runs immediately
    → no RECOVERING dwell / no gradual drain policy
```

## J. CPU FORENSIC

- **management/console:** treat Profile/Winbox as confounders (**HIGH CONFIDENCE**).  
- **hotspot≈0%** in sample: does not mean API idle; API may bill under management.  
- **spi:** **NOT PROVEN** Renz-Fi.  
- **bridging:** traffic, not API login proof.

## K. STABILITY RISKS

CRITICAL: health FSM absent + activation retry under outage; Critical Verify login cadence.  
HIGH: login-per-job; 8s worker monopoly on failure.  
MEDIUM: priority model; weak coalesce.  
LOW: stack margins.

## L. REMEDIATION PLAN

Phases 1–10 as above — **design only this pass**. Implement only after review.

## M. HARDWARE VALIDATION PLAN

Clean baseline without Profile → idle Renz-Fi → single activate → verify cadence → outage/reboot tests → only then multi-client.

## N. GO / NO-GO

**NO-GO** for declaring RouterOS stability “production-validated” or for stress/reboot campaigns.

**Conditional GO** only for **narrow** single-client functional checks **without** Winbox Profile as the CPU judge, understanding Verify/activate will still load API.

**Must fix first (before stability claim):** health FSM + suppress activation/verify during UNAVAILABLE + demote/coalesce Verify + recovery dwell/drain policy.

---

## NO-CODE-CHANGES CONFIRMATION

This document is **forensic only**. No firmware, portal, RouterOS, or worker code was modified in this pass.
