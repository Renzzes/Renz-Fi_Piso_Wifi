# Renz-Fi RouterOS API Login Stability Forensic — 2026-08-15

**Mode:** SOURCE + supplied hardware log. **NO code changes. NO patch. NO flash. NO portal upload. NO RouterOS config changes.**

**Scope:** Remaining failure after a proven-successful ₱1 / 5-minute activation: later `verify-hotspot-active` TCP-connects to `10.20.0.1:8728` then fails `/login` after ~8 seconds.

**Related (do not reopen as root cause):**

- `docs/RENZFI_SESSION_SYNC_FORENSIC_2026-08-15.md`
- `docs/RENZFI_SESSION_CLOCK_FORENSIC_2026-08-15.md`
- `docs/RENZFI_DUPLICATE_ACTIVATION_FORENSIC_2026-08-15.md`
- `docs/RENZFI_MIKROTIK_ROUTEROS_STABILITY_FORENSIC_2026-08-15.md` (earlier 100% CPU / Verify-as-Critical audit; health FSM has since been implemented)

**Customer identity from the working activation:** MAC `06:36:E3:2C:C4:E8`.

---

## 1. EXECUTIVE VERDICT

The remaining problem is **RouterOS API login/session establishment instability after a successful TCP connect**, not Ethernet drop, not a wrong API host, and not destruction of the paid session by Verify.

| Category | Verdict | Confidence |
|---|---|---|
| RouterOS API login / session establishment | **PRIMARY.** TCP connect succeeded (`tcp_connect=17 ok=1`). `/login` was written (`login_tx=18`). No login reply was accepted within `ROUTEROS_IO_TIMEOUT_MS` (8000 ms). `ros_login=8019 ok=0`. `login_rx=` never printed. | **92%** |
| Health FSM behavior | **CONTRIBUTING recovery pressure.** Failed Verify → `DEGRADED` → `PROBING` → another full connect+login (`health-probe`) because a Connected paid session makes `needsRouterOsWork()==true`. Serialized, not parallel; not a tight loop. | **88%** |
| Excessive RouterOS workload | **CONTRIBUTING, bounded.** Every HotSpot job still does a fresh connect+login+disconnect. Verify is one login / ≥60 s (and skipped for 120 s after Activate). Not idle polling. Not enough by itself to prove 100% CPU on this run (no Profile sample in this incident). | **70%** |
| Configuration mismatch (`10.20.0.1` vs `10.10.10.1`) | **NOT the failure.** Same production host authenticated successfully minutes earlier. Ping target ≠ API host by design. | **95% that 10.20.0.1 is intentional** |
| Network / L3 instability | **NOT proven.** Ping `10.10.10.1` SUCCESS; `eth_ip=10.10.10.2`; TCP to API host succeeded in 17 ms. One 149 ms ping spike does not explain an 8 s login-reply absence. | **15% as cause** |
| W5500 / lwIP socket instability | **NOT proven.** Possible that a reply was lost after TCP accept; no W5500 error, DMA-low, or link-down log was supplied for this failure. | **20% as cause** |
| Heap / ESP32 CPU / watchdog | **NOT implicated.** Heap ~8.4 MB, `jobs=0 queue=0`, login path uses `vTaskDelay(1)` while waiting (not a busy spin). 8 s wait is blocking for the **router_worker** only. | **10% as cause** |

**Deepest proven statement:** RouterOS accepted a TCP session on the configured API endpoint and did not return a parseable `/login` sentence to the ESP32 within 8 seconds. Firmware then correctly classified the Verify as a transport failure (`query_ok=no`) and must not treat that as `not_active`.

**Not proven (do not implement as if proven):** why RouterOS stayed silent — CPU stall, API session slot, missing `/quit`, W5500 RX drop, or delayed reply >8 s.

---

## 2. CONFIRMED FACTS

Only items proven by source and/or the supplied log.

1. First activation of this run succeeded: `operation=create`, `user/add`, `active/print`, `active/login`, `active/print`, `active_id=*A1400FB`, `session_time_left=300`, `[router-worker] activate-hotspot-user … ok=yes`, `[session-clock] … usedActiveSet=no activeLogin=yes activeVerify=yes`.

2. Credentials, TCP path, and `production-router-json` host `10.20.0.1` **can** authenticate. `MikroTikDriver::openRouterSession` sets `source=production-router-json`.

3. Ethernet stayed up: `eth_ip=10.10.10.2`. Diagnostic ping of `10.10.10.1` succeeded (typical 5–26 ms; one 149 ms). Ping target is hardcoded in `NetworkDiagnostics.cpp` as `kPingTarget = "10.10.10.1"` — **not** the API host.

4. Later job: `[router-worker] dispatch type=verify-hotspot-active priority=normal` then `started type=verify-hotspot-active`.

5. Verify path: `MikroTikDriver::queryHotspotActivePresent` → `openRouterSession` → `connect()` + `login()` → `/ip/hotspot/active/print` → `closeRouterSession`. Login failed, so the print never ran.

6. `connect_gate_wait=0` — transport min-interval/backoff did not delay this connect.

7. `tcp_connect=17 ok=1` — `_client.connect(_host, _port)` returned true (`RouterOsClient::connect`).

8. `login_tx=18` — `sendLoginSentence("/login", =name=, =password=)` returned true.

9. There is **no** `[activate-latency] login_rx=` line. That printf runs only after `drainLoginResult` succeeds (`RouterOsClient.cpp` loginWithPassword). Therefore the login **read** failed.

10. `ros_login=8019 ok=0` matches `ROUTEROS_IO_TIMEOUT_MS = 8000` plus ~19 ms of connect/login overhead around the wait. `MikroTikDriver` constructs the client with those timeouts.

