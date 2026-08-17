#!/usr/bin/env python3
"""Regression guards for router_worker stack sizing and local preview scope."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
CONFIG = ROOT.parent / "src" / "Config.h"
SCAN_CPP = ROOT.parent / "src" / "ExistingNetworkScan.cpp"
WORKER = ROOT.parent / "src" / "RouterProvisioningWorker.cpp"
PROVISIONING = ROOT.parent / "src" / "RouterProvisioningManager.cpp"
SETUP = ROOT.parent / "src" / "web" / "SetupServer.cpp"
HTML = ROOT.parent / "src" / "web" / "SetupWizardPageHtml.h"

STACK_COMMAND_RESULT = re.compile(
    r"(?:(?:RouterOsClient::)?CommandResult|InspectionData|RouterSession)\s+&?\*?\s*\w+\s*;"
)

FORBIDDEN_PREVIEW_PRINTS = (
    "/ip/hotspot/profile/print",
    "/ip/hotspot/print",
    "/ip/hotspot/walled-garden/print",
    "/ip/firewall/nat/print",
    "/interface/print",
)


def find_stack_locals(path: Path, allowed: set[str]) -> list[str]:
    text = path.read_text(encoding="utf-8")
    struct_start = text.find("struct InspectionData {")
    struct_end = text.find("};", struct_start) if struct_start >= 0 else -1
    hits: list[str] = []
    for match in STACK_COMMAND_RESULT.finditer(text):
        if struct_start >= 0 and struct_end > struct_start:
            if struct_start <= match.start() <= struct_end:
                continue
        decl = match.group(0)
        if "&" in decl or "*" in decl:
            continue
        name = decl.split()[-1].rstrip(";")
        if name in allowed:
            continue
        line_no = text.count("\n", 0, match.start()) + 1
        hits.append(f"{path.name}:{line_no}: stack-local {decl.strip()}")
    return hits


def local_preview_body(text: str) -> str:
    start = text.find("buildStaticLocalPlan")
    end = text.find("validateApplyTargetCatalog", start)
    return text[start:end if end > start else start + 2500]


def apply_preflight_body(text: str) -> str:
    start = text.find("bool inspectApplyTargets(")
    end = text.find("void buildActionsFromInspection", start)
    return text[start:end if end > start else start + 4000]


def main() -> int:
    errors: list[str] = []

    config = CONFIG.read_text(encoding="utf-8")
    worker = WORKER.read_text(encoding="utf-8")
    prov = PROVISIONING.read_text(encoding="utf-8")
    setup = SETUP.read_text(encoding="utf-8") + HTML.read_text(encoding="utf-8")
    preview_body = local_preview_body(prov)
    apply_body = apply_preflight_body(prov)

    match = re.search(r"RENZFI_ROUTER_WORKER_STACK_WORDS\s*=\s*(\d+)", config)
    if not match:
        errors.append("Config.h must define RENZFI_ROUTER_WORKER_STACK_WORDS")
    elif int(match.group(1)) < 12288:
        errors.append("RENZFI_ROUTER_WORKER_STACK_WORDS must be >= 12288 words")

    if "RENZFI_ROUTER_WORKER_STACK_WORDS" not in worker:
        errors.append("Worker task creation must use RENZFI_ROUTER_WORKER_STACK_WORDS")

    for forbidden in FORBIDDEN_PREVIEW_PRINTS:
        if forbidden in preview_body:
            errors.append(f"Local preview builder must not reference {forbidden}")

    if "buildLocalPlan" not in prov:
        errors.append("RouterProvisioningManager must expose buildLocalPlan")

    if "inspectRouter(" in prov:
        local_start = prov.find("buildLocalPlan(")
        local_end = prov.find("applyConfiguration(", local_start)
        local_body = prov[local_start:local_end if local_end > local_start else local_start + 800]
        if "inspectRouter(" in local_body:
            errors.append("Preview must not call inspectRouter")

    if "BuildPlan" in worker or "enqueueBuildPlan" in worker:
        errors.append("Preview must be removed from RouterProvisioningWorker")

    if "allocInspectionData()" not in prov or "allocRouterSession(" not in prov:
        errors.append("Apply preflight must heap-allocate session/inspection data")

    scan = SCAN_CPP.read_text(encoding="utf-8")
    if "allocScanCatalog()" not in scan:
        errors.append("Existing network scan must heap-allocate ScanCatalogData")

    if "?name=" not in apply_body or "inspectApplyTargets" not in prov:
        errors.append("Apply must use targeted inspectApplyTargets queries")

    for path in (WORKER, PROVISIONING, SCAN_CPP):
        allowed = {"_loginResult", "_cmdPrimary", "_cmdSecondary"}
        for hit in find_stack_locals(path, allowed):
            errors.append(hit)

    if "WORKER_JOB_FAILED" not in worker:
        errors.append("Worker must mark unexpected job termination as failed")
    if "Show Planned Configuration" not in setup:
        errors.append("Step 5 button must be renamed")

    if errors:
        for err in errors:
            print(f"router-worker-stack-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("router-worker-stack-check: OK (worker stack + local preview guards passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
