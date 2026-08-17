# Renz-Fi Voucher Expiry Remediation — 2026-08-15

**Status:** Source remediation complete · firmware build SUCCESS · **not flashed**  
**Forensic precursor:** `docs/RENZFI_VOUCHER_EXPIRY_REMEDIATION_FORENSIC_2026-08-15.md`

---

## FILES CREATED

| Path | Purpose |
|---|---|
| `docs/RENZFI_VOUCHER_EXPIRY_REMEDIATION_FORENSIC_2026-08-15.md` | Pre-change forensic |
| `docs/RENZFI_VOUCHER_EXPIRY_REMEDIATION_2026-08-15.md` | This validation report |
| `ESP32_S3_Firmware/tools/voucher-expiry-contract-check.mjs` | Source contract tests (Node) |
| `ESP32_S3_Firmware/tools/voucher-expiry-contract-check.py` | Same checks (Python; optional) |

## FILES MODIFIED

| Path | Change |
|---|---|
| `ESP32_S3_Firmware/src/SalesTime.h` / `.cpp` | `salesAddSecondsToIso`, `salesSecondsUntilIso` |
| `ESP32_S3_Firmware/src/VoucherManager.cpp` | Stamp immutable `serviceExpiresAt` at reserve; never overwrite on activate; legacy normalize |
| `ESP32_S3_Firmware/src/PortalSessionManager.cpp` | Absolute expiry authority; expire when not Active; boot past-due; activate/reconnect/retry gates; profile path unchanged |
| `src/pages/VouchersPage.tsx` | Validity labeling; HotSpot profile select from `/api/router/profiles` |
| `deployment/mikrotik-hotspot/*`, `Final_Build_Portal/*` | Regenerated from existing `portal/` for sync tests only (no portal source edit) |

## FILES DELETED

None.

---

## SOURCE FILES CHANGED

### `ESP32_S3_Firmware/src/SalesTime.cpp` / `.h`
- **Functions:** `salesAddSecondsToIso`, `salesSecondsUntilIso`
- **Reason:** Shared ISO wall-clock arithmetic for redeem stamp + remaining.

### `ESP32_S3_Firmware/src/VoucherManager.cpp`
- **`reserve`:** On first unused→redeeming, set `serviceExpiresAt = redeemedAt + minutes×60` (immutable). Idempotent path normalizes legacy missing expiry from `redeemedAt` or `activatedAt` + minutes (does not invent `redeemedAt`).
- **`markActivated`:** Sets `activatedAt` only; **does not overwrite** existing `serviceExpiresAt`.

### `ESP32_S3_Firmware/src/PortalSessionManager.cpp`
- **`redeemVoucher`:** Entitlement = remaining until `serviceExpiresAt`; refuse if past due.
- **`reconnectVoucher`:** Wall-clock gate; expire if past due.
- **`onSessionActivated`:** Voucher `timeoutSeconds` = remaining wall seconds; past due → one `ExpireSession` (no ROS from tick).
- **`tickSessions`:** Absolute expire for vouchers even when **not** Active → one `ExpireSession`; auto-retry blocked after expiry.
- **`recoverSessionsAfterReboot`:** Past-due vouchers → ExpireSession; never Activate.
- **`drainHotspotOutcomes` (Activate):** Prefer session `serviceExpiresAt`; never recompute from `activatedAt` when present.
- **`enrichSessionCapabilities`:** Sync voucher `secondsLeft` from wall clock; clear Connected view if past due; `timerRunning` requires `connected`.

### `src/pages/VouchersPage.tsx`
- **GenerateDialog:** Profile dropdown from cached RouterOS profiles; validity labeled as minutes-after-redeem (3 days = 4320).

---

## CUSTOMER PORTAL SOURCE CHANGED

**NO** — `portal/` sources were not edited for this remediation.

Generated artifacts were rebuilt solely to clear a pre-existing source↔deployment SHA mismatch so lifecycle tests could run.

## GENERATED PORTAL FILES

Rebuild output (unchanged source content except URL substitution):