11. `[router-api] LOGIN FAILED host=10.20.0.1 user=admin source=production-router-json reason=unknown code=ROUTEROS_API_UNAVAILABLE`.

12. `reason=unknown` is **not** a RouterOS trap. `_loginFailureReason` is only stored when `_logLoginIo` is true; `_logLoginIo = (RENZFI_VERBOSE_ROUTER_API != 0)` and the default is `0`. Timeout `setError` uses `ROUTEROS_API_UNAVAILABLE` when verbose is off, vs `ROUTEROS_API_READ_TIMEOUT` when verbose is on. The printed code therefore **is** the non-verbose read-timeout code, not “unknown class of error.”

13. There is **no** `[router-api] LOGIN RX TIMEOUT` dump in the supplied log. That dump is also gated on `_logLoginIo`. So this run cannot distinguish `sentence_deadline_exceeded` vs `connection_closed` vs `router_closed_connection` vs `job_deadline_exceeded`.

14. Auth trap would set `reason=auth_trap` / `ROUTEROS_API_AUTH_TRAP` and would require a reply sentence. That path was not taken.

15. Failed login calls `disconnectInternal` (flush + `stop` + `releaseSession` + 5 ms delay). There is **no** RouterOS `/quit` sentence anywhere in `RouterOsClient.cpp`.

16. Verify outcome: `query_ok=no present=no`. Worker sets `slot.result.ok = queryOk` (false). Drain: `if (!outcome.ok) continue` — **does not** clear `connected`, freeze the clock, or move to `activation_error`. Only `reason=="not_active"` with `ok==true` does that.

17. `[ros-health] state=DEGRADED reason=job_failed failures=1` — `endJob(false)` → `noteJobFailure`. `ROUTER_HEALTH_FAILS_TO_UNAVAILABLE = 2`, so one failure stays DEGRADED.

18. `[ros-health] state=PROBING reason=readiness_check` then `[router-worker] dispatch type=health-probe`. Probe enqueue requires `wantsHealthProbe()` **and** `needsRouterOsWork()`. A Connected, unpaused, remaining>0 Active session returns true from `needsRouterOsWork()`.

19. One `RouterProvisioningWorker`. Queue depth 1. `acquireSession()` forbids overlapping API sockets from this firmware. Verify is **Normal** priority (not Critical).

20. Verify is scheduled from `PortalSessionManager::loop()` every 1 s tick → `maybeEnqueueActiveVerify()`: at most one enqueue / 60 s, only HEALTHY, only one Connected MAC, skipped for `ROUTER_ACTIVATE_TRUST_WINDOW_MS` (120000) after Activate success for that MAC. Not GET/SSE/heartbeat/coin.

21. Heap snapshot (~8.42 MB free, largest ~8.25 MB, min ~8.36 MB, jobs=0, queue=0, sse=0/1, portal=1) does not show exhaustion at the time it was captured.

---

## 3. CURRENTLY WORKING AND MUST REMAIN FROZEN

Do not touch unless a later prompt proves direct causality (this forensic does **not**).

- `sessionGeneration` on session / jobs / outcomes; stale cleanup no-op
- `donePaying` reserve → `activating` → `connected=false` → one critical Activate
- Connected committed only after matching Activate success (not optimistic)
- Session clock: `authorizedAtMs + grantedSeconds`; authorization captured at Active login/verify
- No `/ip/hotspot/active/set limit-uptime`
- User Model B: `new_limit = existing_uptime + requested`
- Duplicate-activation guard: same generation + Connected + already router-authorized → do not enqueue another Activate
- One RouterWorker; serialized RouterOS; no HTTP/SSE/coin direct ROS
- Zero idle ROS polling when `needsRouterOsWork()` is false
- TWDT, W5500 init, network init, stack sizes, SSE, portal assets, setup wizard
- Voucher / pause / terminate generation / promo / coin processing

Hardware-proven on this unit in the same run: create user, active/login, active verify inside Activate, session-clock commit, 300 s entitlement sync.

---

## 4. EXACT FAILURE TIMELINE

Absolute wall-clock timestamps were not in the supplied excerpt. Order is proven; the ≥120 s gap is proven by source, not by a printed millis.

```
T0  Customer pays (₱1) → reserve entitlement → activating, connected=false
    ONE Activate job (critical)

T1  openRouterSession(10.20.0.1, admin) → TCP + login SUCCESS
    /ip/hotspot/user/add
    /ip/hotspot/active/print
    /ip/hotspot/active/login  → active_id=*A1400FB, session_time_left=300
    /ip/hotspot/active/print  (in-job verify)
    closeRouterSession()      TCP stop, no /quit
    [router-worker] activate-hotspot-user ok=yes
    [session-clock] gen=4 granted=300 usedActiveSet=no activeLogin=yes activeVerify=yes
    ESP32 commits Active + connected=true
    Portal Connected; timer runs
    Health: job success → HEALTHY (backoff cleared)

T1+  Ethernet diagnostics continue: eth_ip=10.10.10.2, ping 10.10.10.1 SUCCESS
     Customer HotSpot path (10.20.0.0/24) is independent of API login

T1 .. T1+120s
     maybeEnqueueActiveVerify() skips this MAC (trust window 120s)
     No idle /ip/hotspot/active/print
     (If zero Connected, also: [router-worker] idle no-router-work)

T2  ≥120s after Activate success, and ≥60s since last verify enqueue
     [portal-verify] mac=… queued   (if that line was printed)
     [router-worker] dispatch type=verify-hotspot-active priority=normal
     [router-worker] started type=verify-hotspot-active

T2a connect_gate_wait=0
    tcp_connect=17 ok=1          TCP to 10.20.0.1:8728 accepted
    login_tx=18                  /login sentence written
    (wait ~8000 ms, no login_rx)
    ros_login=8019 ok=0
    LOGIN FAILED host=10.20.0.1 … reason=unknown code=ROUTEROS_API_UNAVAILABLE
    disconnectInternal("login_failed")
    queryHotspotActivePresent returns false
    [activate] operation=verify_active … query_ok=no present=no
    [router-worker] verify-hotspot-active query_ok=no present=no
    drainHotspotOutcomes: !ok → CONTINUE (Connected preserved)

T2b endJob(false) → [ros-health] state=DEGRADED reason=job_failed failures=1
    recordFailure() already set backoff 10s during login()

T2c PortalSessionManager::loop:
     tickHealth: when backoff elapsed, probeDesired=true (DEGRADED)
     needsRouterOsWork() true because session is Active+connected
     tryEnqueueHealthProbe → beginHealthProbe
     [ros-health] state=PROBING reason=readiness_check
     [router-worker] dispatch type=health-probe
     HealthProbe: another openRouterSession (connect+login) + /system/identity/print
```

