#!/usr/bin/env python3
"""Regression guard for local router plan preview behavior."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

PROVISIONING = ROOT.parent / "src" / "RouterProvisioningManager.cpp"
ROUTEROS = ROOT.parent / "src" / "RouterOsClient.cpp"
SETUP = ROOT.parent / "src" / "web" / "SetupServer.cpp"
HTML = ROOT.parent / "src" / "web" / "SetupWizardPageHtml.h"


def main() -> int:
    errors: list[str] = []

    text = PROVISIONING.read_text(encoding="utf-8")
    setup = SETUP.read_text(encoding="utf-8")
    ui = setup + HTML.read_text(encoding="utf-8")

    if "buildPlan(" in text and "buildLocalPlan" not in text:
        errors.append("Router plan preview must use buildLocalPlan")
    if "inspectRouter(" in text:
        local_start = text.find("buildLocalPlan(")
        local_end = text.find("applyConfiguration(", local_start)
        local_body = text[local_start:local_end if local_end > local_start else local_start + 800]
        if "inspectRouter(" in local_body:
            errors.append("Preview path must not call inspectRouter")
    if "local preview start" not in text:
        errors.append("Local preview must log start marker")
    if "ensureLocalPreviewPreconditions" not in text:
        errors.append("Local preview must use ensureLocalPreviewPreconditions")

    get_start = setup.find('server.on("/api/setup/router-plan", HTTP_GET')
    get_end = setup.find('server.on("/api/setup/router-plan", HTTP_POST', get_start)
    get_handler = setup[get_start:get_end]
    if "RouterProvisioningWorker" in get_handler or "enqueueBuildPlan" in get_handler:
        errors.append("GET preview must not use RouterProvisioningWorker")

    if "evaluateLoginResult" not in ROUTEROS.read_text(encoding="utf-8"):
        errors.append("RouterOsClient must evaluate login before success")
    if "formatPlanError" not in ui:
        errors.append("Setup wizard must display specific preview error codes")
    if "Show Planned Configuration" not in ui:
        errors.append("Setup wizard must rename preview button")

    if errors:
        for err in errors:
            print(f"router-plan-preview-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("router-plan-preview-check: OK (local preview guards passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
