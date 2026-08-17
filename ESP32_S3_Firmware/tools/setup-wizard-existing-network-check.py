#!/usr/bin/env python3
"""Regression guards for existing-network scan and sync configure flow."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SCAN_CPP = ROOT.parent / "src" / "ExistingNetworkScanner.cpp"
SCAN_JSON = ROOT.parent / "src" / "ExistingNetworkScan.cpp"
MANAGER = ROOT.parent / "src" / "RouterProvisioningManager.cpp"
WORKER = ROOT.parent / "src" / "RouterProvisioningWorker.cpp"
SETUP = ROOT.parent / "src" / "web" / "SetupServer.cpp"
HTML = ROOT.parent / "src" / "web" / "SetupWizardPageHtml.h"
TYPES = ROOT.parent / "src" / "RouterProvisioningTypes.h"

FORBIDDEN_SCAN_COMMANDS = (
    "/interface/print",
    "/ip/nat/print",
    "/ip/hotspot/profile/print",
    "/ip/hotspot/walled-garden/print",
)

ALLOWED_SCAN_COMMANDS = (
    "/interface/bridge/print",
    "/ip/address/print",
    "/ip/pool/print",
    "/ip/dhcp-server/print",
    "/ip/dhcp-server/network/print",
    "/ip/hotspot/print",
    "/ip/firewall/filter/print",
)


def main() -> int:
    errors: list[str] = []
    scan = SCAN_CPP.read_text(encoding="utf-8")
    scan_json = SCAN_JSON.read_text(encoding="utf-8")
    scan_all = scan + "\n" + scan_json
    manager = MANAGER.read_text(encoding="utf-8")
    worker = WORKER.read_text(encoding="utf-8")
    setup = SETUP.read_text(encoding="utf-8")
    html = HTML.read_text(encoding="utf-8")
    types = TYPES.read_text(encoding="utf-8")
    app = (ROOT.parent / "src" / "FirmwareApp.cpp").read_text(encoding="utf-8")
    storage_paths = (ROOT.parent / "src" / "StoragePaths.h").read_text(encoding="utf-8")
    backup = (ROOT.parent / "src" / "BackupManager.cpp").read_text(encoding="utf-8")
    factory_reset = (ROOT.parent / "src" / "FactoryResetWorker.cpp").read_text(
        encoding="utf-8")

    if "configureExistingNetwork" not in manager:
        errors.append("RouterProvisioningManager must implement configureExistingNetwork")
    if "networkMode" not in manager or "isExistingNetworkAdopted" not in manager:
        errors.append("Router provisioning persistence must store network mode/adoption flags")
    if "wifiSetupComplete" not in manager:
        errors.append("RouterProvisioningManager must expose wifiSetupComplete")
    if 'kModeNew' not in manager or "wifiSetupComplete" not in manager:
        errors.append("wifiSetupComplete must treat Create New SSID as complete without interfaceId")
    if "_routerProvisioning.loop()" not in app:
        errors.append("FirmwareApp must call RouterProvisioningManager::loop() for deferred Wi-Fi persist")

    if "ExistingNetworkScanner" not in worker:
        errors.append("Router worker must run ExistingNetworkScanner")
    if "enqueueExistingNetworkScan" not in worker:
        errors.append("Router worker must enqueue existing-network scan jobs")
    if "enqueueConfigureExistingNetwork" not in worker:
        errors.append("Router worker must enqueue existing-network configure jobs")

    for cmd in ALLOWED_SCAN_COMMANDS:
        if cmd not in scan:
            errors.append(f"Existing network scan must read {cmd}")

    if "setCommandReplyLimits" not in scan or "kReplyCap" not in scan:
        errors.append("Scan must cap reply records per command")

    for write_path in (
        "/interface/bridge/add",
        "/ip/address/add",
        "/ip/pool/add",
        "/ip/dhcp-server/add",
        "/ip/firewall/filter/add",
    ):
        if write_path in scan:
            errors.append("Scan module must not write RouterOS configuration")

    if "bridge-lan" in scan.lower():
        errors.append("Scan must not hardcode bridge-lan auto-adoption")

    if 'out.scanStatus = "compatible_candidate"' not in scan and \
       "ready_for_selection" not in scan:
        errors.append(
            "Scan must set compatible_candidate when adoptable candidates are found")

    if 'row["origin"]' not in scan_json:
        errors.append("Scan JSON must expose candidate origin (generic|renzfi|imported)")
    if 'dataOut["confirmAllowed"]' not in scan_json:
        errors.append("Scan JSON must expose confirmAllowed")
    if "finalizeScanDecision" not in scan_json and "finalizeScanDecision" not in scan:
        errors.append("Existing network scan must compute confirmAllowed via finalizeScanDecision")

    reset_sources = backup + "\n" + factory_reset
    for reset_path in (
        "RouterCacheFile",
        "RouterProvisioningFile",
        "InstallationFile",
    ):
        if reset_path not in reset_sources:
            errors.append(f"Factory reset must delete {reset_path}")

    if "RouterCacheFile" not in storage_paths:
        errors.append("StoragePaths must define RouterCacheFile")

    if "ADOPT EXISTING RENZ-FI NETWORK" not in types:
        errors.append("Configure confirmation constant must be defined")

    if "/api/setup/router/existing-network/scan" not in setup:
        errors.append("SetupServer must register existing-network scan endpoint")
    if "/api/setup/router/existing-network/configure" not in setup:
        errors.append("SetupServer must register existing-network configure endpoint")
    if "ensureSetupOwnerOnly" not in setup:
        errors.append("Setup routes must block operator sessions")

    if "Rescan" not in html:
        errors.append("Review UI must expose rescan button")
    if "executeAdoption" not in html:
        errors.append("Review UI must configure via executeAdoption()")
    if "/api/setup/router/existing-network/configure" not in html:
        errors.append("Review UI must POST to existing-network/configure")
    if "handleExistingScanJobDone" not in html:
        errors.append("Scan UI must handle completed scan responses")
    if "lastExistingScanData" not in html:
        errors.append("Scan UI must preserve lastExistingScanData")
    if "lastExistingScanData.confirmAllowed" not in html:
        errors.append("Confirm button must use confirmAllowed only")

    if "NetworkAdoptionWorkflow" in setup or "NetworkAdoptionWorkflow" in html:
        errors.append("Legacy NetworkAdoptionWorkflow must remain removed")

    if 'row["password"]' in scan_json or 'dataOut["password"]' in scan_json:
        errors.append("Scan JSON must not expose credentials")

    for cmd in FORBIDDEN_SCAN_COMMANDS:
        if cmd in scan_all:
            errors.append(f"Scan must not use forbidden command {cmd}")

    if errors:
        for err in errors:
            print(f"setup-wizard-existing-network-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("setup-wizard-existing-network-check: OK (existing network guards passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
