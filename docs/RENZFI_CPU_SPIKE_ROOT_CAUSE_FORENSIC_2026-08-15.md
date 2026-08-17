# Renz-Fi MikroTik CPU Spike / RouterOS API Instability — Final Root-Cause Forensic

**Date:** 2026-08-15  
**Mode:** SOURCE (authoritative for current behavior) + supplied hardware/serial/WinBox evidence.  
**Status:** **NO CODE CHANGES. NO PATCH. NO FLASH. NO PORTAL CHANGE. NO MIKROTIK CHANGE.**

**Product state:** Fully operational (coin → Activate → Active → clock). This investigation is STABILITY / EFFICIENCY / API PRESSURE on a working system, not a dead-product repair.

**Current source is authoritative.** Older logs that show `/ip/hotspot/active/set` or Verify `priority=critical` describe **previous** firmware. Current source: no `active/set`; Verify is `priority=normal`.

---

## 1. EXECUTIVE VERDICT

**Renz-Fi firmware is not proven to be the sole cause of 100% MikroTik CPU.**

**Renz-Fi firmware is proven to generate meaningful, serialized RouterOS API work** (one connect+login per job; ~5 HotSpot commands per Activate; Verify = another full login for one `active/print`). That work **can and does** add API/CPU pressure on this hAP lite, especially when RouterOS login already takes seconds.

**The 8-second failure is a login-reply timeout** (`ROUTEROS_IO_TIMEOUT_MS=8000`) after a fast TCP connect. That is the API failure boundary. It is **not** Ethernet, wrong host, or wrong password.

**The strongest technically defensible remaining root cause:**

> A **650 MHz single-core / 32 MiB RouterOS 7.20.7 hAP lite** becomes CPU-saturated under **combined** load: RouterOS 7 + wireless HotSpot + WinBox/Profile measurement + NAT/DHCP/DNS for even 1–2 clients + **Renz-Fi’s per-job API login cost**. When CPU is high, RouterOS answers `/login` slowly or not within 8 s. Firmware then reports `ROUTEROS_API_UNAVAILABLE`. Health recovery can add **one more login** because a Connected session still counts as `needsRouterOsWork()`.

This is **combination G**, not a single-source claim.

**Optimization of firmware API session churn is justified** to reduce pressure **without** changing customer-visible session/clock/activation behavior. The product is not “broken.”

---

## 2. ROOT CAUSE CLASSIFICATION

