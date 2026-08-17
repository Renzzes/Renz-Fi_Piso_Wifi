#!/usr/bin/env python3
"""Finish must not hard-fail after production activation on router-cache only."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENGINE = (ROOT / "src" / "RouterProvisioningEngine.cpp").read_text(encoding="utf-8")


def main() -> int:
    errors: list[str] = []

    if "persistFinishRouterCache" not in ENGINE:
        errors.append("Finish must use persistFinishRouterCache (not a fourth RouterOS session)")

    # Hard-fail return after router-cache must not remain.
    marker = 'FinishTrace::StageScope cacheStage("router-cache");'
    idx = ENGINE.find(marker)
    if idx < 0:
        errors.append("router-cache FinishTrace stage missing")
    else:
        window = ENGINE[idx : idx + 1200]
        if 'result.errorCode    = "ROUTER_CACHE_REFRESH_FAILED"' in window:
            errors.append(
                "router-cache must not hard-fail Finish with ROUTER_CACHE_REFRESH_FAILED "
                "after production activation"
            )
        if "non-blocking" not in window and "Non-fatal" not in window and "non-fatal" not in window:
            errors.append("router-cache stage must document non-blocking/non-fatal contract")
        if "commitFinishInstallationState" not in ENGINE[idx:]:
            errors.append("commitFinishInstallationState must still run after router-cache")

    if "refreshRouterCache(true)" in ENGINE:
        # Finish path specifically should not reopen RouterOS for cache.
        finish_idx = ENGINE.find("runFinishPipeline")
        if finish_idx >= 0 and "refreshRouterCache(true)" in ENGINE[finish_idx:]:
            errors.append("runFinishPipeline must not call refreshRouterCache(true)")

    if errors:
        for err in errors:
            print(f"finish-router-cache-gate-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("finish-router-cache-gate-check: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
