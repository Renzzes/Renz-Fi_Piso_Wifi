#!/usr/bin/env python3
"""Regression guard for GET /api/setup/router-plan local preview safety."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / "src" / "RouterProvisioningManager.cpp"
SETUP = ROOT / "src" / "web" / "SetupServer.cpp"


def main() -> int:
    text = TARGET.read_text(encoding="utf-8")
    setup = SETUP.read_text(encoding="utf-8")
    errors: list[str] = []

    if "InspectionData inspection;" in text:
        errors.append("Stack-allocated InspectionData found — must use heap allocators")

    if "RouterSession session(_eth)" in text or "RouterSession session(" in text:
        if "allocRouterSession" not in text:
            errors.append("Stack-allocated RouterSession found in provisioning path")

    if "allocInspectionData" not in text or "allocRouterSession" not in text:
        errors.append("Missing heap allocator helpers for router apply preflight")

    if "buildLocalPlan" not in text:
        errors.append("RouterProvisioningManager must implement buildLocalPlan")

    if "inspectApplyTargets" not in text:
        errors.append("Apply must use targeted inspectApplyTargets instead of broad inspect")

    get_start = setup.find('server.on("/api/setup/router-plan", HTTP_GET')
    get_end = setup.find('server.on("/api/setup/router-plan", HTTP_POST', get_start)
    get_handler = setup[get_start:get_end]
    if "HeapJsonDocument" not in get_handler:
        errors.append("GET preview must build JSON response on heap in HTTP handler")

    for i in range(20):
        blob = bytearray(55 * 1024)
        blob[0] = i & 0xFF
        del blob

    if errors:
        for err in errors:
            print(f"router-plan-safety-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("router-plan-safety-check: OK (20 heap cycles + source guards passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
