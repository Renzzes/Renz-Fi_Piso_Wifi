# Renz-Fi Session Clock Remediation — 2026-08-15

**Status:** Source remediation complete. **READY FOR HARDWARE VALIDATION.**  
**Do not claim hardware/MikroTik fixed until the hardware procedure is run.**

**Baseline:** `docs/RENZFI_SESSION_CLOCK_FORENSIC_2026-08-15.md`

---

## 1. Root cause

MikroTik Session Time Left and the portal countdown were **two clocks**.

RouterOS Active remaining is:

```text
session-time-left ≈ limit-uptime − uptime
```

Firmware wrote leftover/live Active as:

```text
active/set limit-uptime = ESP32 timeoutSeconds   // “from now”
```

If Active already had ~4 minutes of uptime and the write was `00:05:00`:

```text
MikroTik remaining ≈ 60s
ESP32 / portal     ≈ 300s
```

That is why WinBox could show ~1 minute while the portal showed ~4 minutes.

Secondary: ESP32 reserved `secondsLeft` at `donePaying` and only started decrementing at Connected commit. The browser could `trustFully` re-anchor `Date.now() + secondsLeft`, reconstructing the original purchase after RouterOS had already been burning.

Android “Login or authentication required” is **not** part of this clock.

---

## 2. Exact timeline that caused the discrepancy

```text
T2  donePaying          secondsLeft = 300, connected = 0, timer frozen
    leftover or live Active may already have uptime U ≈ several minutes
T8  active/set          limit-uptime = 300
    Clock B remaining = 300 − U     → tens of seconds when U ≈ 4 min
T10 outcome commit      connected = 1, secondsLeft still 300
T12 browser             deadline = now + 300
```

Offset = Active uptime at the `active/set` write, not a constant grace.

---

## 3. Files changed

| File | Role |
|------|------|
| `docs/RENZFI_SESSION_CLOCK_FORENSIC_2026-08-15.md` | Causal proof (written first) |
| `ESP32_S3_Firmware/src/Models.h` | `ActivateAuthTrace` |
| `ESP32_S3_Firmware/src/router/IRouterDriver.h` | `lastActivateAuthTrace` |
| `ESP32_S3_Firmware/src/router/drivers/MikroTikDriver.h/.cpp` | Active Model B + snapshot |
| `ESP32_S3_Firmware/src/router/RouterPlatform.h/.cpp` | Trace facade |
| `ESP32_S3_Firmware/src/RouterProvisioningWorker.h/.cpp` | Outcome timestamps / ROS snapshot |
| `ESP32_S3_Firmware/src/PortalSessionManager.cpp` | Absolute millis expiry, pause freeze, owner reset generation |
| `ESP32_S3_Firmware/src/ActivationLatencyTrace.h` | Named authorization timeline |
| `portal/renzfi-app.js` | Presentation-only monotonic clock |
| `ESP32_S3_Firmware/tools/session-clock-sync-contract-check.mjs` | 21 static contracts |

Unchanged: coin rates, wizard, W5500/TWDT, RouterWorker task count, user Model B, voucher `serviceExpiresAt`.

---

## 4. Functions changed

| Function | Change |
|----------|--------|
| `MikroTikDriver::loginHotspotActive` | `new_active_limit = active.uptime + requested_seconds` |
| `MikroTikDriver::createHotspotUser` | Fills `ActivateAuthTrace` |
| `MikroTikDriver::lastActivateAuthTrace` | New |
| `RouterPlatform::lastActivateAuthTrace` | New |
| `RouterProvisioningWorker::publishHotspotOutcome` | Copies auth trace |
| `commitAuthorizedClockUnlocked` | `expiresAtMs = authorizedAtMs + grantedSeconds * 1000` |
| `freezeSessionClockUnlocked` | Pause / auth-loss |
| `PortalSessionManager::donePaying` | Clears expiry until authorization |
| `PortalSessionManager::pause` / `reset` | Freeze / clear clock; owner Deauth tagged with generation |
| `drainHotspotOutcomes` | Commits clock from T8; `[session-clock]` serial |
| `tickSessions` / `enrichSessionCapabilities` | Derive remaining from `expiresAtMs` |
| `applySessionClock` (portal) | GET/SSE cannot increase remaining |

---

## 5. New authoritative clock model (coin)