The pasted DEGRADED → PROBING lines have no millis. Source requires backoff (10 s first failure) and probe min-interval (15 s). Immediate-in-the-paste is **not** proven as a zero-delay enqueue.

---

## 5. ROUTEROS CONNECTION LIFECYCLE

### 5.1 Success path (Activate — proven working)

```
MikroTikDriver::openRouterSession
  setCredentials(host from /config/router.json, port 8728)
  setCredentialSource("production-router-json")
  RouterOsClient::connect
    waitUntilConnectAllowed (here: 0 ms)
    acquireSession()            one firmware-wide API socket
    recordConnectAttempt()
    TCP connect host:8728       tcp_connect ok=1
    _connected=true, _loggedIn=false
  RouterOsClient::login
    sendLoginSentence /login =name= =password=
    drainLoginResult until !done / !trap / !fatal
    evaluateLoginResult → finalizeLoginSuccess → _loggedIn=true
  executeCommand × N (user/add, active/print, active/login, active/print)
  MikroTikDriver::closeRouterSession → disconnect
    flush + stop TCP
    releaseSession()
    vTaskDelay(5)
    NO /quit
```

### 5.2 Failure path (Verify — this incident)

```
openRouterSession
  connect                         tcp_connect=17 ok=1
  login
    sendLoginSentence             login_tx=18
    drainLoginResult
      readByte / readWord
      wait until _sentenceTimeoutMs (8000) with vTaskDelay(1)
      no complete login sentence
      setError(..., ROUTEROS_API_UNAVAILABLE)  // verbose off
      return false                no login_rx printf
    reason stays ""               // setLoginFailureReason no-ops unless verbose
    LOGIN FAILED … reason=unknown
    recordFailure()               backoff 10s → 20s → … → 60s
    disconnectInternal("login_failed")
  openRouterSession returns false
  queryHotspotActivePresent returns false (print never sent)
```

### 5.3 Reuse / stale-state questions (source answers)

| Question | Proven answer |
|---|---|
| Does LOGIN fail close the socket? | **Yes.** `login()` calls `disconnectInternal`. `openRouterSession` also `disconnect()` on login fail. |
| Is the same `RouterOsClient` object reused? | **Yes.** `MikroTikDriver` holds one `_routerOs`. Next job calls `connect()` again. |
| Does `connect()` reuse a live TCP socket? | If `_connected` or `_client.connected()`, it `disconnectInternal` first, then a new `_client.connect`. |
| New socket on next attempt? | **Yes**, intended. |
| Failed login leave firmware session gate held? | **No** if `disconnectInternal` runs (`releaseSession`). |
| Failed login leave RouterOS API session half-open? | **Unknown.** Firmware never sends `/quit`. RouterOS may retain the TCP-accepted session until its own timeout. Not proven as this incident’s cause (Verify is ≥120 s after Activate close). |
| Overlapping firmware sessions? | **No.** One worker + `acquireSession`. Health probe cannot run until Verify job `endJob`. |
| Next object state after fail? | `_connected=false`, `_loggedIn=false`. Reusable. |

---

## 6. ROOT CAUSE

**Proven mechanism (firmware):**

The Verify job’s RouterOS `/login` **read timed out**. TCP handshake succeeded. The login sentence was written. `drainLoginResult` never completed. Elapsed time equals the configured 8 s sentence timeout. The public log line is misleading: `reason=unknown` / `ROUTEROS_API_UNAVAILABLE` are the **verbose-off encoding of a login RX timeout**, not proof of “service missing” or “wrong password.”

**Proven non-causes:**

- Wrong API host (same `10.20.0.1` just authenticated)
- Wrong password (would be `auth_trap` after a reply; Activate just succeeded)
- Ethernet unplugged (ping + TCP connect)
- Verify wiping the paid session (`!ok` is a no-op on Connected)
- Parallel RouterOS calls from HTTP/SSE/coins
- Heap exhaustion
- Immediate reconnect after Activate (trust window 120 s; `connect_gate_wait=0` means backoff was clear because the last job **succeeded**)

**Deepest unproven WHY (RouterOS or path silent):**

The ESP32 waited 8 s with `vTaskDelay(1)` for the first login reply byte(s). Without the verbose `LOGIN RX TIMEOUT` dump we cannot prove:

- `client.connected()==0` (peer close / never replied), or
- `connected==1` and `bytesAvailable==0` (accepted TCP, no API bytes), or
- job deadline vs sentence deadline.

**Highest-likelihood explanations, ranked, not claimed as proven:**

1. **RouterOS API service accepted TCP but did not complete login within 8 s** (CPU/contention, Winbox/Profile, or API worker stall). Consistent with the earlier 2026-08-15 stability forensic (`ros_login≈8019` under 100% CPU). This run has **no** CPU Profile sample, so this stays a hypothesis.

