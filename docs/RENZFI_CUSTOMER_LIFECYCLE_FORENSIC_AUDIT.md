# Renz-Fi Customer Lifecycle Forensic Audit

**Date:** 2026-08-11  
**Mode:** READ-ONLY forensic investigation — **NO CODE CHANGES**  
**Baseline preserved:** `docs/RENZFI_GURU_MEDITATION_PREVENTION_BASELINE.md`

---

## Build / ELF identity

| Item | Value |
|---|---|
| Workspace `firmware.elf` SHA256 | `5168048ACD61FFFEA3F5251E913A2D401C8BCB680493B967BB15BF364CC81F1F` |
| Path | `ESP32_S3_Firmware/.pio/build/freenove_esp32_s3_wroom/firmware.elf` |
| Match to hardware test binary | **NOT PROVEN** — runtime log did not include ELF SHA; previous crash used `8f93b74f9`. Do not assume this workspace ELF is the flashed unit without serial `ELF file SHA256` confirmation. |

---

## Executive summary

Seven customer-lifecycle observations were investigated independently. Two issues have **proven** firmware root causes from source + log agreement (sales aggregation vs uptime markers; Active Users heartbeat filter vs MikroTik active). Coin UI delay and Android “!” behavior have **strong architectural explanations** but lack millisecond-correlated UI timestamps, so they are **LIKELY / NOT YET FULLY PROVEN**. Portal re-entry and expiry UX are largely **architecture + Android captive-validation behavior**, not a single ESP32 “disconnect bug.”

TWDT baseline classes are **not** implicated as the cause of these functional UX issues by the supplied activation log (activation completed, RouterOS authorize ok).

---

## Issue 1 — Coin credit / time reflection delay

### Observed

Physical coin → noticeable delay before Insert Money modal shows credit / purchased time. Firmware log **does** show detection and attribution.

### Proven path (source)

```text
GPIO ISR (CoinManager)
 → loop finalize after settleMs (default 200 ms)
 → CoinManager::processCoin
 → PortalSessionManager::onCoinInserted
    → PromoManager::resolveForAmount → PromoManager::list → StorageManager::readJson(PROMOS_FILE)
    → RAM session credits/purchasedMinutes update
    → enqueueWork(EmitSessionEvent, "portal.coin.credit")
    → enqueueEmitBus("sessions.changed")
    → enqueueSaveSessions()
 → PortalSessionManager::loop → processDeferredWork (ONE item per loop)
    → emitSessionEvent → EventBus::emit("portal.coin.credit", session JSON)
 → portal renzfi-app.js:
    SSE handleSessionPush OR coin poll every COIN_POLL_MS=2000
```

### Timing model (architecture; hardware T0–T6 not measured in attached log)

| Stage | Owner | Expected order | Bound from source |
|---|---|---|---|
| T0 pulse | Hardware/ISR | — | Debounce 100 ms |
| T1 group finalize | CoinManager loop | after quiet | **settleMs = 200 ms** (`Config.h`) |
| T2 credit in RAM | `onCoinInserted` | after promo resolve | Includes **SD `readJson` promos** every coin |
| T3 SSE emit | deferred work queue | after prior queue items | **Not immediate**; waits for `processDeferredWork` |
| T4 HTTP poll sees credit | GET `/api/portal/session` | if SSE missed | Up to **2000 ms** (`COIN_POLL_MS`) |
| T5–T6 UI | browser | after payload | Render only |

### First incorrect state

**Not coin detection.** Firmware already has credits when Serial prints `[coin] mac=… sessionCredits=…` and `[INFO] portal: Credit +PHP …`.

First user-visible incorrect state is: **portal UI still shows old credit after firmware RAM credit is already updated.**

### Classification

| Claim | Status |
|---|---|
| Coin hardware detection broken | **NOT A ROOT CAUSE** (log proves pulses/peso/credit) |
| Firmware never updates session | **NOT A ROOT CAUSE** |
| Noticeable UI lag possible from settle + deferred SSE + 2 s poll fallback | **LIKELY ROOT CAUSE (composite)** |
| Promo `readJson` on every coin contributes | **POSSIBLE CONTRIBUTOR** (source: `PromoManager::resolveForAmount` → `list`) |
| Prior `SaveSessions` SD work ahead of `EmitSessionEvent` in work queue | **POSSIBLE CONTRIBUTOR** (single-item drain) |
| Exact bottleneck milliseconds (T1–T6) | **ROOT CAUSE NOT YET PROVEN** — need timestamped serial + browser network waterfalls |

