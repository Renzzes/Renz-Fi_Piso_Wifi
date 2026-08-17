#!/usr/bin/env python3
"""Regression guards for local-only GET /api/setup/router-plan preview."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SETUP = ROOT.parent / "src" / "web" / "SetupServer.cpp"
HTML = ROOT.parent / "src" / "web" / "SetupWizardPageHtml.h"
PROVISIONING = ROOT.parent / "src" / "RouterProvisioningManager.cpp"
PROVISIONING_H = ROOT.parent / "src" / "RouterProvisioningManager.h"
WORKER = ROOT.parent / "src" / "RouterProvisioningWorker.cpp"
WORKER_H = ROOT.parent / "src" / "RouterProvisioningWorker.h"

FORBIDDEN_IN_GET_HANDLER = (
    "enqueueBuildPlan",
    "buildPlan(",
    "inspectRouter(",
    "RouterOsClient",
    "RouterSession",
    "allocRouterSession",
    "pollRouterJob(res.json.data.jobId",
)

SECRET_MARKERS = (
    "passwordProtected",
    "enc:v1:",
    "enc:v2:",
    "resolveRouterCredentials",
)


def extract_get_handler(text: str) -> str:
    start = text.find('server.on("/api/setup/router-plan", HTTP_GET')
    if start < 0:
        return ""
    end = text.find('server.on("/api/setup/router-plan", HTTP_POST', start)
    return text[start:end if end > start else start + 2000]


def main() -> int:
    errors: list[str] = []

    setup = SETUP.read_text(encoding="utf-8")
    html = HTML.read_text(encoding="utf-8")
    ui = setup + html
    prov = PROVISIONING.read_text(encoding="utf-8")
    header = PROVISIONING_H.read_text(encoding="utf-8")
    worker = WORKER.read_text(encoding="utf-8")
    worker_h = WORKER_H.read_text(encoding="utf-8")
    get_handler = extract_get_handler(setup)

    if "buildLocalPlan" not in header:
        errors.append("RouterProvisioningManager must expose buildLocalPlan")
    if "local preview start" not in prov or "local preview complete" not in prov:
        errors.append("buildLocalPlan must log local preview start/complete")
    if "buildStaticLocalPlan" not in prov:
        errors.append("Local preview must use buildStaticLocalPlan")
    if "Preview is local and does not inspect" not in prov:
        errors.append("Local preview must include explicit local-only warning")

    if "buildLocalPlan" not in get_handler:
        errors.append("GET /api/setup/router-plan must call buildLocalPlan")
    if "serveJson(req, 200" not in get_handler:
        errors.append("GET /api/setup/router-plan must return HTTP 200 immediately")
    if "202" in get_handler or "buildQueuedJobBody" in get_handler:
        errors.append("GET preview must not enqueue async worker jobs")

    for token in FORBIDDEN_IN_GET_HANDLER:
        if token in get_handler:
            errors.append(f"GET preview handler must not reference {token}")

    if "Show Planned Configuration" not in ui:
        errors.append("Step 5 button must be renamed to Show Planned Configuration")
    if "Not checked in local preview" not in ui:
        errors.append("UI must show Not checked in local preview for router identity/version")
    if ui.count("fetch('/api/setup/router-plan'") != 1:
        errors.append("Exactly one direct fetch to /api/setup/router-plan is allowed")
    preview_click_start = ui.find("getElementById('previewPlanBtn').addEventListener('click'")
    preview_click_end = ui.find("getElementById('applyConfirm').addEventListener('input'", preview_click_start)
    preview_click = ui[preview_click_start:preview_click_end if preview_click_end > preview_click_start else preview_click_start + 900]
    if "pollRouterJob" in preview_click:
        errors.append("Local preview must not poll router jobs")

    fetch_plan_start = ui.find("function fetchRouterPlan")
    fetch_plan_end = ui.find("function showFormError", fetch_plan_start)
    fetch_plan = ui[fetch_plan_start:fetch_plan_end if fetch_plan_end > fetch_plan_start else fetch_plan_start + 400]
    if "pollRouterJob" in fetch_plan:
        errors.append("fetchRouterPlan must not poll router jobs")

    if "BuildPlan" in worker_h or "enqueueBuildPlan" in worker:
        errors.append("Preview must be removed from RouterProvisioningWorker")

    if "METHOD_NOT_ALLOWED" not in setup:
        errors.append("POST /api/setup/router-plan must remain HTTP 405")

    local_plan_section = prov[prov.find("buildStaticLocalPlan"): prov.find("validateApplyTargetCatalog")]
    for marker in SECRET_MARKERS:
        if marker in local_plan_section:
            errors.append(f"Local preview builder must not reference secret marker {marker}")

    if "routerConnection" not in prov or "previewMode" not in prov:
        errors.append("Local preview JSON must include previewMode and routerConnection metadata")

    if errors:
        for err in errors:
            print(f"router-plan-local-preview-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("router-plan-local-preview-check: OK (local preview guards passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
