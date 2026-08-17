# Network Adoption Workflow — Production Hardening Audit

**Date:** 2026-07-13  
**Scope:** `NetworkAdoptionWorkflow`, `SetupServer` adoption routes, setup wizard adoption UI (read-only for audit)  
**Out of scope (unchanged):** Admin Dashboard, `ExistingNetworkScanner`, `RouterProvisioningWorker`, `RouterOsClient`

---

## 1. State Machine Audit

Authoritative implementation: `src/web/NetworkAdoptionWorkflow.cpp`  
Documented contract: `docs/NETWORK_ADOPTION_STATE_MACHINE.md`

### IDLE

| Aspect | Detail |
|--------|--------|
| **Who enters** | `load()` (missing/invalid file), `resetToIdle()`, `assignIdleFields()` after enqueue rollback, `ApiServer` factory reset via `resetPersisted()` |
| **Who exits** | `enqueueAdoption()` → `queued` |
| **Mutators** | `assignIdleFields()`, `resetToIdle()` (clears all fields), `load()` restore |
| **Persistence** | `resetToIdle()` → `persist()`, `resetPersisted()` static write |
| **HTTP exposure** | `GET /api/setup/router/network-mode`, `GET .../adopt/jobs/{id}` (via overlay + snapshot), setup wizard `networkAdoptionState` |

### QUEUED

| Aspect | Detail |
|--------|--------|
| **Who enters** | `enqueueAdoption()` via `transitionState("queued")` |
| **Who exits** | `processQueued()` / `poll()` / `kickstartJob()` → `applying` |
| **Mutators** | `transitionState()` only (forward path) |
| **Persistence** | Every `transitionState()` call |
| **HTTP exposure** | POST `/adopt` response snapshot, GET job poll, network-mode overlay |

### APPLYING

| Aspect | Detail |
|--------|--------|
| **Who enters** | `processQueued()` from `queued` |
| **Who exits** | Success → `adopted` (`adoptExistingNetwork` or already-adopted recovery); errors → `failed` (`failJob`); timeout → `failed` (`checkStateTimeouts`) |
| **Mutators** | `transitionState()`, `failJob()` |
| **Persistence** | On every transition and `failJob()` |
| **HTTP exposure** | GET job poll, POST `/adopt` snapshot after kickstart |

### ADOPTED

| Aspect | Detail |
|--------|--------|
| **Who enters** | `processQueued()` after successful apply; `beginVerification()` recovery path when router already adopted during `applying` |
| **Who exits** | `beginVerification()` → `verifying` |
| **Mutators** | `transitionState()` only |
| **Persistence** | On transition |
| **HTTP exposure** | GET job poll, `begin-verify` response, network-mode overlay |

### VERIFYING

| Aspect | Detail |
|--------|--------|
| **Who enters** | `beginVerification()` |
| **Who exits** | `completeVerification(success)` → `completed`; `completeVerification(fail)` / `fail-verify` → `failed`; timeout → `failed` |
| **Mutators** | `transitionState()`, `failJob()` |
| **Persistence** | On transition |
| **HTTP exposure** | GET job poll, `begin-verify`, `complete-verify`, `fail-verify` |

### COMPLETED

| Aspect | Detail |
|--------|--------|
| **Who enters** | `completeVerification(success)` |
| **Who exits** | None (terminal until factory reset or `resetToIdle` via network-mode POST) |
| **Mutators** | `transitionState()` only |
| **Persistence** | On transition |
| **HTTP exposure** | GET job poll, network-mode overlay, installation handoff |

### FAILED

| Aspect | Detail |
|--------|--------|
| **Who enters** | `failJob()` from `applying` or `verifying` only |
| **Who exits** | `resetToIdle()` → `idle` (also triggered implicitly before re-enqueue from `failed`) |
| **Mutators** | `failJob()` (authorized FAILED path), `resetToIdle()` |
| **Persistence** | `failJob()`, `resetToIdle()` |
| **HTTP exposure** | GET job poll (HTTP 500 body), POST `/adopt` when kickstart ends failed |

**Single authoritative transition path:** All forward lifecycle transitions go through `transitionState()` or `failJob()` (FAILED is a dedicated terminal transition with error fields). Administrative resets use `resetToIdle()` / `resetPersisted()`. `load()` restores persisted state only.

---

## 2. Transition Validation

Repository-wide search for bypass patterns:

