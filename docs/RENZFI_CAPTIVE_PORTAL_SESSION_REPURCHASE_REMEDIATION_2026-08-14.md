# Renz-Fi Captive Portal Session Re-Purchase — Remediation Report

**Date:** 2026-08-14  
**Incident:** `docs/incidents/RENZFI_CAPTIVE_PORTAL_SESSION_REPURCHASE_INCIDENT_2026-08-14.md`  
**Forensics:** `docs/RENZFI_UPTIME_LIMIT_FORENSIC.md`, `docs/RENZFI_SESSION_DESYNC_EXPIRY_FORENSIC.md`  
**Hardware validation:** **NOT CLAIMED** — source build + portal tests only

---

## 1. Root cause summary

Renz-Fi mapped ESP32 `secondsLeft` directly to RouterOS HotSpot user `limit-uptime` on a **persistent MAC username** (`0636E32CC4E8`). RouterOS treats `limit-uptime` as a **cumulative lifetime cap** against user `uptime`, not “minutes from now.” After the first 5 minutes (`uptime=5m`, `limit-uptime=5m`), Active disappears but the user remains. A second ₱1 purchase set `limit-uptime=5m` again and `/ip/hotspot/active/login` failed with `your uptime limit is reached`.

Separately, ESP32 `connected` / portal Connected did not verify `/ip/hotspot/active`, and overlapping GET `/session` responses could rebase the browser countdown.

---

## 2. Source map (pre-edit)

| # | Concern | File |
|---|---|---|
| 1 | RouterOS user activation | `ESP32_S3_Firmware/src/router/drivers/MikroTikDriver.cpp` → `createHotspotUser` |
| 2 | RouterOS active authorization | same → `loginHotspotActive` |
| 3 | Expiration / deauth | `PortalSessionManager` ExpireSession → `deauthorizeUser` |
| 4 | Session manager | `ESP32_S3_Firmware/src/PortalSessionManager.cpp` |
| 5 | Frontend session sync | `portal/renzfi-app.js` → `fetchSession` / `syncSessionFromServer` |
| 6 | Frontend countdown | `portal/renzfi-app.js` → `anchorSession` / `displaySeconds` |
| 7 | Captive login | `portal/login.html` |
| 8 | MikroTik portal build | `scripts/build-mikrotik-portal.mjs` |
| 9 | Deployment / export | `deployment/mikrotik-hotspot/`, `scripts/export-captive-portal.bat` → `C:\Captive_Portal_BAT` |
| 10 | HotSpot status behavior | MikroTik servlet; Renz-Fi `portal/status.html` overlay (not ESP32 `/status`) |

---

## 3. Files changed

### Firmware

- `ESP32_S3_Firmware/src/router/drivers/MikroTikDriver.cpp` / `.h`
- `ESP32_S3_Firmware/src/router/IRouterDriver.cpp` / `.h`
- `ESP32_S3_Firmware/src/router/RouterPlatform.cpp` / `.h`
- `ESP32_S3_Firmware/src/RouterProvisioningWorker.cpp` / `.h`
- `ESP32_S3_Firmware/src/PortalSessionManager.cpp` / `.h`

### Portal (canonical `portal/` only)

- `portal/renzfi-app.js`  
  Regenerated: `deployment/mikrotik-hotspot/`, `Final_Build_Portal/`, exported `C:\Captive_Portal_BAT`

### Docs

- `docs/incidents/RENZFI_CAPTIVE_PORTAL_SESSION_REPURCHASE_INCIDENT_2026-08-14.md`
- `docs/RENZFI_CAPTIVE_PORTAL_SESSION_REPURCHASE_REMEDIATION_2026-08-14.md` (this file)

---

## 4. Functions changed

| Function | Change |
|---|---|
| `MikroTikDriver::parseRouterOsDurationSeconds` | **New** — parse `5m` / `1h2m3s` / `00:05:00` |
| `MikroTikDriver::createHotspotUser` | **Model B:** `new_limit = existing_uptime + requested_seconds` (no grace); explicit `operation=create\|reuse` logs |
| `MikroTikDriver::loginHotspotActive` | Verify Active row after login; clearer logs |
| `MikroTikDriver::queryHotspotActivePresent` | **New** — Active print only |
| `RouterPlatform::queryHotspotActivePresent` | **New** wrapper |
| `IRouterDriver::queryHotspotActivePresent` | Default false |
| `RouterProvisioningWorker::tryEnqueueVerifyHotspotActive` | **New** Critical job |
| `PortalSessionManager::maybeEnqueueActiveVerify` | **New** — ≤1 MAC / 60s while Connected |
| `PortalSessionManager::drainHotspotOutcomes` | Handle `VerifyActive` → `activation_error`, preserve `secondsLeft` |
| `renzfi-app.js` `fetchSession` / `applyFetchedSession` | Session sync generation — drop stale GETs |
| `renzfi-app.js` `startCoinSessionAPI` | Invalidate in-flight GET; throw on invalid payload |
| `renzfi-app.js` `handleInsertCoin` | Surface API `code`/`message`; don’t treat business errors as outage |

---

## 5. Exact old behavior

```text
timeoutSeconds = secondsLeft
limit-uptime   = format(timeoutSeconds)   // e.g. 00:05:00
user/set or user/add
active/login
```

On reused user with `uptime=5m`: `limit-uptime=5m` → trap.

Activation success did not require a post-login Active row.  
GET `/session` responses could apply out of order.  
Connected was ESP32-only.

---

## 6. Exact new behavior

### Entitlement (Model B)

```text
user/print → read uptime (and existing limit)
new_limit  = uptime + secondsLeft
user/set|add limit-uptime = new_limit
active/login|set (session remaining = secondsLeft)
after login: active/print must show a row or activation fails
```

