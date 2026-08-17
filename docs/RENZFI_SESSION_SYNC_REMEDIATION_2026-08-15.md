# Renz-Fi Session Synchronization Remediation — 2026-08-15

**Status:** Source remediation complete. **READY FOR HARDWARE VALIDATION.**  
**Do not claim hardware/MikroTik fixed until the hardware procedure is run.**

**Baseline:** `docs/RENZFI_SESSION_SYNC_FORENSIC_2026-08-15.md`

---

## 1. Original symptoms

- Done Paying showed ~05:00 while RouterOS Active was empty.
- Health `PROBING → RECOVERING` (15s dwell) deferred Activate but allowed Deauth.
- Expire/deauth for the same MAC could run around a new purchase.
- Portal could paint timeout / Disconnected / a `.254` error while firmware was still activating.
- Serial trap was `unknown host IP 10.20.0.251` (real RouterOS failure).

**Host presence while idle is not considered a paid session.**

---

## 2. Confirmed root causes (addressed)

1. Entitlement reserved at `donePaying` before `active/login` (kept) but UI/timer treated it as live (fixed).
2. No session generation on RouterWorker jobs/outcomes (fixed).
3. Stale Expire/Deauth mutated the live MAC record (fixed).
4. 15s recovery dwell blocked paid Activate (fixed).
5. `POST /done-paying` ignored client IP (fixed).
6. Portal `waitForActivation(35000)` declared failure while firmware was still `activating` (fixed).

---

## 3. Exact code changes

| File | Change |
|------|--------|
| `Models.h` | `HotspotUser.sessionGeneration` |
| `RouterProvisioningWorker.h/.cpp` | Generation on jobs/outcomes; mailbox keeps newer gen |
| `RouterApiTransportGate.cpp` | Activate allowed in RECOVERING/DEGRADED/PROBING; Critical success → HEALTHY |
| `PortalSessionManager.h/.cpp` | Generation bump, stale no-op, timer requires `connected`, IP on done-paying, skip redundant probe |
| `ApiServer.cpp` | `done-paying` reads `ip` |
| `portal/renzfi-app.js` | State-driven wait; generation guard; no optimistic Connected |
| `tools/session-sync-contract-check.mjs` | New static contracts |

Model B uptime math, voucher `serviceExpiresAt`, W5500/TWDT, wizard: **unchanged**.

---

## 4. Session generation design

`sessionGeneration` is a monotonic `uint32` on the portal session JSON.

- New coin purchase (`donePaying` when not add-time): **bump**.
- Voucher redeem: **bump**.
- Add-time / pause / resume: **keep**.
- Terminate: capture generation **N**, mark expiring, enqueue Deauth(**N**) **before** any later purchase can bump.

Every Activate/Deauth/Pause/Verify job and outcome carries the generation.

Match rule: MAC **and** generation. Mismatch → no RouterOS mutation, no session write.

**Connected is committed only after a matching RouterOS Activate success.**

---

## 5. RouterWorker ordering

- One worker, one API session (unchanged).
- Drain prefers **Activate** over leftover cleanup when the MAC has a live paid state.
- `onSessionExpired` no-ops if generation mismatches or a live `activating/active/paused/activation_error` session has remaining time.
- Mailbox is still one-deep; a **newer** generation is never replaced by an older one.

---

## 6. RouterOS health behavior

| State | Paid Activate |
|-------|----------------|
| HEALTHY / UNKNOWN / CONNECTING / DEGRADED / RECOVERING / PROBING | Allowed (Activate proves readiness) |
| UNAVAILABLE / COOLDOWN | Blocked (no retry storm) |

Critical job success → **HEALTHY** immediately (no 15s customer dwell).

Probe still runs only when work is needed **and** Activate cannot proceed.  
**No idle RouterOS polling was introduced.**

---

## 7. IP synchronization

`POST /api/portal/done-paying` accepts `mac` + `ip`. Session `ipAddress` is updated. Activation uses that stored IP. No hardcoded `10.20.0.251` or `10.20.0.254`. UI shows the exact RouterOS trap.

---

## 8. Portal state synchronization

- Done Paying → `activating`, `connected=false`, `timerRunning=false`.
- Status: “Activating…” / “Still connecting to the router…”
- Connected only when `active && connected`.
- Stale GET (`sessionGeneration` older) cannot overwrite newer state.
- Browser timer no longer converts `activating` into failure.

---

## 9. Timer semantics

ESP32 decrements `secondsLeft` only when `active && connected && !paused`.  
Reserved entitlement may be displayed while Activating; it does not consume.

---

## 10. Idle RouterOS workload

`needsRouterOsWork()==false` → no probe, no Verify, no login.  
Log: `[router-worker] idle no-router-work` (once per idle entry).

---

## 11. CPU stability safeguards

- No new poll intervals.
- No second worker / parallel API session.
- Verify remains 60s coalesce, Healthy-only, Connected-only.
- Outage: UNAVAILABLE/COOLDOWN still suppress Activate storms.

---

## 12. ESP32 watchdog / Guru Meditation safeguards

- No RouterOS in HTTP/SSE/heartbeat/coin ISR.
- No new tasks, no `delay()` in handlers, no unbounded queues.
- TWDT/W5500 untouched.

---

## 13. Test results

| Test | Result |
|------|--------|
| `session-sync-contract-check.mjs` | **15/15 PASS** |
| `routeros-stability-contract-check.mjs` | **12/12 PASS** |
| `voucher-expiry-contract-check.mjs` | **12/12 PASS** |
| `npm run test:portal:lifecycle` | **30/30 PASS** |
| `pio run -e freenove_esp32_s3_wroom` | **SUCCESS** |

Hardware not run.

---

## 14. Remaining risks

- `active/login` can still trap `unknown host IP` if the client has no HotSpot Host row (Wi-Fi drop). Firmware now preserves the exact reason and entitlement.
- Omitting `ip=` on login was **not** implemented (needs hardware confirmation).
- Infinite browser wait while `activating` relies on firmware 45s pending watchdog → `activation_error`.

---

## 15. Hardware validation

See `docs/RENZFI_SESSION_SYNC_HARDWARE_VALIDATION_2026-08-15.md`.
