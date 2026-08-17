# Renz-Fi — MikroTik RouterOS Stability Remediation

**Date:** 2026-08-15  
**Scope:** Firmware-only RouterOS workload / health FSM stabilization  
**Source of truth (problems):** `docs/RENZFI_MIKROTIK_ROUTEROS_STABILITY_FORENSIC_2026-08-15.md`  
**Status:** Source remediation complete. **Hardware validation not performed.** Do not claim MikroTik/hardware “stable” until bench validation below passes.

---

## 1. Original proven problems (forensic)

| # | Proven issue |
|---|--------------|
| 1 | VerifyActive = full connect/login + `active/print`, priority **Critical**, ~every 60s while Connected |
| 2 | Failed login can monopolize the single RouterWorker (~`ROUTEROS_IO_TIMEOUT_MS` / 8000 ms) |
| 3 | `activation_error` auto-retry continued on a fixed schedule **independent of RouterOS health** |
| 4 | No global RouterOS HEALTHY/UNAVAILABLE FSM; no recovery dwell/drain |
| 5 | Idle systems could still schedule Verify when any session was Connected; no “zero Connected ⇒ zero Verify” hard rule at health layer |
| 6 | Outage increased retry pressure instead of reducing RouterOS work |

**Not redesigned in this pass:** Model B coin uptime, voucher absolute expiry, portal UI, W5500/TWDT, RouterOS protocol, persistent API sessions.

---

## 2. Exact source files changed

| File | Change |
|------|--------|
| `ESP32_S3_Firmware/src/Config.h` | Health FSM + activate trust-window constants |
| `ESP32_S3_Firmware/src/RouterApiTransportGate.h` | Health enum + tick/probe/allow gates |
| `ESP32_S3_Firmware/src/RouterApiTransportGate.cpp` | Non-blocking FSM; job success/failure → health; transition logs |
| `ESP32_S3_Firmware/src/RouterProvisioningWorker.h` | `HealthProbe` op + `tryEnqueueHealthProbe()` |
| `ESP32_S3_Firmware/src/RouterProvisioningWorker.cpp` | Health gates on activate/verify/deauth/pause/admin; Verify **Normal**; HealthProbe job |
| `ESP32_S3_Firmware/src/PortalSessionManager.h` | Trust-window fields; `needsRouterOsWork()` |
| `ESP32_S3_Firmware/src/PortalSessionManager.cpp` | `tickHealth`; probe only if work needed; idle log; verify/activate defer; drain priority |
| `ESP32_S3_Firmware/src/router/IRouterDriver.h` | `probeApiReady()` default |
| `ESP32_S3_Firmware/src/router/RouterPlatform.h/.cpp` | Forward `probeApiReady()` |
| `ESP32_S3_Firmware/src/router/drivers/MikroTikDriver.h/.cpp` | Minimal identity/print probe |
| `ESP32_S3_Firmware/tools/routeros-stability-contract-check.mjs` | **New** static contracts |
| `docs/RENZFI_MIKROTIK_ROUTEROS_STABILITY_REMEDIATION_2026-08-15.md` | This document |

---

## 3. Exact health FSM

Owned by `RouterApiTransportGate` (no second worker, non-blocking).

```
UNKNOWN ──(job ok)──► HEALTHY
CONNECTING ──(job ok)──► HEALTHY

HEALTHY ──(job fail)──► DEGRADED
DEGRADED ──(fail count ≥ 2)──► UNAVAILABLE
DEGRADED ──(backoff elapsed + needs ROS work)──► desire PROBE

UNAVAILABLE ──(backoff window)──► COOLDOWN
COOLDOWN ──(elapsed + needs ROS work)──► PROBING (HealthProbe job)
PROBING ──(ok)──► RECOVERING (dwell ROUTER_HEALTH_RECOVERY_DWELL_MS = 15s)
PROBING ──(fail)──► DEGRADED / UNAVAILABLE
RECOVERING ──(dwell complete)──► HEALTHY
RECOVERING ──(job fail)──► DEGRADED / UNAVAILABLE
```

### Gate semantics

| Work | Allowed when |
|------|----------------|
| Activate | `HEALTHY` or `UNKNOWN` |
| VerifyActive | `HEALTHY` only |
| Deauth / Pause | `HEALTHY`, `RECOVERING`, `DEGRADED`, `UNKNOWN` |
| Admin non-essential | `HEALTHY` or `UNKNOWN` |
| HealthProbe | Only if `wantsHealthProbe()` **and** `needsRouterOsWork()` |

**Idle + no pending ROS work:** no health probe logins (MikroTik power-off while idle does not create a probe loop).

---

## 4. Idle behavior

When `needsRouterOsWork()` is false:

- No VerifyActive enqueue  
- No HealthProbe  
- No activate/admin ROS jobs  
- One-shot log: `[router-worker] idle no-router-work`  
- Local portal HTTP / heartbeat / coin / timers continue  

