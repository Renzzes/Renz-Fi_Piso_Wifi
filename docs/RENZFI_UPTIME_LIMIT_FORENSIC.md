# Renz-Fi HotSpot Uptime Limit Forensic Investigation

**Date:** 2026-08-14  
**Mode:** SOURCE + LOG FORENSICS ONLY — **NO CODE CHANGES, NO ROUTEROS CHANGES**  
**Incident:** `/ip/hotspot/active/login` rejected with **`your uptime limit is reached`** after a valid ₱1 / 5-minute purchase (`remaining=300`).

**Source of truth:** current firmware + supplied logs. Prior docs are evidence, not gospel.

---

## 1. Incident Summary

Customer MAC `06:36:E3:2C:C4:E8` purchased additional time.

Renz-Fi correctly computed:

```text
purchasedMinutes = 5
secondsLeft / remaining = 300
```

Activation reused the existing MikroTik HotSpot user (`/ip/hotspot/user/set`) and then attempted `/ip/hotspot/active/login`.

RouterOS accepted the API session and the user/set, then **rejected authorization**:

```text
your uptime limit is reached
```

No Active row was created. The customer had a paid ESP32 entitlement and no Internet.

This is **not** an ESP32/Ethernet/API/WAN/portal failure. The failure boundary is HotSpot **user accounting vs login**.

---

## 2. Customer / MAC Involved

| Field | Value |
|---|---|
| Customer MAC | `06:36:E3:2C:C4:E8` |
| Derived HotSpot username | `0636E32CC4E8` (`MikroTikDriver::macToHotspotUsername`) |
| HotSpot password | same as username |
| Promo | ₱1 → 5 minutes → 300 seconds |
| RouterOS | 7.20.7 long-term |

Username mapping (`ESP32_S3_Firmware/src/router/drivers/MikroTikDriver.cpp` 335-339):

```cpp
String MikroTikDriver::macToHotspotUsername(const String &mac) {
  String username = mac;
  username.replace(":", "");
  username.toUpperCase();
  return username;
}
```

That username is **persistent** for the life of the `/ip/hotspot/user` object.

---

## 3. Expected Business Rule

**Frozen invariant**

| Case | Entitlement |
|---|---|
| New session | `purchasedSeconds = promoMinutes * 60` — **exactly** that many seconds of Internet |
| Add Time on an already-entitled session | `existingRemaining + purchasedSeconds` |

Must **not**:

- shrink a 5-minute purchase to 4:59 / 0 / reject
- inflate it
- subtract **historical RouterOS user `uptime`** from a newly purchased entitlement

Clock A (Renz-Fi `secondsLeft`) and Clock B (RouterOS `uptime` / `limit-uptime`) are **not** the same clock. Authorization must honor Clock A.

---

## 4. Observed Failure

Sequence from firmware logs:

```text
purchasedMinutes=5
remaining=300
  → /ip/hotspot/user/print     (user exists)
  → /ip/hotspot/user/set
  → /ip/hotspot/active/print   (no active row)
  → /ip/hotspot/active/login
  → !trap  "your uptime limit is reached"
```

API connect / authenticate / print / set all succeeded. Only Active login failed.

Post-incident WinBox (after investigation, **not** the live failure snapshot):

- `/ip hotspot active print ... mac=06:36:E3:2C:C4:E8` → none
- `/ip hotspot user print detail` → `default-trial`, `test` only (MAC user not listed **now**)
- User profiles: `idle-timeout=none`, `keepalive-timeout=2m`

`/ip hotspot user reset-counters` was run **after** the incident. It is **not** treated as the production fix and is **not** in firmware.

---

## 5. Successful First Activation

Same MAC, first purchase of 5 minutes (`remaining=300`):

```text
/ip/hotspot/user/print  → not found
/ip/hotspot/user/add    limit-uptime=00:05:00
/ip/hotspot/active/print → none
/ip/hotspot/active/login → SUCCESS
```

Observed Active row (equivalent):

```text
user="0636E32CC4E8"
uptime=18s
session-time-left=4m42s
```

Arithmetic:

```text
18s + 4m42s = 5m00s = 300s
```

**PROVEN from this hardware sample:** RouterOS `session-time-left` is **not** “a fresh 5-minute window.” It is:

