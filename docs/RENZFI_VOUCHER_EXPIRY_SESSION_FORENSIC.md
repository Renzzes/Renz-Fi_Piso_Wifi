# Renz-Fi Voucher Expiry / RouterOS Session Authorization Forensic

**Date:** 2026-08-15  
**Mode:** SOURCE-ONLY AUDIT — **NO VOUCHER CODE CHANGES, NO FLASH, NO ROUTEROS CHANGES**  
**Related:** `docs/RENZFI_UPTIME_LIMIT_FORENSIC.md`, `docs/RENZFI_SESSION_DESYNC_EXPIRY_FORENSIC.md`, coin Model B remediation (grace removed: `new_limit = existing_uptime + requested_seconds`)

**Classification standard:** PROVEN | STRONGLY INDICATED | UNPROVEN | RULED OUT

---

## 1. Executive Summary

The appliance voucher pipeline **does** generate codes, require NTP to redeem, bind a MAC, authorize a Hotspot user via the **same** `createHotspotUser` / `active/login` path as coin, and can eventually deauthorize through `ExpireSession` → `deauthorizeUser` (`active/remove` + `user/remove` + cookies).

It does **not** implement the frozen business contract of **absolute calendar expiry from redemption T0**.

| Contract expectation | Current source |
|---|---|
| Redeem at T0 → service ends at T0 + validity | Service end `serviceExpiresAt` is set at **first successful activate**, not redeem |
| Validity as calendar days | Admin stores **`minutes`** (duration), plus optional **Redeem Before** date |
| At absolute expiry, Active empty | Tick clamp + ExpireSession only while `sessionState == active` |
| Internet blocked at calendar end | Depends on Active cleanup; leftover user/cookie can still authorize |
| Portal Connected = Internet | Firmware flags + coalesced Active verify; not continuous Active authority |

**One-line verdict:** Voucher Internet is **duration-from-first-login** plus **coin Model B `limit-uptime`**, not true **T0+N days absolute** RouterOS enforcement.

Coin Model B **must not** be treated as the voucher calendar contract. They currently share the same Hotspot activate path — that is a **gap**, not a feature.

---

## 2. Voucher Business Contract (frozen)

Example:

```text
Price:    ₱100
Validity: 3 days
Speed:    selected Promo Rate

Redeemed: 2026-08-14 12:00:00
Expires:  2026-08-17 12:00:00
```

At expiry **all** must become true:

- `/ip hotspot active` empty for that voucher session  
- Internet blocked  
- Voucher status expired / not redeemable  
- Portal must not claim authorized Connected  

UI expiry alone is insufficient. **Active** is the Internet-authority boundary. **Host ≠ Active.**

---

## 3. Current Voucher Data Model

**PROVEN.** Schema v2, JSON array at `StoragePaths::VouchersFile` (`/vouchers/vouchers.json`).  
`VoucherManager.cpp` `generate` / `normalizeRecord`.

| Field | Created | Updated | Authoritative for | Survives reboot |
|---|---|---|---|---|
| `code` | generate | — | Identity | Yes |
| `amount` | generate (₱) | — | Sale price | Yes |
| `minutes` | generate | — | Service **duration** (minutes) | Yes |
| `status` | `unused` | reserve / markActivated / expire / disable / archive | Lifecycle | Yes |
| `expires` / `validUntil` | generate (“Redeem Before”) | expire on deadline | **Code shelf life**, not service end | Yes |
| `boundMac` | reserve | — | Device bind | Yes |
| `sessionId` | reserve | — | Session link | Yes |
| `redeemedAt` | reserve | — | Redeem wall time | Yes |
| `activatedAt` | markActivated | — | First successful authorize | Yes |
| `serviceExpiresAt` | markActivated | — | Service end ISO (from **activate**) | Yes |
| `profileName` | generate (optional) | — | Hotspot profile name | Yes |
| `speed` | generate (optional) | — | Display only | Yes |
| `terminalReason` / `updatedAt` / `archivedAt` | terminal transitions | — | Audit | Yes |

