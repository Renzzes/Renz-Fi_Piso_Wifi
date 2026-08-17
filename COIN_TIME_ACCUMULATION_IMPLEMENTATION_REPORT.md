# Coin Time Accumulation — Implementation Report

**Date:** 2026-08-09  
**Scope:** Per-denomination purchased-time accumulation  
**Release verdict:** **IMPLEMENTED — HARDWARE VALIDATION REQUIRED**

## Summary

The portal session now stores purchased minutes independently from monetary
credits.

For every accepted denomination:

```text
credits += denomination value
purchasedMinutes += configured minutes for that denomination
```

Preview JSON returns the stored accumulated `purchasedMinutes`.
Done Paying consumes the same stored value and converts it to `secondsLeft`.
It no longer recomputes accumulated entitlement from total credits.

Examples under the approved configuration:

```text
PHP1 (5 min) + PHP1 (5 min)
credits = 2
purchasedMinutes = 10

PHP1 (5 min) + PHP5 (10 min)
credits = 6
purchasedMinutes = 15

PHP10 (20 min) + PHP5 (10 min) + PHP1 (5 min)
credits = 16
purchasedMinutes = 35
```

## Files Changed

Production source:

```text
ESP32_S3_Firmware/src/PortalSessionManager.cpp
```

Documentation:

```text
COIN_TIME_ACCUMULATION_IMPLEMENTATION_REPORT.md
```

No other production source was changed.

## Functions Changed

### `PortalSessionManager::enrichSessionPurchasedMinutes()`

New behavior:

- Returns the session's stored `purchasedMinutes`.
- Does not recalculate new-format sessions from total credits.
- Retains a compatibility fallback only for an unpaid session persisted by
  older firmware without the new field.

### `PortalSessionManager::onCoinInserted()`

New behavior:

1. Receives the already validated peso denomination.
2. Uses the existing PromoManager resolver with that single denomination.
3. Accepts configured minutes only when the matched promo denomination exactly
   equals the accepted coin.
4. Retains the existing five-minutes-per-peso fallback when no exact promo
   exists.
5. Adds money to `credits`.
6. Adds denomination time to `purchasedMinutes`.
7. Persists both through the existing session-save path.

The ISR, debounce, pulse grouping, denomination detection, and CoinManager were
not changed.

### `PortalSessionManager::donePaying()`

New behavior:

- Reads the already accumulated `purchasedMinutes`.
- Uses that exact value for `saleMinutes`.
- Converts that exact value to `purchasedSeconds`.
- Clears pending credits and purchased minutes only after reservation.
- Restores both values if sale queueing fails.

PromoManager's highest-matching result is still consulted for the existing
Hotspot profile-selection policy, but its returned minute value is ignored.

### Session creation and termination

New sessions initialize:

```json
"purchasedMinutes": 0
```

Termination clears pending purchased minutes together with pending credits.

## PromoManager Caller Classification

### Before implementation

| Caller | Classification | Action |
|---|---|---|
| `PortalSessionManager::enrichSessionPurchasedMinutes()` | Preview time | Replaced for new-format sessions |
| First calculation in `PortalSessionManager::donePaying()` | Activation time | Replaced |
| Final calculation in `PortalSessionManager::donePaying()` | Activation time and profile selection | Minute result replaced; profile selection retained |
| `PromoManager::minutesForAmount()` | Wrapper | Retained |
| `POST /api/coin/test` via `minutesForAmount(1)` | Authenticated Admin/test path | Retained |
| `PortalSessionManager::getRates()` | View Rates | Uses `PromoManager::list()`, unchanged |

No additional production caller was found.

## Why PromoManager Is No Longer the Accumulated-Time Source

PromoManager is now used at coin-accept time only to look up the configured time
for the single accepted denomination.

It does not derive accumulated purchased time from:

- Total credits.
- Highest matching money package.
- Remaining money.
- Repeated preview polling.

The accumulated total is owned by the portal session:

```text
session["purchasedMinutes"]
```

PromoManager remains intact because it is still used by:

- Exact accepted-denomination lookup.
- Existing Hotspot profile selection.
- Authenticated coin test.
- Rates and Admin promo management.

## Why Preview and Activation Match

Both paths read the same stored session value.

Preview:

```text
session["purchasedMinutes"]
  -> GET /api/portal/session
  -> portal normalizeSession()
  -> renderCoinModal()
```

Activation:

```text
session["purchasedMinutes"]
  -> donePaying saleMinutes
  -> purchasedSeconds
  -> session["secondsLeft"]
  -> RouterWorker activation
```

There is no second accumulated-time calculation during Done Paying.

## Compatibility Behavior

Old persisted unpaid sessions do not contain `purchasedMinutes`, and their
historical individual denomination sequence cannot be reconstructed from total
credits.

For those sessions only:

- The previous entitlement policy supplies a one-time compatibility baseline.
- Each new accepted coin then adds its own configured minutes.
- New sessions never use total-credit recomputation for accumulated time.

Active sessions are unaffected because their entitlement already resides in
`secondsLeft`.

## Regression Analysis

### Coin detection

No impact.

Unchanged:

