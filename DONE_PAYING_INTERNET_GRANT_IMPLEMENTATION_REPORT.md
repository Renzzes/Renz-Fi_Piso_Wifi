# Done Paying → Internet Grant — Implementation Report

**Date:** 2026-08-11  
**Primary acceptance criterion:** Real customer device clicks Done Paying and receives Internet on the MikroTik hotspot.  
**Constraint:** No architecture redesign. Preserve RouterWorker, PortalSessionManager, RouterProvisioningEngine, RouterOS command budget, MikroTik CPU stability.

---

## 1. Verdict — where the lifecycle stopped

Forensic proof (unchanged):

```text
Done Paying → PortalSessionManager → RouterWorker → ActivateHotspotUser
  → ensureProductionRouterCredentials()
  → syncProductionRouterCredentials()
  → resolveRouterCredentials(Persisted)
  → FAIL: "Saved MikroTik connection is unavailable"
  → RouterOS login NEVER attempted
```

Root chain:

1. Blank SD seeds empty `router.json` username/password (expected).
2. Production activation depends on setup writing verified `router-connection.json` and syncing `router.json`.
3. Owner setup TWDT reboot prevented that synchronization from completing.
4. Activation therefore aborted at **credential reconciliation**, before any RouterOS API call.

Secondary gaps (this change set):

| Gap | Effect |
|---|---|
| Generic portal status | Customer saw “Activation failed” without the exact reason |
| Outcome mailbox drop | Could leave session permanently in `activating` |
| Fast-retry budget exhausted | Paid time preserved but no ongoing / customer-visible retry |
| No Activating watchdog | Hang with no HotspotOutcome never recovered |

---

## 2. What this implementation changes

### A. Prerequisite (already shipped this session): Owner TWDT fix

See `OWNER_SETUP_TWDT_IMPLEMENTATION_REPORT.md`.

Without owner setup completing, Internet grant cannot succeed. That fix unblocks:

Owner create → Router save (`connectionVerified=true`) → `syncProductionRouterCredentials` → activation reconcile OK → RouterOS login.

### B. Activation reliability & observability (this change set)

| File | Change | Why safe |
|---|---|---|
| `PortalSessionManager.h/.cpp` | 45 s Activating watchdog → `activation_error` with exact timeout reason; preserve `secondsLeft` | Prevents permanent Activating; 0 RouterOS cmds |
| `PortalSessionManager.cpp` | After fast-retry budget, 60 s cooldown then reset attempts | Continues recovery when credentials appear; spaced to protect MikroTik CPU |
| `PortalSessionManager.cpp` | `canResume=true` on `activation_error` with time left | Uses **existing** `resume()` re-queue path — no new API |
| `PortalSessionManager.cpp` | Track `activationStartedAt` | Watchdog only |
| `RouterProvisioningWorker.cpp` | Stage logs for credentials + worker job | Proves where lifecycle stops on serial |
| `RouterProvisioningWorker.cpp` | Outcome mailbox: newest wins if full | Never drop latest Activate result |
| `RouterProvisioningWorker.h` | `reason[72]` → `reason[128]` | Exact reasons less truncated |
| `portal/renzfi-app.js` | Status shows **exact** `activationErrorReason` | No silent/generic failure UI |
| `portal/renzfi-app.js` | **RETRY INTERNET** button via resume path | Customer retry without losing purchased time |

`Final_Build_Portal` regenerated via `npm run build:mikrotik-portal` (source: `portal/` only).

---

## 3. Lifecycle with logging (proof points)

Expected serial stages on success:

```text
[portal-activate] mac=… job=queued …
[activate] stage=worker_job mac=… remaining=…
[activate] stage=credentials OK (cached|reconciled)
[activate] router step 1 — load credentials
[activate] router step 2 — open RouterOS session
[activate] router step 3 — print existing hotspot user
[activate] router step 4 — update|add hotspot user
[activate] router step 5 — authorize active
[activate] stage=worker_done ok=yes
[portal-activate] mac=… ok=yes
```

