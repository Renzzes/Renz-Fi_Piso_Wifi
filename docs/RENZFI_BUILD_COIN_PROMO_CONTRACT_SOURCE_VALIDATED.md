# Build Record — Coin/Promo Contract Correction

**Status:** SOURCE VALIDATED · HARDWARE UNVALIDATED  
**Date:** 2026-08-12  
**Scope isolation:** Keep this firmware/portal artifact **separate** from the prior hardware-validated stability build.

---

## Classification

| Field | Value |
|-------|--------|
| Change class | Coin / promo **time accumulation contract** correction only |
| Source validation | Pass (unit/table tests + `pio run -e freenove_esp32_s3_wroom`) |
| Hardware validation | **Not performed** — do not treat as production-proven until physical test |
| Relation to prior build | **Additive contract fix** on top of earlier work; flash/test as its own candidate |

---

## What this build changes

- Each physical coin insertion is resolved **independently** (exact denomination → minutes).
- `purchasedMinutes = Σ(minutes granted by each insertion)`.
- Never `resolvePromo(totalCredits)` / never greedy decompose of the accumulated peso total.
- Legacy missing-`purchasedMinutes` paths use arithmetic `credits * 5` only (not promo re-resolution).
- Hotspot **profile** selection on Done Paying remains highest-match for profile only; minutes still come from accumulated `purchasedMinutes`.

Authoritative forensic: `docs/RENZFI_COIN_PROMO_INSERTION_CONTRACT_FORENSIC.md`

---

## Preserved stability constraints (do not regress)

These must remain true for any flash/validation of this candidate:

| Constraint | Requirement |
|------------|-------------|
| Coin countdown | 55↔56 monotonic fix **remains untouched** |
| Pause / Resume | RouterWorker revoke/reauthorize path **remains untouched** |
| RouterOS / MikroTik | Behavior **remains untouched** (no new polling / command storms) |
| TWDT prevention | Baseline **remains untouched** |
| SD / storage architecture | **Remains untouched** |
| Portal Promo Grant | Customer-facing row **remains removed** |
| Purchased time authority | **`purchasedMinutes` remains the authoritative purchased-time value** (portal displays ESP32 value; no frontend promo math) |

---

## Recommended flash / validation posture

1. Treat the **previous hardware-validated build** as the last known-good for countdown / Pause-Resume / terminate / CORS stability.
2. Flash **this** build only when ready to validate coin/promo accumulation on hardware.
3. On failure of promo/time display only, prefer bisect against the prior hardware build rather than mixing unrelated remediations.
4. Do **not** claim production readiness until hardware confirms the insertion-sequence matrix (₱1, ₱1+₱1, ₱1+₱5, ₱1×3+₱5, ₱1×8, order variants).

---

## Source checks already run (not hardware)

- `ESP32_S3_Firmware/tools/promo-resolve-for-amount-check.py` — insertion-sequence contract
- `pio run -e freenove_esp32_s3_wroom` — SUCCESS at correction time

Portal countdown / lifecycle tests from the prior session remain the gate for UI countdown; they are not a substitute for physical coin pulses.