- ISR.
- GPIO handling.
- Debounce.
- Post-group guard.
- Pulse settlement.
- Pulse-to-peso mapping.
- Anti-double-pulse behavior.

### Credits

The existing monetary accumulation statement is unchanged.

### Coin-window timeout

No timer or timeout logic changed.

Pending purchased minutes are preserved with pending credits when the coin
window closes.

### Portal heartbeat and polling

No frequency or request path changed.

For new sessions, preview requests no longer invoke PromoManager repeatedly;
they return the stored integer.

### Done Paying and activation

The activation queue, RouterWorker, RouterPlatform, MikroTikDriver, and RouterOS
commands are unchanged.

Only the duration supplied to the existing activation path changes to the
approved accumulated value.

### Add Time

Each newly inserted denomination accumulates independently.
Done Paying adds the accumulated purchased seconds to existing remaining time
using the existing add-time logic.

### Sale queue failure

Rollback now restores:

- Credits.
- Inserted amount.
- Purchased minutes.
- Existing remaining time.

### Voucher, authentication, synchronization, Admin, WAN

No related source was changed.

### Portal and MikroTik assets

No frontend change was needed. The existing canonical JavaScript already reads
and displays backend `purchasedMinutes`.

No portal build or MikroTik upload is required.

## CPU Impact

Expected operational impact: **none to lower preview load**.

- No continuous work was added.
- No timer was added.
- No busy loop was added.
- No polling was added.
- Promo lookup occurs once per accepted coin.
- New-format session preview polling now returns stored minutes instead of
  re-reading/re-evaluating promos.

## RouterOS Impact

Expected: **none**.

- Zero additional RouterOS commands.
- Zero additional RouterOS sessions.
- Zero additional Hotspot scans.
- Zero additional retries.
- Idle RouterOS polling remains zero.
- RouterWorker behavior and pacing are unchanged.

## RAM Impact

No new task, queue, buffer, JSON document, or document-capacity constant was
added.

The existing portal-session JSON now retains one additional integer field,
`purchasedMinutes`, per session. Therefore the dynamic per-session JSON footprint
is slightly larger; it is not literally zero bytes.

Promo lookup reuses the existing PromoManager resolver and its existing bounded
document. Preview polling no longer performs that lookup for new sessions.

## Flash and Storage Impact

- No additional persistence operation was added.
- No new storage file was added.
- No SPIFFS/SD architecture changed.
- Existing session writes contain one additional integer field.
- Write frequency is unchanged.

The serialized session payload is therefore slightly larger, but flash-write
count and cadence are unchanged.

## Build Verification

Command:

```powershell
pio run -e freenove_esp32_s3_wroom
```

Result:

```text
SUCCESS
RAM:   31.8% — 104180 / 327680 bytes
Flash: 86.9% — 2278831 / 2621440 bytes
```

IDE diagnostics report no linter errors in the changed production file.

Existing project warnings about ArduinoJson deprecations and USB macro
redefinitions remain. No new warning from the accumulation changes remains.

## Deployment Impact

Required:

```text
Build and flash ESP32 firmware
```

Not required:

```text
Portal rebuild
MikroTik file upload
MikroTik configuration change
SPIFFS upload
SD migration
Admin Dashboard rebuild
```

## Hardware Validation Checklist

### Exact denomination accumulation

- Insert PHP1: credit PHP1, purchased time 5 minutes.
- Insert another PHP1: credit PHP2, purchased time 10 minutes.
- Insert PHP1 then PHP5: credit PHP6, purchased time 15 minutes.
- Insert PHP10, PHP5, PHP1: credit PHP16, purchased time 35 minutes.
- Repeat with every configured denomination.

### Preview and activation equality

- Record preview `purchasedMinutes`.
- Press Done Paying.
- Confirm `secondsLeft == purchasedMinutes * 60`.
- Confirm sale `durationMinutes` equals the same value.
- Confirm RouterOS user timeout receives the same seconds.

### Add Time

- Activate an initial session.
- Open a new coin window.
- Insert multiple denominations.
- Confirm preview is their sum.
- Press Done Paying.
- Confirm existing remaining time increases by exactly the preview amount.
- Confirm the existing active profile remains preserved.

### Rollback and failure

- Force sale queue failure and verify credits/time are restored.
- Test unavailable wall clock separately.
- Test RouterWorker busy/failure and verify purchased seconds remain recoverable.
- Reboot with pending unpaid credits and validate compatibility behavior.

### Stability

- Verify one accepted pulse produces one denomination contribution.
- Verify anti-double-pulse behavior remains unchanged.
- Verify no watchdog reset or Guru Meditation.
- Verify heap and DMA stability during repeated insertions.
- Verify no additional RouterOS command appears before Done Paying.
- Verify idle RouterOS command count remains zero.
- Verify portal heartbeat and polling intervals are unchanged.
- Verify SD and SPIFFS write cadence is unchanged.

## Release Verdict

**IMPLEMENTED — HARDWARE VALIDATION REQUIRED**

The firmware compiles successfully, but the new product contract changes paid
entitlement. Real-coin and RouterOS activation validation is required before
release-candidate designation.
