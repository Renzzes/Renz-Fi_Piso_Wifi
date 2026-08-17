# Owner Setup TWDT — Implementation Report

**Date:** 2026-08-11  
**Mode:** Production-safe, minimal, evidence-based  
**Forensic basis:** `TWDT_OWNER_ENDPOINT_ROOT_CAUSE.md`  
**Related:** activation credential failure secondary to setup never completing (`Saved MikroTik connection is unavailable`)

---

## 1. Verdict

`POST /api/setup/owner` no longer performs the proven TWDT-tipping durable SD work on `async_tcp`. Owner credentials remain durable in NVS immediately; `provisioning.json` / `installation.json` commit on `FirmwareApp::loop()` with boot repair from NVS if power is lost mid-window.

No architecture redesign. RouterWorker, StorageManager, PortalSessionManager, RouterProvisioningEngine, SD durability mechanisms, setup wizard structure, RouterOS command count, and TWDT policy are unchanged.

**No MikroTik captive portal upload required** — portal sources were not modified.

---

## 2. Proven root causes addressed

| Issue | Proven cause | Fix |
|---|---|---|
| Task WDT on owner create | Synchronous SD on `async_tcp` after `"Owner credentials provisioned"` | Remove tipping I/O from the HTTP path; defer remaining SD commits |
| Pre-log budget burn | `invalidateAllSessions` → `clearJsonArray` transactional rewrite of empty admin sessions | Skip SD rewrite when no in-memory sessions |
| Post-log tip | `Logger::info` → `appendHistory` → `file.flush()` | `infoLocal` (Serial/RAM/SSE only) |
| Remaining durable writes | `persist()` + `syncInstallationState()` on same task | Defer to `SetupProvisioningManager::loop()` |
| Activation `"Saved MikroTik connection is unavailable"` | Setup never finished → no verified `router-connection.json` → reconcile fails | Unblocked by successful owner setup continuing to router save/sync (existing path) |

---

## 3. Files modified

| File | Change | Why | Safety |
|---|---|---|---|
| `ESP32_S3_Firmware/src/Logger.h` | Added `infoLocal()`; `write(..., durableHistory)` | Allow non-durable info on TWDT-sensitive paths | Default `info`/`warn`/`error` still durable |
| `ESP32_S3_Firmware/src/Logger.cpp` | Implement `infoLocal`; gate `appendHistory` | Removes SD flush tip after provision log | History/ledger/transactional paths elsewhere unchanged |
| `ESP32_S3_Firmware/src/AuthManager.cpp` | Skip `clearJsonArray` when no active sessions; use `infoLocal` in `provisionOwnerCredentials` | Removes empty transactional rewrite + history flush on first-boot owner create | When sessions exist, SD clear still runs |
| `ESP32_S3_Firmware/src/SetupProvisioningManager.h` | `loop()`, `_pendingOwnerDurableCommit`, `commitPendingOwnerDurableState()` | Deferred durable commit API | Flag is RAM-only; boot repair covers power loss |
| `ESP32_S3_Firmware/src/SetupProvisioningManager.cpp` | `createOwner` returns 200 after NVS/RAM; defer SD; strengthen `synchronizeAtBoot` | Move SD off `async_tcp` | Owner password/username already in NVS; SD catch-up + repair |
| `ESP32_S3_Firmware/src/FirmwareApp.cpp` | Call `_setupProvisioning.loop()` | Drive deferred commit on `loopTask` | Same pattern as other managers |

### Not modified (intentionally)

- RouterWorker / RouterProvisioningWorker retry strategy  
- StorageManager transactional write / CRC / rollback / recovery / replay  
- PortalSessionManager state machine  
- RouterProvisioningEngine credential sync algorithm  
- Setup wizard steps / UI  
- Portal / `Final_Build_Portal`  
- TWDT timeout / disable  
- RouterOS API command paths  

---

## 4. New owner create execution timing

```text
async_tcp (POST /api/setup/owner)
  Validation
  AuthManager::provisionOwnerCredentials
    hash + NVS saveCredentials
    invalidateAllSessions          ← RAM clear only if no sessions (typical first boot)
    Logger::infoLocal              ← Serial/RAM/SSE; NO appendHistory
  setOwnerUsername (NVS)
  in-memory provisioning fields + _pendingOwnerDurableCommit=true
  HTTP 200

loopTask (FirmwareApp::loop)
  SetupProvisioningManager::loop
    persist() → provisioning.json   (transactional, unchanged)
    syncInstallationState(OwnerCreated) → installation.json
```