```text
session-time-left ≈ limit-uptime − user.uptime
```

On a **new** user, `uptime=0`, so `limit-uptime=00:05:00` **happens to equal** the purchased entitlement. That is why the first purchase works. It does **not** prove that `limit-uptime` means “minutes remaining from now” on a reused user.

---

## 6. Failed Re-Activation

Same MAC, later purchase again `purchasedMinutes=5`, `remaining=300`.

This time the user **already existed**, so the driver took the **set** branch, then **login** (because Active was empty):

```text
user/print → found
user/set   limit-uptime=00:05:00   (same 300s, no uptime reset)
active/print → none
active/login → "your uptime limit is reached"
```

**PROVEN from the branch taken:** `/ip/hotspot/user` for `0636E32CC4E8` was still present. Otherwise the code would have logged `user/add`, as on the first success.

That leftover user still carried **Clock B `uptime`** from the previous session. Renz-Fi then wrote Clock A’s new 300s into `limit-uptime` **without clearing or adding Clock B `uptime`**.

RouterOS then evaluated:

```text
if user.uptime >= user.limit-uptime:
    reject login with "your uptime limit is reached"
```

After a prior ~5-minute session, `uptime` is ≈ 5 minutes. Setting `limit-uptime=00:05:00` again makes remaining ≈ 0. Login is correctly rejected **by RouterOS given that accounting state**. The defect is that Renz-Fi **constructed that state**.

---

## 7. Source-Level Execution Trace

### 7.1 Purchase → `secondsLeft = 300`

`PortalSessionManager::donePaying()`  
`ESP32_S3_Firmware/src/PortalSessionManager.cpp` 615-641

```text
saleMinutes            = session["purchasedMinutes"]     // 5
purchasedSeconds       = saleMinutes * 60L               // 300
existingRemaining      = session["secondsLeft"]
addTime                = existingRemaining > 0
                         && state in {active, paused, activating, activation_error}
newRemaining           = addTime ? existingRemaining + purchasedSeconds
                                 : purchasedSeconds
session["secondsLeft"] = newRemaining                    // 300 on this incident
sessionState           = activating
connected              = false
```

Logged as `remaining=300`. That is **new-session arithmetic** (`addTime == false`), not Add Time. Add Time would have been `existingRemaining + 300` (e.g. 7:30).

Promo minutes themselves are **not** the bug. ₱1 → 5 → 300 is correct.

### 7.2 Enqueue HotSpot job

`PortalSessionManager::onSessionActivated()`  
`PortalSessionManager.cpp` 2333-2373

```text
user.mac             = "06:36:E3:2C:C4:E8"
user.ip              = session["ipAddress"]
user.timeoutSeconds  = session["secondsLeft"]            // 300
user.profile         = session["hotspotProfile"]
tryEnqueueActivateHotspotUser(user)
Serial: remaining=300
```

**Faulty variable at the RouterOS boundary:** `HotspotUser.timeoutSeconds` is treated as if it were a **fresh lifetime cap**, not “seconds of Internet from now,” and **not** `current RouterOS uptime + entitlement`.

### 7.3 Worker → driver

`RouterProvisioningWorker.cpp` 1148-1171  
→ `RouterPlatform::provisionHotspotUser()` (`RouterPlatform.cpp` 601-604)  
→ `MikroTikDriver::authorizeUser()` = `createHotspotUser()` (`MikroTikDriver.cpp` 2629-2630, 2489+)

### 7.4 `limit-uptime` construction

`MikroTikDriver::formatLimitUptime()` 342-349

```text
timeoutSeconds = 300
→ "00:05:00"
```

There is **no** read of RouterOS `uptime`. There is **no** `uptime + timeoutSeconds`. There is **no** `/ip/hotspot/user/reset-counters` anywhere in the firmware (repo grep: only coin-slot `CoinManager::resetCounters`).

**Exact meaning of what Renz-Fi sends:**

```text
limit-uptime = formatLimitUptime(session["secondsLeft"])
             = purchasedSeconds          // this incident (new session)
             = existingRemaining + purchasedSeconds  // Add Time path
```

It is **never**:

```text
limit-uptime = RouterOS.uptime + entitlement
```

### 7.5 User print → set (this incident)

`createHotspotUser()` 2531-2570

