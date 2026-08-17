# Final Production Communication Stability + Data-Consistency Audit

**MODE:** FORENSIC AUDIT → VALIDATE → ROOT-CAUSE → MINIMAL FIX ONLY IF PROVEN → BUILD → DOCUMENTATION  
**Date:** 2026-08-03  
**Firmware env:** `freenove_esp32_s3_wroom`  
**Authoritative source:** CURRENT tree (docs consulted; source wins on conflict)

**RELEASE VERDICT: HARDWARE VALIDATION REQUIRED**

---

## 1. Executive verdict

| Area | Verdict |
|------|---------|
| Idle Admin / System Configuration RouterOS load | **PASS** — 0 sessions/min, 0 commands/min |
| Admin mutations AsyncTCP/TWDT | **PASS** — HTTP 202 + `enqueueAdmin*` + job poll |
| D1–D5 System Configuration remediation | **PASS** — still present in source |
| MikroTik avoidable CPU storm from Admin idle/nav | **PASS** (static) |
| Setup Unlock Admin management | **CONFIRMED DEFECT → FIXED** (was missing) |
| Skip Operator vs portal skip conflation | **CONFIRMED DEFECT → FIXED** |
| Operator create claimed in Admin but absent | **CONFIRMED DEFECT → FIXED** |
| Speculative architecture changes | **NONE** |
| Hardware proof | **REQUIRED** — not claimed from source |

---

## 2. Source files audited (primary)

Firmware: `ApiServer.cpp`, `RouterPlatform.cpp`, `MikroTikDriver.cpp`, `RouterOsClient.*`, `RouterApiTransportGate.*`, `RouterProvisioningWorker.cpp`, `RouterCacheManager.*`, `RouterWirelessAdapter.cpp`, `RouterProvisioningEngine.cpp`, `SetupProvisioningManager.*`, `SetupWizardPageHtml.h`, `SetupServer.cpp`, `AuthManager.*`, `Config.h`

Frontend: Admin pages under `src/pages/*`, `src/hooks/*`, `src/services/*`, `systemConfigurationStatus.ts`

Docs read: Admin status/profile remediation, async worker hardening, AsyncTCP TWDT forensic, post-finish issues, wireless false-negative, Finish DMA/portal verify, idempotent provisioning.

---

## 3. RouterOS command matrix (production Admin + key paths)

| Feature | HTTP | Worker | Commands (summary) | Sessions | Proplist | Periodic | Verdict |
|---------|------|--------|-------------------|----------|----------|----------|---------|
| Dashboard status | GET `/api/status` | no | none | 0 | — | client poll ESP | PASS |
| Sys Config open | GET settings/cache/wireless/profiles | no | none (cache/SD) | 0 | — | no | PASS |
| Test Connection | POST `/api/router/test` 202 | AdminTest | identity, resource, user-profile print, hotspot print | 1 | yes (profiles+hotspot) | no | PASS |
| Save router settings | PUT settings 202 | AdminSave | none (SD) | 0 | — | no | PASS |
| Save wireless | PUT wireless 202 | AdminSaveWireless | set + bounded print verify | 1 | yes (legacy wireless) | no | PASS |
| Synchronize | POST cache/sync 202 | AdminSync | snapshot collect (bounded where remediated) | 1 | mostly yes | no | PASS |
| Profiles GET | GET profiles | no | none | 0 | — | no | PASS |
| Hotspot activate | portal enqueue | ActivateHotspotUser | user print/set/add | 1 | no | event | PASS |
| Finish Setup | POST setup/finish | FinishSetup | multi-stage finish session | 1 reused | mixed | no | OBSERVE residual Setup unbounded prints |
| `/tool/profile` | — | — | **not used** | — | — | — | PASS |
| registration-table poll | — | — | **not used** | — | — | — | PASS |

---

## 4. Admin page request matrix (idle)

| Page | ESP local polls | RouterOS on mount/idle |
|------|-----------------|------------------------|
| Dashboard | status/coin/health/rgb when SSE down (~30s); SSE preferred | **0** |
| System Configuration | `/api/status` 30s | **0** |
| System Settings | status+rgb 30s | **0** |
| Promo/Vouchers/Sales/Portal | mount GETs | **0** |
| Active Users | users poll if SSE down | **0** RouterOS |
| Global | `/api/health` 5s + EventSource | **0** RouterOS |

