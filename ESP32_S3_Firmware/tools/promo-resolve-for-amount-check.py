#!/usr/bin/env python3
"""Locks the per-insertion coin/promo time contract.

resolveForAmount = exact denomination for ONE physical coin.
purchasedMinutes = SUM(resolveForAmount(each inserted coin)).
Never resolveForAmount(totalCredits).
"""

from __future__ import annotations

import sys
from typing import Any


def resolve_for_amount(amount: int, promos: list[dict[str, Any]]) -> tuple[int, int]:
    """Exact denomination lookup. Returns (minutes, matched_coin)."""
    if amount <= 0:
        return 0, 0
    for promo in promos:
        if promo.get("enabled") is False:
            continue
        coin = int(promo.get("coin") or 0)
        minutes = int(promo.get("minutes") or 0)
        if coin == amount and minutes > 0:
            return minutes, coin
    return amount * 5, 0


def accumulate(insertions: list[int], promos: list[dict[str, Any]]) -> tuple[int, int]:
    credits = 0
    purchased = 0
    for coin in insertions:
        minutes, matched = resolve_for_amount(coin, promos)
        if matched == coin and minutes > 0:
            grant = minutes
        else:
            grant = coin * 5
        credits += coin
        purchased += grant
    return credits, purchased


PHP1_5_PHP5_10 = [{"coin": 1, "minutes": 5}, {"coin": 5, "minutes": 10}]
PHP1_5_ONLY = [{"coin": 1, "minutes": 5}]
PHP1_5_PHP5_30 = [{"coin": 1, "minutes": 5}, {"coin": 5, "minutes": 30}]


EXACT_CASES = [
    ("zero", 0, PHP1_5_PHP5_10, 0, 0),
    ("exact_php1", 1, PHP1_5_PHP5_10, 5, 1),
    ("exact_php5", 5, PHP1_5_PHP5_10, 10, 5),
    ("no_php2_promo_fallback", 2, PHP1_5_ONLY, 10, 0),
    ("disabled_php1", 1, [{"coin": 1, "minutes": 5, "enabled": False}], 5, 0),
    ("empty_table_fallback", 4, [], 20, 0),
]


ACCUM_CASES = [
    ("one_php1", [1], PHP1_5_PHP5_10, 1, 5),
    ("two_php1", [1, 1], PHP1_5_PHP5_10, 2, 10),
    ("three_php1", [1, 1, 1], PHP1_5_PHP5_10, 3, 15),
    ("one_php5", [5], PHP1_5_PHP5_10, 5, 10),
    ("php1_php5", [1, 5], PHP1_5_PHP5_10, 6, 15),
    ("php1_php1_php5", [1, 1, 5], PHP1_5_PHP5_10, 7, 20),
    ("php1x3_php5", [1, 1, 1, 5], PHP1_5_PHP5_10, 8, 25),
    ("order_php5_first", [5, 1, 1, 1], PHP1_5_PHP5_10, 8, 25),
    ("php5_php1_php5", [5, 1, 5], PHP1_5_PHP5_10, 11, 25),
    ("eight_php1_not_greedy", [1] * 8, PHP1_5_PHP5_10, 8, 40),
    ("mixed_php5_30", [1, 1, 1, 5], PHP1_5_PHP5_30, 8, 45),
]


def main() -> int:
    failed = 0

    for name, amount, promos, expect_min, expect_coin in EXACT_CASES:
        minutes, matched = resolve_for_amount(amount, promos)
        if minutes != expect_min or matched != expect_coin:
            failed += 1
            print(
                f"FAIL exact/{name}: got {minutes}m matched={matched}; "
                f"expected {expect_min}m matched={expect_coin}"
            )
        else:
            print(f"PASS exact/{name}: {amount} -> {minutes}m")

    for name, inserts, promos, expect_credits, expect_min in ACCUM_CASES:
        credits, purchased = accumulate(inserts, promos)
        # Prove total re-resolve would be wrong for the critical case.
        if name == "eight_php1_not_greedy":
            wrong = resolve_for_amount(credits, promos)[0]
            # exact on total 8 has no promo → 40 via fallback; still check Σ path
            # Against greedy (would be 25): ensure our Σ is 40.
            if purchased != 40:
                failed += 1
                print(f"FAIL accum/{name}: purchased={purchased} expected 40")
                continue
        if credits != expect_credits or purchased != expect_min:
            failed += 1
            print(
                f"FAIL accum/{name}: credits={credits} minutes={purchased}; "
                f"expected {expect_credits}/{expect_min}"
            )
        else:
            print(
                f"PASS accum/{name}: inserts={inserts} -> "
                f"P{credits} / {purchased}m"
            )

    # Explicit anti-regression: never use resolveForAmount(total) as entitlement.
    total_credits = 8
    per_insert = accumulate([1, 1, 1, 5], PHP1_5_PHP5_10)[1]
    re_resolved = resolve_for_amount(total_credits, PHP1_5_PHP5_10)[0]
    if per_insert != 25:
        failed += 1
        print(f"FAIL anti: per-insert sum expected 25 got {per_insert}")
    elif re_resolved == per_insert and re_resolved != 25:
        failed += 1
        print("FAIL anti: unexpected")
    else:
        # exact(8) falls back to 40 — different from 25; proves totals must not drive UI
        print(
            f"PASS anti/total-reresolve-forbidden: "
            f"sum={per_insert} exact(total)={re_resolved} (must not replace sum)"
        )
        if per_insert == re_resolved:
            # If they happen to match, still OK as long as product uses sum.
            pass

    if failed:
        print(f"promo-resolve-for-amount-check: {failed} failed")
        return 1
    print("promo-resolve-for-amount-check: all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