- `deployment/mikrotik-hotspot/login.html`
- `deployment/mikrotik-hotspot/status.html`
- `deployment/mikrotik-hotspot/renzfi-style.css`
- `deployment/mikrotik-hotspot/renzfi-app.js`
- `deployment/mikrotik-hotspot/md5.js`
- `deployment/mikrotik-hotspot/admin.html`
- (+ assets copied as usual)
- `Final_Build_Portal/` overlay synchronized

## BAT EXPORT

**Not required** for this task (no customer portal source change; no upload).  
Use `scripts\export-captive-portal.bat` only if preparing a MikroTik upload later.

## MIKROTIK CONFIGURATION CHANGED

**NO**

## ESP32 FIRMWARE CHANGED

**YES** (source + compiled image). **Not flashed.**

## SD DATA CHANGED

**NO** (on-device). Backward-compatible interpretation for existing voucher JSON fields.

## FLASHED

**NO**

---

## PROVEN ROOT CAUSES (fixed)

1. `serviceExpiresAt` stamped at **activation** (`activatedAt + minutes`) instead of **redeem**.
2. Absolute expiry tick only while `sessionState == active`.
3. Boot recovery did not expire past-due vouchers.
4. Auto-retry / reconnect could proceed on `secondsLeft > 0` without calendar gate.
5. Admin profile field was free-text (not bound to cached RouterOS profiles).
6. Validity UI said “Minutes” without stating redeem-start clock (product “3 days” = 4320 minutes).

## STRONGLY INDICATED (documented, not redesigned)

- Same-MAC multi-voucher identity collision (MAC-scoped HotSpot user / session doc).
- Leftover user+cookie can restore Internet only if ExpireSession never runs (mitigated by absolute expire enqueue).

## UNPROVEN (hardware)

- Cookie auto-login behavior on site HotSpot profile.
- Exact Host leftovers after deauth.

## RULED OUT

- Need for continuous Active polling or second Router Worker.
- Coin Model B formula as the voucher calendar bug (shared activate path remains; calendar stamp fixed separately).

---

## FIXES IMPLEMENTED

| Contract | Implementation |
|---|---|
| `serviceExpiresAt = redeemedAt + validity` | `VoucherManager::reserve` stamp; activate preserves |
| Speed = selected existing profile | Admin select → `profileName` → `hotspotProfile` → `user profile=` |
| Expiry when not Active | `tickSessions` → one ExpireSession |
| No reconnect / auto-retry after expiry | Gates in reconnect, activate, retry, boot |
| Portal not Connected after auth ends | `connected` + coalesced VerifyActive + enrich wall gate |
| Coin Model B | **Unchanged** (`new_limit = existing_uptime + requested`) |
| CPU / worker | No new ROS polling; ExpireSession via existing worker only |

## NOT CHANGED

- Coin Model B / grace policy  
- MikroTik configuration  
- RouterOS login protocol  
- W5500 / TWDT  
- RouterWorker architecture (single serialized)  
- Pause / resume / terminate / sales core  
- Captive portal `portal/` sources  
- Continuous Active poll / HTTP→ROS  

---

## VALIDATION

| Check | Result |
|---|---|
| Forensic doc | Present |
| `node ESP32_S3_Firmware/tools/voucher-expiry-contract-check.mjs` | **12/12 PASS** |
| `npm run test:portal:lifecycle` | **30/30 PASS** |
| `pio run -e freenove_esp32_s3_wroom` | **SUCCESS** (~53s) |
| Flash / portal upload / MikroTik edit | **Not done** |

---

## READY FOR HARDWARE VALIDATION

**YES** — after flashing firmware (and deploying admin SPA for Vouchers UI). Do not flash until explicitly requested.

### Suggested hardware matrix

1. Generate ₱100 / 4320 min / named 5 Mbps profile → redeem → confirm `serviceExpiresAt ≈ redeemedAt+3d` and ROS user `profile=`.  
2. Disconnect before expiry → reconnect OK; expiry unchanged.  
3. Wait past expiry offline → one deauth; no reconnect; portal not Connected.  
4. Reboot after expiry → no Internet restore.  
5. Coin ₱1 ×2 → Model B additive 300s usable each; no voucher calendar bleed.
