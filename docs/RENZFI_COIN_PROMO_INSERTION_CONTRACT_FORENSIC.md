# Coin / Promo Time Accumulation Contract — Forensic Correction

## FORENSIC RESULT

### 1. Actual current flow (intended happy path)

```
GPIO pulse → CoinManager → PortalSessionManager::onCoinInserted(pesoAmount)
  → PromoManager::resolveForAmount(pesoAmount)   // THIS insertion only
  → if matchedCoin == pesoAmount: use promo minutes; else pesoAmount * 5
  → session.credits        += pesoAmount
  → session.insertedAmount += pesoAmount
  → session.purchasedMinutes += coinMinutes      // accumulate; do not replace
  → save / SSE / GET session returns purchasedMinutes as-is
  → donePaying uses session.purchasedMinutes for entitlement seconds
```

Portal (`renzfi-app.js`) only **displays** `purchasedMinutes` from the ESP32.
It does not compute promo minutes.

### 2. Actual calculation violations found

| ID | File | Function | Range | Behavior | Contract violation |
|----|------|----------|-------|----------|--------------------|
| A | `PromoManager.cpp` | `resolveForAmount` | ~136–223 | **Greedy decompose** of any amount into denominations | Turns totals (and non-exact amounts) into reconstructed coin mixes. Wrong for `[₱1×8]` with ₱5 promo present. |
| B | `PortalSessionManager.cpp` | `enrichSessionPurchasedMinutes` | 285–298 | If `purchasedMinutes` missing → `resolveForAmount(credits)` | Re-resolves **accumulated** credits. |
| C | `PortalSessionManager.cpp` | `donePaying` | 488–496 | Legacy: `resolveForAmount(credits)` when field absent | Same as B at finalization. |
| D | `PortalSessionManager.cpp` | `onCoinInserted` | 381–387 | Seeds missing field via `resolveForAmount(previousCredits)` | Same as B for legacy unpaid sessions. |

**Not a violation (minutes):** `donePaying` ~554–559 calls `resolveForAmount(saleAmount, &hotspotProfile)` for **profile only**; returned minutes are ignored. Accumulated `purchasedMinutes` remains authoritative for time.

**Happy path when `purchasedMinutes` is present:** already accumulates per insertion in `onCoinInserted` — correct.

### 3. Critical counter-example

Config: ₱1=5m, ₱5=10m. Insert eight × ₱1:

- Correct: Σ = 40m
- Greedy `resolveForAmount(8)`: 1×₱5 + 3×₱1 = **25m** ← wrong

### 4. Minimal change required

1. Restore `resolveForAmount` to **exact denomination** lookup (`coin == amount`) for a single physical insertion; fallback `amount * 5`.
2. Stop re-resolving totals in enrich / donePaying legacy / onCoinInserted seed — use non-promo `credits * 5` only when the field is absent (ancient firmware).
3. Keep highest-match **only** for Done Paying hotspot **profile** selection (separate helper).
4. Replace greedy unit tests with per-insertion accumulation tests.
5. Do **not** touch coin countdown, Pause/Resume, RouterOS, portal Promo Grant removal.

### 5. Policy docs note

`CAPTIVE_PORTAL_SESSION_PRODUCTION_IMPLEMENTATION.md` still describes “highest single promo ≤ amount” as shared for preview/Done Paying. That doc is **stale** relative to the product contract and to `onCoinInserted`’s exact-match gate (`matchedCoin == pesoAmount`). This correction aligns code with the product contract, not that stale doc sentence.
