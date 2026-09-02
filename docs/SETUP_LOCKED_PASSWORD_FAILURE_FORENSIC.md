# Setup Locked Password Failure — Forensic Report

**Date:** 2026-08-22  
**Mode:** Forensic only (no code change in this pass)  
**Context:** After factory-reset / SSE quiesce work, Setup AP at `192.168.4.1` shows **Setup Locked**; operator’s known password does not unlock.  
**UI evidence:** Locked card copy matches `SetupServer.cpp` PROGMEM page (`An owner account has already been created…` / `Unlock Setup`).

---

## 1. VERDICT

| Claim | Status |
|-------|--------|
| Screen is the Setup Unlock gate (not Admin Ethernet login) | **PROVEN** |
| Unlock secret ≠ Admin/owner login password (may differ) | **PROVEN** |
| Factory default unlock plaintext is `renzfi-setup` | **PROVEN** |
| Locked UI can appear when installation is **not** `Ready` if `ownerCreated` | **PROVEN** |
| `POST /api/setup/unlock` short-circuits when `!isReady()` without verifying password and **without** opening unlock session | **PROVEN** (source bug) |
| That short-circuit makes a correct password appear to “fail” (reload stays locked) | **PROVEN** by control-flow |
| Incomplete factory reset can leave owner meta + unlock hash inconsistent with what the operator expects | **STRONGLY INDICATED** (matches recent Guru mid-wipe) |
| Exact password the device currently stores | **UNKNOWN** without serial / NVS dump |

**This is not a DMA incident.**

---

## 2. WHAT “SETUP LOCKED” MEANS

Gate in `serveSetupPage` (`SetupServer.cpp`):

```text
locked = requiresSetupUnlock() && !hasActiveSetupUnlockSession()
```

`requiresSetupUnlock()` is true when **any** of:

1. Active re-entry session flag, or  
2. Installation state is `Ready`, or  
3. `_ownerCreated == true`  ← incomplete install / recovery

So the locked page does **not** require production Ready. Owner-already-created is enough.

Unlock is **only** `POST /api/setup/unlock` with JSON `{ "password": "…" }` → hash compare against `_setupUnlockPasswordHash` (NVS-primary after migration; SD/`/fb` mirror).

---

## 3. PASSWORD CONFUSION (VERY COMMON)

| Credential | Where used | Default / notes |
|------------|------------|-----------------|
| **Setup Unlock Password** | Setup Locked page only | Set at owner create; if blank at create, **copied from owner password**; later Admin “Change Setup Unlock Password” can diverge; factory default hash is for **`renzfi-setup`** |
| **Owner / Admin password** | Ethernet Admin login | NVS `renz-auth`; separate from unlock |

**PROVEN:** Trying the Admin password unlocks Setup **only if** that string still matches the stored unlock hash.

---

## 4. PROVEN CONTROL-FLOW BUG (UNLOCK API)

`POST /api/setup/unlock` currently:

```text
if (!_installation->isReady()) {
  return HTTP 200 success "Setup already available"
  // DOES NOT call unlockSetup(password)
  // DOES NOT verify password
}
… else verify password via unlockSetup() …
```

