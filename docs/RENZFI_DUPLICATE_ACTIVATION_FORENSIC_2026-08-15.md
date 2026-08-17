# Renz-Fi Duplicate Activation Forensic — 2026-08-15

**Mode:** SOURCE + supplied runtime log. **No flash. No portal upload.**

**Customer:** MAC `06:36:E3:2C:C4:E8`

**This is not a clock-redesign pass.** It explains why one Done Paying produced two `activate-hotspot-user` jobs, why the second sent unsupported `/ip/hotspot/active/set limit-uptime`, and why a RouterOS TRAP became `ok=yes`.

---

## Runtime (authoritative)

**First job (SUCCESS):**

```text
[portal-activate] mac=06:36:E3:2C:C4:E8 job=queued profile= remaining=300
[activate] operation=create ... requested_seconds=300 new_limit=300
/ip/hotspot/active/login
[activate] operation=active_login ... result=ok active_id=*A1400FB session_limit=00:05:00
[activate] operation=create ... existing_uptime=0 requested_seconds=300 new_limit=300 active_authorized=yes
[router-worker] activate-hotspot-user mac=06:36:E3:2C:C4:E8 ok=yes
```

**Second job (~8 s later):**

```text
[router-worker] dispatch type=activate-hotspot-user
[portal-activate] mac=06:36:E3:2C:C4:E8 queued ... remaining=299
[activate] operation=reuse ... existing_id=*5 existing_uptime=0 existing_limit=300 requested_seconds=299 new_limit=300
/ip/hotspot/active/print
/ip/hotspot/active/set
[router-api] TRAP message=unknown parameter limit-uptime
[activate] ... active_authorized=yes
[router-budget] operation=activate ... ok=yes
[router-worker] activate-hotspot-user ... ok=yes
```

`remaining=299` means the first outcome had already been committed (`connected=true`) and the coin clock had advanced by one second (or T8-derived remaining). This is **not** Add Time (that would be 300+300).

GET `/session` and SSE do not call RouterOS or enqueue Activate (`ApiServer.cpp`, `renzfi-app.js`).

---

## Answers

### 1. Why was ActivateHotspotUser queued twice?

`PortalSessionManager::onSessionActivated()` was entered **twice**, and **both** times `tryEnqueueActivateHotspotUser()` succeeded.

There is **no** guard that says: generation N is already `hadRouterAuth && connected` → do not enqueue again.

A second portal `ActivateSession` work item (or `activationRetryPending` idle retry) is enough. `hasPendingActivationUnlocked()` only inspects the **portal deferred queue**, not the in-flight RouterWorker job. So a second enqueue is legal while job 1 is running, and becomes dispatchable the moment job 1 finishes (~8 s, matching the log).

### 2. What function initiated the second enqueue?

The second **dispatch** is always:

```text
processDeferredWork(ActivateSession)
  → onSessionActivated()
    → tryEnqueueActivateHotspotUser()
      → [router-worker] dispatch type=activate-hotspot-user
```

The `ActivateSession` item is created by one of:

| Function | When |
|----------|------|
| `enqueueActivateSession()` | `donePaying` if the first `tryEnqueue` failed; `retryPendingRouterWork`; resume/voucher |
| `retryPendingRouterWork()` | Worker idle + `activationRetryPending` — **does not check Connected** |
| `tickSessions` auto-retry | `activation_error` only — **not this log** (no `auto-retry` line) |
| `recoverSessionsAfterReboot` | Boot leftover `activating` — `generation=0` (bypasses stale-gen check) |

**Proven race that produces two items for one Done Paying:**

```text
onSessionActivated() tryEnqueue FAIL (worker busy or outcome mailbox not empty)
  → activationRetryPending = true
  → enqueueActivateSession()          // queue item A
worker becomes idle
  → retryPendingRouterWork()
    → enqueueActivateSession()        // queue item B  (hasPending may be true — then this is a no-op)
OR item A runs while job 1 is in flight
  → tryEnqueue FAIL
  → activationRetryPending = true again
job 1 completes, drain commits Connected
  → drain does NOT clear activationRetryPending
idle callback
  → retryPendingRouterWork() enqueues ActivateSession
  → onSessionActivated() reads secondsLeft=299
  → second worker job
```

`donePaying` itself calls `onSessionActivated()` **directly** (first `job=queued remaining=300`). It only calls `enqueueActivateSession()` when that returns false. The **second** `job=queued remaining=299` is therefore **not** a second Done Paying. It is a later `onSessionActivated` from deferred work / idle retry.

GET/heartbeat/SSE are **ruled out** as RouterOS initiators.

### 3. Was it the same sessionGeneration?

**Yes (STRONGLY INDICATED).** `remaining` went 300 → 299, `operation=create` then `reuse` of the user just created. Add Time would bump remaining by another 300 and would not `create` first. Generation is not bumped on tick. A new purchase would log a new reserve of 300, not 299.

`onSessionActivated` only ignores a job when `generation != 0 && generation != currentGen`. Tick/boot `ActivateSession` items are often **generation=0**, which **never** fail that check.

### 4. PortalSessionManager state before the second activation?