| Finding | Evidence | Classification | Confidence | Why |
|---|---|---|---|---|
| 100% CPU is a real RouterOS observation | WinBox Resource 3%…100%; multiple Profile samples at ~93–100% | **PROVEN ROOT CAUSE** of the *symptom* (saturation exists) | 99% | Screenshots, not theory |
| WinBox/Profile inflates `management` / `profiling` / `console` | Profile taken *while* profiling; `profiling` ~2%; `management` 57–68% | **HIGH-CONFIDENCE CONTRIBUTING FACTOR** | 85% | Category cannot be split WinBox vs API; MikroTik Profile is known-expensive on smips |
| `management` ≠ Renz-Fi | No RouterOS counter maps API to `management` in the supplied samples | **NOT PROVEN** that management=firmware | — | Do not equate |
| `spi` ≠ Renz-Fi API | SPI 15–27% while API is Ethernet (W5500 on ESP32, not MikroTik SPI bus to ESP) | **NOT PROVEN** as firmware API | — | hAP lite SPI is typically radio/flash/internal |
| HotSpot subsystem is the CPU owner | Profile `hotspot` often ~0–6.5% at 100% total | **RULED OUT** as the *dominant* Profile category | 80% | Low hotspot % does not prove HotSpot idle; it proves Profile did not assign most CPU there |
| Renz-Fi issues real API logins/commands during tests | Serial: connect/login, user/print, add/set, active/print, active/login, verify, health-probe | **PROVEN ROOT CAUSE** of *API workload* | 98% | Source + serial |
| Renz-Fi is the **sole** cause of 100% CPU | 1–2 phones + WinBox open + API jobs serialized queue=0–1; Verify is O(1) not O(N) | **NOT PROVEN** | — | Correlation while measuring ≠ sole causality |
| Per-job connect+login+disconnect is expensive on this CPU | Healthy `ros_login` ~344–406 ms; degraded 2.5–8.9 s; Activate wall 2.4–15.2 s for 5 cmds | **HIGH-CONFIDENCE CONTRIBUTING FACTOR** | 90% | TCP 3–20 ms vs login seconds proves cost is RouterOS API servicing, not LAN |
| 8019 ms login fail = 8000 ms IO timeout, no `login_rx` | Source `ROUTEROS_IO_TIMEOUT_MS`; `login_tx=18`; no `login_rx`; `tcp_connect=17 ok=1` | **PROVEN ROOT CAUSE** of Failure Class A | 95% | Exact match |
| Wrong API host / Ethernet down / wrong password | Same `10.20.0.1` authenticated; ping `10.10.10.1` OK; trap would be `auth_trap` | **RULED OUT** for Class A | 95% | |
| Parallel API storm / unbounded queue | Worker depth 1; `acquireSession`; observed jobs=0–1 | **RULED OUT** | 98% | |
| Portal heartbeat/session hammers RouterOS | `ApiServer` heartbeat only updates RAM `lastSeen`; GET session is JSON | **RULED OUT** | 98% | |
| ESP32 heap/DMA causing MikroTik CPU | Heap ~8.4 MB; ESP32 stays up | **RULED OUT** | 95% | |
| Two phones linearly double firmware API logins | `maybeEnqueueActiveVerify` picks **one** MAC then `break`; 60 s coalesce | **RULED OUT** as 2× Verify | 95% | See §7 |
| Connected session keeps health-probe logins alive after Verify fail | `needsRouterOsWork()` true for Active+connected; `tryEnqueueHealthProbe` | **PROVEN ROOT CAUSE** of *extra recovery logins* | 92% | Source + serial DEGRADED→PROBING |
| Health FSM eliminated all unnecessary ROS work | Verify still full login; probe after fail; per-job sessions remain | **NOT PROVEN** “enough” | — | Remediation reduced Critical Verify + added gates; churn remains |
| `unknown host IP 10.20.0.251` | After successful login; `active/login` TRAP | **PROVEN ROOT CAUSE** of Failure Class B only | 90% | Separate from Class A |
| Historical ESP32 stack panics cause current CPU | Current logs: heap stable, worker running | **RULED OUT** | 90% | |
| Increasing IO timeout would fix CPU | Longer wait monopolizes worker while ROS is already slow | **NOT PROVEN**; **do not do** | — | Treats symptom, worsens occupancy |

---

## 3. HARDWARE EVIDENCE

| Item | Value |
|---|---|
| Board | MikroTik hAP lite |
| CPU | MIPS 24Kc V7.4, **1 core**, 650 MHz (`smips`) |
| RAM | 32.0 MiB; typical free **~7.6–8.8 MiB** |
| Flash | 16 MiB; ~5.5 MiB free |
| RouterOS | **7.20.7 long-term** (2026-01-08); factory 6.49.8 |
| ESP32 | ESP32-S3-W5500-N8R8; `eth_ip=10.10.10.2` |
| API host | `10.20.0.1:8728` (`production-router-json`) |
| Ping target | `10.10.10.1` (not API) |
| Clients | 1 then 2 phones; Actives `10.20.0.251`, `10.20.0.247` |
| MACs | `06:36:E3:2C:C4:E8`, `DA:77:0A:31:67:B5` |
| Observed CPU | ~3, 4, 12, 16, 26, 28, 34, 43, 51, 62, 64, 91, **100%** |

**One-client test:** spikes still occurred.  
**Two-client test:** spikes still occurred.  
**Interpretation:** 1–2 HotSpot clients are **sufficient** to observe saturation **in this measurement setup** (WinBox+Profile+ROS7+wireless+Renz-Fi). It does **not** prove “too many phones.” It also does **not** prove firmware API scales with client count (it does not for Verify).

This platform is **severely constrained** for RouterOS 7. That is a hardware/OS fact, not an insult to the product.

---

## 4. ROUTEROS CPU FORENSICS

Profile samples (operator):