**No** `validDays` field. A “3-day” product is encoded as `minutes = 4320` unless the admin enters that manually.

Portal session also stores `serviceExpiresEpoch` (RAM/SD session JSON) for tick clamp.

---

## 4. Admin Creation Flow

**PROVEN.** Admin `VouchersPage` / `GenerateDialog` → `POST /api/vouchers` → `VoucherManager::generate`.

| Control | Stored | Role |
|---|---|---|
| Amount ₱ | `amount` | Revenue |
| Minutes | `minutes` | Service length |
| Redeem Before | `expires` / `validUntil` | Last day **unused** code may be redeemed |
| Bandwidth Profile | `profileName` | Hotspot `profile=` |
| Display Speed | `speed` | UI / sale metadata |

**When does the service clock start?**

| Event | Service clock |
|---|---|
| Generate | **Not started** (`serviceExpiresAt=""`) |
| Redeem (`reserve`) | **Not started** for service end; sets `secondsLeft = minutes*60` |
| First successful Activate outcome | **Starts** — `serviceExpiresAt = activatedAt + secondsLeft` |

**PROVEN:** intended “redeemed + 3 days” is **not** what source implements. Source is **activated + minutes**. Delayed first login **extends** calendar end.

Promo Rate: voucher path does **not** call `PromoManager::resolveHighestProfileForAmount`. Profile is the admin-typed name (or router default). Changing promos after generate does not rewrite existing vouchers.

---

## 5. Redemption Flow

**PROVEN** (`PortalSessionManager::redeemVoucher` ~1081–1204):

```text
POST /api/portal/voucher/redeem
  → CLOCK_NOT_READY if salesRecordedAtNow() empty
  → reject if live coin session on MAC
  → VoucherManager::reserve (unused→redeeming, bind MAC)
  → upsertSale pending_activation
  → session source=voucher, activating, secondsLeft=minutes*60
  → enqueueActivateSession
       → createHotspotUser (Model B) + active/login
       → markActivated + serviceExpiresAt = activatedAt + seconds
       → connected=true, sessionState=active
```

Differences vs ideal pipeline:

- Absolute expiry from redeem T0 — **missing**  
- Promo table speed resolve — **missing**  
- Separate voucher RouterOS username — **missing** (MAC user)  
- Calendar enforcement independent of Active state — **missing**

Reconnect: `reconnectVoucher` recomputes remaining from `serviceExpiresAt`; if 0 → expire.

---

## 6. RouterOS User Lifecycle

**PROVEN — vouchers share coin MAC Hotspot identity.**

| Attribute | Value |
|---|---|
| Username | `macToHotspotUsername(mac)` (colonless uppercase MAC) |
| Password | Same as username |
| Profile | `session.hotspotProfile` / router default |
| User `limit-uptime` | Model B: `existing_uptime + timeoutSeconds` |
| Active login | `/ip/hotspot/active/login` (or `active/set` if Active exists) |
| Active verify after login | Yes (post-login `active/print`) |
| Rate-limit on user | **Not set** — comes from Hotspot **profile** |
| Persistent user | Yes until deauth removes it |
| Deleted at expiry | Yes **if** `deauthorizeUser` succeeds |

**RULED OUT:** username = voucher code.

---

## 7. Promo / Speed Mapping

**PROVEN:** `profileName` string only. No managed `renzfi-speed-*` creation on voucher redeem. `speed` is display. Wrong profile name → user add/set trap → `activation_error`.

---

## 8. Absolute Expiry Calculation

**PROVEN** (`drainHotspotOutcomes` Activate, voucher branch):

```text
serviceExpiresAt = addSecondsToRecordedAt(activatedAt, voucherSeconds)
serviceExpiresEpoch = time(nullptr) + voucherSeconds
```

`voucherSeconds` = session `secondsLeft` at activate success (typically `minutes * 60`).

Tick (`tickSessions` voucher branch): if wall clock ≥ 2024-01-01 and epoch set, clamp `secondsLeft` to `expiryEpoch - wallNow`.

---

