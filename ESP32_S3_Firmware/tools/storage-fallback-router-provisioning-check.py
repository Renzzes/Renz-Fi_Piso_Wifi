#!/usr/bin/env python3
"""Regression guards for SPIFFS fallback setup/router provisioning storage."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
STORAGE = ROOT.parent / "src" / "StorageManager.cpp"
PATHS = ROOT.parent / "src" / "StoragePaths.h"
CONFIG = ROOT.parent / "src" / "Config.h"
WORKER = ROOT.parent / "src" / "RouterProvisioningWorker.cpp"
SCAN = ROOT.parent / "src" / "ExistingNetworkScan.cpp"
MANAGER = ROOT.parent / "src" / "RouterProvisioningManager.cpp"
SETUP = ROOT.parent / "src" / "web" / "SetupServer.cpp"
HTML = ROOT.parent / "src" / "web" / "SetupWizardPageHtml.h"
API = ROOT.parent / "src" / "ApiServer.cpp"
SETUP_PROV = ROOT.parent / "src" / "SetupProvisioningManager.cpp"

SETUP_CONTRACT_PATHS = (
    "StoragePaths::InstallationFile",
    "StoragePaths::ProvisioningFile",
    "StoragePaths::RouterConnectionFile",
    "StoragePaths::RouterProvisioningFile",
    "StoragePaths::SetupWizardFile",
)

FB_PATHS = (
    ("FB_INSTALLATION", "FbInstallation"),
    ("FB_PROVISIONING", "FbProvisioning"),
    ("FB_ROUTER_CONNECTION", "FbRouterConnection"),
    ("FB_ROUTER_PROVISIONING", "FbRouterProvisioning"),
    ("FB_SETUP_WIZARD", "FbSetupWizard"),
)


def main() -> int:
    errors: list[str] = []

    storage = STORAGE.read_text(encoding="utf-8")
    paths = PATHS.read_text(encoding="utf-8")
    config = CONFIG.read_text(encoding="utf-8")
    worker = WORKER.read_text(encoding="utf-8")
    scan = SCAN.read_text(encoding="utf-8")
    manager = MANAGER.read_text(encoding="utf-8")
    setup = SETUP.read_text(encoding="utf-8")
    html = HTML.read_text(encoding="utf-8")
    api = API.read_text(encoding="utf-8")
    setup_prov = SETUP_PROV.read_text(encoding="utf-8")

    if "SD unavailable, using SPIFFS fallback" not in storage:
        errors.append("StorageManager must log SPIFFS fallback when SD mount fails")
    if "FB_INSTALLATION" not in storage or "SPIFFS fallback seeded" not in storage:
        errors.append(
            "Boot without SD must seed SPIFFS installation/operational checkpoints"
        )

    eligible_body = storage[
        storage.find("isFallbackEligible") : storage.find("toFallbackPath")
    ]
    for token in SETUP_CONTRACT_PATHS:
        if token not in eligible_body:
            errors.append(f"isFallbackEligible must include {token}")

    for config_token, paths_token in FB_PATHS:
        if config_token not in config:
            errors.append(f"Config.h must define SPIFFS fallback alias {config_token}")
        if paths_token not in paths:
            errors.append(f"StoragePaths.h must define SPIFFS path {paths_token}")

    if "toFallbackPath" not in storage or "FB_ROUTER_CONNECTION" not in storage:
        errors.append("toFallbackPath must map router-connection.json to SPIFFS")

    if "fileSizeBytes" not in storage or "spiffsFileSize(toFallbackPath" not in storage:
        errors.append("fileSizeBytes must support SPIFFS fallback eligible paths")

    for forbidden in ("SD.begin", "SD.open", "SD_MMC", "#include <SD.h>"):
        if forbidden in scan:
            errors.append(f"ExistingNetworkScan must not use direct SD API ({forbidden})")

    if "scanExistingNetwork" not in manager or "configureExistingNetwork" not in manager:
        errors.append("RouterProvisioningManager must implement scan and adoption")

    if "SD" in manager and "StoragePaths::RouterProvisioningFile" in manager:
        pass  # path constants only
    if re.search(r"\bSD\.", manager):
        errors.append("RouterProvisioningManager must not call SD APIs directly")

    if "Use Existing Renz-Fi Network" not in html or "Create New Renz-Fi Network" not in html:
        errors.append("Step 8 UI must remain available without SD-specific gating")

    if "existing-network/scan" not in setup:
        errors.append("SetupServer must expose existing-network scan without SD gate")

    if 'storage["ok"] = _storage->healthy() || _storage->usingFallback()' not in setup_prov:
        errors.append("Setup status must treat SPIFFS fallback as operational storage")

    if "healthy() || _storage->usingFallback()" not in api:
        errors.append("ApiServer must treat SPIFFS fallback as operational for health/status")

    if "allocScanCatalog()" not in scan:
        errors.append("Existing network scan must keep heap ScanCatalog allocation")

    stack_log = re.search(
        r"\[router-worker\] started stackWords=%u stackBytes=%u",
        worker,
    )
    if not stack_log:
        errors.append("Router worker must log stackWords and stackBytes at startup")
    if "sizeof(StackType_t)" not in worker:
        errors.append("Router worker stackBytes must use sizeof(StackType_t)")
    if "stackBytes=%u" not in worker:
        errors.append("Router worker must log stackBytes at startup")

    match = re.search(r"RENZFI_ROUTER_WORKER_STACK_WORDS\s*=\s*(\d+)", config)
    if not match:
        errors.append("Config.h must define RENZFI_ROUTER_WORKER_STACK_WORDS")
    elif int(match.group(1)) != 12288:
        errors.append("RENZFI_ROUTER_WORKER_STACK_WORDS must remain 12288 words for this guard")

    if errors:
        for err in errors:
            print(f"storage-fallback-router-provisioning-check: FAIL — {err}", file=sys.stderr)
        return 1

    print(
        "storage-fallback-router-provisioning-check: OK "
        "(SPIFFS fallback setup/router guards passed)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