| Sample | management | spi | other | total |
|---|---|---|---|---|
| A | ~57% | ~18% | dhcp 6.5, dns 6, hotspot 6.5, kernel 3.5 | 100% |
| B | ~61.5% | ~21% | neighbor-discovery 6.5, kernel 3, profiling 2 | ~96–100% |
| C | ~59.5% | ~27% | — | 100% |
| D | ~68% | ~15% | — | ~93% |

### Category meaning (defensible)

- **`profiling`:** the measurement itself. Present in at least one high sample (~2%). **WinBox/Profile is a confounder.** Closing Profile would change the number; that does **not** “fix production CPU,” it only deconfounds the *meter*.
- **`management`:** WinBox, API, console, and other control-plane work are **lumped**. Renz-Fi API **may** appear here. WinBox **definitely** can. **Cannot split** from screenshots.
- **`console`:** earlier forensic ~7.5% with WinBox. Same confounder class.
- **`spi`:** On hAP lite this is **not** the ESP32 W5500 SPI. It is MikroTik-internal (typically wireless radio / NAND). **Not** a Renz-Fi Ethernet API fingerprint.
- **`hotspot`:** Often low at 100% total. So Profile does **not** blame the HotSpot process as the majority. Guest traffic can still appear as `networking` / `dns` / `dhcp` / wireless-via-`spi`.
- **`bridging` / `networking` / `dns` / `dhcp`:** 1–2 phones still generate DHCP, DNS, NAT, wireless. Non-zero in samples. **HIGH-CONFIDENCE** that client dataplane is *a* contributor, **not** the 57% management bar.

**Firm statement:** The screenshots prove **RouterOS is CPU-saturated while being profiled.** They do **not** prove which control-plane client (WinBox vs API vs both) owns `management`.

---

## 5. ROUTEROS API FORENSICS

| Observation | Meaning |
|---|---|
| `tcp_connect` 3–20 ms, `ok=1` | L4 to `10.20.0.1:8728` is healthy |
| Healthy `ros_login` ~344–406 ms | Login **can** be sub-second |
| Degraded `ros_login` 2584…8903 ms | RouterOS login **reply** slows under load |
| `login_tx=18`, no `login_rx`, `ros_login=8019 ok=0` | Write succeeded; **read timed out** at 8000 ms |
| `code=ROUTEROS_API_UNAVAILABLE`, `reason=unknown` | Verbose-off encoding of that timeout (`RENZFI_VERBOSE_ROUTER_API=0`) |
| Ping `10.10.10.1` 1–15 ms | Different host; proves appliance LAN, not API login |
| Prior Activate authenticated to `10.20.0.1` | Host/credentials work |

**Failure Class A boundary (proven):**

```
TCP CONNECT (fast, ok)
  → /login transmitted (login_tx ok)
  → RouterOS login reply delayed or absent
  → firmware waits ROUTEROS_IO_TIMEOUT_MS
  → ros_login≈8019 ok=0
```

Not Ethernet. Not wrong IP. Not auth trap.

**Login latency tracks RouterOS load, not W5500 RTT.** That is the important distinction.

---

## 6. Renz-Fi ROUTEROS WORKLOAD

**Invariant (current source):** one `RouterProvisioningWorker`; one `acquireSession()`; every HotSpot op `openRouterSession` → commands → `closeRouterSession`. **No `/quit`. No persistent session.**

### Activate (`createHotspotUser` + `loginHotspotActive`) — 1 TCP, 1 login

| # | Command | When |
|---|---|---|
| 1 | `/ip/hotspot/user/print ?name=` | Always (Model B) |
| 2 | `/ip/hotspot/user/add` **or** `/user/set` | Create vs reuse |
| 3 | `/ip/hotspot/active/print ?mac=` | Detect existing Active |
| 4 | `/ip/hotspot/active/login` | Only if **no** Active row. **Current source does not send `active/set`.** |
| 5 | `/ip/hotspot/active/print ?mac=` | After login, confirm row. Skipped if Active already present (`active_present` / no set) |

Serial `commands=5 session=1` matches this budget. Wall time 2.4 s (healthy) to ~9–15 s (loaded).

### Verify — 1 TCP, 1 login

| Command | `/ip/hotspot/active/print ?mac=` |

Priority **normal**. HEALTHY only. ≥60 s. Trust window 120 s after Activate for that MAC. **One MAC per interval** (`break`).

### Pause — 1 TCP, 1 login

