#!/usr/bin/env python3
"""Regression guards for owner/operator RBAC."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
API = ROOT.parent / "src" / "ApiServer.cpp"
AUTH_H = ROOT.parent / "src" / "AuthManager.h"
AUTH_CPP = ROOT.parent / "src" / "AuthManager.cpp"

OWNER_ROUTES = (
    "/api/system/wifi/config",
    "/api/router/settings",
    "/api/router/test",
    "/api/system/factory-reset",
    "/api/system/reboot",
    "/api/settings/backup",
    "/api/settings/restore",
)


def main() -> int:
    errors: list[str] = []
    api = API.read_text(encoding="utf-8")
    auth_h = AUTH_H.read_text(encoding="utf-8")
    auth_cpp = AUTH_CPP.read_text(encoding="utf-8")

    if "AuthRole" not in auth_h or "sessionRole" not in auth_h:
        errors.append("AuthManager must expose AuthRole and sessionRole")
    if "provisionOperatorCredentials" not in auth_cpp:
        errors.append("AuthManager must provision operator credentials")
    if "requireOwnerAuth" not in api:
        errors.append("ApiServer must define requireOwnerAuth")
    if "OWNER_REQUIRED" not in api:
        errors.append("ApiServer must return OWNER_REQUIRED for operator blocks")

    for route in OWNER_ROUTES:
        idx = api.find(route)
        if idx < 0:
            errors.append(f"Missing route guard target {route}")
            continue
        if route == "/api/system/wifi/config":
            post_idx = api.find("wifiConfigWrite", idx)
            window = api[post_idx : post_idx + 400] if post_idx >= 0 else api[idx : idx + 600]
        else:
            window = api[idx : idx + 600]
        if "requireOwnerAuth" not in window and "AuthRequirement::OwnerOnly" not in window:
            errors.append(f"{route} write handler must use requireOwnerAuth or OwnerOnly")

    if errors:
        for err in errors:
            print(f"setup-wizard-rbac-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("setup-wizard-rbac-check: OK (RBAC guards passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
