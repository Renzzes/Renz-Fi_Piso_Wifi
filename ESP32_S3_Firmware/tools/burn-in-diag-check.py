#!/usr/bin/env python3
"""Ensure burn-in diagnostics are debug-gated and wired into the main loop."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


def read(name: str) -> str:
    return (SRC / name).read_text(encoding="utf-8")


def main() -> int:
    errors: list[str] = []
    debug = read("RenzFiDebug.h")
    config = read("Config.h")
    burn_in_cpp = read("BurnInDiagnostics.cpp")
    app = read("FirmwareApp.cpp")
    platformio = (ROOT / "platformio.ini").read_text(encoding="utf-8")

    if "RENZFI_BURN_IN_DIAG" not in debug:
        errors.append("RenzFiDebug.h must define RENZFI_BURN_IN_DIAG")
    if "#define RENZFI_BURN_IN_DIAG 0" not in debug.replace(" ", ""):
        if "#define RENZFI_BURN_IN_DIAG 0" not in debug:
            errors.append("RENZFI_BURN_IN_DIAG must default to 0")

    if "#if RENZFI_BURN_IN_DIAG" not in burn_in_cpp:
        errors.append("BurnInDiagnostics.cpp must compile only when flag is set")
    if "[health]" not in burn_in_cpp:
        errors.append("Burn-in logger must emit [health] samples")
    if "uxTaskGetStackHighWaterMark" not in burn_in_cpp:
        errors.append("Burn-in logger must report task stack HWM")
    if "getMinFreeHeap" not in burn_in_cpp:
        errors.append("Burn-in logger must report MinFreeHeap")

    if "BurnInDiagnostics::begin" not in app or "BurnInDiagnostics::loop" not in app:
        errors.append("FirmwareApp must start and poll BurnInDiagnostics")
    if app.count("#if RENZFI_BURN_IN_DIAG") < 2:
        errors.append("FirmwareApp burn-in hooks must be ifdef-guarded")

    if "BURN_IN_DIAG_INTERVAL_MS" not in config:
        errors.append("Config.h must define burn-in sample interval")
    if "RENZFI_BURN_IN_DIAG=1" not in platformio:
        errors.append("platformio.ini developer env must enable burn-in flag")

    if errors:
        for err in errors:
            print(f"burn-in-diag-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("burn-in-diag-check: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