`active/remove`, `cookie/remove` (user kept).

### Deauth/expire — 1 TCP, 1 login

`active/remove`, `user/print`, `user/remove`, cookie remove.

### HealthProbe — 1 TCP, 1 login

`/system/identity/print` then disconnect. Only if `wantsHealthProbe()` and `needsRouterOsWork()`.

### Admin (same worker, HEALTHY/UNKNOWN)

`testSettings` / cache sync / wireless / profiles: **many** prints. Gated `allowsAdminNonEssential()`. Not in the 1–2 phone HotSpot serial unless Admin UI was used. `fillStatistics()` (unfiltered `active/print`) is **not called** from the production worker/API.

### Not RouterOS

`GET /api/portal/session`, `POST /api/portal/heartbeat` (RAM `lastSeen` only), coin ISR, SSE.

---

## 7. CLIENT SCALING MODEL

**Critical source fact:** Verify does **not** run once per client per minute. It runs **at most once per 60 s for a single Connected MAC**.

Steady-state **HEALTHY**, no retries, no admin, no probes (probes only when unhealthy):

| | 1 Connected | 2 Connected | 5 Connected | 10 Connected |
|---|---|---|---|---|
| TCP connects / min | **1** (Verify) | **1** | **1** | **1** |
| API logins / min | **1** | **1** | **1** | **1** |
| `active/print` / min | **1** | **1** | **1** | **1** |
| `user/print` / min | 0 | 0 | 0 | 0 |
| Health probes / min | 0 | 0 | 0 | 0 |

**Per activation (event, not per minute):** 1 connect, 1 login, 1 `user/print`, 1 `user/add|set`, 1–2 `active/print`, 0–1 `active/login`.

**Idle client (not Connected, no pending flags):** **0** ROS. `[router-worker] idle no-router-work`.

**Expired client:** 1 Deauth job (1 login + several commands).

**Recovering (DEGRADED + any Connected):** Verify **suppressed**. HealthProbe **~1 login per 15–60 s** (backoff), **still O(1)**, not O(N), because probe is global.

**Two-phone CPU spike is therefore NOT explained by 2× firmware Verify logins.**  
If 2 phones raise CPU, the extra load is **dataplane / wireless / DHCP/DNS / second Activate event / WinBox**, not linear API Verify scaling.

Do **not** claim linear CPU vs N. Firmware API Verify is **flat** in N.

---

## 8. HEALTH FSM FORENSICS

**Implemented now** (not the old “no FSM” architecture):

```
HEALTHY ──job fail──► DEGRADED (1)
DEGRADED ──fail≥2──► UNAVAILABLE → COOLDOWN
DEGRADED/COOLDOWN + backoff + needsRouterOsWork → PROBING (HealthProbe)
PROBING ok → RECOVERING (15s dwell) → HEALTHY
```

| Work | Gate |
|---|---|
| Activate | HEALTHY, UNKNOWN, RECOVERING, DEGRADED, PROBING, CONNECTING — **not** UNAVAILABLE/COOLDOWN |
| Verify | **HEALTHY only** |
| Pause/Deauth | HEALTHY, RECOVERING, DEGRADED, UNKNOWN |
| Admin non-essential | HEALTHY or UNKNOWN |
| Probe | Unhealthy + `needsRouterOsWork()` + 15 s min |

**What the remediation fixed:** Verify demoted from Critical; Verify skipped when unhealthy; Activate retry **does not burn budget** when `!allowsHotspotActivate()`; idle with no sessions does not probe; trust window skips Verify 120 s after Activate.

**What remains:** `needsRouterOsWork()` is **true for a healthy Connected session** (so Verify can be scheduled). After Verify **fails**, that same flag **keeps HealthProbe logins going** for the whole remaining paid time. Recovery **reduces Verify** but **can increase logins** via probes.

**Does FSM reduce work during degradation?** **Yes for Verify. Partially for Activate. No for Connected-only probe.**

---

## 9. RETRY FORENSICS

