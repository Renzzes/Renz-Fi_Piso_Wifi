# Renz-Fi Session Clock Hardware Validation — 2026-08-15

**Do not mark the product fixed until this procedure is run on the production ESP32 + MikroTik.**

Firmware flash and portal upload are **operator steps**. This document does not flash.

Customer identity from the defect report: MAC `06:36:E3:2C:C4:E8`, IP `10.20.0.251`.

---

## Serial lines to capture

```text
[session-clock] mac=… granted=… authorizedAtMs=…
  existingUserUptime=… existingUserLimit=… newUserLimit=…
  activeUptime=… activeSessionTimeLeft=… usedActiveSet=…
  routerAuthorizationToPortalCommitMs=…

[activate-latency] … activeLoginSuccessAt=… portalCommitAt=…

[activate] operation=active_set … active_uptime=… requested=… new_active_limit=…
[activate] operation=active_login … session_time_left=…
[router-worker] idle no-router-work
```

Acceptance: `routerAuthorizationToPortalCommitMs` is small (job drain, not minutes).  
If `usedActiveSet=yes`, `new_active_limit` must be `active_uptime + granted`, not `granted` alone.

---

## Procedure

### A. Idle ≥10 minutes

No customer session. No pending Activate/Pause/Cleanup.

**Pass:** Serial shows `[router-worker] idle no-router-work`. Zero `/ip/hotspot/active/print`, `user/print`, `login` from Renz-Fi. MikroTik CPU is not in a Renz-Fi poll spike.

### B. Insert ₱1

**Pass:** Portal shows **Activating…**. `timerRunning` false. Countdown does not run. `connected=false`.

### C. RouterOS Active appears → Connected

**Pass:** WinBox Active row for the MAC, then ESP32 `connected=true` and portal **Connected** immediately after the matching Activate outcome. No optimistic Connected before Active.

### D. Portal timer starts at the same authorization event

**Pass:** `[session-clock]` `authorizedAtMs` is T8 (login/set success), not done-paying and not the first browser GET. Portal countdown starts only after Connected.

### E–F. MikroTik Session Time Left vs portal remaining

Compare WinBox `session-time-left` to portal remaining at Connected + 30 s + 2 min.

**Pass:** Difference is a small bounded tolerance (a few seconds of drain/display), **not** minutes.  
**Fail:** Portal still ~4:00 while WinBox is tens of seconds.

### G. Internet after authorization

**Pass:** Client can reach the Internet as soon as Active exists. Do not wait for Android’s icon.

### H. Android “Login or authentication required”

**Pass:** May linger. Must **not** change Renz-Fi `connected`, generation, or remaining. Do not add polls to satisfy Android.

### I. Active disappears (confirmed)

Remove/expire the Active row while ESP32 still claims Connected (lab only).

**Pass:** Next coalesced Verify (`not_active`) → portal `activation_error`, time preserved, not a transport-fail wipe.

### J. RouterOS transport failure

Unplug/block API while a paid session is Connected.

**Pass:** Entitlement remains. Connected is not cleared on query failure.

### K. New purchase after previous session

Terminate or expire, then buy again.

**Pass:** New `sessionGeneration`. A late Deauth/outcome for the old generation does not kill the new session.

### L. Router reboot

Reboot MikroTik while ESP32 stays up, with and without a paid session.

**Pass:** No RouterOS polling storm. Idle still zero unnecessary API. Paid session reconciles through the existing worker/health path without restarting the entitlement clock from a full new purchase.

### M. ESP32 stability

**Pass:** No Guru Meditation, TWDT reset, stack canary, or heap corruption during A–L.

### N. MikroTik CPU

**Pass:** No sustained CPU spike caused by Renz-Fi idle or heartbeat polling.

### O. Pause / resume / owner terminate (additional)

Customer pause → timer freezes immediately; Active removed; remaining unchanged.  
Resume → Activating until Active; timer restamps from new authorization.  
Owner pause/resume/disconnect on the admin dashboard follow the same session.

**Pass:** No added delay. Portal and WinBox remaining stay aligned after resume. Owner disconnect cannot kill a newer generation.

### P. Voucher (additional)

Redeem a voucher. Remaining follows `serviceExpiresAt`, not coin Model B.

**Pass:** Voucher expiry still absolute. Coin and voucher clocks are not merged.

---

## Decision

| Result | Meaning |
|--------|---------|
| All A–N pass (O–P recommended) | Session clock accepted on hardware |
| Any multi-minute E/F offset | **Not fixed** — capture `[session-clock]` and WinBox Active detail |
| Idle API traffic | **Regress** — stop and restore zero-idle contract |
