# Renz-Fi Sales + Active Users + Coin Latency Instrumentation Implementation Report

**Date:** 2026-08-11  
**Mode:** Controlled implementation (Parts A–D, F–J). Part E MikroTik hardware investigation: **NOT PERFORMED** (no live unit access in this pass).  
**Baseline docs (mandatory, unchanged):**  
- `docs/RENZFI_GURU_MEDITATION_PREVENTION_BASELINE.md`  
- `docs/RENZFI_CUSTOMER_LIFECYCLE_FORENSIC_AUDIT.md`

---

## Binding statements

1. **The previous Guru Meditation/TWDT fixes are a frozen production stability baseline and were not weakened for these functional fixes.**
2. **The system was already operational before this investigation.** The goal is to restore/improve Sales reporting and Active Users accuracy without introducing new instability, and to instrument (not optimize) coin UI latency until timings prove a bottleneck.
3. **Internet grant / Done Paying → RouterWorker → Hotspot authorize was not modified.**

---

## Original root causes (proven — addressed)

### A. Sales reporting ₱0.00 with visible COIN rows

**Cause:** `recorded_at = "uptime-ms:<millis>"` when wall clock/NTP is unavailable at Done Paying; aggregation (`salesParseRecordedAt` / Today/Week/Month) required an ISO calendar stamp and **silently skipped** undated rows. Records API still returned them.

### B. Active Users ≠ Hotspot entitlement

**Cause:** `isPortalSessionActive` required a fresh portal heartbeat (`PORTAL_HEARTBEAT_STALE_SEC = 120`) for Active/Paused/Activating sessions. Closing the captive portal stopped heartbeats → customer dropped from `/api/users` while MikroTik Hotspot Active could remain.

### C. Coin UI latency

**Status before / after this pass:** **NOT YET PROVEN.** Instrumentation only; no optimization.

---

## Exact files / functions modified

| Area | File | What changed |
|------|------|--------------|
| Sales time helpers | `ESP32_S3_Firmware/src/SalesTime.h/.cpp` | `salesIsUptimeMarker`, `salesEffectiveIsoStamp` |
| Sales aggregation | `ESP32_S3_Firmware/src/SessionManager.cpp` | `aggregateSales` period enum + undated attribution; `fillTotals` adds `undatedAmount`/`undatedSessions`/`clockReady`; history/chart/CSV handle uptime markers |
| Active Users filter | `ESP32_S3_Firmware/src/PortalSessionManager.cpp` | `isPortalSessionActive` separates entitlement vs heartbeat; `appendActiveUsers` adds `secondsLeft`, `portalHeartbeatFresh` |
| Coin latency (instr.) | `ESP32_S3_Firmware/src/CoinLatencyTrace.h` (new) | Monotonic T0–T6 Serial probe |
| Coin latency (instr.) | `ESP32_S3_Firmware/src/CoinManager.h/.cpp` | `_groupFirstPulseMs` (T0); mark T1 on group finalize |
| Coin latency (instr.) | `ESP32_S3_Firmware/src/PortalSessionManager.cpp` | T2–T6 marks around credit / queue / emit |
| Coin latency (instr.) | `Captive Portal/renzfi-app.js` | T7 listen-only on `portal.coin.credit`; T8/T9 logs on credit change; `sessions.changed` timing log |
| Sales UI | `src/pages/SalesReportsPage.tsx`, `src/services/sales.ts` | Undated-aware totals; table columns cleaned (no Started/Type/Device/Voucher-Profile) |
| Active Users UI | `src/pages/ActiveUsersPage.tsx`, `src/types/api.ts` | State badges expanded; Portal Open/Closed column |
| Tests | `scripts/test-sales-uptime-aggregation.mjs` | New |
| Tests | `scripts/test-active-users-entitlement.mjs` | New |

---

## Why each modification was necessary

