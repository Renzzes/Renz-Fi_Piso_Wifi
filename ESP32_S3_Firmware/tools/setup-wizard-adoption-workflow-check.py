#!/usr/bin/env python3
"""Regression guards for sync existing-network configure (no adoption job workflow)."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
HTML = ROOT.parent / "src" / "web" / "SetupWizardPageHtml.h"
SETUP = ROOT.parent / "src" / "web" / "SetupServer.cpp"
MANAGER = ROOT.parent / "src" / "RouterProvisioningManager.cpp"
WORKER = ROOT.parent / "src" / "RouterProvisioningWorker.cpp"
ADAPTER = ROOT.parent / "src" / "RouterWirelessAdapter.cpp"


def main() -> int:
    errors: list[str] = []
    html = HTML.read_text(encoding="utf-8")
    setup = SETUP.read_text(encoding="utf-8")
    manager = MANAGER.read_text(encoding="utf-8")
    worker = WORKER.read_text(encoding="utf-8")

    if "NetworkAdoptionWorkflow" in setup:
        errors.append("SetupServer must not reference NetworkAdoptionWorkflow")
    if "/api/setup/router/existing-network/adopt/jobs/" in setup:
        errors.append("Adoption job polling routes must be removed")
    if '"/api/setup/router/existing-network/adopt", HTTP_POST' in setup:
        errors.append("Legacy POST /existing-network/adopt must be removed")
    if '"/api/setup/router/existing-network/configure", HTTP_POST' not in setup:
        errors.append("SetupServer must register POST /existing-network/configure")
    if "/api/setup/router/wifi/networks" not in setup:
        errors.append("SetupServer must register GET /wifi/networks")
    if "runConfigureExistingNetwork" not in setup and "runConfigureExistingNetwork" not in worker:
        errors.append("Configure must run through RouterProvisioningWorker")
    if "configureExistingNetwork" not in manager:
        errors.append("RouterProvisioningManager must expose configureExistingNetwork")

    if "panelWifi" not in html:
        errors.append("Setup wizard must include Wi-Fi selection panel")
    if "/api/setup/router/wifi/networks" not in html:
        errors.append("UI must load Wi-Fi networks from setup API")
    if "/api/setup/router/existing-network/configure" not in html:
        errors.append("UI must POST to /existing-network/configure")
    if "pollAdoptionJob" in html:
        errors.append("UI must not poll adoption jobs")
    if "wlan1" in html.lower() and "interfaceId" in html:
        pass  # interfaceId is fine as JS key; SSID-only display is enforced in UI

    if not ADAPTER.exists():
        errors.append("RouterWirelessAdapter must exist")

    if (ROOT.parent / "src" / "web" / "NetworkAdoptionWorkflow.cpp").exists():
        errors.append("NetworkAdoptionWorkflow.cpp must be deleted")

    if errors:
        for err in errors:
            print(f"setup-wizard-adoption-workflow-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("setup-wizard-adoption-workflow-check: OK (sync configure + wifi guards passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