**Idle System Configuration:** RouterOS commands/min = **0**, sessions/min = **0** — **NO CHANGE NEEDED**.

---

## 5. Setup → Admin data mapping

| Setup field | Persist | Admin destination | Secret? |
|-------------|---------|-------------------|---------|
| Router host/user/profile | router settings SD | System Configuration | password never returned (`passwordConfigured`) |
| SSID / wireless iface | canonical wireless + cache | Wireless summary | PSK not exposed on GET cache path intentionally |
| Hotspot / bridge / profiles | router-cache.json | System Configuration / status observation | no |
| Rate limits | profileDetails after Test/Sync | Available Profiles UI | no |
| Owner account | AuthManager NVS | Admin login | hash only |
| Operator | AuthManager NVS (`op_user`/`op_hash`) | login + System Settings (now) | hash only |
| Setup unlock | provisioning.json `setupUnlockPasswordHash` | System Settings (now): Configured + Change | hash only |
| Installation state | installation.json | setup status / Ready | no |

---

## 6. MikroTik CPU analysis

- Idle Admin does not open RouterOS sessions.
- Cooldown: `ROUTER_API_MIN_CONNECT_INTERVAL_MS = 5000`; failure backoff 10–60s.
- Single session gate; worker queue depth 1 → BUSY 503.
- Test = 1 session, 4 bounded commands.
- No `/tool/profile` polling introduced.
- Software cannot guarantee MikroTik never hits 100% CPU; Renz-Fi avoids avoidable storms.

---

## 7. ESP32 task / AsyncTCP analysis

| Task | Role | Blocks on RouterOS? |
|------|------|---------------------|
| async_tcp | validate + enqueue + 202 / local JSON | **No** for Admin router ops |
| router_worker | RouterOS + cooldown `vTaskDelay` | Yes (correct place) |
| SSE | job/state events | No |

Residual blocking `dispatch()` only on Setup debug/legacy paths — not Admin production Test/Save/Wireless/Sync.

---

## 8. Session / cooldown

- Min connect interval: **5s**
- Queue depth: **1**
- BUSY when worker running
- One RouterOS API session at a time via transport gate

---

## 9. W5500 / SPI / DMA

- Prior portal-verify DMA fix preserved (targeted `/file/print`, SKIPPED = zero file queries).
- `MAX_ATTRS` remains **24**.
- Admin wireless/profile paths use `.proplist`.
- Residual unbounded wireless prints remain on **Setup discovery/finish** paths — OBSERVATION, not changed (Finish/setup freeze + DMA history).

---

## 10. Setup Skip + Operator audit

### Before (confirmed defect)

`Skip for Now` on Step 5:
1. Skipped operator creation  
2. Forced `portalDeploymentMode: 'skipped'`  
3. Immediately finished  

Creating Operator forced default `manual_external` portal mode.

Portal choice and Operator choice were **conflated**. Wizard claimed Admin could create Operator later, but Admin had **no** operator UI/API.

### After (minimal fix)

- Step 5: independent **Captive portal on Finish** radios (`skipped` default / `manual_external`).
- **Skip Operator & Finish** uses selected portal mode only (does not hide portal choice).
- **Create Operator** then Finish uses the same selected portal mode.
- Admin System Settings: create Operator via same `AuthManager::provisionOperatorCredentials` store.

---

## 11. Setup Unlock Password audit

| Item | Finding |
|------|---------|
| Storage | Hash only in provisioning.json |
| Plaintext recoverable | **No** |
| Factory default | `renzfi-setup` hashed only when hash empty — does not overwrite owner-set hash |
| Before | `setSetupUnlockPassword()` had **zero** Admin call sites |
| After | GET/POST `/api/settings/setup-unlock` (owner); UI shows **Configured** + Change form (current/new/confirm) |
| API never returns | hash or plaintext |

---

## 12. System Configuration (D1–D5) revalidation

| Item | Status |
|------|--------|
| Configured vs Online | PASS |
| Hotspot available/unavailable/unknown | PASS |
| Profiles + rate-limit + Not set yet | PASS |
| Wireless .proplist Admin paths | PASS |
| Idle RouterOS = 0 | PASS |
| MAX_ATTRS raised | NO |

---

## 13. Confirmed defects / changes applied

### C1 — Setup Unlock not manageable from Admin