2. **Login reply lost or never queued on the API service** after TCP accept (session limit, half-open previous API TCP without `/quit`, firewall/hotspot treating API-on-10.20.0.1 specially). Plausible because API is reached **via the HotSpot interface address**, not the ESP32 gateway address. Not proven.

3. **W5500/lwIP accepted SYN-ACK, TX login, missed RX.** Lower: no supporting diagnostic in the supplied log.

**Do not stop at “login failed.”** Stop at: **login reply did not arrive inside 8000 ms on a live TCP socket to a host that had just authenticated.**

---

## 7. CONTRIBUTING FACTORS

1. **Per-job session churn.** Every Verify is a full connect+login+disconnect for one `active/print`. Expensive relative to the command. Already documented; not redesigned.

2. **Connected session counts as `needsRouterOsWork()`.** After Verify login fails, health recovery **keeps generating logins** for the whole remaining paid session, even though there is no Activate/Pause/Cleanup pending. That is recovery-driven, not idle polling, but it is extra API pressure on a sick login path.

3. **Health probe repeats the same failing operation** (connect+login) instead of treating the Verify login timeout as the readiness sample.

4. **Verbose-off diagnostics.** Production hides `sentence_deadline_exceeded`, `loginBytesReceived`, and `client.connected()`, so field logs look like a generic UNAVAILABLE.

5. **No `/quit` on close.** May leave RouterOS API sessions until timeout. Not proven causal at T+120 s.

6. **8 s login wait monopolizes `router_worker`.** Serialized by design. During that wait, no Activate/Deauth can run. Not a watchdog spin (`vTaskDelay`).

7. **Earlier 100% CPU confounder (Winbox Profile)** from the morning forensic is **not in this log**. Do not reuse that screenshot as proof for this incident.

---

## 8. CPU SPIKE ANALYSIS

### Worst-case RouterOS request rate (current source, one Connected customer)

| Event | Frequency | API logins | Commands after login |
|---|---|---|---|
| Idle, zero sessions | **0** | 0 | 0 |
| Connected, HEALTHY | 1 Verify / 60 s after 120 s trust | 1 | 1× `active/print` |
| Activate (pay) | Event | 1 | ~4–5 (print/add/login/print) |
| Pause / Deauth | Event | 1 | 2–4 |
| Verify login fail → DEGRADED | 1 | (already counted) | 0 |
| Health probe while Connected + unhealthy | ≥15 s min interval, backoff 10→60 s | 1 per probe | 1× `identity/print` if login ok |
| `activation_error` retry | Gated by health; Activate allowed in DEGRADED/RECOVERING/PROBING; blocked in UNAVAILABLE/COOLDOWN | bounded by worker depth 1 | — |

**Login storm?** There is **no** `while (!login) connect()` tight loop. Backoff exists (`recordFailure` 10/20/40/60 s, min connect 5 s, probe min 15 s, queue depth 1).

**Possible retry storm?** After two failures → UNAVAILABLE → COOLDOWN → probe. While a customer remains Connected, probes continue until login succeeds or the session ends. Rate ≈ **at most one login per 15–60 s**, each potentially blocking 8 s. That can annoy a loaded RouterOS; it is **not** tens of logins per second.

**Concurrent operations?** **No** from this firmware. One worker, one `g_sessionActive`.

**Session churn?** **Yes, by design:** login per job. Verify + failed-verify probe = **two logins** around one failure. That is the main avoidable extra traffic.

**100% CPU from this path alone?** **Not proven on this run.** An 8 s hung login plus a follow-up probe is compatible with an already-overloaded API (previous Profile: management 55% while profiling). It is **not** sufficient to claim Renz-Fi alone pegs CPU at 100% with one Verify per minute.

---

## 9. ESP32 STABILITY ANALYSIS

| Risk | This failure path |
|---|---|
| Blocking | **Yes, on `router_worker` only.** `readByte` waits up to 8 s with `vTaskDelay(1)`. Portal HTTP/SSE stay on other tasks. |
| Tight retry / busy loop | **No.** Delay is 1 ms tick, then backoff before next connect. |
| Watchdog / Guru Meditation | **Not indicated.** Worker yields. Do not change TWDT from this evidence. |
| Heap fragmentation / leak | **Not indicated.** Heap ~8.4 MB. Login uses existing `RouterOsClient` / scratch command result. |
| Repeated String construction | Login path builds name/password words each attempt. One attempt per job. Not a storm. |
| Queue buildup | Depth 1; reject if busy. `jobs=0 queue=0` after. |
| W5500 socket leak | Firmware `stop()` + `releaseSession` on fail. RouterOS-side leak unproven. W5500 has a finite socket table; one session at a time should not exhaust it unless `stop()` fails silently (not logged). |
| Deadlock | Gate mutex taken with timeouts. Login holds IoLock on the client. Health probe waits until Verify ends. No proven deadlock. |
| Recursive callbacks | Verify is worker-loop, not async_tcp. |

**Verdict:** This login timeout is an **availability** problem for RouterOS work, not an ESP32 crash signature. Do not change memory architecture, W5500 init, or TWDT on this evidence.

---

## 10. HEALTH FSM ANALYSIS

### States and owners

Owned by `RouterApiTransportGate` (non-blocking). Transitions from `endJob` / `noteJobSuccess` / `noteJobFailure` / `tickHealth` / `beginHealthProbe` / `endHealthProbe`.