Boot repair if power lost before loop flush:

- `_auth->firstBootCompleted()` (NVS) ⇒ restore `_ownerCreated` / username  
- `persist()` + advance installation Factory → OwnerCreated  

---

## 5. Credential path after successful setup (unchanged architecture)

```text
Router save (RouterWorker SaveConnection)
  SetupRouterConnectionManager::saveConnection
    → router-connection.json with connectionVerified=true
  RouterProvisioningEngine::syncProductionRouterCredentials
    → router.json populated for production activation

Activation
  ensureProductionRouterCredentials / resolveRouterCredentials(Persisted)
  → hasVerifiedConnection() true when setup completed
```

---

## 6. Build results

| Target | Result | Notes |
|---|---|---|
| `freenove_esp32_s3_wroom` | **SUCCESS** | Production firmware |
| `w5500_minimal` | FAILED (linker) | Pre-existing: `EthernetManager` refs `NetworkDiagnostics` / `SetupDnsPolicy` not in minimal `build_src_filter`. Unrelated to this change set. |

### Production size (`freenove_esp32_s3_wroom`)

| Metric | Value |
|---|---|
| Flash | **91.3%** (2 394 343 / 2 621 440 bytes) |
| RAM | **32.4%** (106 316 / 327 680 bytes) |

---

## 7. Impact analysis

| Area | Impact |
|---|---|
| **AsyncTCP** | Owner path no longer stacks transactional admin rewrite + history flush + dual JSON persists on the HTTP task. Primary TWDT fix. |
| **Storage** | All durability mechanisms preserved. Timing only: owner SD writes move to `loopTask`. |
| **RouterOS command count** | Unchanged (0 on owner path; activation path unchanged). |
| **MikroTik CPU** | Unchanged — no added RouterOS traffic or polling. |
| **Portal / voucher / sales / history** | Untouched. Owner provision log is non-durable by design; other `Logger::info` calls still append history. |
| **DMA / Heap** | No new allocations of significance; no Guru Meditation class changes expected. |

---

## 8. Regression analysis

| Risk | Mitigation |
|---|---|
| Power loss between HTTP 200 and deferred SD flush | NVS owner credentials + `synchronizeAtBoot` repair |
| Custom setup-unlock password lost if power loss before flush | Narrow window; unlock falls back to default hash seed path on empty; rare field case — owner password still valid for login |
| HTTP 200 before SD confirms | Intentional; matches forensic §8.2; credentials already NVS-durable |
| Sessions present during owner create | Still clears admin sessions on SD (correct invalidation) |
| Wizard 409 after success | `_ownerCreated` set in RAM before response; unchanged admit logic |
| Activation credentials | Still require router save + sync; this fix unblocks reaching that step |

---

## 9. Hardware validation checklist

- [ ] Erase / blank SD factory boot  
- [ ] `POST /api/setup/owner` returns **200**, **no** `task_wdt`, **no** reboot  
- [ ] Serial: `"Owner credentials provisioned"` then `"deferred owner durable commit complete"`  
- [ ] `/config/provisioning.json` shows `ownerCreated: true` shortly after  
- [ ] Installation advances to `OwnerCreated`  
- [ ] Complete router connection → `router-connection.json` has `connectionVerified: true`  
- [ ] `credentialSyncOk: true` / `router.json` has non-empty username/password  
- [ ] Customer: captive portal → coin → Done Paying → Activating → Connected  
- [ ] Internet granted; countdown firmware-owned (no jump/freeze/drift)  
- [ ] Coin during payment updates credits/time immediately  
- [ ] Pause / Resume / Terminate / Expire → Waiting for Payment  
- [ ] Voucher + sales recording unchanged  
- [ ] No new TWDT; heap/DMA stable; MikroTik CPU unchanged under activation  

---

## 10. Remaining known limitations

1. **Owner provision info line** is not written to durable history NDJSON (by design). Serial/RAM/SSE still show it.  
2. **Sub-second power loss** after owner HTTP 200 before `loop()` flush may require boot repair; custom unlock password may reset to default until reconfigured. Owner login credentials remain in NVS.  
3. **`w5500_minimal`** link failure is out of scope for this fix (recovery env src filter vs EthernetManager deps).  
4. Hardware TWDT confirmation still requires a field flash of this build.

---

## 11. Portal deployment

**No MikroTik captive portal upload required.**  
`Final_Build_Portal` was not regenerated.