```text
/ip/hotspot/user/print
  ?name=0636E32CC4E8

existingId not empty
  → /ip/hotspot/user/set
       =.id=<existingId>
       =profile=<profile>
       =limit-uptime=00:05:00
       =disabled=no
       =comment=06:36:E3:2C:C4:E8
```

**Not sent:** `uptime`, `reset-counters`, user remove+re-add.

`user/set` **reuses** the persistent user. RouterOS keeps cumulative `uptime`, `bytes-in`, `bytes-out`.

### 7.6 Contrast: first success (`user/add`)

Same function, 2571-2593, when print finds no id:

```text
/ip/hotspot/user/add
  =name=0636E32CC4E8
  =password=0636E32CC4E8
  =profile=<profile>
  =limit-uptime=00:05:00
  =comment=06:36:E3:2C:C4:E8
```

New object → `uptime=0` → `00:05:00` remaining → login succeeds.

### 7.7 Active print → login (this incident)

`loginHotspotActive()` 2427-2478

```text
/ip/hotspot/active/print
  ?mac-address=06:36:E3:2C:C4:E8

activeId empty  → NOT the Add-Time active/set branch

/ip/hotspot/active/login
  =user=0636E32CC4E8
  =password=0636E32CC4E8
  =mac-address=06:36:E3:2C:C4:E8
  =ip=<client ip if present>
```

Login does **not** send `limit-uptime`. RouterOS applies the **user** object’s `limit-uptime` vs **user** `uptime`.

On trap (`2472-2477` + `2602-2610`):

```text
Hotspot active login failed: your uptime limit is reached
[activate] FAILED step=hotspot-active-login reason=...
```

### 7.8 What the code never does on reuse

| Operation | Present? |
|---|---|
| `/ip/hotspot/user/reset-counters` | **No** |
| `=uptime=0s` on set | **No** |
| Delete user then add | **Only on expire/terminate `deauthorizeUser`**, not on activate |
| Read `uptime` and add it to `limit-uptime` | **No** |
| Create a new username per purchase | **No** — MAC is the username forever |

`deauthorizeUser()` (`2673-2732`) **does** `user/remove` (plus active/cookie). If that job had completed, the next purchase would have been `user/add` again. This failure used **`user/set`**, so the user object **had not been removed**.

`pauseHotspotUser()` (`2633-2670`) **keeps** the user on purpose (resume without recreate). That is a second, designed leftover-user path with the same `limit-uptime` mistake on resume.

---

## 8. RouterOS Accounting Model

Supported by (a) MikroTik HotSpot user fields, (b) the first-activation sample, (c) the exact trap string.

| Field | Role |
|---|---|
| `/ip/hotspot/user limit-uptime` | **Total allowed online time** for that user object (0 = unlimited). Not “seconds from now” unless `uptime` is 0. |
| `/ip/hotspot/user uptime` | **Cumulative** time this user has been logged in. Persists across Active logout until reset-counters or user remove. |
| `/ip/hotspot/active` | Current authorized session. Empty ≠ user gone. |
| `session-time-left` | Remaining of the cap: observed `4m42s = 5m − 18s`. |
| Trap `your uptime limit is reached` | Login when `uptime >= limit-uptime`. |

User profiles in the supplied print (`idle-timeout=none`, `keepalive-timeout=2m`) do **not** produce this trap. This trap is the **user uptime cap**.

Post-incident `user print` without `0636E32CC4E8` does **not** contradict leftover-user-at-failure: the failure path was `user/set`, which requires the object to exist **then**. Later investigation (or a later deauth) can remove it.

---

## 9. Renz-Fi Accounting Model

| Field | Meaning | Persistence |
|---|---|---|
| `purchasedMinutes` | This coin-window’s promo minutes (5). Cleared to 0 at Done Paying. | RAM/SD session |
| `secondsLeft` | Entitlement remaining on **ESP32**. New session = `purchasedMinutes*60`. Add Time = old remaining + new. Decremented by `tickSessions()` (loop seconds). | RAM/SD |
| `sessionState` / `connected` | Local lifecycle. `connected=true` only after Activate **outcome.ok**. | RAM/SD |
| `timeoutSeconds` | Copy of `secondsLeft` at enqueue. Sent to RouterOS as `limit-uptime`. | Job slot |