```
HEALTHY ──(job fail)──► DEGRADED (failures=1)
DEGRADED ──(fail ≥2)──► UNAVAILABLE
DEGRADED/UNAVAILABLE/COOLDOWN + backoff elapsed + needsRouterOsWork
        ──► desire probe ──► PROBING (HealthProbe job)
PROBING ──(ok)──► RECOVERING (15 s dwell) ──► HEALTHY
PROBING ──(fail)──► DEGRADED / UNAVAILABLE
```

### What is allowed

| Job | Allowed |
|---|---|
| Activate (paid) | HEALTHY, UNKNOWN, RECOVERING, DEGRADED, PROBING, CONNECTING. **Not** UNAVAILABLE/COOLDOWN. |
| Verify | **HEALTHY only** |
| Deauth / Pause | HEALTHY, RECOVERING, DEGRADED, UNKNOWN |
| HealthProbe | `wantsHealthProbe` and not already PROBING/HEALTHY/RECOVERING/CONNECTING |
| Admin non-essential | HEALTHY or UNKNOWN |

### Feedback loop?

**Bounded loop, yes:**

```
Verify (login timeout)
  → DEGRADED
  → (Connected ⇒ needsRouterOsWork)
  → HealthProbe (another login)
  → fail → failures=2 → UNAVAILABLE
  → COOLDOWN / backoff
  → HealthProbe again
```

Not uncontrolled: 15 s min interval, exponential backoff to 60 s, one worker.

**Unsafe aspect:** recovery **repeats login against an API that just failed login**, while the customer does not need a new authorization. That can extend RouterOS API pressure for the whole remaining session.

**Transient login failure vs Activate:** DEGRADED still **allows** Activate. UNAVAILABLE/COOLDOWN **block** Activate (prevents retry storm; can delay Add Time until a probe succeeds). A single Verify fail does **not** block the next paid Activate.

**Idle:** `needsRouterOsWork()==false` (no Connected, no pending flags) → no probe. **Do not add idle polling.**

---

## 11. IP TOPOLOGY ANALYSIS

| Address | Role | Evidence |
|---|---|---|
| `10.10.10.2` | ESP32 Ethernet / appliance HTTP API | Serial `eth_ip=`; Admin SPA; portal API; `StorageManager` wifi-config default `staIp` |
| `10.10.10.1` | ESP32’s **gateway / DNS** on the appliance LAN; **network-diag ping target** | `NetworkDiagnostics.cpp` `kPingTarget`; wifi-config `staGateway`; docs (`ADMIN_GATEWAY_10_20_0_1_FORENSIC.md`) |
| `10.20.0.1` | MikroTik **HotSpot gateway** and **production RouterOS API host** | Login FAILED `host=10.20.0.1`; Activate succeeded against the same source `production-router-json`; HotSpot `http://10.20.0.1/login` |
| `10.20.0.251` | Customer station on HotSpot LAN | Prior session-sync / clock forensics (same MAC) |

**Verdict: option A (+ D for ping).**

- **A.** `10.20.0.1` is the intentional HotSpot / API address. `10.10.10.1` is the appliance-LAN gateway (typically another address on the **same** MikroTik).
- **B.** Not a stale firmware default. Factory `kDefaultRouter` host is `10.40.0.1` (VLAN40 template). Field `10.20.0.1` comes from production `/config/router.json` after setup. First activation proves it works.
- **C.** Routing exists: ESP32 `10.10.10.2` TCP-connected to `10.20.0.1:8728` in 17 ms.
- **D.** Ping tests **gateway 10.10.10.1**, not API `10.20.0.1:8728`. Ping SUCCESS does not prove API login health; TCP connect SUCCESS does prove L4 reachability of the API port.

Firmware does not hardcode `10.20.0.1` as the API host. Do not “fix” the host.

```
Customer 10.20.0.251 ──HotSpot── 10.20.0.1 (MikroTik)
                                 │
                                 ├── API :8728  ◄── ESP32 10.10.10.2 (this login)
                                 └── gw 10.10.10.1 ◄── ping diagnostic
```

---

## 12. CUSTOMER EXPERIENCE IMPACT

### What this failure does **not** do (proven)

- Does **not** clear `connected` or move to `activation_error` (`!outcome.ok` continue).
- Does **not** zero `secondsLeft` / `grantedSeconds` / generation.
- Does **not** send `active/remove` or `user/remove`.
- Does **not** require a new login on the portal merely because Verify failed.
- Customer **Internet** (existing HotSpot Active `*A1400FB`) is independent of a later API login. If RouterOS HotSpot still has that Active, browsing continues while ESP32 cannot talk API.

### What the customer **can** experience

| Symptom | Likelihood from this incident |
|---|---|
| Portal stays **Connected**, timer keeps running | **Expected** (desired last-known-valid) |
| Internet continues | **Expected** if Active row still exists |
| Internet already working while portal says disconnected | **Not** from this Verify-fail path |
| Paid time destroyed | **Not** from this path |
| Add Time / new Activate delayed | If a **second** failure reaches UNAVAILABLE/COOLDOWN before the customer pays again |
| Pause/expire delayed | Deauth still allowed in DEGRADED; blocked only if health later forbids |
| “RETRY INTERNET” / activation_error | Only if a **later successful** Verify returns `not_active` (authoritative missing Active) |
| Long reconnect delay | Health probe + 8 s login timeout + backoff; portal may not show it |

**Desired conceptual behavior is already the Verify drain policy.** The remaining CX risk is **recovery logins** delaying a later critical job, and **UNAVAILABLE** blocking a subsequent Activate — not the first Verify timeout itself.

---

## 13. MINIMAL SAFE FIX

**Do not implement in this pass.**

Do **not**: persistent API session reuse, idle `active/print`, health login while idle, TWDT/W5500/sessionGeneration/clock/portal changes, reintroduce `active/set`, multiple workers.

### Smallest architecture change that matches the proven problem

