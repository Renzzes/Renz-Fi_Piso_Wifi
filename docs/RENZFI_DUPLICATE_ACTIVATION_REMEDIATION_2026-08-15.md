# Renz-Fi Duplicate Activation Remediation — 2026-08-15

**Baseline:** `docs/RENZFI_DUPLICATE_ACTIVATION_FORENSIC_2026-08-15.md`

**Status:** Source fix complete. **Not hardware-ready** until a single Done Paying produces one Activate job and no `active/set` TRAP.

---

## Fixes

1. `alreadyAuthorizedThisGeneration()` — Connected + `hadRouterAuth` + Active + no pending resume → skip Activate.
2. Applied in `onSessionActivated`, `enqueueActivateSession`, and `retryPendingRouterWork`.
3. Activate success clears `activationRetryPending`.
4. `donePaying`: if the worker is busy, one deferred `ActivateSession` is the only retry (idle retry is disarmed).
5. Tick/boot Activate items carry `sessionGeneration` (generation 0 no longer bypasses stale check).
6. Removed `/ip/hotspot/active/set limit-uptime`. Existing Active = already authorized. User Model B `user/set|add` remains the entitlement write.
7. `RouterOsClient` TRAP still returns false and is no longer discarded by the driver.

Add Time still runs one Activate job to apply user Model B; it does **not** `active/login` or `active/set` when Active already exists.

---

## Tests

`ESP32_S3_Firmware/tools/duplicate-activation-contract-check.mjs`

Existing session-clock / session-sync / stability / voucher / portal lifecycle contracts must stay green.