| Path | Bound | ROS while UNAVAILABLE? |
|---|---|---|
| Transport fail | backoff 10→60 s, min connect 5 s | Next connect delayed |
| activation_error tick | 3 attempts / 20 s, then 60 s cooldown, then reset | **Deferred** if `!allowsHotspotActivate()` — does **not** hammer UNAVAILABLE |
| `activationRetryPending` + worker idle | one deferred Activate | Same gate |
| Duplicate Activate same gen already Connected | **skipped** (`alreadyAuthorizedThisGeneration`) | No extra login |
| Deauth cleanup | limited retries | Deauth still allowed in DEGRADED |
| HealthProbe | 15 s min, one worker | Extra login if Connected |
| Tight `while(!login)` | **None** | — |

**No unbounded retry storm in current source.** The remaining amplifier is **Connected → probe after login timeout**, not activation_error spinning on UNAVAILABLE.

---

## 10. VERIFY FORENSICS

**Still expensive relative to value:** a full login for one filtered `active/print`.

**Still useful:** only authoritative detector of `not_active` (successful print, empty Active) vs transport fail (`query_ok=no` → **does not** clear Connected).

**Already cheaper than old firmware:** Normal priority; HEALTHY only; 120 s trust; one MAC / 60 s; not Critical.

**Unnecessarily expensive today:** paying a **new API session** for a check that, for the first 120 s, is skipped anyway; after that, still a login. **Not** proven as the 100% CPU sole cause (1/min, O(1)).

**Verdict:** Keep Verify. Do **not** remove it. Do **not** make it Critical again. Optional later: event-driven only — **not** required to explain 100% CPU.

---

## 11. ACTIVATION FORENSICS

| Step | Necessary? |
|---|---|
| `user/print` | **Yes** — Model B reuse / uptime |
| `user/add` or `set` | **Yes** — entitlement |
| pre `active/print` | **Yes** — skip `active/login` if already authorized |
| `active/login` | **Yes** if no Active — this **is** Internet |
| post `active/print` | **Yes for first login** — Connected must not be optimistic. Redundant if `active_present` path already confirmed the row (current code **skips** login+verify print in that branch) |
| `active/set` | **Removed** in current source. Older serial with `active/set` is **not current**. |

**Cost:** 1 session, ~5 commands. Wall time is **RouterOS reply time**, not ESP32 compute. 15 s Activate = slow ROS, not a firmware busy loop.

Do **not** remove `active/login` or Model B prints to “save CPU.”

---

## 12. NETWORK/W5500 FORENSICS

| Claim | Verdict |
|---|---|
| Ethernet down | **RULED OUT** — link UP, `10.10.10.2`, ping OK, TCP OK |
| W5500 RTT causes 8 s login | **RULED OUT** — TCP 17 ms vs login 8019 ms |
| Wrong API host | **RULED OUT** — `10.20.0.1` previously authenticated |
| Ping vs API confusion | **RULED OUT** as contradiction — different roles |
| Occasional 149 ms ping | **CORRELATION ONLY** — does not explain 8 s |

W5500 RX drop of a login reply after TCP accept: **NOT PROVEN** (no DMA/link error in the failure window).

---

## 13. ESP32 MEMORY/CPU FORENSICS

Heap ~8.4 MB, largest ~8.25 MB, DMA tens of KB, jobs=0, portal up. Login wait uses `vTaskDelay(1)` on `router_worker` only.

**RULED OUT** as cause of MikroTik 100% CPU or the 8 s API timeout.

Historical `InstrFetchProhibited` / stack canary: **not present** in this production run. Worker stack 48 KB. Do not reopen.

---

## 14. UNKNOWN HOST IP FAILURE

**Failure Class B — separate.**

Sequence: `ros_login` **succeeds** (e.g. 3192 ms) → `user/print` ok → `user/set` ok → `active/print` ok → `active/login` **TRAP** `unknown host IP 10.20.0.251`.

RouterOS HotSpot `active/login` requires a **host** entry for that IP. Causes include: client not in `/ip/hotspot/host`, IP changed, login sent with an IP RouterOS does not have as a host.

Firmware sends `=ip=` when `user.ip` is non-empty (`loginHotspotActive`). That is a **HotSpot host-table** rejection, not Class A.

**Do not** treat Class B as proof of API timeout or 100% CPU. It can **coincide** with a slow/loaded router (login already 3 s) but the trap is a different mechanism.

---

## 15. HISTORICAL STACK/CRASH ISSUES

