# Renz-Fi Captive Portal — Production Incident & Forensic Root-Cause Report

**Incident Date:** 2026-08-14  
**System:** Renz-Fi ESP32-S3 + W5500 + MikroTik HotSpot  
**Firmware:** Production Renz-Fi firmware  
**Affected Component:** Customer captive portal / HotSpot authorization / session lifecycle  
**Severity:** High  
**Status:** Root cause identified — remediation implemented in source (hardware validation pending)  
**Forensic Mode:** Source + live RouterOS evidence + live ESP32 logs  
**Code Modified During Investigation:** No (investigation phase). Remediation is a separate implementation pass.

---

## 1. Executive Summary

A production Renz-Fi customer session exhibits multiple related failures:

1. A customer purchases ₱1 and correctly receives 5 minutes.
2. The session expires after approximately 5 minutes.
3. MikroTik `/ip hotspot active` becomes empty.
4. The customer's `/ip hotspot user` remains present.
5. The RouterOS user retains `uptime=5m` and `limit-uptime=5m`.
6. The customer remains associated with Guest Wi-Fi and remains visible under `/ip hotspot host`.
7. The customer can still reach the Renz-Fi portal/API (walled garden to `10.10.10.2`).
8. The portal can continue displaying local ESP32 session state even though MikroTik no longer authenticates the customer.
9. A subsequent ₱1 purchase is accepted by coin/ESP32 accounting.
10. Renz-Fi attempts to authorize again.
11. RouterOS rejects the reused HotSpot account because cumulative `uptime` already reached `limit-uptime`.
12. The customer receives no Internet despite inserting another coin.
13. The portal can display Connected / countdown because ESP32 and RouterOS are not one authoritative state.
14. The countdown may appear to move backward because overlapping GET `/session` responses rebase the browser deadline.
15. Manually opening `http://10.20.0.1/status` is a **separate** MikroTik HotSpot servlet flow from Android captive detection → `login.html`.

**Primary authorization failure:** RouterOS cumulative-accounting mismatch.  
**Captive re-entry behavior:** separate HotSpot servlet concern — must not break the working Android captive path.

See also:

- `docs/RENZFI_UPTIME_LIMIT_FORENSIC.md`
- `docs/RENZFI_SESSION_DESYNC_EXPIRY_FORENSIC.md`
- `docs/RENZFI_AUTHENTICATED_PORTAL_REENTRY_FORENSIC.md`
- `docs/RENZFI_CAPTIVE_PORTAL_SESSION_REPURCHASE_REMEDIATION_2026-08-14.md`

---

## 2–28. Full incident narrative

The complete production narrative (topology, live RouterOS evidence, dual clocks, countdown, `/status` vs captive, validation matrix) is the operator brief captured in the remediation task and the linked forensics above.

### Decisive live RouterOS state

```text
name="0636E32CC4E8"
limit-uptime=5m
uptime=5m
Active = EMPTY
Host   = PRESENT (10.20.0.251)
Cookie = none
```

### Decisive contract error (pre-fix)

```text
ESP32 secondsLeft  →  RouterOS limit-uptime
```

treated as “minutes from now” while RouterOS treats `limit-uptime` as a **lifetime cap** against cumulative `uptime`.

### First purchase works

```text
uptime=0, limit-uptime=5m → 5m available
```

### Second purchase fails

```text
uptime=5m, limit-uptime=5m → uptime >= limit → active/login trap
```

---

## 29. Incident Status

**PRIMARY ROOT CAUSE IDENTIFIED.**

Transition: repeated reproduction → **source remediation + targeted hardware validation**.

No further generic captive-portal testing is justified before validating the entitlement/accounting fix on hardware.