Renz-Fi **never stores** RouterOS `uptime`. It **assumes** `limit-uptime = secondsLeft` is a remaining-time grant.

That assumption is true **only** for a brand-new user (`uptime=0`) or if counters were reset.

---

## 10. Clock / State Comparison

| Item | Renz-Fi (Clock A) | RouterOS (Clock B) | Meaning | Persistence | Used for authorization? |
|---|---|---|---|---|---|
| New purchase | `secondsLeft=300` | not consulted | Paid entitlement | ESP32 session | No (ESP32 only) |
| `limit-uptime` written | `formatLimitUptime(300)` = `00:05:00` | User lifetime cap | Driver treats remaining as lifetime | User object | **Yes** |
| `uptime` | not read, not reset | Cumulative consumed time | Prior session(s) | User object until remove/reset | **Yes** (compared to limit) |
| Active row | `connected` after outcome | `/ip/hotspot/active` | Internet grant | Until logout/expire/remove | **Yes** (traffic) |
| First success | remaining 300 | uptime 0 → left 5:00, then 4:42 at 18s | Fresh user | New user | Login OK |
| This failure | remaining 300 | uptime ≥ ~5:00, limit 5:00 | Leftover user | Reused user | Login trap |
| Host row | ignored | device on Hotspot LAN | Not auth | Host table | No |

**Incorrect coupling:** Clock A remaining is written into Clock B’s **lifetime cap**, while Clock B’s **consumed uptime** is left in place.

---

## 11. Root Cause

**PROVEN**

Renz-Fi reuses a persistent MAC-named `/ip/hotspot/user` and sets `limit-uptime` to the **new ESP32 entitlement** (`secondsLeft`, here 300 s / `00:05:00`) **without resetting or compensating RouterOS cumulative `uptime`.**

On a **new** user, `uptime=0`, so that write is accidentally correct and `/ip/hotspot/active/login` succeeds.

On a **reused** user whose previous session already consumed ≈ that cap, RouterOS evaluates `uptime >= limit-uptime` and rejects login with **`your uptime limit is reached`**. The 5-minute purchase is never applied as a **fresh** 5-minute grant.

**Exact responsibility**

| Layer | Location |
|---|---|
| Faulty assumption | `limit-uptime` == remaining entitlement (true iff `uptime==0`) |
| Variable | `HotspotUser.timeoutSeconds` ← `session["secondsLeft"]` |
| Function | `MikroTikDriver::createHotspotUser()` existing-user branch (`user/set` 2548-2570) |
| Triggering op | `MikroTikDriver::loginHotspotActive()` → `/ip/hotspot/active/login` (2456-2478) |
| State transition | leftover user (prior session / pause / missed `user/remove`) + new purchase `secondsLeft=300` + empty Active |

This is a **Renz-Fi construction error**, not a broken RouterOS login implementation. RouterOS enforced its documented user-uptime cap on the object Renz-Fi left in place.

---

## 12. Evidence Matrix

| Evidence | Observation | Implication | Classification |
|---|---|---|---|
| First activation `user/add` + login OK | Fresh user, `uptime=18s`, `session-time-left=4m42s` | `left = 5m − uptime`; ₱1→300s math is valid | **PROVEN** |
| Second activation `user/set` + login trap | User existed; Active empty; trap is uptime cap | Reuse path; leftover `uptime` vs new `limit-uptime=5m` | **PROVEN** (mechanism). Exact leftover `uptime` seconds at trap time **not printed** → that numeric remainder is **STRONGLY INDICATED** (~≥300s) |
| `user/set` attrs in source | profile, limit-uptime, disabled, comment only | No uptime reset | **PROVEN** |
| No `reset-counters` in firmware | Grep: coin counters only | Firmware never clears Clock B | **PROVEN** |
| `timeoutSeconds = secondsLeft` | `onSessionActivated` 2350 | New 5 min sent as lifetime 5 min | **PROVEN** |
| `remaining=300` with `purchasedMinutes=5` | `donePaying` newRemaining = purchasedSeconds | New-session formula, not Add Time | **PROVEN** |
| `active/login` not `active/set` | Empty Active → login branch | Not in-place Add Time | **PROVEN** |
| API/print/set succeeded | Logs | Not transport/CPU/WAN | **PROVEN** |
| Why user still existed | `user/set` taken | `deauthorizeUser` did not remove it beforehand | **PROVEN** that user remained; **STRONGLY INDICATED** why (independent `limit-uptime` logout and/or pause-keep-user and/or failed `user/remove`) |
| CPU graphs | High console/management/spi | Different symptom; trap is application-level | **RULED OUT** as cause of this string |
| Post-incident user print missing MAC user | After investigation | Must not be used as failure-time state | **PROVEN** (timing) |