Meanwhile the **lock page ignores `isReady()`** and locks whenever `ownerCreated` (contract check #3 in `setup-unlock-contract-check.mjs` explicitly requires this).

### Failure mode (PROVEN)

```text
ownerCreated=true, installation ≠ Ready
        ↓
GET /admin/setup → Setup Locked (UI)
        ↓
User enters correct unlock password
        ↓
POST /api/setup/unlock → 200 "Setup already available"
        ↓
JS: location.replace('/admin/setup')
        ↓
Still no unlock session → Setup Locked again
        ↓
Operator concludes “password not working”
```

Error UI may show a generic failure only on network/JSON issues; on this path the API reports **success**, so the field may flash briefly then return to Locked with **no** “Invalid setup unlock password” — or the operator only notices that unlock never sticks.

If installation **is** `Ready` and the hash does not match → **PROVEN** path returns `403 SETUP_UNLOCK_INVALID` (“Invalid setup unlock password”).

---

## 5. FACTORY-RESET / GURU INTERACTION (THIS DEVICE’S LIKELY HISTORY)

Recent physical crash during factory reset was **after** quiesce and around asset delete (`[INFO] assets: music deleted`), **before** full reboot completion.

Quiesce step (**PROVEN**):

- Clears RAM unlock + `AuthCredentials::clearSetupUnlockCredentials()` (NVS unlock)
- `AuthManager::resetToDefault(false)` → `firstBootDone=false`, default admin hash
- Clears RAM `ownerCreated`

Later steps (may **not** have completed if Guru):

- Delete SD/`/fb` provisioning files  
- `resetToFactory()` installation  
- Reboot + full auth invalidate  

### Post-crash boot combinations

| SD `provisioning.json` | NVS unlock | NVS firstBoot | Result |
|------------------------|------------|---------------|--------|
| Still present with custom unlock | Cleared then re-migrated from SD | false | Locked; **custom** unlock should verify if `isReady` and API reaches verify |
| Still present, ownerCreated | Cleared | false | Locked; same |
| Wiped / missing | Cleared | false | Defaults → unlock hash = **`renzfi-setup`**; may **not** lock if `ownerCreated` false |
| Present ownerCreated, install not Ready | any | any | Locked UI + **§4 short-circuit** → unlock never sticks |

So after an interrupted factory reset, the operator’s “known” password may be:

1. Correct but defeated by §4 short-circuit, or  
2. Stale (device now holds `renzfi-setup` or an older SD hash), or  
3. The Admin password while unlock was set differently.

---

## 6. AUTHORITATIVE UNLOCK STORAGE (CURRENT CODE)

Per `SetupProvisioningManager::load` / `persist` and `docs/SETUP_LOCK_PERSISTENCE_FORENSIC.md` (updated contract):

| Store | Role |
|-------|------|
| NVS `renz-auth` `unlockHash` / `unlockBlob` | **Authoritative** Setup Unlock Key |
| SD + `/fb` `provisioning.json` | Mirror + migration source |
| RAM session | Unlock window (~20 min); not durable |

Empty hash → code substitutes hash of **`renzfi-setup`**.

---

## 7. WHAT TO TRY ON HARDWARE (NO CODE CHANGE)

1. Note exact API error (browser Network tab on `POST /api/setup/unlock`):  
   - `SETUP_UNLOCK_INVALID` → wrong string vs current hash  
   - `200` + still Locked → **§4 bug** (install not Ready)  
2. Try factory default unlock: **`renzfi-setup`** (especially after aborted factory reset).  
3. If Admin still reachable on Ethernet: System Settings → Setup Unlock panel can show recovered plaintext when protect-blob exists (owner session).  
4. Capture serial around unlock: installation state, `ownerCreated`, whether unlock logs appear.

---

## 8. MINIMAL FIX — APPLIED 2026-08-22

`POST /api/setup/unlock` no longer treats `!isReady()` as “already available.”

Skip password only when:

- `!requiresSetupUnlock()`, or  
- `hasActiveSetupUnlockSession()`

Otherwise always `unlockSetup(password)` before returning success. `reopenSetupWizard()` still runs only when currently `Ready`.

**Physical validation:** flash required; not claimed from compile alone.

---

## 9. EVIDENCE INDEX

| Item | Location |
|------|----------|
| Locked HTML | `SetupServer.cpp` `kSetupLockedHtml` |
| Lock predicate | `serveSetupPage` |
| Unlock short-circuit | `SetupServer.cpp` `POST /api/setup/unlock` `!isReady()` early return |
| Unlock verify | `SetupProvisioningManager::unlockSetup` / `verifySetupUnlockPassword` |
| Default unlock | `kDefaultSetupUnlockPassword = "renzfi-setup"` |
| Quiesce clears unlock NVS | `beginFactoryResetQuiesce` + `clearSetupUnlockCredentials` |
| Owner create sets unlock | `createOwner` (defaults unlock ← owner password if blank) |
| Prior persistence forensic | `docs/SETUP_LOCK_PERSISTENCE_FORENSIC.md` |

---

## 10. CLASSIFICATION SUMMARY

| Finding | Status |
|---------|--------|
| Locked screen = Setup Unlock gate | **PROVEN** |
| Unlock API / lock UI Ready mismatch | **PROVEN** defect |
| Password confusion Admin vs Unlock | **PROVEN** product ambiguity |
| Default `renzfi-setup` after wipe/clear | **PROVEN** |
| This operator’s exact stored secret | **UNKNOWN** |
| DMA related | **REJECTED** |

**STOP:** Unlock short-circuit fix applied in `SetupServer.cpp` (2026-08-22). Flash Waveshare build, then retry Unlock at `http://192.168.4.1/admin/setup`. If still `SETUP_UNLOCK_INVALID`, try `renzfi-setup` (post-abort reset default).
