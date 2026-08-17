# Renz-Fi Voucher Expiry Remediation Forensic — 2026-08-15

**Mode:** Forensic complete before implementation  
**Constraints:** Single Router Worker · no ROS from async_tcp · no poll storm · coin Model B unchanged  
**Prior art:** `docs/RENZFI_VOUCHER_EXPIRY_SESSION_FORENSIC.md` (still accurate; verified against current source)

---

## 1. Current architecture

```text
Admin GenerateDialog (minutes + free-text profileName + display speed)
  → POST /api/vouchers → VoucherManager::generate → /vouchers/vouchers.json

Customer redeem
  → POST /api/portal/voucher/redeem
  → salesRecordedAtNow() (NTP gate)
  → VoucherManager::reserve (unused→redeeming, bind MAC)
  → PortalSessionManager session source=voucher, activating
  → RouterWorker ActivateHotspotUser (Critical)
  → MikroTikDriver::createHotspotUser (SAME path as coin: Model B limit-uptime)
  → active/login + verify
  → markActivated: serviceExpiresAt = activatedAt + secondsLeft   ← BUG
```

Internet enforcement authority today is split across:

| Clock | Owner | Role |
|---|---|---|
| `minutes` | voucher JSON | Duration product encoding |
| `redeemedAt` | voucher JSON | Redeem wall time (set, underused) |
| `serviceExpiresAt` | voucher + session | Set at **activate**, not redeem |
| `secondsLeft` | session | Decremented only while Active |
| RouterOS `uptime`/`limit-uptime` | Hotspot user | Coin Model B lifetime cap |
| RouterOS Active | Hotspot | Traffic auth |
| Cookie | Hotspot | Can re-login leftover user |

---

## 2–5. Lifecycles (proven)

### Voucher

`unused` → `redeeming` (reserve) → `active` (markActivated) → `expired`/`disabled`/`archived`

### RouterOS

MAC username → user add/set with Model B limit → active/login → (optional 60s VerifyActive) → deauth: active/remove + user/remove + cookies

### Speed profile

Admin free-text `profileName` + display `speed`. No profile dropdown. Stored on voucher. Copied to `session.hotspotProfile` if non-empty. `createHotspotUser` uses `user.profile` else router.json default. Rate-limit is **inherited from the Hotspot user profile**, not set on the user. Promo table is **not** consulted on voucher path.

### Expiry

Tick clamp of `serviceExpiresEpoch` only when `sessionState==active && !paused`. Then ExpireSession → deauth. **No** absolute expire when not Active. **No** boot scan of past `serviceExpiresAt`.

---

## 6. Every authority / clock

| Field | When set (current) | Should be |
|---|---|---|
| `redeemedAt` | reserve | keep |
| `serviceExpiresAt` | markActivated = activatedAt+minutes | **redeemedAt + minutes×60** immutable |
| `serviceExpiresEpoch` | activate outcome | derived from serviceExpiresAt |
| `secondsLeft` | redeem = full minutes; tick while Active | remaining until serviceExpiresAt |
| Model B limit | every activate | OK as **secondary** ROS duration for remaining wall seconds; ESP32 calendar is authority |

---

## 7–12. Paths

| Path | Behavior | Gap |
|---|---|---|
| Activate | Model B + login | Extends calendar if expiry stamped at activate |
| Deauth | ExpireSession → deauthorizeUser | OK when enqueued |
| Auto-retry activation_error | Re-activate if secondsLeft>0 | Can re-auth past calendar |
| Reconnect | Uses serviceExpiresAt if set | OK if stamp correct; fails if empty |
| Reboot | Re-activate activating; expire only if already expiring | Misses past-due non-Active vouchers |
| Cookie | Cleared on deauth; kept on pause | Leftover user+cookie if expire never runs |
| VerifyActive | 60s coalesce, worker | Clears Connected; auto-retry can re-grant |

---

## 13. Proven root causes

