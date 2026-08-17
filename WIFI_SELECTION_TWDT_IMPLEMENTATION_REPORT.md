# Wi-Fi Selection TWDT Fix + Setup AsyncTCP Audit — Implementation Report

**Date:** 2026-08-11  
**Forensic basis:** `TWDT_WIFI_SELECTION_ROOT_CAUSE.md`, `SETUP_ASYNCTCP_TWDT_PREVENTION.md`  
**Related:** `TWDT_OWNER_ENDPOINT_ROOT_CAUSE.md`, `OWNER_SETUP_TWDT_IMPLEMENTATION_REPORT.md`, SD hardening reports  

**Mode:** Minimal production-safe fix. No architecture redesign. No TWDT/policy change. No RouterOS command increase. SD durability preserved.

---

## 1. Verdict (source/build)

**HARDWARE VALIDATION REQUIRED**

Firmware builds; Wi-Fi selection durable work is deferred off `async_tcp`; owner TWDT fix preserved; setup endpoint audit completed. Physical ESP32 + SD + MikroTik + real client validation has **not** been performed in this session, so production readiness cannot be declared.

---

## 2. What changed and why

### Proven defect

`POST /api/setup/router/wifi/selection` ran `persist()` → `writeJson(router-provisioning.json)` (transactional SD + SPIFFS checkpoint) on `async_tcp`, tipping at `SPIFFS.exists` / `esp_flash_read` (ELF `bafccbb34`).

### Fix (owner-style deferred durable commit)

| Before | After |
|---|---|
| HTTP validates → RAM update → **sync** `writeJson` on `async_tcp` → HTTP 200 “saved” | HTTP validates → RAM update → schedule deferred persist → HTTP **202** “accepted — persisting” |
| Durable write claimed complete in response | Browser polls `/api/setup/status` until `durableCommitStatus=persisted` |
| Checkpoint on HTTP task | Checkpoint on `loopTask` via `RouterProvisioningManager::loop()` |

Same deferred pattern applied to `POST /api/setup/router/network-mode` (same `persist()` / same file / same TWDT class), without inventing a second job system.

### Owner TWDT fix

**Preserved and not regressed.** `createOwner` still sets `_pendingOwnerDurableCommit` and does not sync-persist provisioning/installation on `async_tcp`.

---

## 3. Files / functions changed

| File | Functions / area |
|---|---|
| `RouterProvisioningManager.h` | `loop()`, durable-status accessors, pending flags |
| `RouterProvisioningManager.cpp` | `scheduleDeferredPersist`, `commitPending…`, `loop`, `durableCommitStatus`; `saveWifiSelection` / `setNetworkModePreference` defer; `persist()` clears pending on success; `fillNetworkModeStatus` exposes status |
| `SetupServer.cpp` | wifi/selection returns `result.httpStatus` (202); network-mode same; boot log “defer durable” |
| `SetupWizardPageHtml.h` | `waitForWifiSelectionDurable()` before adoption |
| `FirmwareApp.cpp` | `_routerProvisioning.loop()` |

**Not modified:** StorageManager durability, RouterWorker, MikroTikDriver, PortalSessionManager, owner deferral logic, TWDT config, portal captive bundle (`Final_Build_Portal`).

---

## 4. Execution paths

### Wi-Fi selection — constraint-aligned path

```text
async_tcp
  → validate request
  → update safe in-memory state
  → scheduleDeferredPersist()   // QUEUED (single-slot coalesce)
  → HTTP 202 + durableCommitStatus=QUEUED
  → return quickly (no writeJson / no wait)

loopTask (RouterProvisioningManager::loop)
  → PERSISTING
  → persist() → StorageManager::writeJson (unchanged)
      → transactional SD write
      → SPIFFS checkpoint
      → recovery/verification
  → PERSISTED | FAILED

UI polls /api/setup/status until PERSISTED → existing adoption path
```

### Status values (exact)

`QUEUED` | `PERSISTING` | `PERSISTED` | `FAILED`

Duplicates: identical selection already Idle/PERSISTED → HTTP 200; already Queued/Persisting → HTTP 202 coalesce (no second job). Mid-write RAM change → one follow-up Queued commit.

---

## 5. RouterOS / MikroTik impact