**PROVEN from `remaining=299` + tick rules:**

- `sessionState=active`
- `connected=true`
- `hadRouterAuth=true`
- `routerAuthPending=false` (cleared on first outcome)
- `secondsLeft≈299`
- timer running

`onSessionActivated` still builds `HotspotUser.timeoutSeconds` from `secondsLeft` and enqueues.

### 5. RouterOS Active state before the second activation?

**PROVEN:** Active `*A1400FB` from the first `active/login` is still present. The second job’s `active/print` finds it and takes the `active/set` branch.

### 6. Why did the second activation select `operation=reuse`?

`createHotspotUser()` `user/print` finds `0636E32CC4E8` created by job 1 (`existing_id=*5`). Reuse + Model B: `existing_uptime=0`, `existing_limit=300`, `requested=299`, `new_limit=300` (never shrink a still-valid cap). User Model B is correct here.

### 7. Why did it call `/ip/hotspot/active/set`?

`loginHotspotActive()` (session-clock pass): if `active/print` returns an `.id`, it **skips** `active/login` and sends `active/set limit-uptime=…`.

That path exists only because a **second** Activate ran against an already-authorized Active.

### 8. Why was `limit-uptime` sent to Active?

The session-clock remediation treated Active `limit-uptime` like user Model B. **This RouterOS build rejects that parameter on `/ip/hotspot/active/set`** (`unknown parameter limit-uptime`). User `limit-uptime` and Active session state are separate objects. Active remaining is `user.limit-uptime − user.uptime` (already proven in `RENZFI_UPTIME_LIMIT_FORENSIC.md`).

### 9. Why was the TRAP converted into `ok=yes`?

**PROVEN — explicit ignore in `MikroTikDriver::loginHotspotActive`:**

```cpp
(void)_routerOs.executeCommand("/ip/hotspot/active/set", setAttrs, 2, setResult);
return true;   // unconditional
```

`RouterOsClient::executeCommandImpl` on TRAP:

- logs `[router-api] TRAP`
- `recordFailure()` (health FSM)
- `disconnectInternal("protocol_error")`
- **returns false**

The driver **discards** that false. `createHotspotUser` then prints `active_authorized=yes`. The worker publishes `ok=yes`. `drainHotspotOutcomes` treats it as success.

RouterOS client did **not** lie. The driver overwrote the failure.

### 10. Can this second activation modify or reset the session clock?

**Yes.** A successful second outcome calls `commitAuthorizedClockUnlocked(authorizedAtMs, grantedSeconds)` with `granted=299` and a **new** T8. That restamps `expiresAtMs` and can move the portal deadline. Even this TRAP path published `ok=yes`, so the portal **did** recommit.

### 11. Can this cause portal / MikroTik timer desynchronization?

**Yes.**

- Second job can restamp ESP32 expiry from 299 “from now” while RouterOS Active has already been running since the first login.
- The TRAP also **disconnects the API session** and records a health failure, even though the worker said success. That can delay later Pause/Resume/Verify and produce further retries.

This is a **second** defect on top of the leftover-Active formula bug. It does not replace that diagnosis; it can recreate an offset after a clean first login.

### 12. Smallest safe fix

1. **Same generation already Connected / `hadRouterAuth` → do not enqueue or run Activate.** (`onSessionActivated`, `enqueueActivateSession`, `retryPendingRouterWork`)
2. **Clear `activationRetryPending` on Activate success.** Drain must not leave a retry armed.
3. **One retry path:** if `enqueueActivateSession` accepts the deferred item, do not also leave `activationRetryPending` for the idle callback.
4. **Never send `/ip/hotspot/active/set limit-uptime`.** If Active already exists after user Model B `user/set|add`, treat Active as already authorized (verify print). Entitlement lives on the **user** object.
5. **Never ignore a RouterOS TRAP** on a command that was actually sent. `executeCommand` false / `trapReceived` → `authorizeUser()==false` → worker `ok=false`.
6. Stamp `sessionGeneration` on tick/boot `ActivateSession` items (generation 0 must not bypass the stale check).

No new poll. No `sleep()`. No extra worker. No user Model B change. No wizard change.

---

## Callers (complete)

| Symbol | Callers |
|--------|---------|
| `tryEnqueueActivateHotspotUser` | `onSessionActivated` only |
| `onSessionActivated` | `donePaying` (direct); `processDeferredWork(ActivateSession)` |
| `enqueueActivateSession` | `donePaying` (only if direct enqueue failed); `resume`; `redeemVoucher`; `reconnectVoucher`; `retryPendingRouterWork` |
| `ActivateSession` work | those + `tickSessions` auto-retry + `recoverSessionsAfterReboot` |
| `activate-hotspot-user` log | `RouterProvisioningWorker::tryEnqueueActivateHotspotUser` / `runOp` |

HTTP GET `/session`, POST `/heartbeat`, and SSE **never** appear in this list.

---

## What must not be claimed yet

Hardware session-clock validation is **blocked** until:

- one Done Paying → one Activate job for that generation
- no `active/set limit-uptime`
- TRAP cannot become `ok=yes`