## 9. Expiry Authority

| Clock | Authority for | When it runs |
|---|---|---|
| `serviceExpiresEpoch` / ISO | ESP32 remaining clamp | Only if `Active && !paused` |
| ESP32 `secondsLeft` | Tick decrement | Same gate |
| RouterOS user `limit-uptime` | Cumulative **connected** uptime | While logged in |
| RouterOS cookie + leftover user | Can restore Active without ESP32 | Until user/cookie removed |

**PROVEN conflict:** offline time does not consume RouterOS uptime or ESP32 Active tick; calendar “3 days from redeem” is not enforced. Cookie auto-login can restore Internet with leftover duration — **STRONGLY INDICATED** by architecture comments + cookie retention on pause/activate paths.

**Authoritative for Internet today:** whoever still has Hotspot Active (RouterOS), not voucher JSON alone.

---

## 10. Active Session Enforcement

**PROVEN path when session is Active and seconds reach 0:**

```text
tickSessions → Expiring → ExpireSession
  → onSessionExpired → tryEnqueueDeauthorizeHotspotUser
  → MikroTikDriver::deauthorizeUser
       active/print+remove
       user/print+remove
       cookie remove
  → voucher expire() + completeAccounting
```

**GAP — PROVEN:** no automatic ExpireSession from tick if session is not `active` (e.g. `activation_error`, disconnected). No boot scan of `serviceExpiresAt`.

Host is never queried or cleaned — correct for Host≠Active; incorrect if Active leftover remains.

---

## 11. RouterOS Disconnect Path

| Item | Value |
|---|---|
| Function | `MikroTikDriver::deauthorizeUser` |
| File | `ESP32_S3_Firmware/src/router/drivers/MikroTikDriver.cpp` |
| Trigger | ExpireSession / terminate / reset / administer expire |
| Identity | MAC → Hotspot username |
| Commands | `active/remove`, `user/remove`, cookie remove |

Also: `queryHotspotActivePresent` + `VerifyActive` (≤1 MAC / 60s) can clear ESP32 Connected without removing Active if only verify runs — verify clears local Connected and sets `activation_error`; auto-retry may **re-activate** with Model B if `secondsLeft > 0`.

---

## 12. Portal Connected Authority

**PROVEN:** UI Connected iff `sessionState==="active" && connected && secondsLeft>0` (`renzfi-app.js`).

`connected=true` only after Activate outcome ok (including voucher `markActivated`).

Active verify applies to vouchers (no source exclusion). Lag ≤60s+ worker busy. After `not_active`, auto-retry can re-grant Internet — **GAP** vs calendar end.

Coin Active-verify remediation **does** touch voucher sessions but **does not** implement absolute voucher expiry.

---

## 13. Reboot Recovery

**PROVEN** (`recoverSessionsAfterReboot`):

- Re-queue Activate if activating / resumePending with time left  
- Re-queue Expire if expiring/expired cleanup incomplete  
- **Does not** scan `serviceExpiresAt` for past-due vouchers left non-Active  

Persisted ISO / epoch / secondsLeft survive SD. Until NTP, tick skips epoch clamp.

**GAP:** reboot past calendar end with leftover Hotspot user/cookie → Internet can continue until something enqueues deauth.

---

## 14. NTP / Clock Dependency

**PROVEN** (`SalesTime` + `redeemVoucher`):

| Question | Answer |
|---|---|
| Redeem before NTP? | **No** — `CLOCK_NOT_READY` |
| Existing Active if NTP lost? | Duration clocks continue; epoch clamp skipped if `time()` invalid |
| Reboot invalid clock? | Redeem/reconnect gated; Active sessions not auto-expired by calendar |

No RTC in `SalesTime`. Voucher absolute product **requires** wall clock — source agrees for redeem, not for offline expiry enforcement.

---

## 15. Voucher Reuse Rules