**Treat a HotSpot job’s failed `openRouterSession` / login timeout as the health sample. Do not enqueue a HealthProbe that immediately repeats connect+login.**

Concretely (implementation later):

1. **Deduplicate recovery login.** When Verify (or any job) already failed at TCP-connect or `/login`, `endJob(false)` already moved health to DEGRADED. Skip `tryEnqueueHealthProbe` until backoff **and** either (a) pending **critical** work exists (Activate/Pause/Deauth), or (b) a longer single bounded probe is due **without** classifying “Connected-only” as `needsRouterOsWork()` for probe purposes.

   Split if needed:
   - `needsRouterOsWork()` — keep Verify scheduling for Connected while HEALTHY (unchanged 60 s / 120 s trust).
   - `needsHealthRecoveryProbe()` — true only for pending Activate/Pause/Cleanup/activation_error retry, **not** merely Connected.

   Effect: after this incident, firmware would log DEGRADED, skip the extra `health-probe` login, leave portal Connected, and wait for backoff before the next **necessary** ROS job.

2. **Always print login RX timeout cause on the existing LOGIN FAILED line** (`reason=sentence_deadline_exceeded|router_closed_connection|…`, `bytesReceived`, `connected`). Not a behavior change; required so the next hardware run can prove WHY the 8 s wait ended. Keep verbose packet dumps off.

3. **Optional, only if the next hardware dump shows `router_closed_connection` or RouterOS still listing a stale API session:** send `/quit` then TCP close. Still one worker, still per-job sessions. Do not do this until logs justify it.

4. **Do not** lengthen `ROUTEROS_IO_TIMEOUT_MS` as the first fix (worker job budget is 20 s; a longer hang worsens monopolization). Do not add idle polling. Do not reuse sessions in the first implementation prompt.

### Verify job itself

**Remain as-is for now** (option A), except it must not trigger a second login:

- It is session-driven, coalesced, HEALTHY-only, trust-windowed.
- It is the only authoritative `not_active` detector.
- Making it event-only would hide a real Active disappearance until pause/expiry.

Frequency reduction (B/C/D) is a later product choice, not required to stop this login-timeout + probe-churn.

---

## 14. SAFETY GUARANTEES

The proposed next implementation must keep:

| Guarantee | How |
|---|---|
| One RouterWorker | No new task |
| Serialized RouterOS | Existing gate + queue depth 1 |
| Bounded reconnect | Keep 5 s min connect, 10–60 s backoff, 15 s probe min |
| No login storm | No probe from Connected-only; no extra login after a login-timeout job |
| No health-probe storm | Probe only when critical work needs readiness |
| No idle polling | Zero ROS when no sessions and no pending flags |
| No CPU 100% from Renz-Fi poll | No new periodic prints/logins |
| No ESP32 busy loop | Keep `vTaskDelay` waits |
| No watchdog starvation | Do not extend login wait; do not change TWDT |
| No W5500 socket leak | Keep disconnect on every fail; optional `/quit` only if proven |
| No heap leak | No new per-loop allocations |
| No duplicate activation | Frozen guard stays |
| No stale generation mutation | Frozen |
| No customer entitlement loss | Keep `!ok` Verify as no-op; never map login timeout to `not_active` |
| No unnecessary ROS traffic | One login per necessary job, not Verify-fail + probe |

---

## 15. HARDWARE VALIDATION PLAN

Do not mark fixed until a controlled bench run. Operator flash only after an implementation prompt.

Enable enough logging to see `LOGIN FAILED reason=` **without** full verbose word dumps.

### A. Idle ≥10 minutes, no session

**Must appear:** `[router-worker] idle no-router-work`  
**Must not:** `/login`, `health-probe`, `verify-hotspot-active`, `user/print`, `active/print`

### B. ₱1 purchase (MAC `06:36:E3:2C:C4:E8` if same client)

**Must:** one Activate; `active/login` ok; `[session-clock] usedActiveSet=no`; Connected after outcome; no second Activate.

### C. Sit Connected through the 120 s trust window

**Must:** first Verify only after ≥120 s; `priority=normal`; if healthy, `query_ok=yes present=yes`.  
**Must not:** Verify during trust window; GET/SSE causing ROS.

### D. Repeat ₱1 after expiry / terminate (generation bump)

**Must:** stale cleanup no-op on new gen; one Activate; clock 300 s.

### E. Forced API silence during Connected (lab: disable `/ip/service api` or firewall drop 8728 **after** Active exists)

**Must:** `tcp_connect` fail **or** login timeout; `query_ok=no`; portal **stays Connected**; time preserved.  
**Must not:** `activation_error` from this transport fail; `not_active` without a successful print; **no** immediate second `health-probe` login if the Connected-only probe split is implemented.

### F. Restore API

**Must:** next **necessary** job (Add Time / new Activate / or one bounded probe if critical work exists) succeeds; health returns HEALTHY after dwell.  
**Must not:** login every few seconds; UNAVAILABLE blocking a paid Activate longer than one backoff window without a probe **when Activate is pending**.

### G. Multiple customers (if supported)

**Must:** still one worker; Verifies coalesced (one MAC / 60 s).  
**Must not:** parallel API sockets.

### H. Optional RouterOS CPU (Winbox Profile **closed** for a clean sample)

Capture CPU during idle, during Activate, during one Verify, during induced login timeout.  
**Must not** claim 100% from Renz-Fi if Profile/Winbox are open (previous forensic confounder).

### Serial lines that must appear on a login-timeout (after diagnostic-only log fix)

```
[activate-latency] tcp_connect=… ok=1
[activate-latency] login_tx=…
(no login_rx)
[activate-latency] ros_login≈8000 ok=0
[router-api] LOGIN FAILED … reason=<specific> code=ROUTEROS_API_READ_TIMEOUT
[router-worker] verify-hotspot-active query_ok=no present=no
```