- **Root cause:** Hash stored; change API unused; no Admin UI.
- **Fix:** Owner-only GET/POST `/api/settings/setup-unlock` + System Settings panel.
- **Files:** `SetupProvisioningManager.*`, `ApiServer.cpp`, `settings.ts`, `SetupSecurityPanels.tsx`, `SystemSettingsPage.tsx`
- **RouterOS impact:** none

### C2 — Skip conflated Operator + portal

- **Root cause:** One button forced portal `skipped` and finished without separate portal choice.
- **Fix:** Portal radios on existing Step 5; Skip Operator uses selected mode; Create Operator uses selected mode.
- **Files:** `SetupWizardPageHtml.h`
- **RouterOS impact:** none until Finish; default remains skip (DMA-safer)

### C3 — Operator “create later in Admin” was false

- **Root cause:** Copy promised Admin create; no endpoint/UI.
- **Fix:** GET/POST `/api/settings/operator` → same AuthManager NVS; System Settings panel.
- **Files:** `ApiServer.cpp`, `AuthManager.*` (optional `invalidateSessions=false` for Admin), frontend panels
- **RouterOS impact:** none

---

## 14. Intentionally NOT changed

- Async worker architecture, cooldown, MAX_ATTRS
- Captive portal ownership / coin / voucher / pause / rates / branding
- Setup wizard step count/order (freeze)
- Aggressive Setup wireless inventory unbounded prints (Finish/setup residual)
- No new RouterOS polling for status
- No plaintext unlock password storage

---

## 15. RouterOS request budget (derived)

| Context | Sessions | Commands |
|---------|----------|----------|
| Dashboard idle | 0/min | 0/min |
| System Configuration idle | 0/min | 0/min |
| Test Connection | 1/action | ~4 bounded |
| Save router settings | 0 | 0 |
| Wireless Save | 1 | set + verify prints |
| Synchronize | 1 | snapshot set |
| Setup Finish | 1 session | multi-stage (portal skipped ⇒ 0 file prints) |
| Captive portal idle | 0 RouterOS from ESP Admin | activate only on pay events |

---

## 16. Build result

```
platformio run -e freenove_esp32_s3_wroom → SUCCESS
RAM:   31.8% (104140 / 327680)
Flash: 84.5% (2215355 / 2621440)

npm run build (EMBEDDED_BUILD=1) → SUCCESS
```

Pre-existing ArduinoJson deprecation warnings remain.

---

## 17. Static validation matrix (selected)

| ID | Result |
|----|--------|
| A–E Idle Admin / Config | STATIC PASS |
| F–O Mutations async + BUSY/cooldown | STATIC PASS |
| U–W Skip + Operator separation | STATIC PASS (after fix) |
| Y Setup→Admin data | STATIC PASS |
| Z Profiles/rates | STATIC PASS |
| AE–AJ Captive/coin/voucher/etc. | NOT MODIFIED — STATIC PASS intent |
| AM–AN TWDT/Guru | STATIC mitigated Admin; HARDWARE REQUIRED |

---

## 18. Hardware validation procedure

1. Flash `freenove_esp32_s3_wroom` + upload embedded web assets.  
2. Boot configured unit; open Admin; navigate all pages 5 min — Serial should show **no** `[router-api] START` storm.  
3. Idle System Configuration 15 min — 0 RouterOS sessions.  
4. Test Connection ×10 — one session each; profiles show dynamic rate-limits; empty → Not set yet.  
5. Wrong password / unplug ETH — job fails; no reboot; connectivity Offline/unknown appropriately.  
6. Wireless Save ×5 — one session; no second refresh.  
7. System Settings: Setup Unlock shows Configured; change with current/new/confirm.  
8. Create Operator from System Settings; login as operator; verify RBAC.  
9. Fresh Setup: select Skip portal + Create Operator; Finish → Ready; portal status skipped.  
10. Fresh Setup: Verify portal + Skip Operator; Finish.  
11. MikroTik (sparingly): `/system resource print`, `/ip hotspot user profile print detail`, `/tool profile` briefly — not in a loop.  
12. Capture ESP Serial: heap, DMA snapshots if printed, router-worker queue, no TWDT, no Guru Meditation.

---

## 19. Production release verdict

**HARDWARE VALIDATION REQUIRED**

Do not mark PRODUCTION READY until hardware evidence for idle RouterOS silence, Test/Save stability, Setup Skip/Operator paths, and captive-portal regression is captured.