Prior: large `CommandResult` on stack, AsyncWebServer doing ROS, `RouterSession` in HTTP. **Current:** dedicated `router_worker`, scratch `CommandResult`, HTTP enqueues only.

**Not relevant** to current MikroTik CPU unless a new panic appears in serial (none in this evidence).

---

## 16. WHAT IS PROVEN

1. MikroTik CPU reaches 100% under the test setup.  
2. TCP to API is fast; login/command replies slow or time out at 8 s.  
3. Renz-Fi performs serialized per-job login + HotSpot commands.  
4. Activate works (including ₱1 and add-time Model B).  
5. Verify is one login/60 s/one MAC; failure does not wipe Connected.  
6. Portal HTTP is not RouterOS.  
7. No parallel API storm.  
8. Health probe can follow a failed Verify because Connected ⇒ `needsRouterOsWork()`.  
9. Current source does not send `active/set`.  
10. Class A (login timeout) ≠ Class B (unknown host IP).

---

## 17. WHAT IS HIGH-CONFIDENCE

1. WinBox/Profile **materially inflates** the `management` bar.  
2. hAP lite + ROS 7 + 32 MB is **near the platform ceiling** with HotSpot+wireless+WinBox.  
3. Renz-Fi API login/command cost **contributes** to control-plane pressure when jobs run.  
4. SPI % is **not** ESP32 W5500.  
5. Slow `user/print` (0.7–2.0 s) is RouterOS under load, not ESP32.  
6. Extra HealthProbe after login timeout is **avoidable** firmware work.

---

## 18. WHAT IS ONLY CORRELATION

1. “Renz-Fi was running when CPU hit 100%.”  
2. Two phones connected while spikes continued.  
3. `management` high while API jobs existed (WinBox also existed).  
4. Occasional high ping RTT.  
5. Class B trap during a generally slow API session.

---

## 19. WHAT IS RULED OUT

- Firmware as **sole proven** cause of 100% CPU  
- Parallel login storm / unbounded queue  
- Portal heartbeat → RouterOS  
- Ethernet unplug / wrong `10.20.0.1` / wrong password for Class A  
- ESP32 heap/DMA/watchdog as MikroTik CPU cause  
- “Too many phones” as the classification  
- Linear Verify scaling with client count  
- Current `active/set` path  
- Idle ROS polling with zero sessions  
- Historical ESP32 panics as this incident’s cause

---

## 20. REMAINING ROOT CAUSE

**One engineering conclusion:**

The remaining problem is **RouterOS control-plane saturation on an undersized hAP lite**, **measured with WinBox/Profile (confounded `management`)**, **aggravated by Renz-Fi’s correct-but-expensive per-job API login** whose latency **is** RouterOS’s login handler, not the LAN.

When saturation is high enough, `/login` exceeds 8 s → Failure Class A. Firmware then may add a HealthProbe login (Connected-only). That is the **actionable firmware remainder**. It is **not** “the system is non-functional.”

**100% CPU causality:** **PARTIALLY PROVEN** (saturation real; sole source **NOT PROVEN**; firmware contribution to API pressure **PROVEN**).

---

## 21. REQUIRED OPTIMIZATION

**YES — narrow, behavior-preserving.**

Justified by evidence:

1. **Stop Connected-only HealthProbe** after a job that already failed at login. Probe only when Activate/Pause/Deauth is pending.  
2. **Print the real login RX timeout reason** on the existing FAILED line (diagnostics, not a load change).  
3. **Do not** lengthen 8 s timeout, add workers, persist sessions blindly, remove Verify, remove `active/login`, disable TWDT, or change MikroTik as the first step.

**Not justified yet:** persistent API session (reliability risk; previous forensic said later). `/quit` only if next dump shows stale API sessions. Event-only Verify only after CX review.

**CPU pacing (`waitBeforeCommand`):** inserts 100–500 ms **between commands**. It **does not reduce command count**. On a slow router it **lengthens** the API session. It does **not** fix 100% CPU.

---

## 22. DO-NOT-TOUCH / FROZEN FUNCTIONALITY

sessionGeneration; donePaying reserve; Connected only after Activate success; session clock (`authorizedAtMs` + `grantedSeconds`); Model B user limit; no `active/set`; duplicate-activation guard; one RouterWorker; zero idle poll; coin/promo/voucher; portal UI/SSE/heartbeat contract; W5500 init; TWDT; setup wizard; sales bookkeeping; `active/login` success path.