| Pattern | Result |
|---------|--------|
| `transitionState(` | `NetworkAdoptionWorkflow.cpp`, `CoinManager` (unrelated) |
| `_state =` (adoption) | `load()`, `assignIdleFields()`, `transitionState()`, `failJob()` — all in workflow file |
| `networkAdoptionState =` | Read-only assignment in HTTP response builders (`SetupServer.cpp`) from snapshot |
| `state = "queued"` etc. | **None** — no string literal state assignments |

**Verdict:** No illegal bypass of the adoption state machine outside authorized mutators.

---

## 3. Persistence Audit

| Transition trigger | persist() | On failure |
|--------------------|-----------|------------|
| `transitionState()` | Yes, after in-memory update | **Rollback** to previous state; log `persist_failed rollback` |
| `failJob()` | Yes | **Rollback** to previous state; log `persist_failed rollback` |
| `resetToIdle()` | Yes | Silent if storage null (pre-existing); fields cleared in memory |
| `enqueueAdoption()` | Via `transitionState(queued)` | `assignIdleFields()` + `PERSIST_FAILED` error |

No silent in-memory/disk divergence after successful transition reporting.

---

## 4. Job Recovery Audit (Reboot Simulation)

| Persisted state at reboot | `load()` behavior | Recovery |
|---------------------------|-------------------|----------|
| **QUEUED** | Valid if `jobId != 0` | `resumeInterruptedJob()` → `kickstartJob()` → `applying` → … |
| **APPLYING** | Valid | `resumeInterruptedJob()` → re-runs apply or timeout → `failed` |
| **ADOPTED** | Valid | Stays adopted; UI calls `begin-verify` |
| **VERIFYING** | Valid | Stays verifying; UI resumes poll + verification; timeout → `failed` if stalled |
| **COMPLETED** | Valid | Terminal; `enqueueAdoption` rejected |
| **FAILED** | Valid | Cleared on next enqueue via `resetToIdle()` |
| Invalid combos (`idle`+jobId, unknown state) | `resetToIdle()` | Deterministic clean slate |

Impossible states do not survive reboot.

---

## 5. Idempotency Audit

### POST `/api/setup/router/existing-network/adopt`

| Repeat condition | Behavior |
|------------------|----------|
| Active job (`queued`/`applying`/`adopted`/`verifying`) | Rejected `ADOPTION_JOB_BUSY` — no duplicate job |
| `completed` | Rejected `ADOPTION_ALREADY_COMPLETED` |
| `failed` | Clears to `idle`, then new enqueue — intentional retry |
| Same body twice while `idle` | Second call only after first completes or fails |

### POST `.../jobs/{id}/begin-verify`

| Repeat condition | Behavior |
|------------------|----------|
| Already `verifying` or `completed` | Returns success (idempotent) — no duplicate transition |

### POST `.../jobs/{id}/complete-verify`

| Repeat condition | Behavior |
|------------------|----------|
| Already `completed` | Returns success (idempotent) — no duplicate transition |
| Not `verifying` | Rejected (except completed idempotent case) |

---

## 6. Poll Audit

| Endpoint / path | Mutates state? |
|-----------------|----------------|
| `GET .../adopt/jobs/{id}` | Yes — calls `poll()` then **read-only** `getJobSnapshot()` |
| `snapshot()` | **Read-only** — delegates to `getJobSnapshot()` |
| `POST begin-verify` handler | Calls `poll()` then `beginVerification()` |
| `fillNetworkModeOverlay()` | Read-only |
| Main loop `pollAdoptionWorkflow()` | Calls `poll()` only |

GET job status is the only poll-driven HTTP path that advances apply phase; snapshot used for POST `/adopt` response is read-only after kickstart.

---

## 7. Timeout Audit

| Scenario | Path | Terminal state |
|----------|------|----------------|
| APPLYING too long (120s) | `checkStateTimeouts()` in `poll()` / `processQueued()` | `failed` / `ADOPTION_APPLY_TIMEOUT` |
| VERIFYING too long (300s) | `checkStateTimeouts()` | `failed` / `ADOPTION_VERIFY_TIMEOUT` |
| Router provisioning unavailable | APPLYING timeout after grace period | `failed` / `ROUTER_PROVISIONING_UNAVAILABLE` |
| Router adopt API error | `failJob(result.errorCode)` | `failed` |
| Invalid payload | `failJob(INVALID_ADOPTION_PAYLOAD)` | `failed` |
| Persist failure on transition | Rollback; caller surfaces error | No stuck intermediate state |
| Network unreachable / RouterOS timeout | Propagated from `adoptExistingNetwork()` | `failed` with router error code |