### Watchdog regression check

Coin credit path does **not** run RouterOS. SSE emit is ESP32-local. Deferred `SaveSessions` is intentional durability off the HTTP path. Do not “fix” UI lag by moving SD save into async_tcp.

---

## Issue 2 — Portal says Connected; Android Wi-Fi still shows “!”

### Layer distinction (mandatory)

```text
Wi-Fi association
≠ Hotspot authorization
≠ Internet reachability
≠ Android network validation
≠ Renz-Fi application session state
```

### Proven firmware sequence (from supplied log + source)

1. Done Paying → session `activating`, credits cleared, `secondsLeft=300`
2. `router_worker` `activate-hotspot-user` (~1933 ms, 4 RouterOS cmds)
3. `/ip/hotspot/user/add` + `/ip/hotspot/active/login`
4. `[portal-activate] … ok=yes` → session becomes Connected in portal API/SSE

### Android “!”

Android captive network validation (HTTP(S) probes to vendor endpoints) is **independent** of Renz-Fi “Connected” label. After Hotspot login, Android may continue showing limited connectivity until validation succeeds or a timeout/revalidation occurs.

### Classification

| Claim | Status |
|---|---|
| ESP32 falsely marks Connected without RouterOS auth | **NOT A ROOT CAUSE** for this log (authorize + ok=yes) |
| Internet unavailable while portal Connected | **NOT PROVEN** (user reports Internet works) |
| Android validation lag after successful auth | **LIKELY ROOT CAUSE** of icon delay |
| MikroTik still intercepting after auth | **ROOT CAUSE NOT YET PROVEN** (needs live `/ip/hotspot/active` + client probe while “!” shown) |
| DNS still captive after auth | **ROOT CAUSE NOT YET PROVEN** |

### First incorrect state

Relative to user expectation (“Connected means phone icon clean”): **Android network-validation state**, not Renz-Fi session state, is the first mismatched layer when Internet already works.

---

## Issue 3 — How customer reopens captive portal while still connected

### Architecture (source-proven)

| Surface | Owner | Role |
|---|---|---|
| Customer UI (`login.html`, `renzfi-app.js`) | **MikroTik Hotspot HTML directory** | Served by router after Hotspot redirect |
| Portal API `/api/portal/*` | **ESP32** at `RENZFI_APPLIANCE_BASE_URL` (Ethernet IP, typically `http://10.10.10.2`) | Session/coin/activation |
| ESP32 `GET /login` | Admin SPA plane (`WebServerManager`) | **Admin dashboard login**, not customer captive portal |
| ESP32 `GET /portal` | Dev/recovery fallback only (`PortalServer.cpp`) | Not production customer entry |

Default guest gateway in firmware types: **`10.20.20.1`** (`RouterProvisioningTypes.h`). Field log shows client IP **`10.20.0.253`**, so this unit’s adopted guest subnet is **`10.20.0.0/24`**, not the compile-time default `10.20.20.x`.

### Answer: Is `10.20.0.1/login` correct?

**Conditionally — for this appliance’s adopted guest gateway, YES as the Hotspot login host path; not proven as a universal constant.**

- If MikroTik Hotspot gateway/DNS is `10.20.0.1`, then `http://10.20.0.1/login` (or Hotspot’s configured login page) is the customer portal entry while captive.
- It is **not** the ESP32 Admin `/login`.
- After authorization, Hotspot typically **stops forcing** redirect; manual reopen usually requires typing the Hotspot login URL (or bookmark). Whether that URL remains reachable post-auth depends on Hotspot/walled-garden config — **not fully proven from this session’s live RouterOS print**.

### Classification

| Claim | Status |
|---|---|
| Customer portal is ESP32 `/login` | **NOT A ROOT CAUSE / FALSE** |
| Customer portal is MikroTik Hotspot login page | **PROVEN** (PortalServer + Finish portal deploy comments) |
| Exact reopen URL for this unit | **LIKELY `http://<guestGateway>/login`** with guestGateway from provisioning (`10.20.0.1` on this unit) — confirm with live Hotspot profile |

---

## Issue 4 — Session expires; phone Wi-Fi still “Connected”; no auto login prompt

### Proven expiration path (source)

```text
PortalSessionManager::tickSessions (1 Hz)
 → secondsLeft reaches 0
 → state Expired + routerCleanupQueued
 → PortalWorkType::ExpireSession
 → onSessionExpired
 → RouterProvisioningWorker::tryEnqueueDeauthorizeHotspotUser
 → MikroTikDriver hotspot active remove / deauthorize
```

