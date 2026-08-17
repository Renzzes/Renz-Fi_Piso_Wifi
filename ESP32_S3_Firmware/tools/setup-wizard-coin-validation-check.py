#!/usr/bin/env python3
"""Regression guards for setup wizard coin validation."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
WIZARD = ROOT.parent / "src" / "SetupWizardConfigManager.cpp"


def main() -> int:
    errors: list[str] = []
    text = WIZARD.read_text(encoding="utf-8")

    if "validateCoinSetup" not in text:
        errors.append("SetupWizardConfigManager must validate coin setup")
    if "minutes == 0" not in text:
        errors.append("Coin validation must reject zero minutes")
    if "abuseCount" not in text or "banMinutes" not in text:
        errors.append("Coin setup must include abuse settings")
    if "rateTable" not in text:
        errors.append("Coin setup must persist rateTable to settings.json")
    if "COIN_SETUP_INVALID" not in text:
        errors.append("Invalid coin setup must return COIN_SETUP_INVALID")

    if errors:
        for err in errors:
            print(f"setup-wizard-coin-validation-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("setup-wizard-coin-validation-check: OK (coin validation guards passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