**Must not appear after that Verify fail (once probe split is in):**

```
[ros-health] state=PROBING reason=readiness_check
[router-worker] dispatch type=health-probe
```

unless an Activate/Pause/Deauth is actually pending.

---

## 16. IMPLEMENTATION GATE

**DO NOT MODIFY CODE YET.**

Before an implementation prompt is written, confirm:

1. **Next hardware capture** of a failed Verify should include a **specific** login RX reason (`sentence_deadline_exceeded` vs `router_closed_connection` vs `job_deadline_exceeded`) and `loginBytesReceived` / `client.connected()`. That is the only missing proof of *why* the 8 s wait ended. Until then, implement only the **already-proven** extra-login (health-probe after Verify login fail) suppression — not timeout-value guesses, not session reuse, not `/quit` unless the dump shows peer-close/stale session.

2. Confirm with the operator whether Winbox/Profile was open during this login timeout (confounder for RouterOS CPU).

3. Confirm RouterOS `/ip/service print` for `api` (port 8728) and any `max-sessions` / firewall on `10.20.0.1` vs `10.10.10.1` — **inspect only**, do not change.

4. Product confirmation of the probe split: **Connected-only must not keep health-probe logins alive.** Verify-while-HEALTHY stays.

5. Explicit freeze list remains: sessionGeneration, clock, Model B, `active/set` removal, duplicate-activation guard, W5500, TWDT, wizard, portal UI.

When those are accepted, the implementation prompt should be **only**:

- stop Connected-only health-probe after a login-timeout job;
- print non-verbose login timeout reason on the existing FAILED line;
- contract test that Verify `query_ok=no` does not enqueue HealthProbe;
- hardware plan §15.

No other files.

---

**DO NOT MODIFY CODE YET.**
)
</think>

The forensic report is in `docs/RENZFI_ROUTEROS_LOGIN_STABILITY_FORENSIC_2026-08-15.md`. Below is the required 16-section report.

---

## 1. EXECUTIVE VERDICT

The remaining problem is **RouterOS API login/session establishment**: TCP to `10.20.0.1:8728` succeeds, `/login` is written, and **no login reply is accepted within 8 seconds**.

| Category | Confidence |
|---|---|
| RouterOS API login/session instability | **92% — PRIMARY** |
| Health FSM extra login after that failure | **88% — contributing** |
| Excessive RouterOS workload (per-job login) | **70% — contributing, bounded** |
| `10.20.0.1` vs `10.10.10.1` misconfiguration | **5% as cause** (`10.20.0.1` is intentional; **95%**) |
| Network / Ethernet drop | **15%** |
| W5500 / lwIP | **20%** |
| ESP32 heap / watchdog | **10%** |

---

## 2. CONFIRMED FACTS

- Activation succeeded against `production-router-json` host `10.20.0.1` (`active/login` `*A1400FB`, 300 s, `usedActiveSet=no`).
- Ethernet stayed up (`eth_ip=10.10.10.2`); ping of **`10.10.10.1`** succeeded. That ping target is **not** the API host (`NetworkDiagnostics.cpp`).
- Later `verify-hotspot-active` (`priority=normal`): `tcp_connect=17 ok=1`, `login_tx=18`, **no** `login_rx=`, `ros_login=8019 ok=0`.
- `8019 ms` matches `ROUTEROS_IO_TIMEOUT_MS = 8000`. `login_rx` is printed only if `drainLoginResult` succeeds — the **read** failed.
- `reason=unknown` / `ROUTEROS_API_UNAVAILABLE` is the **verbose-off encoding of a login RX timeout**, not a password trap (`RENZFI_VERBOSE_ROUTER_API` defaults to 0; timeout reason is only stored when verbose is on).
- Failed login closes the socket (`disconnectInternal`). There is **no** `/quit`.
- `query_ok=no` does **not** clear Connected (`if (!outcome.ok) continue`).
- `DEGRADED` then `health-probe` because a Connected session makes `needsRouterOsWork()==true`.
- One RouterWorker, queue depth 1, Verify only while HEALTHY, at most once / 60 s, skipped 120 s after Activate.

---

## 3. CURRENTLY WORKING AND MUST REMAIN FROZEN

sessionGeneration, donePaying → activating → one Activate, Connected only after Activate success, session clock, no `active/set`, Model B, duplicate-activation guard, one worker, zero idle ROS polling, W5500/TWDT/SSE/wizard/portal.

---

## 4. EXACT FAILURE TIMELINE

```
T0  Pay → reserve → activating, connected=false → one Activate
T1  TCP+login OK → user/add → active/login *A1400FB → session-clock 300s
    close TCP (no /quit) → Connected + timer
T1–T1+120s  Verify skipped (trust window); ping 10.10.10.1 OK
T2  ≥120s: verify-hotspot-active
    connect_gate_wait=0, tcp_connect=17 ok=1, login_tx=18
    wait ~8s, no login_rx, ros_login=8019 ok=0
    LOGIN FAILED host=10.20.0.1 reason=unknown ROUTEROS_API_UNAVAILABLE
    query_ok=no → drain no-op (Connected kept)
T2b DEGRADED failures=1
T2c PROBING → health-probe (second connect+login) because Connected ⇒ needsRouterOsWork
```

Pasted DEGRADED→PROBING has no millis; source still requires 10 s backoff / 15 s probe minimum.

---

## 5. ROUTEROS CONNECTION LIFECYCLE

```
connect (acquireSession, TCP) → login (/login write → drain reply)
  → commands → disconnect (flush+stop, releaseSession, 5ms)
  → later job: new TCP + login
```