1. **RC-V1:** `serviceExpiresAt` computed from `activatedAt`, not `redeemedAt` (`PortalSessionManager` Activate outcome ~2545–2589; `VoucherManager::markActivated`).  
2. **RC-V2:** Absolute expiry tick only runs while Active (`tickSessions` ~1997–2037).  
3. **RC-V3:** Boot recovery does not expire past-due vouchers (`recoverSessionsAfterReboot` ~2122–2172).  
4. **RC-V4:** Activation auto-retry / reconnect can proceed on `secondsLeft>0` without calendar gate.  
5. **RC-V5:** Admin “validity” UI is **Minutes**, not Days; product “3 days” must be entered as 4320 (UI/product mismatch).  
6. **RC-V6:** Bandwidth profile is free-text; not validated against cached RouterOS profiles (profile may silently fall back to router default if empty).

---

## 14. Strongly indicated

- Leftover Hotspot user+cookie can restore Internet if ExpireSession never runs offline.  
- Same-MAC second voucher overwrites portal session document (MAC-scoped identity).

---

## 15. Unproven (hardware)

- Cookie auto-login on this Hotspot profile.  
- Exact Host leftovers after deauth.

---

## 16. Ruled out

- Missing deauth command sequence (it exists).  
- Coin Model B formula as the voucher calendar bug (shared path is a contributor; calendar stamp is the root).  
- Need for continuous Active polling.  
- Need for second Router Worker.

---

## 17. Exact files/functions responsible

| File | Function | Issue |
|---|---|---|
| `PortalSessionManager.cpp` | Activate outcome voucher branch | Stamps expiry at activate |
| `PortalSessionManager.cpp` | `tickSessions` | No non-Active absolute expire |
| `PortalSessionManager.cpp` | `recoverSessionsAfterReboot` | No past-due voucher expire |
| `PortalSessionManager.cpp` | activation_error retry | No calendar gate |
| `PortalSessionManager.cpp` | `redeemVoucher` / `reconnectVoucher` | Incomplete expiry authority |
| `VoucherManager.cpp` | `reserve` | Does not set serviceExpiresAt |
| `src/pages/VouchersPage.tsx` | GenerateDialog | Minutes label; free-text profile |

---

## 18. Minimum safe remediation

1. On first `reserve`: set immutable `serviceExpiresAt = redeemedAt + minutes*60`.  
2. `markActivated`: set `activatedAt` only; **never overwrite** existing `serviceExpiresAt`.  
3. Activate outcome: use existing `serviceExpiresAt`; recompute epoch/secondsLeft from it.  
4. `tickSessions`: for any voucher (any non-terminal session state) with NTP + past `serviceExpiresAt`/`epoch` → one ExpireSession (idempotent flags).  
5. Boot recovery: same past-due → ExpireSession; never Activate if expired.  
6. Gate activate enqueue / auto-retry / reconnect: if remaining wall ≤ 0 → expire path.  
7. Voucher `timeoutSeconds` for ROS = **remaining wall seconds** (Model B still adds to uptime — grants remaining duration from now; ESP32 calendar still deauths at absolute end). Coin path untouched.  
8. Admin: load profiles from `/api/router/profiles` select; keep minutes storage; label validity clearly (minutes = product duration after redeem).  
9. No new ROS polling; no HTTP→ROS; ExpireSession only via existing worker.

---

## 19. MikroTik CPU stability analysis

| Change | ROS load |
|---|---|
| Absolute expire enqueue | +0–1 Deauth job when deadline hit (already exists for Active path) |
| Boot past-due scan | Local JSON only; may enqueue Deauth once per MAC |
| Profile dropdown | Uses existing cached profiles GET (0 ROS if cache warm) |
| VerifyActive | Unchanged 60s coalesce |

**No** heartbeat/session GET Active print. **No** parallel workers.

---

## 20. Regression risks vs coin Model B

| Risk | Mitigation |
|---|---|
| Changing createHotspotUser | **Do not** — only change `timeoutSeconds` fed for vouchers |
| Shrinking coin entitlement | Coin still uses `secondsLeft` as today |
| Mass-expire legacy vouchers | Legacy: if `serviceExpiresAt` empty and `activatedAt` set, derive activatedAt+minutes; if only `redeemedAt`, derive redeemedAt+minutes on normalize/read; unused unchanged |

---

## Implementation gate

Forensic complete. Proceed to remediation under the constraints above.