---

## 13. Ruled-Out Causes

| Candidate | Why |
|---|---|
| ESP32 network / Ethernet failure | User print + user set completed on the same API session |
| RouterOS API connection / `/login` failure | Explicitly succeeded before HotSpot commands |
| Portal UI / captive portal HTML | Failure is worker `active/login` trap |
| WAN failure | HotSpot auth does not require WAN |
| `user/set` API failure | Set succeeded; failure is the next command |
| `active/print` failure | Print ran; empty result selected the **login** branch |
| Promo / `purchasedMinutes` wrong | 5 × 60 = 300; first add with the same math worked |
| CPU as direct cause | Trap is HotSpot accounting, not a timeout/disconnect |
| “RouterOS is broken” | RouterOS applied `uptime` vs `limit-uptime` consistently with the 18s / 4m42s sample |
| This being the same *symptom* as session-desync Connected-after-Active-empty | Different symptom (login reject vs UI Connected). They **can share a precondition** (leftover user after Active died on `limit-uptime`) — see §14 |

---

## 14. Remaining Unknowns

| Unknown | Status |
|---|---|
| Numeric `uptime` / `limit-uptime` on `0636E32CC4E8` **at the trap instant** | **UNKNOWN** — not in the supplied print (user already gone later). Mechanism does not require that dump once `user/set` + this trap + the 4m42s identity are in evidence. |
| Exact leftover-user reason (independent ROS expiry vs pause vs failed `user/remove`) | **STRONGLY INDICATED** independent `limit-uptime` logout from the prior desync incident; not uniquely proven for this one purchase |
| Whether a cookie was still present at login | **UNKNOWN** — activate does not clear cookies; deauth/pause do. Cookie does not replace the uptime check. |
| Add Time while Active (`active/set`) for this MAC | **Not this incident** — login branch was used |

### Relation to `docs/RENZFI_SESSION_DESYNC_EXPIRY_FORENSIC.md`

That document proved dual clocks: ESP32 `secondsLeft` vs RouterOS Active/`limit-uptime`, and that Active can empty while ESP32 still shows Connected.

**Do not merge the symptoms.** This incident is: **purchase → login trap**.

**They compose:**

1. Prior session: `user/add` `limit-uptime=5m`, `uptime` climbs, Active ends when Clock B hits the cap (desync doc).
2. User object **remains** if ESP32 has not run `deauthorizeUser` (desync: ESP32 still “connected”; pause: user kept by design).
3. Customer pays again. Clock A sets `secondsLeft=300`. Clock B user still has `uptime≈5m`.
4. `user/set limit-uptime=00:05:00` + `active/login` → **this trap**.

The desync forensic listed independent `limit-uptime` expiry as STRONGLY INDICATED. This incident **upgrades the accounting misuse** (`limit-uptime = remaining`, ignore `uptime`) to **PROVEN**, because the trap string and the add-vs-set split match that model exactly.

Prior `INVESTIGATION_B_DONE_PAYING_ACTIVATION_FORENSIC.md` correctly lists `user/set` `limit-uptime=<HH:MM:SS>` from `secondsLeft`, but treats that as sufficient remaining time. **Source + this trap contradict that interpretation** for reused users. Source + logs win.

---

## 15. Correctness Invariants

Freeze:

```text
NEW PURCHASE
  purchasedSeconds = promoMinutes * 60

NEW SESSION (no remaining Renz-Fi entitlement)
  entitlement = purchasedSeconds
  RouterOS must allow exactly that many seconds of Internet
  historical user.uptime must not consume that grant

ADD TIME (remaining > 0 on an entitled Renz-Fi session)
  entitlement = existingRemaining + purchasedSeconds
  RouterOS must allow that many seconds from the moment of grant
  (or keep the current Active and extend remaining — not a lifetime cap clash)

NEVER
  subtract historical RouterOS uptime from a newly purchased entitlement
  set limit-uptime = entitlement on a user whose uptime is already ≈ entitlement
```

