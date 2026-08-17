# Network Adoption State Machine

Backend-owned setup-plane lifecycle for **Existing Network Adoption**.
Authoritative implementation: `NetworkAdoptionWorkflow` (`src/web/NetworkAdoptionWorkflow.cpp`).

The setup wizard UI reads `networkAdoptionState` from this workflow only. It does **not** use `RouterProvisioningWorker` for adoption (scan jobs use the worker; adoption uses this state machine + `poll()` / `kickstartJob()`).

## State classification

| Class | States | Helper |
|-------|--------|--------|
| **Active workflow** | `queued`, `applying`, `verifying` | `isWorkflowRunning()` |
| **Transitional checkpoint** | `adopted` | `isCheckpointState()` — exists only briefly during apply→verify handoff |
| **Terminal** | `idle`, `completed`, `failed` | `isTerminalState()` |

UI/network-mode protection uses `isPhaseActive()` = running **or** checkpoint.

## States

```
IDLE
 │
 ▼
QUEUED
 │
 ▼
APPLYING
 │
 ▼
ADOPTED
 │
 ▼
VERIFYING
 │
 ▼
COMPLETED
```

Terminal / recovery:

- `VERIFYING` → `FAILED`
- `APPLYING` → `FAILED`
- `ADOPTED` → `FAILED` (checkpoint timeout if verification handoff never starts)
- `ADOPTED` → `VERIFYING` (automatic via `resumeAdoptedCheckpoint()` on poll/kickstart/boot)
- `FAILED` → `IDLE` (explicit reset via `resetToIdle()` from `failed` or `completed` only)
- `FAILED` / `COMPLETED` → `IDLE` (administrative cleanup — **not** while `queued`/`applying`/`adopted`/`verifying`)
- Corrupt persisted state → `IDLE` (`forceResetCorruptState()` on load)
- Factory reset / reconfigure → `resetPersisted()` (always allowed)

**Active workflow protection:** `resetToIdle()` rejects `queued`, `applying`, `adopted`, and `verifying`. UI paths (e.g. network-mode POST) must not clear an in-flight adoption job.

## Allowed transitions

| From       | To         | Trigger |
|------------|------------|---------|
| `idle`     | `queued`   | `enqueueAdoption()` |
| `queued`   | `applying` | `processQueued()` / `poll()` / `kickstartJob()` |
| `applying` | `adopted`  | apply success (checkpoint; immediately handoffs to `verifying`) |
| `applying` | `verifying`| `transitionAdoptedHandoff()` — apply success then immediate `beginVerification()` |
| `applying` | `failed`   | adopt error, invalid payload, missing request |
| `adopted`  | `verifying`| `beginVerification()` or `resumeAdoptedCheckpoint()` |
| `adopted`  | `failed`   | checkpoint timeout (`ADOPTION_VERIFY_STALL`) or superseded enqueue |
| `verifying`| `completed`| `completeVerification(success)` |
| `verifying`| `failed`   | `completeVerification(fail)` or verification error |
| `failed`   | `idle`     | `resetToIdle()` |

All other transitions are **rejected** and logged as:

```
[adoption] illegal transition rejected: <from> -> <to>
```

This prevents subtle bugs such as `queued` → `completed`, `adopted` → `applying`, or jumping backward in the chain.

## POST `/api/setup/router/existing-network/adopt` pipeline

The HTTP handler performs three **separate** operations (no mixed responsibility):

1. **`enqueueAdoption()`** — persist job, transition `idle` → `queued`
2. **`kickstartJob()`** — run `processQueued()` until stable (`applying` / `adopted` / `failed`)
3. **`snapshot(jobId)`** — read-only status for the HTTP response

If enqueue fails → HTTP 409 with explicit error code.  
If snapshot unavailable after kickstart → HTTP 500.  
If kickstart ends in `failed` → HTTP 500 with failure payload.

## Verification handoff

- `transitionAdoptedHandoff()` — after apply success: `adopted` → `verifying` in one backend pass (checkpoint is not a resting state)
- `resumeAdoptedCheckpoint()` — recovery only for legacy/crash-interrupted `adopted` persistence
- `begin-verify` — `adopted` → `verifying` (idempotent if already `verifying` or `completed`)
- `complete-verify` — `verifying` → `completed` (+ installation `Provisioned` when needed)
- `fail-verify` — `verifying` → `failed`

## UI mapping (setup wizard)

| State      | Wizard step   | User-visible progress |
|------------|---------------|------------------------|
| `queued`   | `applying`    | Preparing... |
| `applying` | `applying`    | Applying Configuration... |
| `adopted`  | `verifying`   | Configuration adopted. → begin-verify |
| `verifying`| `verifying`   | Verifying... |
| `completed`| `complete`    | Setup Complete panel |
