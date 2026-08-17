#!/usr/bin/env python3
"""Static guards for existing-network scan null safety and heap catalog usage."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SCAN_CPP = ROOT.parent / "src" / "ExistingNetworkScan.cpp"
SCAN_H = ROOT.parent / "src" / "ExistingNetworkScan.h"
ROUTEROS = ROOT.parent / "src" / "RouterOsClient.cpp"
WORKER = ROOT.parent / "src" / "RouterProvisioningWorker.cpp"
MANAGER = ROOT.parent / "src" / "RouterProvisioningManager.cpp"

STACK_COMMAND_RESULT = re.compile(
    r"RouterOsClient::CommandResult\s+\w+\s*;"
)

SCAN_COMMANDS = (
    "/interface/bridge/print",
    "/ip/address/print",
    "/ip/pool/print",
    "/ip/dhcp-server/print",
    "/ip/dhcp-server/network/print",
    "/ip/firewall/filter/print",
    "/ip/hotspot/print",
)


def find_stack_command_results(text: str, path_name: str) -> list[str]:
    hits: list[str] = []
    for match in STACK_COMMAND_RESULT.finditer(text):
        line_no = text.count("\n", 0, match.start()) + 1
        hits.append(f"{path_name}:{line_no}: stack-local {match.group(0).strip()}")
    return hits


def scan_run_body(text: str) -> str:
    start = text.find("bool runReadOnlyScan(")
    end = text.find("void serializeScanJson", start)
    return text[start:end if end > start else start + 4000]


def main() -> int:
    errors: list[str] = []

    scan_cpp = SCAN_CPP.read_text(encoding="utf-8")
    scan_h = SCAN_H.read_text(encoding="utf-8")
    routeros = ROUTEROS.read_text(encoding="utf-8")
    worker = WORKER.read_text(encoding="utf-8")
    manager = MANAGER.read_text(encoding="utf-8")
    run_body = scan_run_body(scan_cpp)

    if "ScanCatalogData" not in scan_h:
        errors.append("ExistingNetworkScan.h must define heap ScanCatalogData")
    if "allocScanCatalog" not in scan_cpp or "freeScanCatalog" not in scan_cpp:
        errors.append("ExistingNetworkScan must heap-allocate scan catalog")
    if "initializeScanCatalog" not in scan_cpp:
        errors.append("ExistingNetworkScan must initialize every catalog CommandResult")
    if "initializeCommandResult" not in run_body and "initializeScanCatalog" not in run_body:
        errors.append("runReadOnlyScan must initialize CommandResult before commands")

    for hit in find_stack_command_results(run_body, SCAN_CPP.name):
        errors.append(hit)

    if "CatalogGuard" not in scan_cpp:
        errors.append("runReadOnlyScan must use RAII catalog guard for heap cleanup")

    if "catalog allocation" not in scan_cpp.lower():
        errors.append("runReadOnlyScan must fail safely when catalog allocation fails")

    for cmd in SCAN_COMMANDS:
        if cmd not in scan_cpp:
            errors.append(f"Scan must include read-only command {cmd}")

    if "runLimitedPrint" not in scan_cpp:
        errors.append("Scan commands must go through runLimitedPrint helper")
    if "executeCommand(" in run_body and "runLimitedPrint" not in run_body:
        errors.append("runReadOnlyScan must not call executeCommand directly")

    if "reason=null_output" not in routeros:
        errors.append("RouterOsClient must reject null CommandResult output pointers")
    if "reason=null_attributes" not in routeros:
        errors.append("RouterOsClient must reject null attribute pointer with count > 0")
    if "ROUTEROS_API_PROTOCOL_ERROR" not in routeros:
        errors.append("RouterOsClient must use ROUTEROS_API_PROTOCOL_ERROR for setup rejects")
    if "inspect write-start" not in routeros or "output=%p" not in routeros:
        errors.append("RouterOsClient must log output pointer validity before write")

    if "ExistingNetworkScan" not in worker:
        errors.append("Router worker must handle ExistingNetworkScan jobs")
    if "scanExistingNetwork" not in worker:
        errors.append("Worker must delegate existing scan to RouterProvisioningManager")
    if "WORKER_JOB_FAILED" not in worker:
        errors.append("Worker must mark unfinished jobs as failed instead of rebooting")
    scan_worker = worker[worker.find("job.type == JobType::ExistingNetworkScan"):]
    scan_worker = scan_worker[: scan_worker.find("} else if (job.type == JobType::ApplyConfiguration)")]
    if "finishJob(" not in scan_worker:
        errors.append("Existing network scan worker path must finishJob on failure")

    if "scanExistingNetwork" not in manager:
        errors.append("RouterProvisioningManager must implement scanExistingNetwork")
    if "existing-network-scan" not in manager:
        errors.append("Scan failure must report existing-network-scan stage")
    if "EXISTING_NETWORK_SCAN_FAILED" not in manager and "scanErrorCode" not in manager:
        errors.append("Scan manager must propagate scan error codes")

    if errors:
        for err in errors:
            print(f"existing-network-scan-null-safety-check: FAIL — {err}", file=sys.stderr)
        return 1

    print(
        "existing-network-scan-null-safety-check: OK "
        "(heap catalog + null guards passed)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
