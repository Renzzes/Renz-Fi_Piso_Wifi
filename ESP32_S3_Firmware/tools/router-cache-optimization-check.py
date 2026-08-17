#!/usr/bin/env python3
"""Validate RouterOS API optimization — local router cache wiring."""

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
    worker = read("RouterProvisioningWorker.cpp")
    mikrotik = read("router/drivers/MikroTikDriver.cpp")
    storage_paths = read("StoragePaths.h")

    if 'RouterCacheFile' not in storage_paths or '"/config/router-cache.json"' not in storage_paths:
        errors.append("StoragePaths missing RouterCacheFile contract path")

    for needle in (
        "RouterCacheManager::begin",
        "applyLiveSnapshot",
        "fillWireless",
        "fillProfiles",
        "markProvisioned",
    ):
        if needle not in cache_mgr and needle not in cache_hdr:
            errors.append(f"RouterCacheManager missing {needle}")

    if "_cache->fillWireless" not in platform:
        errors.append("RouterPlatform GET wireless must read cache, not live RouterOS")

    if "_active->fillWireless" in platform:
        errors.append("RouterPlatform still delegates fillWireless to live driver")

    if "_active->listProfiles" in platform:
        errors.append("RouterPlatform still delegates listProfiles to live driver")

    if "collectCacheSnapshot" not in mikrotik:
        errors.append("MikroTikDriver missing collectCacheSnapshot live probe")

    if "/api/router/cache/refresh" not in api:
        errors.append("ApiServer missing manual cache refresh route")

    if "refreshRouterCache(true)" not in worker:
        errors.append("Finish provisioning worker must populate cache before reboot")

    if errors:
        for err in errors:
            print(f"router-cache-optimization-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("router-cache-optimization-check: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