```text
authorizedAtMs + grantedSeconds = expiresAtMs
remainingSeconds = max(0, (expiresAtMs − millis()) / 1000)
```

- `authorizedAtMs` = RouterOS Active login/set + verify success (`millis()` at T8)
- `grantedSeconds` = purchased entitlement from that instant
- Timer runs only when `active && connected && !paused`
- Activating: `expiresAtMs = 0`, `timerRunning = false`

Voucher keeps `serviceExpiresAt` (wall clock). Clocks are not merged.

---

## 6. RouterOS authorization timestamp model

Captured in the worker on Activate success and copied into `HotspotOutcome`:

- `authorizedAtMs`, `grantedSeconds`
- `existingUserUptime`, `existingUserLimit`, `newUserLimit`
- `activeUptime`, `activeSessionTimeLeft`
- `activeLoginSuccess`, `activeVerifySuccess`, `usedActiveSet`

`[session-clock]` prints `routerAuthorizationToPortalCommitMs`.

User Model B is unchanged: `new_limit = existing_uptime + requested`.

---

## 7. Browser timer model

Browser is presentation only.

- Prefers `expiresAtMs − serverNowMs` when both are present
- Adopts a later deadline only when `sessionGeneration` or `grantedSeconds` increases
- `waitForActivation` no longer `trustFully`-rebases the running clock
- `/status` re-entry cannot reconstruct the original purchase against a running timer

---

## 8. Session-generation interaction

Unchanged match rule: MAC **and** generation.

Owner `reset()` now tags ExpireSession with `resetGen` (same as customer terminate).

Old Activate/Deauth/Pause/Verify outcomes still no-op on mismatch.

---

## 9. Mid-session RouterOS-loss behavior

Unchanged coalesced Verify (≤1 / 60 s, Connected only, one worker).

- Transport/query failure: entitlement and Connected preserved
- Confirmed `not_active`: `activation_error`, clock frozen, time preserved

No 1-second poll. No heartbeat → RouterOS.

---

## 10. Idle RouterOS workload

`needsRouterOsWork() == false` → zero Activate/Verify/Deauth/probe.

GET `/session` and heartbeat still do not call RouterOS.

---

## 11. CPU protection

No extra FreeRTOS task. No extra idle API. Verify remains coalesced. Health probe still skipped when a paid Activate can prove readiness.

---

## 12. ESP32 watchdog protection

No RouterOS from `async_tcp`, heartbeat, or coin ISR. No added `delay()` on Activate/Pause/Resume. Worker stack / W5500 / TWDT paths untouched.

---

## 13. Pause / resume / terminate / voucher

| Action | Clock | RouterOS |
|--------|-------|----------|
| Customer/owner pause | Freeze immediately (`expiresAtMs=0`) | Async Active+cookie remove, user kept |
| Customer/owner resume | New `authorizedAt` after Activate success | Same Activate path + Active Model B |
| Customer terminate | Clear clock, Deauth(gen) | Existing |
| Owner disconnect | Clear clock, Deauth(gen) — **now generation-tagged** | Existing |
| Voucher | Wall `serviceExpiresAt` unchanged | Unchanged |

No pause/resume dwell added.

---

## 14. Tests

- `tools/session-clock-sync-contract-check.mjs` (20 required cases + pause/resume)
- Existing session-sync, RouterOS stability, voucher, portal lifecycle
- `pio run -e freenove_esp32_s3_wroom`
- `RENZFI_APPLIANCE_BASE_URL=http://10.10.10.2 npm run build:mikrotik-portal`

---

## 15. Remaining risks

1. Hardware must confirm leftover-Active `activeUptime` vs portal remaining after this build.
2. If a very old RouterOS ignores `active/set limit-uptime`, user Model B still governs new `active/login`; leftover Active would need the next login path.
3. Coin expiry uses `millis()` (wrap ~49 days). Acceptable for kiosk uptime; voucher still uses NTP wall clock.
4. T8→T10 is now absorbed into remaining (`expiresAt` uses T8). A multi-minute leftover Active offset should disappear because Active limit is Model B.

**Not claimed fixed on hardware.**

---

## 16. Hardware validation

See `docs/RENZFI_SESSION_CLOCK_HARDWARE_VALIDATION_2026-08-15.md`.