| Metric | Impact |
|---|---|
| RouterOS command count | **Unchanged** (Wi-Fi selection never called RouterOS) |
| RouterWorker | Unchanged |
| MikroTik CPU | No new polling/retries |

---

## 6. Build / automated tests

| Check | Result |
|---|---|
| `freenove_esp32_s3_wroom` | **SUCCESS** |
| RAM | **32.4%** (106 332 / 327 680) — +16 B vs prior ~106 316 |
| Flash | **91.6%** (2 399 951 / 2 621 440) |
| `npm run test:portal:lifecycle` | **22/22 PASS** |

---

## 7. Mandatory setup endpoint audit table

| Endpoint | Task | Durable I/O | Network wait | RouterOS wait | Worker wait | Storage lock | Classification | Action |
|---|---|---|---|---|---|---|---|---|
| `POST /api/setup/owner` | async_tcp | Deferred to loop (NVS immediate) | No | No | No | On loop persist | **DEFERRED** | None (preserved) |
| `POST /api/setup/unlock` | async_tcp | Hash verify / session RAM | No | No | No | No | **SAFE** | None |
| `POST /api/setup/lock` | async_tcp | RAM session clear | No | No | No | No | **SAFE** | None |
| `POST /api/setup/router/test` | async_tcp enqueue → worker | Worker may persist connection | No on HTTP | On worker | Non-blocking enqueue | On worker | **WORKER-OFFLOADED** | None |
| `POST /api/setup/router/save` | async_tcp enqueue → worker | Worker persists connection + sync credentials | No on HTTP | On worker | Non-blocking enqueue | On worker | **WORKER-OFFLOADED** | None |
| `POST /api/setup/router/wifi/selection` | async_tcp | Deferred `router-provisioning.json` | No | No | No | On loop | **DEFERRED** | **Fixed this change** |
| `GET /api/setup/router/wifi/networks` | async_tcp | Cache read / may kick background refresh | No long wait | Background worker | No sync wait | Possible short | **SAFE** / background | None |
| `POST /api/setup/router/existing-network/scan` | async_tcp → worker | Worker | No on HTTP | On worker | Enqueue 202 | On worker | **WORKER-OFFLOADED** | None |
| `POST /api/setup/router/existing-network/configure` | async_tcp → worker | Worker `persist()` | No on HTTP | On worker | Enqueue 202 | On worker | **WORKER-OFFLOADED** | None |
| `POST /api/setup/router/network-mode` | async_tcp | Deferred provisioning persist; rare `setState` may still sync install write | No | No | No | On loop / rare sync | **DEFERRED** (+ residual rare install write) | Deferred preference persist |
| `POST /api/setup/router-apply` | (legacy/disabled path per SetupServer) | — | — | — | — | — | N/A | Unchanged |
| `POST /api/setup/finish` | async_tcp → worker | On worker | No on HTTP | On worker | Enqueue 202 | On worker | **WORKER-OFFLOADED** | None |
| `POST /api/setup/complete` | async_tcp | Handoff + reboot | No | No | No | Possible short | **SYNCHRONOUS-BUT-SHORT** / intentional reboot | None |
| `POST /api/setup/operator` | async_tcp | NVS operator + **sync** `SetupWizardFile` persist | No | No | No | Yes on persist | **UNSAFE-ON-ASYNCTCP** (not ELF-proven this incident) | Documented next risk — not changed |
| `POST /api/setup/ethernet` | async_tcp | Sync wizard persist | No | No | No | Yes | **UNSAFE-ON-ASYNCTCP** (legacy / off frozen primary path) | Documented |
| `POST /api/setup/guest-wifi` | async_tcp | Sync wizard persist | No | No | No | Yes | **UNSAFE-ON-ASYNCTCP** (legacy) | Documented |
| `POST /api/setup/ap-deployment` | async_tcp | Sync wizard persist | No | No | No | Yes | **UNSAFE-ON-ASYNCTCP** (legacy) | Documented |
| `POST /api/setup/coin` | async_tcp | Sync wizard + settings persist | No | No | No | Yes | **UNSAFE-ON-ASYNCTCP** (legacy) | Documented |
| `GET /api/setup/status` | async_tcp | Read-only status | No | No | No | Possible read lock | **SAFE** | None |