Example (second ₱1 after exhausted 5m):

```text
existing_uptime = 300
requested       = 300
new_limit       = 300 + 300 = 600
usable from now = 600 - 300 = 300s  (exactly the purchase)
```

Add Time while Active: ESP32 already does `existingRemaining + purchasedSeconds` in `donePaying` (verified current source). RouterOS user cap becomes `uptime + newTotalRemaining`; Active `limit-uptime` set to remaining.

### False Connected

- Activation cannot succeed without Active present after login.
- Every ~60s, one Connected MAC may get a worker `verify-hotspot-active`.
- If query succeeds and Active is missing → `connected=false`, `activation_error`, **secondsLeft preserved**, UI “RETRY INTERNET”.
- Query/transport failure does **not** clear Connected.

### Countdown

- `sessionSyncGen` per GET `/session`; only the latest generation applies.
- Coin-window open bumps gen so stale GET cannot wipe the modal.

### Coin session error

- Backend success path unchanged.
- Frontend shows appliance error/`code` instead of a generic outage when the ESP32 answered.

---

## 7. Why this fixes cumulative RouterOS uptime

RouterOS remaining time is `limit-uptime − uptime`. Setting `limit-uptime = uptime + entitlement` restores exactly the purchased usable window on a reused user without manual `reset-counters` or deleting the user.

---

## 8. How additional purchases accumulate

`PortalSessionManager::donePaying` already:

```text
addTime ? existingRemaining + purchasedSeconds : purchasedSeconds
```

That value is `timeoutSeconds` → Model B user limit. No overwrite bug in current firmware for Active/Paused/Activating/ActivationError with remaining time.

---

## 9. How stale portal Connected is prevented

1. Activate outcome only after Active verify.  
2. Coalesced worker Active check while claiming Connected.  
3. UI still requires `sessionState==="active" && connected && secondsLeft>0`.

---

## 10. How countdown synchronization was fixed

Overlapping GET `/session` responses with older generations are ignored. The single `mainTimer` + deadline model is unchanged (no second countdown loop).

---

## 11. How “Could not start coin session” handling was fixed

- Invalid/normalized-null responses throw a clear error.  
- Successful open invalidates in-flight GET races.  
- Business error codes from the appliance are shown; they no longer force the “service unavailable” banner.

---

## 12. How automatic Android captive portal was preserved

**Not changed:**

- MikroTik HotSpot topology / guest subnet / walled garden  
- `login.html` as unauthenticated captive entry  
- Production portal still MikroTik-hosted → ESP32 `/api/portal/*`  
- No new `window.location` /status redirects  
- No CHAP / client-side HotSpot login reintroduced  

`portal/status.html` remains the Renz-Fi shell for authenticated HotSpot `/status` overlay uploads (existing design).

---

## 13. What was done about manual `/login` and `/status`

**Documented only — no HotSpot servlet redesign.**

| Flow | Behavior |
|---|---|
| Android “Login or Authentication Required” | Unauthenticated interception → `login.html` — **must keep working** |
| Manual `http://10.20.0.1/login` while authenticated | MikroTik authenticated servlet path — may go to status |
| `http://10.20.0.1/status` / `wifi.renz-fi.local/status` | HotSpot status servlet + dns-name — **not** generated by `renzfi-app.js` |
| Exact why `.local` may be unreachable | **Still NOT PROVEN** — needs live HTTP/DNS capture |

Operators should upload the rebuilt overlay from `C:\Captive_Portal_BAT` (includes `status.html`) so authenticated `/status` serves Renz-Fi UI when the servlet serves that file.

---

## 14. What was intentionally NOT changed

- Setup wizard  
- Promo ₱1 = 5 minutes  
- Firewall / NAT / HotSpot profile idle/keepalive  
- ESP32 production `/portal` as customer entry  
- Legacy `Captive Portal/` tree (not deleted)  
- Browser-as-authority for Internet  
- Manual `reset-counters` as production “fix”  
- Aggressive Active polling from browser or async_tcp  

---

## 15. Build / test results

| Check | Result |
|---|---|
| `pio run -e freenove_esp32_s3_wroom` | **SUCCESS** (~63s) |
| `router-api-transport-check.py` | **OK** |
| `pause-resume-contract-check.py` | **8/8** |
| `npm run build:mikrotik-portal` | **OK** |
| `export-captive-portal.bat` → `C:\Captive_Portal_BAT` | **OK** |
| `test-portal-session-lifecycle.mjs` | **30/30** |

---

## 16. Remaining hardware-only validation

Flash firmware + upload `C:\Captive_Portal_BAT` to MikroTik `hotspot/`, then:

1. First ₱1 → Active present → Internet  
2. Natural expire → Active empty → Internet blocked  
3. Second ₱1 **without** Wi-Fi forget / user delete → Active + Internet  
4. Add Time while Active → cumulative remaining  
5. After Active loss mid-session → portal leaves Connected (verify path / RETRY)  
6. Countdown does not oscillate 00:50 ↔ 01:00 from stale GETs  
7. Android captive still opens `login.html`  
8. Serial should show e.g.  
   `[activate] operation=reuse ... existing_uptime=300 requested_seconds=300 new_limit=600 active_authorized=yes`

**Do not claim production fixed until those hardware steps pass.**

---

## Logging (new)

```text
[activate] operation=reuse|create mac=... username=... existing_uptime=...
           existing_limit=... requested_seconds=... new_limit=...
[activate] operation=active_login ... result=ok|fail ...
[activate] operation=verify_active mac=... query_ok=... present=...
[portal-verify] mac=... queued
[portal-verify] mac=... active=missing -> activation_error
```
