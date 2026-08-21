# Setup Lock Persistence Forensic Report

**Branch:** `feature/waveshare-esp32-s3-eth`  
**Mode:** Forensic + minimal fix  
**Date:** 2026-08-21

## Verdict

**PROVEN:** Setup Unlock Key (`setupUnlockPasswordHash` / `setupUnlockPasswordProtected`) is stored in `/config/provisioning.json` (SD primary) with SPIFFS `/fb/provisioning.json` checkpoint — **not** in NVS. Setup Lock itself is a RAM session gate derived from `requiresSetupUnlock() && !hasActiveSetupUnlockSession()`.

**PROVEN gap vs required product rule:** Appliance-bound Setup Key must survive SD erase/replace/absence as **internal primary**. Today SD/SPIFFS hold the secret; wiping both (or never having checkpointed) while NVS still has `firstBootDone` keeps Setup Locked but can **reset the unlock secret to factory default** `renzfi-setup`.

---

## A. Current sources (PROVEN)

| Artifact | Storage | Path / keys | Authoritative today |
|----------|---------|-------------|---------------------|
| Setup Lock (gate) | RAM | `_setupReentrySession`, expiry | Derived, not persisted |
| Setup Unlock Key | SD + `/fb` | `provisioning.json` fields | SD preferred via `readJson` |
| Owner login password | NVS `renz-auth` | `passwordHash`, `firstBootDone` | NVS |
| Installation lifecycle | SD + `/fb` | `/config/installation.json` | SD preferred |
| Owner metadata | SD + `/fb` | same provisioning file | SD preferred |

Key files: `SetupProvisioningManager.cpp` (`load`/`persist`/`requiresSetupUnlock`/`unlockSetup`), `SetupServer.cpp` (locked HTML + `/api/setup/unlock`), `AuthCredentials.cpp` (NVS auth only), `FactoryResetWorker.cpp` (full wipe).

## B. SD replacement / erase behavior

| Case | Behavior | Classification |
|------|----------|----------------|
| SD missing, `/fb/provisioning.json` present | Unlock key + lock predicate survive | **PROVEN** |
| SD blank, SPIFFS checkpoint present | Same | **INFERRED** (readJson fallthrough) |
| SD + SPIFFS both empty, NVS `firstBootDone` | `synchronizeAtBoot` repairs `ownerCreated`; Setup stays Locked; unlock hash may be factory default | **PROVEN** / **INFERRED** for secret loss |
| Factory reset (`FactoryResetWorker`) | Clears RAM, SD, `/fb`, NVS auth — unlock cleared | **PROVEN** |

## C. Factory reset semantics (PROVEN)

`FactoryResetWorker` is the full appliance wipe including Setup Unlock Key.  
`ProvisioningEngine::factoryReset` only resets installation wizard state — **doc discrepancy** if UI labels it as full factory reset.

## D. N16R8 / resilience contract comparison

`WAVESHARE_STORAGE_RESILIENCE_CONTRACT.md` correctly lists NVS for owner/operator credentials and SD+`/fb` for installation/provisioning. It does **not** place Setup Unlock Key in NVS. Product requirement in this investigation **extends** that contract for Setup Key only.

## E. Minimal fix (approved for implementation)

1. NVS-primary Setup Unlock Key in `renz-auth` (`unlockHash`, `unlockBlob`).
2. Load: NVS wins; migrate from SD once if NVS empty.
3. Persist: write NVS first, keep SD/`/fb` as non-authoritative mirror.
4. Factory reset: clear NVS unlock with `applyRecoveryReset` / quiesce.
5. Do not regenerate Setup Key on blank SD when NVS unlock present.

## F. Classification summary

| Claim | Status |
|-------|--------|
| Unlock key on SD not NVS | **PROVEN** |
| Lock is RAM session | **PROVEN** |
| Owner auth already NVS | **PROVEN** |
| SD wipe alone opens factory setup while NVS owner exists | **REJECTED** |
| Custom unlock survives total SD+SPIFFS wipe | **PROVEN** fails today |
| Installation Ready survives total wipe without `/fb` | **INFERRED** gap (repaired to OwnerCreated only) |