Wi-Fi **association** is not torn down by removing Hotspot Active. That is correct Layer-2 vs Layer-3 separation.

### Android “Login required” banner

Many Android versions primarily re-run captive detection on **association / network change**, not continuously while associated. After deauthorize, traffic fails but icon may stay “connected” until Wi-Fi toggle forces revalidation.

### Desired UX vs architecture

Desired: expire → Internet blocked → stay associated → captive portal easily available without Wi-Fi toggle.

**Currently:** Internet block via Hotspot deauth is implemented. Automatic Android captive prompt **is not guaranteed** by ESP32/MikroTik alone.

### Classification

| Claim | Status |
|---|---|
| Expiration fails to remove MikroTik auth | **NOT PROVEN** for this report (needs paired log); source path exists |
| Wi-Fi association remaining is a firmware bug | **NOT A ROOT CAUSE** (expected) |
| Missing Android login prompt after expiry | **LIKELY Android captive-validation behavior** + possible Hotspot post-auth redirect limits |
| Manual Wi-Fi toggle required | **LIKELY client revalidation requirement** — **ROOT CAUSE NOT YET PROVEN** as sole layer |

### First incorrect state

User expectation “show Login required automatically” fails at **Android captive revalidation**, after (expected) Wi-Fi association remains and (intended) Hotspot authorization is removed.

---

## Issue 5 — Sales report Today/Week = 0 while COIN records visible

### Proven evidence

Log:

```text
[WARN] portal: donePaying: wall clock not ready — sale uses uptime marker
[portal] wall clock unavailable — using uptime sale marker
```

And earlier heartbeat sales line pattern in product history: `today=0 week=0 month=0`.

### Proven sales write path

```text
donePaying
 → recordedAt = salesRecordedAtNow() OR "uptime-ms:" + millis()
 → enqueueRecordSale(...)
 → processDeferredWork RecordSale
 → SessionManager::upsertSale
 → sales JSON: recorded_at = recordedAt; timestamp often also uptime-ms
```

### Proven aggregation path

```text
SessionManager::salesToday / salesWeek / salesMonth / salesHistory / charts
 → filter via salesIsToday / salesIsThisWeek / salesIsThisMonth / salesParseRecordedAt
 → salesParseRecordedAt requires "YYYY-MM-DD..." form
 → "uptime-ms:205930" FAILS parse → sale skipped
```

`/api/sales/records` returns raw rows **without** date filter → UI lower table can show COIN rows with `uptime-ms:…` while Totals stay 0.

### Wall clock readiness (`SalesTime.cpp`)

`salesRecordedAtNow()` returns empty unless:

1. installation `isReady()`, and  
2. NTP/`getLocalTime` yields year ≥ 2024.

NTP is started only after setup complete (`salesTimeBegin`). Early post-Finish / pre-NTP-sync Done Paying **will** record uptime markers.

### Classification

| Claim | Status |
|---|---|
| Sale never persisted | **NOT A ROOT CAUSE** (records table shows COIN rows) |
| Aggregation ignores non-ISO `recorded_at` | **PROVEN ROOT CAUSE** for zero Today/Week/History buckets |
| Wall-clock unavailable at Done Paying causes uptime `recorded_at` | **PROVEN** (log + `donePaying` source) |
| Frontend alone invents zeros | **NOT A ROOT CAUSE** (API aggregates to 0) |

### First incorrect state

Sale record exists with `recorded_at="uptime-ms:…"`. Aggregation treats it as **undated** → Today/Week/Month = 0.

### When sales are recorded (current design)

Recorded at **Done Paying** (queued), status `pending_activation`, not at coin insertion and not only at session completion.

---

## Issue 6 — Sales Reports lower table content (design forensic only)

### What the table is

Frontend `SalesReportsPage` second table binds `salesApi.records()` → `/api/sales/records`.

That endpoint returns **persisted sale/session commercial records** (amount, MAC, IP, minutes, status, `recorded_at`), **not** live Active Users and **not** MikroTik Hotspot Active.

### Design finding (no UI change)

| Page | Should own |
|---|---|
| Sales Reports | Date-bucket revenue, ISO-timestamped transactions, financial export |
| Active Users | Live MAC/IP/state/remaining/pause |