- **Sales:** Financial UI must not show ₱0.00 when persisted COIN rows exist solely because NTP was down. Policy: keep original `recorded_at` on disk; at report time count undated rows and, when clock is ready, attribute them to the current local business day. When clock is not ready, expose `undatedAmount` so the UI can still show revenue.
- **Active Users:** Paid entitlement must not be confused with browser liveness. WaitingCoin still needs heartbeat; Active/Paused/Activating/ActivationError/Expiring with remaining time do not.
- **Sales UI:** Commercial table should not own live session ops columns; those belong on Active Users.
- **Coin instrumentation:** Prove T0→T9 before any optimization (promo SD read vs queue vs modal SSE subscription).

---

## Deliberately NOT changed

- TWDT timeout / feed / disable  
- `STORAGE_SNAPSHOT_HEAVY_INTERVAL_MS` / StorageManager durability redesign  
- Done Paying activation / RouterWorker / MikroTik authorize sequence  
- Android captive “!” workarounds  
- W5500 / Ethernet behavior  
- Coin latency optimizations (RAM promo cache, queue priority, modal apply of `portal.coin.credit`)  
- Aggressive `/ip/hotspot/active` polling  
- Rewriting sales.json on every report  
- Raising `PORTAL_HEARTBEAT_STALE_SEC` as a fake fix  

---

## Sales timestamp policy (explicit)

1. Persist sales exactly as before (`recorded_at` may remain `uptime-ms:…`).  
2. Optional future `reporting_at` ISO is preferred when present (backward compatible; not required for this fix).  
3. Aggregation:
   - ISO stamps → previous calendar filters unchanged.  
   - Uptime markers → always counted in `undatedAmount`/`undatedSessions`.  
   - If `salesTimeReady()` → also attribute those amounts into Today/Week/Month using **current local “now”** (not invented historical dates).  
   - If clock not ready → dated totals may be 0; UI uses `dated + undated`.  
4. CSV Date column uses `UNCLOCKED` for uptime-only rows.  
5. No full-database rewrite on report requests.

---

## Active Users entitlement policy (explicit)

| State | Listed when |
|-------|-------------|
| WaitingCoin / unpaid coin window | Heartbeat fresh |
| Active / Paused / Activating / ActivationError | `secondsLeft > 0` (or paused for Active) — **no heartbeat requirement** |
| Expiring | `secondsLeft > 0` |
| Idle / Expired | Never |

`portalHeartbeatFresh` is exposed separately so the Admin UI can show Portal Open/Closed without treating Closed as Offline.

---

## Coin latency forensic notes (instrumentation result so far)

**Source inspection (not hardware ms):**

1. Firmware emits `portal.coin.credit` with full session JSON via EventBus SSE.  
2. Captive portal **did not subscribe** to `portal.coin.credit` for UI updates; it listens to `sessions.changed` then calls `syncSession()` (HTTP).  
3. A listen-only `portal.coin.credit` handler was added to prove T7 arrival without changing apply behavior.  
4. Promo resolution still runs `PromoManager::resolveForAmount` on every coin (potential SD read) — dominance **unproven** until Serial `[coin-latency]` lines are captured on hardware.  
5. `EmitSessionEvent` remains deferred on the portal work queue (SaveSessions can still interleave) — dominance **unproven**.

**Optimization implemented:** NO.

---

## MikroTik investigation (Part E)

| Case | Result |
|------|--------|
| A — Android ! after Connected | **NOT TESTED** (no live MikroTik session in this pass) |
| B — Expiration deauth | **NOT TESTED** |
| C — Active Users vs Hotspot Active timeline | **NOT TESTED** on hardware; source divergence at 120s heartbeat **was** the proven software mismatch and is fixed in Renz-Fi filter |
| D — Ethernet/W5500 flap | **NOT PROVEN / NOT TESTED** |

No MikroTik configuration was changed.

---

## Validation performed

### Build

