#!/usr/bin/env python3
"""Static check: Access-Control-Allow-Origin must have a single firmware owner."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "src"
WEB = ROOT / "web"

def main() -> int:
    errors: list[str] = []
    wr = (WEB / "WebResponse.cpp").read_text(encoding="utf-8")
    if re.search(
        r'addHeader\(\s*"Access-Control-Allow-Origin"', wr
    ) or re.search(r"addHeader\(\s*'Access-Control-Allow-Origin'", wr):
        errors.append(
            "WebResponse.cpp must not add Access-Control-Allow-Origin "
            "(DefaultHeaders is the single owner)"
        )
    if "Access-Control-Allow-Methods" not in wr:
        errors.append("WebResponse.cpp must still set Access-Control-Allow-Methods")
    if "Content-Type, Authorization" not in wr:
        errors.append("WebResponse.cpp must still allow Content-Type, Authorization")

    wm = (WEB / "WebServerManager.cpp").read_text(encoding="utf-8")
    if "accessControlAllowOriginRegistered" not in wm:
        errors.append(
            "WebServerManager.cpp must guard DefaultHeaders ACAO registration"
        )
    acao_adds = len(
        re.findall(
            r'DefaultHeaders::Instance\(\)\.addHeader\(\s*"Access-Control-Allow-Origin"',
            wm,
        )
    )
    if acao_adds != 1:
        errors.append(
            f"WebServerManager.cpp must add DefaultHeaders ACAO exactly once "
            f"(found {acao_adds})"
        )

    if errors:
        for e in errors:
            print(f"cors-acao-single-owner-check: FAIL — {e}", file=sys.stderr)
        return 1
    print("cors-acao-single-owner-check: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
