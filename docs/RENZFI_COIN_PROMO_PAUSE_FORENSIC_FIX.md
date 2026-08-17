# Coin Modal / Promo Minutes / Pause-Resume Forensic (2026-08-11)

## A. Coin countdown 55↔56

**Cause:** `portal/renzfi-app.js` → `applyNormalizedSession()` re-anchored the
presentation deadline on **every** session poll while `coinSessionActive`:

```js
state.coinCountdown = session.coinCountdown;
anchorCoinWindow(session.coinCountdown);
```

ESP32 ticks `coinWindowRemaining` once per second; portal polls ~2s. A poll can
carry a snapshot that is **1s older** than the local derived countdown, so the
display jumped `55 → 56 → 55`.

**Fix:** monotonic adopt `min(presentation, server)` unless credits/insertedAmount
increase (new coin), window just opened, `trustFully`, or server reports `0`.

## B. Purchased time PHP2→5m

**Cause (prior incorrect “fix”):** `PromoManager::resolveForAmount()` was changed to
**greedy multi-denomination composition** of any amount. That violates the product
contract when applied to totals (e.g. eight × ₱1 with a ₱5 promo present).

**Correct contract:** each physical insertion is resolved independently;
`purchasedMinutes = Σ(minutes of each insertion)`. See
`docs/RENZFI_COIN_PROMO_INSERTION_CONTRACT_FORENSIC.md`.

## C. Promo Grant

**What it was:** Customer UI label in `portal/login.html` (`#coinVoucherTime`)
fed the **same** `purchasedMinutes` as Time. Not a separate backend entitlement,
not VoucherManager, not RouterOS.

**Action:** Removed customer-facing Promo Grant row. Backend PromoManager /
VoucherManager unchanged.

## D–F. Pause / Resume / RouterOS budget

**Already implemented** (not rewritten):

| Action | Path | RouterOS |
|--------|------|----------|
| Pause | `pause()` → freeze → `PauseSession` → `tryEnqueuePauseHotspotUser` → `MikroTikDriver::pauseHotspotUser` | remove active + cookies; **keep** hotspot user (~4 cmds) |
| Resume | `resume()` → `resumePending` → `enqueueActivateSession` → authorize | reuse activation (~4 cmds) |

Countdown frozen via `tickSessions`: `if (isActive && !isPaused)`.

**Gap fixed:** second Pause while `routerPausePending` returned `PAUSE_NOT_ALLOWED`;
second Resume while `resumePending`/`routerAuthPending` could re-queue work.
Both are now idempotent successes.

## G. Race risks (pre-existing, retained)

- Late activation outcome: ignored unless Activating/Paused/ActivationError + `routerAuthPending`.
- Pause failure: reverts to Active (Internet may still be up) — intentional.
- Resume failure: stays Paused — intentional.
- Heartbeat / session GET: **0** RouterOS commands.