---

## 16. Recommended Fix Boundary

**DO NOT IMPLEMENT IN THIS PASS.**

### Change (eventually)

| Item | Detail |
|---|---|
| File | `ESP32_S3_Firmware/src/router/drivers/MikroTikDriver.cpp` |
| Function | `createHotspotUser()` existing-user branch (and the `timeoutSeconds` → `limit-uptime` contract) |
| Faulty assumption | `limit-uptime` is “seconds remaining from now” |
| Actual RouterOS meaning | `limit-uptime` is lifetime cap vs persistent `uptime` |
| State transition | Reuse of `0636E32CC4E8` after prior consumption + new `secondsLeft` |
| Operation | `/ip/hotspot/user/set =limit-uptime=<entitlement>` then `/ip/hotspot/active/login` |

A correct later design must make Clock B honor Clock A. Examples (choose one in a future implementation pass; not now):

- Reset user counters (or remove+re-add the MAC user) **before** applying the new entitlement, **or**
- Set `limit-uptime = currentUptime + entitlement` after reading `uptime`, **or**
- Stop using a finite user `limit-uptime` and let ESP32 expire/deauth be the only cap.

Resume-after-pause uses the **same** `createHotspotUser` + leftover user; any fix must cover new session, Add Time, and resume.

### Must not change (this incident)

- Promo ₱1 = 5 minutes math
- Setup wizard
- HotSpot server NAT/firewall redesign
- `idle-timeout` / `keepalive-timeout` as the “fix” for this trap
- Portal UI hiding the error
- Automatic `reset-counters` as a silent production hammer without a defined lifecycle
- RouterOS API login protocol
- Blaming CPU

---

## Final output (A–J)

**A. Exact root cause**  
Reused persistent HotSpot user + `limit-uptime` set to the new 300 s entitlement **without resetting cumulative `uptime`** → RouterOS rejects `active/login` with `your uptime limit is reached`.

**B. Proof**  
(1) Source: `user/set` attrs have no uptime reset (`MikroTikDriver.cpp` 2548-2556).  
(2) Source: `timeoutSeconds = secondsLeft` (`PortalSessionManager.cpp` 2350), formatted as `00:05:00`.  
(3) Logs: first time `user/add` succeeds; later `user/set` + `active/login` traps.  
(4) Hardware identity: `uptime=18s` + `session-time-left=4m42s` = 5:00, proving Clock B remaining is `limit − uptime`.

**C. Exact file/function**  
`MikroTikDriver::createHotspotUser()` (set branch) + `loginHotspotActive()`; fed by `PortalSessionManager::onSessionActivated()`.

**D. Exact state/variable**  
`HotspotUser.timeoutSeconds` / RouterOS user `limit-uptime` vs leftover user `uptime`. Username `0636E32CC4E8`.

**E. Why first purchase works**  
`user/add` creates `uptime=0`. `limit-uptime=00:05:00` is a full 5-minute remaining grant.

**F. Why re-purchase / re-activation fails**  
`user/print` finds the old user → `user/set` overwrites the cap to 5 minutes again → `uptime` still ≈ previous session → login trap. Active was empty so the code used `login` not `active/set`.

**G. Why the new 5 minutes is not a fresh grant**  
Renz-Fi stores remaining on ESP32 only. It writes that remaining into a **lifetime** field on a user that has **already used** a lifetime. RouterOS subtracts history; Renz-Fi does not.

**H. What must eventually change**  
The activate path’s mapping of `secondsLeft` → `/ip/hotspot/user` accounting (reset, compensate, or stop using finite `limit-uptime`). Same function for resume.

**I. What must not change**  
Purchase arithmetic; wizard; blaming this trap on CPU/WAN/UI; treating RouterOS as randomly broken.

**J. Confidence**  
**PROVEN** for the accounting mechanism and the add-vs-set split.  
**STRONGLY INDICATED** that leftover `uptime` was ≥ the new 5-minute cap (trap + prior 5-minute session).  
**UNKNOWN** exact `uptime` print at the trap instant (not captured).