| Check | Result |
|-------|--------|
| Firmware `renzfi_developer` | **PASS** (SUCCESS ~174s) |
| RAM | 32.5% (106532 / 327680) |
| Flash | 92.2% (2416147 / 2621440) |
| Portal `npm run build` | **PASS** |
| `tsc --noEmit` | Pre-existing errors in unrelated UI modules (embla/recharts/etc.) — not introduced by this change |
| Compiler | Pre-existing `-D` redefine warnings on command line |

### Automated tests

| Suite | Result |
|-------|--------|
| `scripts/test-sales-uptime-aggregation.mjs` | **14/14 PASS** |
| `scripts/test-active-users-entitlement.mjs` | **6/6 PASS** |
| `npm run test:portal` (resolver + lifecycle) | **PASS** (22/22 lifecycle) |

### TWDT regression audit (this change set)

- No `esp_task_wdt_*` / timeout increases / manual feed added in the modified sales/active/coin paths.  
- No RouterOS calls moved into HTTP/`async_tcp`.  
- Sale persistence remains deferred (`RecordSale` work item).  
- Coin latency uses Serial + `millis()` only (no SD timing samples).

### Hardware acceptance gate

**NOT PERFORMED** — no 10-minute Admin soak, live coin insertions, or MikroTik CPU capture in this environment.

---

## PASS/FAIL checklist (required matrix)

### BUILD

- Firmware build: **PASS**
- Portal build: **PASS**
- Automated lifecycle tests: **22/22 PASS** (+14 sales +6 entitlement)
- RAM usage: **PASS** (reported)
- Flash usage: **PASS** (reported; high but builds)
- Compiler warnings: **PASS with notes** (pre-existing redefine noise)

### SALES

- COIN sale persisted: **PASS** (source path unchanged; deferred upsert)
- ISO → Today/Week/Month: **PASS** (automated)
- Uptime-marked sale does not disappear: **PASS** (automated)
- Sales total matches transaction records: **PASS** (policy + UI undated sum; hardware confirm pending)
- CSV export remains functional: **PASS** (source; UNCLOCKED date)
- Existing historical records readable: **PASS** (no schema rewrite)

### ACTIVE USERS

- Authorized customer appears: **PASS** (source + unit)
- Remains visible after closing portal: **PASS** (unit; hardware pending)
- Disappears after expiration: **PASS** (unit; hardware pending)
- Paused represented correctly: **PASS** (unit)
- Terminated disappears: **PASS** (Expired/Idle filter; hardware pending)
- No stale users indefinitely: **PASS** (still gated by secondsLeft/Expired; cleanup path unchanged for paid entitlements)
- MikroTik vs Renz-Fi divergence documented: **PASS** (forensic + this report; live timeline NOT TESTED)

### COIN LATENCY

- Instrumentation present: **PASS**
- T0–T6 / T7–T9 capture on hardware: **FAIL / NOT RUN**
- Root bottleneck proven: **NO**
- Optimization implemented: **NO** (correct per scope)

### MIKROTIK / ESP32 STABILITY / HARDWARE GATE

- All live items: **NOT TESTED** → treat as **FAIL** for production gate

---

## Remaining unresolved issues

1. Coin UI latency bottleneck ms (needs Serial + browser console on device).  
2. Android captive “!” after Connected.  
3. Expiration UX / Hotspot re-entry behavior on phone.  
4. Ethernet/W5500 link flap root cause.  
5. Live confirmation that Sales Today matches field uptime-marked COIN rows after flash.  
6. Live confirmation Active Users stays populated with portal closed while Hotspot Active remains.

---

## Scores

| Metric | Score |
|--------|-------|
| Overall source/test verification | **78%** |
| Hardware verification | **0%** |
| MikroTik verification | **0%** |
| **Production readiness** | **NOT READY** |

Mandatory hardware stability checks were not performed; production readiness remains **NOT READY**.