### Future-risk (next same-class candidates)

1. **`POST /api/setup/operator`** — sync `SetupWizardConfigManager::persist()` after NVS (checkpoint-eligible wizard file).  
2. Legacy **ethernet / guest-wifi / coin / ap-deployment** wizard saves — same pattern if UI still hits them.  
3. Rare **network-mode** confirmation branch calling `InstallationStateManager::setState` (sync installation.json) on `async_tcp`.

These were **not** modified here because they were not the ELF-proven tip; they must be deferred/offloaded before claiming the entire setup plane TWDT-safe.

---

## 8. SD durability

| Mechanism | Preserved? |
|---|---|
| Transactional SD writes | Yes (still via `writeJson`) |
| Rollback / CRC / recovery | Yes |
| SPIFFS continuous checkpoint | Yes (runs on `loopTask`) |
| History / append-only ledgers | Untouched |
| SD primary durable store | Yes — not moved to RAM/SPIFFS-only |

Power-loss window: between HTTP 202 and loop commit, selection exists in RAM only; UI waits for `persisted` before adoption. Reboot before commit loses unsaved selection (same class as owner unlock-password edge window).

---

## 9. FINAL VALIDATION SCORE

Physical hardware checks are marked **FAIL (not executed)** and block PRODUCTION READY.

### Overall: **34/72** checks passed — **47%**  
(Source/build/audit only; hardware + field lifecycle not run)

| Category | Score | Rate |
|---|---|---|
| ESP32 STABILITY | 4/12 | 33% |
| SETUP FLOW | 6/14 | 43% |
| SD STORAGE | 7/10 | 70% |
| MIKROTIK STABILITY | 4/8 | 50% |
| CUSTOMER PORTAL | 5/10 | 50% |
| SESSION LIFECYCLE | 4/10 | 40% |
| REGRESSION SAFETY | 4/8 | 50% |

### ESP32 STABILITY

| Result | Test | Expected | Actual | Evidence | Blocks production? |
|---|---|---|---|---|---|
| PASS | No sync writeJson on wifi/selection HTTP path | Deferred | Deferred | Source: `saveWifiSelection` + `loop()` | Yes if failed |
| PASS | Firmware build | SUCCESS | SUCCESS | PlatformIO log | Yes |
| PASS | Owner path still deferred | Deferred | Deferred | `createOwner` unchanged pattern | Yes |
| PASS | No TWDT timeout change | Unchanged | Unchanged | Diff | Yes |
| FAIL | No GM during Owner setup (HW) | No WDT | Not run | — | **Yes** |
| FAIL | No GM during Router setup (HW) | No WDT | Not run | — | **Yes** |
| FAIL | No GM during Wi-Fi scan (HW) | No WDT | Not run | — | **Yes** |
| FAIL | No GM during Wi-Fi selection (HW) | No WDT | Not run | — | **Yes** |
| FAIL | No GM during configure/apply/finish (HW) | No WDT | Not run | — | **Yes** |
| FAIL | No async_tcp TWDT (HW) | None | Not run | — | **Yes** |
| FAIL | No loopTask / RouterWorker WDT (HW) | None | Not run | — | **Yes** |
| FAIL | No deadlock (HW) | None | Not run | — | **Yes** |

### SETUP FLOW

| Result | Test | Notes |
|---|---|---|
| PASS | wifi/selection returns 202 + pending status | Source |
| PASS | UI waits for persisted before adoption | `SetupWizardPageHtml.h` |
| PASS | network-mode preference deferred | Source |
| PASS | configure/finish remain worker 202 | Unchanged |
| PASS | Endpoint audit table produced | This report §7 |
| PASS | Prevention rule documented | `SETUP_ASYNCTCP_TWDT_PREVENTION.md` |
| FAIL | Full factory→Ready HW walk | Not run — **blocks** |
| FAIL | Empty SD / reboot mid-setup HW | Not run — **blocks** |
| FAIL | Persist survives reboot (HW) | Not run — **blocks** |
| FAIL | Duplicate selection idempotent (HW) | Source-safe; HW not run |
| FAIL | Finish duplicate safe (HW) | Existing worker; HW not run |
| FAIL | Operator sync persist HW proof | Residual risk — **blocks full “setup plane clear” claim** |
| FAIL | Legacy wizard saves HW proof | Residual — document |
| FAIL | Rare network-mode setState HW | Residual |