Lower Sales table is **mixed commercial history**, currently polluted by **uptime markers** that look like session diagnostics. Separation is a **product/UI design recommendation**, not a runtime bug by itself.

---

## Issue 7 — Active Users shows 0 while MikroTik Hotspot Active has client

### Authoritative sources (different)

| Source | Meaning |
|---|---|
| MikroTik `/ip/hotspot/active` | RouterOS authorized forwarding sessions |
| Renz-Fi `/api/users` | `PortalSessionManager::appendActiveUsers` ∪ legacy `SessionManager::appendActiveUsers` |

### Proven filter (`isPortalSessionActive`)

Active rows require:

- not Idle/Expired
- for `Active`: `(secondsLeft > 0 || paused) && heartbeatFresh`
- `heartbeatFresh`: `lastSeen` within **`PORTAL_HEARTBEAT_STALE_SEC = 120`**

Portal heartbeat posts every **10 s** only while captive portal JS runs.

If customer closes portal after Connected, heartbeats stop → after ≤120 s Renz-Fi drops the Active Users row **even if MikroTik Active remains**.

### Classification

| Claim | Status |
|---|---|
| Active Users = MikroTik Hotspot Active | **FALSE** (by design) |
| Heartbeat staleness can zero Active Users while Hotspot Active remains | **PROVEN** (source) |
| This specific hardware “0 while MikroTik shows user” instance | **LIKELY** this filter — confirm with simultaneous `/api/users` JSON vs `/ip/hotspot/active/print` |

### First incorrect state

Divergence first appears at **ESP32 Active Users filter** (`heartbeatFresh`), not necessarily at RouterOS.

### Data lineage

```text
MikroTik /ip/hotspot/active
  (not continuously mirrored into /api/users)

PortalSessionManager RAM sessions
 → appendActiveUsers (heartbeat filter)
 → GET /api/users
 → useActiveUsers (SSE users.active / sessions.changed; else 30 s poll)
 → ActiveUsersPage
```

---

## Ethernet link flap (separate)

Log shows ETH disconnect/reconnect and speed renegotiation (10↔100).  

**ROOT CAUSE NOT YET PROVEN** (cable/switch/PHY/W5500/test harness). Do not merge with portal/session sales issues unless timing correlation is proven.

---

## DMA minimum ~928

Observed as diagnostic minimum during Router activation. Heap remained large (~8 MB).  

**NOT proven as leak or as cause of listed UX issues.** Residual risk only.

---

## Watchdog / TWDT regression analysis (Part 6)

| Path | Sync RouterOS on async_tcp? | Sync heavy FS on async_tcp? |
|---|---|---|
| `/api/portal/done-paying` | No — enqueue worker | Sale/session save deferred |
| `/api/portal/session` | No | RAM snapshot |
| `/api/portal/heartbeat` | No | Light |
| `/api/portal/start-coin-session` | No | Deferred save |
| Activation | Worker | — |
| `/api/storage/status` | No | Cached fill |
| Coin credit | loopTask | Promo readJson + deferred save (not HTTP) |

No evidence these customer issues require weakening TWDT baseline.

---

## State machines (condensed)

### A. Coin lifecycle

```text
IDLE → start-coin-session → WaitingCoin/window
 → pulse settle → onCoinInserted (RAM+queue SSE)
 → Done Paying → Activating → worker authorize → Active/Connected
 → tick to 0 → Expired → deauthorize Hotspot Active
 → Wi-Fi associated may remain → portal reopen via Hotspot login host
```

### B. Network layers

```text
Associated → captive redirect → pay/activate → Hotspot authorized
 → Internet reachable → (later) Android validated
Expire: authorized removed → Internet blocked → associated remains
 → Android may not auto-prompt until revalidation
```

### C. Sales

```text
Done Paying → recordedAt (ISO or uptime-ms) → upsertSale
 → records API shows row
 → today/week/month/history require parseable ISO date → else 0
```

### D. Active Users

```text
Portal session + fresh heartbeat → /api/users
MikroTik active alone → not sufficient for /api/users
```

---

## Summary table