| Case | Expected | Actual | Class |
|---|---|---|---|
| Unused redeem once | SUCCESS | SUCCESS | PROVEN |
| Same MAC again before expiry | REJECT or idempotent reconnect | Idempotent; may reset full minutes if still `redeeming` | PROVEN (nuance) |
| After expiry | REJECT | REJECT once `expire()` ran | PROVEN / GAP if expire never ran |
| User remains after Active gone | Internet blocked | Not guaranteed (cookie/user) | GAP |
| Active removed, user remains | Unusable | Cookie/user may re-auth | STRONGLY INDICATED |

---

## 16. Multiple Concurrent Voucher Sessions

| Scenario | Isolation |
|---|---|
| Same code, two MACs | BoundToAnotherDevice — PROVEN |
| Two codes, two MACs | Separate session rows; worker serializes ROS — PROVEN |
| Two codes, one MAC | One session document overwritten; first voucher may stay `redeeming`/`active` in vouchers.json without cleanup — **GAP** |
| Coin + voucher same MAC | Rejected if coin live — PROVEN |

Expiry of B must not remove A: deauth is **MAC-scoped**. Two vouchers on one MAC share one Hotspot user — **GAP**.

---

## 17. RouterOS Failure Handling

**PROVEN:** enqueue fail → `cleanupRetryPending`; idle retry prefers cleanup; deauth fail → `cleanupRetryCount` up to 3; then stuck. Voucher not recycled to idle.

**GAP:** after retry budget, leftover Active can persist. No slow long-term reaper in source.

---

## 18. Admin Dashboard Visibility

**PROVEN — local JSON only.**

- Vouchers page: `GET /api/vouchers`  
- Active Users: portal session append + legacy users file  
- **Not** live `/ip hotspot active`  

Shows redeem-before `expires`, not necessarily `serviceExpiresAt`. Limitation: dashboard can show expired while Active still exists (or vice versa) until local state catches up.

---

## 19. Sales / Reporting Behavior

**PROVEN:** one sale at redeem (`pending_activation` → `active` → `completed`). Amount unchanged on expire. No refund/duplicate row. Sale `expiresAt` field stores **redeem-before** `validUntil` — naming collision with service end.

---

## 20. Source Map

| Component | Location |
|---|---|
| Schema / generate / reserve / expire | `VoucherManager.cpp` / `.h` |
| Redeem / reconnect / administer / tick / verify | `PortalSessionManager.cpp` |
| Hotspot activate / Model B / deauth | `MikroTikDriver.cpp` |
| Worker Critical jobs | `RouterProvisioningWorker.cpp` |
| APIs | `ApiServer.cpp` `/api/vouchers*`, `/api/portal/voucher/*` |
| NTP | `SalesTime.cpp` |
| Admin UI | `src/pages/VouchersPage.tsx` |
| Portal UI | `portal/renzfi-app.js` |
| Node `server/routes/vouchers.ts` | **Not** appliance runtime |

**MikroTik CPU stability (constraints for any future fix):** Single Router Worker, queue depth 1, Critical hotspot jobs, no ROS from `async_tcp`, Active verify already coalesced 60s — keep it. Do not add per-heartbeat Active polls. Prefer expire-on-calendar enqueue of **one** existing `ExpireSession` job.

---

## 21. Proven Findings

1. Voucher uses MAC Hotspot user + Model B `limit-uptime`.  
2. `serviceExpiresAt` set at **activate**, not redeem.  
3. Admin “validity” is **minutes** (+ optional redeem-before date).  
4. NTP required to redeem.  
5. ExpireSession → `active/remove` path exists.  
6. Tick absolute clamp only while Active.  
7. Portal Connected is firmware-driven; verify applies to vouchers.  
8. Sales complete in-place; no refund.  
9. Admin lists local JSON, not Active.

---

## 22. Strongly Indicated Findings

1. Cookie + leftover user can restore Internet past calendar intent.  
2. Portal reconnect / activation auto-retry can Model-B extend a voucher after Active loss.  
3. Offline days do not consume duration clocks → “3 days” stretches.

---

## 23. Unproven Boundaries