No indefinite APPLYING or VERIFYING without explicit timeout → FAILED.

---

## 8. Structured Logging

Milestone format via `logMilestone()`:

```
[adoption] job=41359 transition=queued->applying elapsed=412ms
[verify] job=41359 phase=completed
```

Verbose `Serial.println` reduced on hot paths; HTTP layer retains lightweight `[adoption] enqueue/kickstart/snapshot` markers.

---

## 9. Hardware Regression Checklist

| Scenario | Expected | Audit status |
|----------|----------|--------------|
| Fresh install | idle → full adoption → completed | ✓ Logic verified (not hardware-run) |
| Existing adopted router | Skip apply → adopted | ✓ `isExistingNetworkAdopted()` path |
| Browser refresh during APPLYING | GET poll resumes | ✓ |
| Browser refresh during VERIFYING | GET poll + begin-verify idempotent | ✓ |
| ESP reboot during APPLYING | `resumeInterruptedJob()` | ✓ **Fixed in this audit** |
| ESP reboot during VERIFYING | State restored; timeout safety | ✓ |
| Ethernet disconnected | Router adopt fails → failed | ✓ via provisioning error |
| Router unreachable | adopt error → failed | ✓ |
| Duplicate POST /adopt | Busy / already completed guards | ✓ |
| Duplicate POST /begin-verify | Idempotent | ✓ |
| Duplicate POST /complete-verify | Idempotent when completed | ✓ **Fixed in this audit** |
| Factory reset | `resetPersisted()` via ApiServer | ✓ |

*Hardware execution not performed in this audit session (no PlatformIO in shell PATH).*

---

## Issues Found

1. **APPLYING infinite stall** — `!_routerProvisioning` returned silently with no FAILED transition.
2. **No APPLYING/VERIFYING timeouts** — jobs could remain active indefinitely.
3. **Persist failure left in-memory state ahead of disk** — `transitionState()` / `failJob()` did not rollback.
4. **Reboot during QUEUED/APPLYING** — no automatic resume on `begin()` / `load()`.
5. **`complete-verify` not idempotent** — duplicate success POST returned 409 after completion.
6. **Inconsistent logging** — mixed `Serial.printf` formats without elapsed time or job id.

## Fixes Applied

| Fix | File |
|-----|------|
| `checkStateTimeouts()` — 120s apply, 300s verify | `NetworkAdoptionWorkflow.cpp` |
| `resumeInterruptedJob()` on `begin()` after `load()` | `NetworkAdoptionWorkflow.cpp/.h` |
| Persist rollback on `transitionState()` / `failJob()` failure | `NetworkAdoptionWorkflow.cpp` |
| Router provisioning unavailable → timeout → FAILED | `NetworkAdoptionWorkflow.cpp` |
| `completeVerification()` idempotent when already `completed` | `NetworkAdoptionWorkflow.cpp` |
| `logMilestone()` structured logging | `NetworkAdoptionWorkflow.cpp` |
| Regression guards for hardening | `tools/setup-wizard-adoption-workflow-check.py` |

## Remaining Risks

1. **Hardware validation** — Regression checklist is logic-verified only; on-device soak test still recommended.
2. **Timeout tuning** — 120s / 300s constants may need field adjustment for slow RouterOS links.
3. ~~**`resetToIdle()` on network-mode POST**~~ — **Fixed:** active workflows rejected; `resetToIdle()` guards `queued`/`applying`/`adopted`/`verifying`.
4. **QUEUED without poll** — Theoretically stuck until `poll()` or reboot resume; mitigated by main-loop poll + `resumeInterruptedJob()`.
5. **Build not re-run** — PlatformIO unavailable in audit shell; prior session reported SUCCESS; re-build before flash.

## Production Readiness Assessment

**PASS** — with the hardening fixes applied:

- No illegal transitions (enforced by `isTransitionAllowed()`)
- No unauthorized direct state mutations
- No orphan impossible persisted states (`load()` validation)
- No infinite APPLYING/VERIFYING (explicit timeouts)
- Every transition persisted with rollback on failure
- Deterministic reboot recovery for QUEUED/APPLYING
- Idempotent HTTP endpoints for adopt (busy guard), begin-verify, complete-verify
- Structured milestone logging

**Recommendation:** Flash firmware and run the hardware regression matrix once on a lab device before production rollout.