If credentials missing:

```text
[activate] stage=credentials FAIL reason=Saved MikroTik connection is unavailable
[portal-activate] mac=… ok=no reason=Saved MikroTik connection is unavailable
```

Portal status line shows that exact string; **RETRY INTERNET** remains available.

---

## 4. Recoverable state contract

| Event | Credits / time | Session state | Customer action |
|---|---|---|---|
| Enqueue fails before job | Credits **restored** | Waiting / prior | Done Paying again |
| RouterOS / credential fail | `secondsLeft` **kept**, credits stay 0 | `activation_error` | Auto-retry + **RETRY INTERNET** |
| Activating timeout (45 s) | `secondsLeft` **kept** | `activation_error` | Same |
| Success | Time runs | `active` / Connected | Browse |

Activation never clears purchased entitlement on RouterOS failure.

---

## 5. Impact analysis

| Area | Impact |
|---|---|
| RouterOS command count | **Unchanged** per activation attempt (~4 cmds in one API session) |
| MikroTik CPU | No extra polling; retries spaced 20 s / 60 s cooldown |
| RouterWorker architecture | Unchanged (mailbox publish hardened only) |
| PortalSessionManager architecture | Unchanged (watchdog + capability flag only) |
| RAM | `HotspotOutcome.reason` +56 bytes; one session field |
| Flash | Production build: **91.4%** (2 396 903 / 2 621 440); RAM **32.4%** |
| AsyncTCP | No new HTTP blocking on activation path |
| Portal upload | **Required** — redeploy `Final_Build_Portal` to MikroTik |

---

## 6. Build / automated validation

| Check | Result |
|---|---|
| `freenove_esp32_s3_wroom` | SUCCESS |
| `npm run build:mikrotik-portal` | SUCCESS → `Final_Build_Portal/` |
| `npm run test:portal:lifecycle` | SUCCESS |

Hardware Internet grant is **not** claimed by this report. Acceptance requires the checklist below on a real Guest SSID client.

---

## 7. Hardware validation checklist (acceptance)

Flash this firmware + upload regenerated `Final_Build_Portal` after completing setup (owner + verified router save).

- [ ] Complete owner setup without TWDT reboot  
- [ ] Router connection verified; `router-connection.json` has `connectionVerified: true`  
- [ ] `router.json` has non-empty username/password (`credentialSyncOk`)  
- [ ] Customer joins Guest SSID → captive portal  
- [ ] Insert coin(s) → credits & minutes update immediately  
- [ ] Done Paying → status **Activating…**  
- [ ] Serial shows credential OK → RouterOS login → hotspot authorize  
- [ ] Portal → **Connected**; countdown starts (firmware-owned)  
- [ ] Customer browses Internet successfully  
- [ ] Time expires → disconnect → Waiting for Payment  
- [ ] Optional: force credential failure → exact reason shown → RETRY INTERNET restores access after fix  

**This implementation is successful only when items through “browses Internet successfully” are proven on hardware.**

---

## 8. Remaining limitations

1. Hardware proof is still outstanding on the field unit.  
2. If setup never saves a verified router connection, activation correctly fails with `"Saved MikroTik connection is unavailable"` — fix is complete setup, not a silent credential bypass.  
3. Long MikroTik trap strings may still truncate at 128 chars.

---

## 9. Files modified (this Internet-grant pass)

- `ESP32_S3_Firmware/src/PortalSessionManager.h`
- `ESP32_S3_Firmware/src/PortalSessionManager.cpp`
- `ESP32_S3_Firmware/src/RouterProvisioningWorker.h`
- `ESP32_S3_Firmware/src/RouterProvisioningWorker.cpp`
- `portal/renzfi-app.js` → regenerated `Final_Build_Portal/`

Related prerequisite: owner TWDT files listed in `OWNER_SETUP_TWDT_IMPLEMENTATION_REPORT.md`.
