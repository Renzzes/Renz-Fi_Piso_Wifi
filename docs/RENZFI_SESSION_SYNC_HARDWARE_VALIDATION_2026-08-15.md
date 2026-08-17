# Renz-Fi Session Sync — Hardware Validation Procedure

**Date:** 2026-08-15  
**Do not mark hardware complete until this checklist is executed on the appliance.**

Source remediation is **not** hardware proof. Do not claim MikroTik/ESP32 “fixed” from compile/tests alone.

---

## Preconditions

- Flash the firmware that contains session-generation + health Activate changes.
- Upload the rebuilt MikroTik portal (`npm run build:mikrotik-portal` artifacts).
- Do **not** change RouterOS HotSpot config for this test.
- Serial connected. Winbox optional (do not treat Profile CPU as Renz-Fi proof).

---

## Sequence

| Step | Action | Pass |
|------|--------|------|
| A | Boot ESP32 | No Guru Meditation / TWDT reset |
| B | Wait for Ethernet + portal API | `10.10.10.2` answers |
| C | Confirm no unnecessary RouterOS polling | No login/`active/print` while idle |
| D | Idle ≥10 minutes | Serial: `idle no-router-work`; MikroTik responsive |
| E | MikroTik still answers Winbox/API | Pass |
| F | Insert ₱1 | Coin credited locally |
| G | Done Paying | `state=activating connected=0` |
| H | Observe Activating on portal | Not Connected; timer not consuming |
| I | RouterOS `/ip/hotspot/active` | Row appears after login ok |
| J | ESP32 `active` + `connected=true` | After matching Activate outcome |
| K | Portal Connected | Only after J |
| L | Timer starts only then | `timerRunning` true |
| M | Internet works | Browse beyond walled garden |
| N | Let session expire | Local remaining → 0 |
| O | Active disappears | `/ip/hotspot/active` empty for that MAC |
| P | Session cleanup | User/cookie per existing policy; **Host may remain** |
| Q | Portal idle / insert coin | Waiting Payment |
| R | Immediate repurchase | New `sessionGeneration` |
| S | Stale cleanup cannot kill new session | No Deauth of new Active |
| T | Reboot MikroTik | ESP32 stays up |
| U | Wait for RouterOS | No login storm |
| V | Insert coin after recovery | Activate proceeds without 15s dwell |
| W | Activation not artificially delayed | No `activate deferred` solely for RECOVERING dwell |
| X | Watch MikroTik CPU | No poll storm (Profile is a confounder) |
| Y | ESP32 serial | No crash / TWDT / Guru Meditation |
| Z | Pause / resume / terminate / add-time | Existing behavior |
| AA | Voucher separately | Absolute expiry unchanged |

---

## Notes

- Host while idle is **not** a paid session. Do not delete Host to “pass” idle.
- Connected is committed only after a matching RouterOS Activate success.
- If `unknown host IP <x>` appears, record the exact IP and whether `/ip/hotspot/host` had that address. Do not hardcode a replacement IP.
