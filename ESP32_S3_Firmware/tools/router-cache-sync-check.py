#!/usr/bin/env python3
"""Validate router cache synchronization and stale-status wiring."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


def read(name: str) -> str:
    return (SRC / name).read_text(encoding="utf-8")


def main() -> int:
    errors: list[str] = []

    cache_mgr = read("RouterCacheManager.cpp")
    cache_hdr = read("RouterCacheManager.h")
    platform = read("router/RouterPlatform.cpp")
    api = read("ApiServer.cpp")
    config = read("Config.h")

    if "ROUTER_CACHE_STALE_THRESHOLD_HOURS" not in config:
        errors.append("Config.h missing ROUTER_CACHE_STALE_THRESHOLD_HOURS")

    for needle in (
        "lastSynchronizedAt",
        "lastSynchronizedEpoch",
        "cacheAgeSeconds",
        "isStale",
        "fillCacheStatus",
        "stampSynchronized",
        "applyProductionNetworkVerification",
        "productionNetwork",
    ):
        if needle not in cache_mgr and needle not in cache_hdr:
            errors.append(f"RouterCacheManager missing {needle}")

    if "synchronizeRouterCache" not in platform:
        errors.append("RouterPlatform missing synchronizeRouterCache")

    if "/api/router/cache/sync" not in api:
        errors.append("ApiServer missing POST /api/router/cache/sync")

    if 'fillRouterCacheStatus(data["routerCache"]' not in api:
        errors.append("/api/status must expose routerCache status from local cache")

    frontend_status = (ROOT.parent / "src" / "types" / "api.ts").read_text(encoding="utf-8")
    frontend_router = (ROOT.parent / "src" / "services" / "router.ts").read_text(encoding="utf-8")
    frontend_banner = (ROOT.parent / "src" / "components" / "RouterCacheStaleBanner.tsx")
    if "routerCache" not in frontend_status:
        errors.append("Frontend SystemStatus type missing routerCache")
    if "syncRouter" not in frontend_router:
        errors.append("Frontend routerApi missing syncRouter")
    if not frontend_banner.exists():
        errors.append("Frontend missing RouterCacheStaleBanner component")

    if errors:
        for err in errors:
            print(f"router-cache-sync-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("router-cache-sync-check: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