---

## 23. VALIDATION PLAN

After a **future** implementation of §21.1–21.2 only:

| Test | Pass |
|---|---|
| Idle 10 min, 0 sessions | `idle no-router-work`; 0 logins; CPU sample **without WinBox Profile** recorded separately from **with Profile** |
| ₱1 Activate | 1 session, ~5 cmds, Connected after outcome |
| Sit Connected 3 min | 0 Verify during 120 s trust; then ≤1 Verify/60 s; **1 login not 2** |
| 2 phones Connected, HEALTHY | Still **≤1 Verify/60 s** (not 2) |
| Lab: disable API during Connected | `query_ok=no`; Connected kept; **no** `health-probe` unless critical work pending |
| Class B reproduce | `active/login` trap vs host table — separate ticket |
| CPU | Capture Resource **with Profile closed** for 60 s, then **open Profile** — delta is the confounder |

---

============================================================  
FINAL FORENSIC VERDICT  
============================================================

PRIMARY FAILURE:  
    RouterOS on hAP lite (ROS 7.20.7, 1×650 MHz, 32 MiB) becomes CPU-saturated; `/login` replies slow or miss the 8 s firmware IO timeout. Product HotSpot activation still works.

FIRMWARE ROOT CAUSE:  
    NOT PROVEN as the sole cause of 100% CPU.

ROUTEROS ROOT CAUSE:  
    HIGH-CONFIDENCE platform/control-plane saturation (ROS7 + wireless HotSpot on smips 32 MiB). Login/command latency is RouterOS-side.

FIRMWARE CONTRIBUTING FACTOR:  
    PROVEN: per-job TCP+login+disconnect; ~5-command Activate; Verify = full login for one print; HealthProbe extra login while Connected after job fail.

ROUTEROS CONTRIBUTING FACTOR:  
    HIGH-CONFIDENCE: slow API login/command servicing under load; HotSpot `active/login` host-table (Class B, separate).

NETWORK ROOT CAUSE:  
    RULED OUT for Class A (TCP 3–20 ms).

W5500 ROOT CAUSE:  
    NOT PROVEN / RULED OUT as primary (link UP, TCP fast).

ESP32 MEMORY ROOT CAUSE:  
    RULED OUT.

WINBOX/PROFILE EFFECT:  
    HIGH-CONFIDENCE confounder of `management` / `profiling` / `console`. Does not mean production CPU is fake; the meter is mixed.

100% CPU CAUSALITY:  
    PARTIALLY PROVEN  
    (saturation proven; sole firmware cause NOT PROVEN; firmware API contribution PROVEN)

8-SECOND API FAILURE:  
    PROVEN login RX timeout (`8019 ≈ 8000`); TCP and login TX succeeded; `ROUTEROS_API_UNAVAILABLE` is the verbose-off label.

UNKNOWN HOST IP FAILURE:  
    SEPARATE Class B — `active/login` TRAP after successful API login; not the 8019 failure.

CURRENT FIRMWARE STATUS:  
    OPERATIONAL. Health FSM reduced Verify priority and unhealthy Verify; per-job login churn and Connected-only probe remain.

OPTIMIZATION REQUIRED:  
    YES

OPTIMIZATION PRIORITY:  
    1) Connected-only health-probe suppression after login-timeout jobs.  
    2) Non-verbose login timeout reason logging.  
    3) Do not change session/clock/Activate/Verify-existence/W5500/TWDT.

PRODUCTION FUNCTIONALITY:  
    PRESERVE

============================================================

Based on the current source and supplied hardware evidence, the root cause is **combined RouterOS control-plane saturation on a constrained hAP lite (confounded by WinBox/Profile) plus Renz-Fi’s serialized per-job API login cost, which times out at 8 s when RouterOS does not answer `/login`**, with **~90% confidence on the API failure boundary and ~85% that firmware is not the sole 100% CPU cause**. The firmware **does** require **narrow** optimization because **Connected-only HealthProbe and per-job login still generate avoidable API pressure**, not because the product is non-functional.

**DO NOT MODIFY CODE YET.**
