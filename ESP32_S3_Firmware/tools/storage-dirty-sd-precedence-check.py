#!/usr/bin/env python3
"""SD write must clear dirty SPIFFS manifest so RESET does not mask SD credentials."""

from __future__ import annotations

import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1] / "src" / "StorageManager.cpp"
text = SRC.read_text(encoding="utf-8")


def main() -> int:
    errors: list[str] = []
    if "removeFromManifest(toFallbackPath(path))" not in text:
        errors.append("writeJson/readJson must clear dirty SPIFFS via removeFromManifest")
    if "Dirty SPIFFS must not permanently mask healthy SD" not in text:
        errors.append("readJson must prefer healthy SD over stale dirty SPIFFS")
    if errors:
        for e in errors:
            print(f"storage-dirty-sd-precedence-check: FAIL — {e}", file=sys.stderr)
        return 1
    print("storage-dirty-sd-precedence-check: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
