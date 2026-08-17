#!/usr/bin/env python3
"""Validate setup finish provisioning engine wiring."""

from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


def read(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8")


def main() -> int:
    errors: list[str] = []

    engine_h = read(SRC / "RouterProvisioningEngine.h")
    engine_cpp = read(SRC / "RouterProvisioningEngine.cpp")
    worker_h = read(SRC / "RouterProvisioningWorker.h")
    worker_cpp = read(SRC / "RouterProvisioningWorker.cpp")
    setup_server = read(SRC / "web" / "SetupServer.cpp")
    html = read(SRC / "web" / "SetupWizardPageHtml.h")
    adoption_path = SRC / "web" / "NetworkAdoptionWorkflow.cpp"
    adoption = adoption_path.read_text(encoding="utf-8") if adoption_path.exists() else ""

    if "class RouterProvisioningEngine" not in engine_h:
        errors.append("RouterProvisioningEngine header missing")
    if "runFinishPipeline" not in engine_cpp:
        errors.append("RouterProvisioningEngine must implement runFinishPipeline")
    if "/tool/fetch" not in engine_cpp:
        errors.append("Engine must upload portal assets via RouterOS /tool/fetch")
    if "walled-garden" not in engine_cpp:
        errors.append("Engine must configure walled-garden rules")
    if "FinishSetupProvisioning" not in worker_h:
        errors.append("Router worker must define FinishSetupProvisioning job type")
    if "enqueueFinishSetup" not in worker_cpp:
        errors.append("Router worker must enqueue finish setup jobs")
    if "rebootScheduled" not in worker_cpp and "rebootScheduled" not in engine_cpp:
        errors.append("Finish setup job must schedule reboot after success")
    if '"/api/setup/finish"' not in setup_server:
        errors.append("SetupServer must expose POST /api/setup/finish")
    if "/api/setup/provisioning/portal/" not in setup_server:
        errors.append("SetupServer must serve tokenized portal assets for fetch")
    if "POST /api/setup/finish" not in html and "/api/setup/finish" not in html:
        errors.append("Setup wizard finishSetup must call /api/setup/finish")
    if "advanceTo(InstallationState::Provisioned)" in adoption:
        errors.append(
            "Adoption verification must not mark provisioned before finish pipeline"
        )

    # Scripts marker is optional — must not abort finish on /system/script/print fail.
    scripts_idx = engine_cpp.find('FinishTrace::StageScope stage("scripts")')
    if scripts_idx < 0:
        errors.append("Finish pipeline must include scripts stage")
    else:
        scripts_block = engine_cpp[scripts_idx : scripts_idx + 1800]
        if "SCRIPT_ENSURE_FAILED" in scripts_block and "return result" in scripts_block:
            errors.append(
                "scripts stage must not return SCRIPT_ENSURE_FAILED (optional/non-blocking)"
            )
        if "scripts blocking=false" not in scripts_block:
            errors.append("scripts stage must log blocking=false when optional")
        if "cpuUnderPressure" not in scripts_block:
            errors.append("scripts stage must skip under RouterOS CPU pressure")
        if "HOTSPOT_NOT_FOUND" not in engine_cpp:
            errors.append("hotspot-verify must remain a blocking required gate")

    if errors:
        print("setup-wizard-finish-provisioning-check FAILED")
        for err in errors:
            print(f"  - {err}")
        return 1

    print("setup-wizard-finish-provisioning-check OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