---

## 5. VerifyActive behavior (after)

1. Priority **Normal** (not Critical).  
2. Suppressed unless health == `HEALTHY`.  
3. Suppressed when zero Connected Active sessions.  
4. Coalesced: one MAC / ~60s; worker single-slot prevents concurrent duplicates.  
5. Post-activate **trust window** (`ROUTER_ACTIVATE_TRUST_WINDOW_MS` = 120s) skips Verify for that MAC.  
6. Still uses targeted `active/print` for one known MAC (no global HotSpot inventory scan).

---

## 6. Activation retry behavior (after)

- `activation_error` preserves `secondsLeft` / credits.  
- Auto-retry **does not** advance attempts or enqueue Activate while `!allowsHotspotActivate()`.  
- Worker enqueue also returns false with `activate deferred reason=router_unavailable` and sets `activationRetryPending`.  
- After HEALTHY, retries resume via existing schedule + `retryPendingRouterWork()`.

---

## 7. Reboot recovery / dwell / drain

1. Failures → DEGRADED → UNAVAILABLE → COOLDOWN.  
2. If (and only if) firmware still needs ROS work → lightweight `probeApiReady()` (`/system/identity/print`).  
3. Probe OK → RECOVERING → 15s dwell → HEALTHY.  
4. Drain order in `retryPendingRouterWork()`: **cleanup → activate → pause** (one job).  
5. During RECOVERING: activate blocked; deauth/cleanup allowed.

---

## 8. RouterOS API activity — before vs after

### Before (forensic)

| Mode | Behavior |
|------|----------|
| Idle | Ideally quiet, but no global health stop; outage + `activation_error` still scheduled Activate |
| Connected | Verify ~every 60s, **Critical**, full login each time |
| Outage | Failed login ~8s monopolizes worker; retries continue |
| Recovery | Immediate resume of pending jobs; no dwell |

### After (source)

| Mode | Behavior |
|------|----------|
| Idle (no ROS need) | **0** periodic API logins / active/print / health probes |
| Connected + HEALTHY | Verify only when due, Normal priority, trust window respected |
| Outage | Verify/activate suppressed; state preserved; probe only if work needed |
| Recovery | Probe → dwell → serial drain |

Activation command set (user/print/set/add, active/login, etc.) **unchanged** — command minimization deferred.

---

## 9. Concurrency guarantees

- Still **one** `RouterProvisioningWorker`.  
- Still **one** RouterOS API session via `RouterApiTransportGate::acquireSession()`.  
- HealthProbe uses the same worker queue (fire-and-forget, no second session).

---

## 10. Financial / session preservation

- Credits / `secondsLeft` / voucher `serviceExpiresAt` not cleared on ROS unavailability.  
- Pending flags (`activationRetryPending`, `cleanupRetryPending`, …) retained until healthy drain.  
- Model B and voucher absolute expiry untouched.

---

## 11. Test results (source)

| Test | Result |
|------|--------|
| `node ESP32_S3_Firmware/tools/routeros-stability-contract-check.mjs` | **12/12 PASS** |
| `node ESP32_S3_Firmware/tools/voucher-expiry-contract-check.mjs` | **12/12 PASS** |
| `npm run test:portal:lifecycle` | **30/30 PASS** |
| `pio run -e freenove_esp32_s3_wroom` (from `ESP32_S3_Firmware/`) | **SUCCESS** |

Not claimed: live RouterOS CPU %, Winbox Profile ownership, hardware reboot storm absence.

---

## 12. Remaining risks

1. First contact after long UNAVAILABLE + pending work still uses full activate command path (intentional).  
2. DEGRADED with no pending work stays quiet until next ROS need — first post-idle job may fail once before FSM engages.  
3. Probe uses a full login + identity/print (minimal, but not zero cost when recovering).  
4. Admin jobs deferred while unhealthy — operator must retry after HEALTHY.  
5. Hardware timing (dwell 15s, probe min 15s) may need tuning after bench.

---

## 13. Hardware validation requirements (separate)

Do **not** mark complete until:

1. Idle appliance, no clients: Serial shows `idle no-router-work`; **no** `[router-api]` / login spam for ≥10 minutes.  
2. Coin purchase → Connected once; Verify at most ~once/60s after trust window.  
3. Power-off MikroTik during Connected: Verify/activate stop; credits remain; no retry storm.  
4. Power-on MikroTik: `[ros-health] PROBING` → `RECOVERING` → `HEALTHY`; drain serial; no login storm.  
5. Pause/resume/terminate/voucher expiry still work.  
6. Portal session GET / heartbeat remain usable with ROS offline.  
7. TWDT / Ethernet remain healthy under outage.

**STOP:** Source validation only. No flash/upload/RouterOS config changes in this remediation.