| Issue | First Incorrect State | Root Cause | Layer | Confidence | Evidence |
|---|---|---|---|---|---|
| Coin delay | UI credit lag after firmware credit | Deferred SSE + 2 s poll (+ settle/promo SD) | ESP32 queue + portal frontend | LIKELY | Source paths; log proves detection; no UI T-delta |
| Android “!” | Phone validation still limited while Internet works | Android captive validation lag | Android (+ maybe Hotspot DNS) | LIKELY | Log authorize ok; user Internet OK |
| Portal re-entry | User tries wrong host (`ESP /login`) | Portal is MikroTik Hotspot login; guest GW unit-specific | Architecture | PROVEN for ownership; URL host LIKELY | PortalServer + defaults + client IP |
| Expiration UX | Expect auto “Login required” | Association remains; Android may not revalidate | Android + Hotspot model | LIKELY | Source deauth path; L2≠L3 |
| Sales totals 0 | `recorded_at=uptime-ms:…` excluded | Aggregation requires ISO date | ESP32 SalesTime/SessionManager | **PROVEN** | Log warning + `salesParseRecordedAt` |
| Sales table design | Session-ish columns on Sales page | `/api/sales/records` mixed commercial history | Frontend product design | PROVEN as data meaning | SalesReportsPage |
| Active Users 0 | Filtered out by heartbeat freshness | `/api/users` ≠ Hotspot Active | ESP32 portal session filter | **PROVEN** mechanism | `isPortalSessionActive` + 120 s |
| Ethernet flap | Link down events | Unknown PHY/cable/switch | Ethernet/W5500 | NOT YET PROVEN | Log only |

---

## Confirmed root causes

1. **Sales Today/Week/Month/History = 0** when `recorded_at` is `uptime-ms:…` because parsers require `YYYY-MM-DD` (`SalesTime` / `SessionManager` aggregation). Triggered when wall clock/NTP not ready at Done Paying.
2. **Active Users can show 0 while MikroTik Hotspot Active remains** because Active Users is portal-session + heartbeat-freshness based, not a live Hotspot Active mirror.

## Unproven hypotheses

- Exact coin UI bottleneck milliseconds (SSE miss vs queue backlog vs promo SD).
- Whether Android “!” is prolonged by DNS/walled-garden after auth.
- Whether expiry always removes Hotspot Active on this unit (needs paired capture).
- Ethernet flap physical cause.
- Workspace ELF `5168048A…` equals the hardware under test.

## Not root causes

- Coin pulse detection failure (for the cited log).
- Claiming ESP32 Admin `/login` is the customer captive portal.
- Treating remaining Wi-Fi association after expiry as automatic firmware defect.
- Blaming TWDT baseline fixes for these UX symptoms without evidence.

## Design-only recommendations (DO NOT IMPLEMENT IN THIS PASS)

1. **Sales:** Ensure ISO `recorded_at` once NTP ready; or include/repair uptime-marked sales into buckets with explicit policy; surface “clock not ready” on Sales UI.
2. **Coin UI:** Prefer immediate emit of credit SSE (or prioritize EmitSessionEvent ahead of SaveSessions); keep poll as fallback; avoid sync promo SD on hot path if proven hot.
3. **Active Users:** Define product source of truth (portal entitlement vs Hotspot Active) and label UI accordingly; optional Hotspot Active merge.
4. **Portal reopen:** Document unit guest gateway login URL; do not advertise ESP `/login` to customers.
5. **Expiry UX:** Document Android limitation; evaluate Hotspot/advertised captive methods only after product approval — no speculative RouterOS command increase.

## Regression risks if fixing carelessly

- Moving sale/credit persistence onto `async_tcp` → TWDT Class 1/2 regression.
- Polling Hotspot Active aggressively → MikroTik CPU / RouterOS storm.
- Disabling heartbeat filter without replacement → stale Active Users forever.
- Forcing Android validation via captive tricks → may break Hotspot security model.

## Hardware validation requirements (next gate)

1. Confirm running ELF SHA on serial.
2. Coin: serial millis at pulse / credit log / SSE send; browser Network for EventSource + `/session` timings.
3. Sales: dump one `/api/sales/records` row `recorded_at`; confirm NTP `salesTimeReady` after Finish.
4. Active Users: simultaneous Hotspot Active print vs `/api/users` with portal open vs closed.
5. Expiry: Hotspot Active removal proof + Android behavior without Wi-Fi toggle.
6. Re-entry: live Hotspot profile gateway + `http://<gw>/login` while authorized.

---

## Production gate

**NO CODE CHANGES MADE:** YES  

**PRODUCTION READINESS:** NOT READY FOR FIX IMPLEMENTATION YET  

**Next gate:** Review forensic findings and approve which proven causes to fix first (recommended order: Sales wall-clock/`recorded_at`, then Active Users source-of-truth UX, then measured coin SSE latency).