1. Whether RouterOS accepts multi-day `HH:MM:SS` limits in all versions.  
2. Exact cookie auto-login behavior on this Hotspot profile.  
3. Host leftovers after deauth.  
4. NTP step/jump on hardware (no RTC).

---

## 24. Regression Risks (vs coin Model B)

| Change if done wrongly | Risk |
|---|---|
| Apply calendar fix to coin path | Breaks ₱1=300s Model B |
| Aggressive Active polling | MikroTik CPU / worker starvation |
| Delete Hotspot users globally on voucher expire | Collateral MAC damage |
| Hide Connected in JS only | Leaves Internet authorized |

**Coin Model B (no grace) must remain gated to duration entitlements.** Voucher remediation must key on `source == "voucher"`.

---

## 25. Recommended Remediation Scope (DO NOT IMPLEMENT HERE)

1. Persist `serviceExpiresAt` at **redeem T0** (or true calendar days if product requires).  
2. Do not use coin Model B lifetime cap as voucher calendar authority; deauth at calendar end even if not Active.  
3. `tickSessions` + boot recovery: if voucher past `serviceExpiresAt` → one `ExpireSession` (idempotent).  
4. Block activate/reconnect/auto-retry when wall remaining == 0.  
5. Skip reconnect when already Active+connected (avoid Critical job spam / CPU).  
6. Slow coalesced cleanup retry after budget — still single worker.  
7. Admin: show `serviceExpiresAt`; still no live Active storm.  
8. Leave setup wizard and coin Model B alone.

---

## 26. Hardware Validation Plan (after a future fix — not this pass)

1. Generate ₱100 / 4320 minutes (or calendar product once implemented).  
2. Redeem with NTP ready → capture Active + user print.  
3. Confirm `serviceExpiresAt` equals product clock (redeem vs activate — after fix).  
4. Wait/advance clock to expiry → Active empty, Internet blocked, voucher expired.  
5. Attempt re-redeem → reject.  
6. Reboot before expiry → still authorized; reboot after expiry → still blocked.  
7. Two vouchers two MACs → only expired MAC loses Active.  
8. MikroTik CPU stays healthy (no poll storm).  
9. Coin second-₱1 Model B still: `new_limit = uptime + 300` exactly.

---

## Final checklist (audit acceptance)

| Item | Status |
|---|---|
| Voucher absolute expiry exists | **PARTIAL** — duration from activate + tick clamp, not redeem T0 calendar |
| Expiry survives reboot | **PARTIAL** — fields persist; no past-due Active sweep on boot |
| Expiry is wall-clock based | **PARTIAL** — epoch clamp needs NTP; only while Active |
| Voucher is one-time | **PROVEN** (once terminal) |
| Promo speed applied | **PARTIAL** — profile name only, no promo resolve |
| RouterOS user created | **PROVEN** (MAC user) |
| Active login occurs | **PROVEN** |
| Active verified | **PROVEN** (activate path) |
| Expiry removes Active | **PARTIAL** — only if ExpireSession runs and deauth succeeds |
| Internet blocked | **PARTIAL** — depends on cleanup |
| Portal Connected follows ROS | **PARTIAL** — verify lag / re-auth gaps |
| Expired not redeemable | **PROVEN** if `expire()` ran |
| Concurrent isolation | **PARTIAL** — same-MAC multi-voucher GAP |
| Disconnect failure handled | **PARTIAL** — 3 retries then stuck |
| Admin reflects authorization | **NO** — local JSON only |

**Answer to the acceptance question:**

From owner create → redeem → activate, the customer gets a MAC Hotspot user with Model B duration and a `serviceExpiresAt` stamped at **first successful login**. Internet ends when ESP32, while the session is still **Active**, drives `secondsLeft` to 0 (tick and/or epoch clamp) and successfully runs `deauthorizeUser`. There is **no** proven automatic RouterOS disconnect solely because wall-clock redeemed+validity has passed while the session is offline / non-Active / cleanup-exhausted. That is a **GAP — INTERNET AUTHORIZATION NOT PROVEN TO END AT ABSOLUTE VOUCHER EXPIRY** under the frozen business contract.