### SD STORAGE

| Result | Test | Notes |
|---|---|---|
| PASS | Transactional writes retained | Source |
| PASS | SPIFFS checkpoint retained | Source |
| PASS | SD remains primary | Source |
| PASS | No durability removal | Diff |
| PASS | Pending flag single-slot | Source |
| PASS | Failed status exposed | `durableCommitError` |
| PASS | Worker persist clears pending | `persist()` |
| FAIL | SD writable on target HW | Not run |
| FAIL | No corruption after reboot HW | Not run |
| FAIL | Fallback functional HW | Not run |

### MIKROTIK STABILITY

| Result | Test | Notes |
|---|---|---|
| PASS | wifi/selection RouterOS cmds = 0 | Forensic + source |
| PASS | No new RouterOS polling | Diff |
| PASS | No new activation retries | Untouched |
| PASS | Command count unchanged for this fix | Documented |
| FAIL | Idle/setup CPU monitor HW | Not run |
| FAIL | Activation CPU HW | Not run |
| FAIL | Pause/resume/terminate CPU HW | Not run |
| FAIL | No 100% spike HW | Not run — **blocks PROD READY** |

### CUSTOMER PORTAL / SESSION

| Result | Test | Notes |
|---|---|---|
| PASS | Portal lifecycle unit tests | 22/22 |
| PASS | No portal countdown ownership change | Untouched |
| PASS | No activation architecture change | Untouched |
| PASS | No MikroTik portal rebuild required | Setup HTML only |
| PASS | Coin/voucher/sales code untouched | Diff scope |
| FAIL | Real coin→Internet HW | Not run — **blocks** |
| FAIL | Countdown continuous HW | Not run |
| FAIL | Pause/resume/terminate HW | Not run |
| FAIL | Expire→Waiting Payment HW | Not run |
| FAIL | Voucher redeem/reconnect HW | Not run |
| FAIL (session) | Duplicate session/user/sale HW | Not run |
| FAIL (session) | Sales recorded HW | Not run |
| FAIL (session) | Internet browse HW | Not run — **blocks** |
| FAIL (session) | Auto disconnect HW | Not run |

### REGRESSION SAFETY

| Result | Test | Notes |
|---|---|---|
| PASS | Owner TWDT fix intact | Source review |
| PASS | No StorageManager redesign | Diff |
| PASS | No RouterWorker redesign | Diff |
| PASS | Setup wizard step count unchanged | Frozen flow |
| FAIL | Operator still sync persist | Audit residual |
| FAIL | Legacy setup POSTs still sync | Audit residual |
| FAIL | Full HW regression suite | Not run |
| FAIL | Cross-reboot setup resume HW | Not run |

---

## 10. Production verdict

### **HARDWARE VALIDATION REQUIRED**

Mandatory ESP32 / setup / Internet / MikroTik CPU hardware checks are incomplete. Source fix for the **proven** Wi-Fi selection TWDT path is in place; residual sync durable writes remain on operator/legacy wizard endpoints and must be cleared before claiming the entire setup plane is TWDT-safe.

---

## 11. Hardware checklist to complete next

Flash this firmware, then prove:

1. Owner → Router test/save → Scan → **Wi-Fi Continue** → Configure → Finish with **zero** `async_tcp` TWDT.  
2. Serial: `Wi-Fi selection accepted` then `deferred durable commit complete`.  
3. Reboot after Wi-Fi selection shows persisted selection.  
4. Customer: coin → Done Paying → Internet → countdown → expire.  
5. MikroTik CPU remains bounded under setup + activation.

---

## 12. Explicit statements

- Forensic reports used: `TWDT_WIFI_SELECTION_ROOT_CAUSE.md`, `SETUP_ASYNCTCP_TWDT_PREVENTION.md`.  
- Previous **Owner TWDT fix was preserved and not regressed**.  
- SD transactional durability and SPIFFS checkpoints were **not** removed.  
- Objective was Wi-Fi selection fix **and** predictive audit of remaining setup async_tcp risks — not “build succeeded” alone.