This failure: TCP OK → `/login` written → drain times out → `disconnectInternal("login_failed")` → client reused on next job with a **new** `connect()`. Firmware does not leak the session gate. Whether **RouterOS** keeps a half-open API session is **unproven**.

---

## 6. ROOT CAUSE

**Proven:** Verify’s `/login` **reply timed out** on a live TCP socket to a host that had just authenticated. The log’s `unknown` / `UNAVAILABLE` hides that.

**Not proven:** *why* RouterOS sent no reply (CPU stall, API slot, missing `/quit`, RX drop). Highest-likelihood: API accepted TCP but did not finish login within 8 s — consistent with the earlier CPU forensic, but this run has **no** Profile sample.

Wrong host, wrong password, unplugged Ethernet, and session wipe are **ruled out**.

---

## 7. CONTRIBUTING FACTORS

- Every job is a full login (Verify = login + one `active/print`).
- Connected-only counts as “needs RouterOS work,” so a failed Verify **schedules another login** (health-probe).
- Production logs omit the real timeout reason.
- No `/quit` on close (possible RouterOS-side leftover; not proven at T+120 s).
- 8 s wait occupies `router_worker` (yields; not a busy loop).

---

## 8. CPU SPIKE ANALYSIS

- Idle, no session: **0** ROS traffic.
- Connected + HEALTHY: **1 login / 60 s** after 120 s trust.
- After Verify login fail: **+1 probe login** per 15–60 s while Connected remains, until health recovers or time expires.
- **No** tight login loop; queue depth 1; no parallel sessions.
- This path **cannot be proven** to be the sole 100% CPU cause on this run. It **can** add avoidable logins to an already slow API.

---

## 9. ESP32 STABILITY ANALYSIS

Blocking only on `router_worker` (`vTaskDelay(1)` for up to 8 s). No busy loop, no heap smoking gun, no queue buildup, no TWDT/W5500 evidence. Do not change memory, W5500, or watchdog from this.

---

## 10. HEALTH FSM ANALYSIS

Failed Verify → DEGRADED (1/2). Verify then **stops** (HEALTHY only). Recovery **does not stop**: Connected keeps `needsRouterOsWork()`, so probes continue. Serialized and backoff-bounded — **not** a storm — but it **repeats the same failing login** without new customer work. UNAVAILABLE (2 failures) can delay a later Activate; the first Verify fail does not.

---

## 11. IP TOPOLOGY ANALYSIS

| Address | Meaning |
|---|---|
| `10.10.10.2` | ESP32 Ethernet / appliance API |
| `10.10.10.1` | Appliance-LAN gateway; **ping diagnostic only** |
| `10.20.0.1` | HotSpot gateway **and** production API host (same host as successful Activate) |
| `10.20.0.251` | Customer on HotSpot LAN |

This is **A + D**: dual-interface MikroTik; ping is a different address than API. Factory default router host is `10.40.0.1`; field `10.20.0.1` is from `router.json`, not a stale firmware constant.

---

## 12. CUSTOMER EXPERIENCE IMPACT

From **this** failure: portal should **stay Connected**, timer should keep running, entitlement should stay, Internet should continue if Active `*A1400FB` still exists.

This path does **not** paint disconnected or destroy paid time. Risk is a **later** UNAVAILABLE blocking Add Time, or a **successful** Verify with `not_active` (real missing Active) — not this transport fail.

---

## 13. MINIMAL SAFE FIX

**Do not implement now.**

Smallest change:

1. **Do not enqueue `health-probe` merely because a Connected session exists** after a job that **already failed at login**. Keep Verify-while-HEALTHY. Split “needs probe” from “needs Verify.”
2. On the existing `LOGIN FAILED` line, always print the real RX timeout reason (`sentence_deadline_exceeded` / `router_closed_connection` / …) and `bytesReceived` / `connected` — no extra ROS traffic.
3. **`/quit` before TCP close** only if the next dump shows peer-close or a stale API session — not in the first patch.
4. Do **not** reuse API sessions, lengthen the 8 s timeout, add idle polling, or touch frozen session/clock/activation code.

Verify itself: **remain as-is** (session-driven, 60 s, 120 s trust).

---

## 14. SAFETY GUARANTEES

The next patch must keep: one worker, serialized ops, 5 s / 10–60 s / 15 s bounds, no Connected-only probe login, no idle poll, no busy loop, no TWDT/W5500 change, no duplicate Activate, no generation mutation, Verify `!ok` never mapped to `not_active`.

---

## 15. HARDWARE VALIDATION PLAN

- Idle 10 min: `idle no-router-work`; no login/probe/verify.
- ₱1: one Activate; `usedActiveSet=no`; Connected after outcome.
- After 120 s: one normal Verify; `present=yes` if API healthy.
- Repeat purchase after expire: generation bump; no stale cleanup of new session.
- Lab: disable API while Active exists → timeout/`query_ok=no`; portal stays Connected; **no** extra `health-probe` if the split is implemented.
- Restore API: next **necessary** job succeeds; no login every few seconds.
- Optional CPU: Profile with Winbox **closed**.

---

## 16. IMPLEMENTATION GATE

**DO NOT MODIFY CODE YET.**

Confirm before any implementation prompt:

1. Next failed-Verify log should include the **specific** RX timeout reason (the only missing WHY).
2. Whether Winbox/Profile was open during this timeout.
3. Inspect-only: RouterOS `api` service / firewall on `10.20.0.1` vs `10.10.10.1`.
4. Product OK: Connected-only must not keep health-probe logins.
5. Freeze list unchanged.

Then the prompt should be **only**: suppress Connected-only health-probe after a login-timeout job; print the real timeout reason; add a contract test; run §15.

**DO NOT MODIFY CODE YET.**