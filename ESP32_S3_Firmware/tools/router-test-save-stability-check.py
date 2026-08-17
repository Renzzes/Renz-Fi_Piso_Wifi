#!/usr/bin/env python3
"""Regression guard for setup router test/save worker isolation."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SETUP = ROOT / "src" / "web" / "SetupServer.cpp"
HTML = ROOT / "src" / "web" / "SetupWizardPageHtml.h"
DIAG = ROOT / "src" / "web" / "WebRequestDiagnostics.cpp"
WORKER = ROOT / "src" / "RouterProvisioningWorker.cpp"
VALIDATOR = ROOT / "src" / "SetupRouterValidator.cpp"
PROVISIONING = ROOT / "src" / "RouterProvisioningManager.cpp"
WIRELESS = ROOT / "src" / "RouterWirelessAdapter.cpp"
PROVISIONING_ENGINE = ROOT / "src" / "RouterProvisioningEngine.cpp"
ROUTEROS_CLIENT = ROOT / "src" / "RouterOsClient.cpp"
CONNECTION_MANAGER = ROOT / "src" / "SetupRouterConnectionManager.cpp"

# Matches a stack-local (non-pointer, non-reference) CommandResult
# declaration, e.g. "RouterOsClient::CommandResult identityResult;". This is
# the exact bug class that caused physical Double-exception / Interrupt-WDT
# crashes: CommandResult is a multi-KB aggregate (32 reply records x 12
# attribute Strings) and must always be heap-allocated when used inside the
# RouterProvisioningWorker call chain.
STACK_COMMAND_RESULT = re.compile(r"CommandResult\s+&?\*?\s*\w+\s*;")


def find_multi_stack_command_results(path: Path) -> list[str]:
    """Flag functions declaring 2+ stack CommandResult locals (same-frame overflow)."""
    text = path.read_text(encoding="utf-8")
    hits: list[str] = []
    pattern = re.compile(r"CommandResult\s+&?\*?\s*(\w+)\s*;")
    func_pattern = re.compile(
        r"^(?:bool|void|String|int|RouterProvisioning\w*::\w+|SetupRouter\w*::\w+|"
        r"RouterWireless::\w+|RouterProvisioningEngine::\w+)\s+\w+\(",
        re.MULTILINE,
    )
    func_starts = [m.start() for m in func_pattern.finditer(text)]
    func_starts.append(len(text))
    struct_ranges: list[tuple[int, int]] = []
    struct_match = re.search(r"struct\s+InspectionData\s*\{", text)
    if struct_match:
        struct_start = struct_match.start()
        struct_end = text.find("};", struct_start)
        if struct_end >= 0:
            struct_ranges.append((struct_start, struct_end + 2))
    for i, start in enumerate(func_starts[:-1]):
        end = func_starts[i + 1]
        body = text[start:end]
        names = []
        for match in pattern.finditer(body):
            if any(s <= match.start() + start <= e for s, e in struct_ranges):
                continue
            decl = match.group(0)
            if "&" in decl or "*" in decl:
                continue
            names.append(match.group(1))
        if len(names) >= 2:
            line_no = text.count("\n", 0, start) + 1
            hits.append(
                f"{path.name}:{line_no}: {len(names)} stack CommandResult in one frame "
                f"({', '.join(names)})"
            )
    return hits


def main() -> int:
    setup = SETUP.read_text(encoding="utf-8") + HTML.read_text(encoding="utf-8")
    diag = DIAG.read_text(encoding="utf-8")
    worker = WORKER.read_text(encoding="utf-8")
    errors: list[str] = []

    # Struct-member declarations that are legitimately fine (heap-owned
    # container members, not function-local stack variables).
    struct_member_names = {
        "bridges", "interfaces", "addresses", "pools", "dhcpServers",
        "dhcpNetworks", "hsProfiles", "hsServers", "filterRules", "natRules",
        "ipServices", "walledGarden", "_loginResult", "_scratch",
        "_cmdPrimary", "_cmdSecondary",
    }
    worker_reachable = (VALIDATOR, PROVISIONING, WIRELESS, PROVISIONING_ENGINE)
    for path in worker_reachable:
        if not path.exists():
            errors.append(f"Missing worker-reachable source {path.name}")
            continue
        for hit in find_multi_stack_command_results(path):
            errors.append(hit)

    forbidden_in_handlers = (
        "testConnection(",
        "saveConnection(",
        "applyConfiguration(",
        "SetupRouterValidator::validate",
    )
    for symbol in forbidden_in_handlers:
        if symbol in setup:
            errors.append(f"SetupServer still references {symbol} (must run in worker)")

    required = (
        "SetupRouterOwnedBodyStore",
        "handleRouterConnectionPost",
        "RouterProvisioningWorker",
        "runTest",
        "runSave",
    )
    for token in required:
        if token not in setup:
            errors.append(f"Missing {token}")

    if "timer.finish()" not in setup:
        errors.append("Router handlers must call timer.finish() before send")
    if "void RequestTimer::finish()" not in diag:
        errors.append("RequestTimer::finish() not implemented")

    if "RENZFI_ROUTER_WORKER_STACK_WORDS" not in worker:
        errors.append("Worker stack size constant not referenced")
    if "xTaskCreatePinnedToCore" not in worker:
        errors.append("Worker FreeRTOS task not created")
    if "[router-worker]" not in worker:
        errors.append("Worker lifecycle logs missing")
    if "ROUTER_WORKER_CORE_AFFINITY" not in worker:
        errors.append("Worker task affinity must be configurable (not hardcoded)")
    if "TcpDiagnostic" not in worker:
        errors.append("Missing non-destructive TCP diagnostic job type")
    if "getMaxAllocHeap" not in worker:
        diag = ROOT / "src" / "RouterWorkerDiagnostics.h"
        if not diag.exists() or "getMaxAllocHeap" not in diag.read_text(encoding="utf-8"):
            errors.append("Worker heap diagnostics must report largest free block")

    routeros = ROUTEROS_CLIENT.read_text(encoding="utf-8")
    if "vTaskDelay" not in routeros:
        errors.append("RouterOsClient wait loops must yield via vTaskDelay")
    if "delay(" in routeros:
        errors.append("RouterOsClient must not use blocking delay() in I/O wait loops")
    if "ROUTER_API_MAX_WORD_LEN" not in routeros:
        errors.append("RouterOsClient must bound word length before allocating")
    if "IoLock" not in routeros:
        errors.append("RouterOsClient must serialize socket access via a mutex")
    if "[router-api] session allocated" not in routeros:
        errors.append("RouterOsClient missing session-allocated diagnostic")
    if "[router-api] job=" not in routeros or "connect attempt=1" not in routeros:
        errors.append("RouterOsClient missing auditable connect attempt log")
    if "close reason=" not in routeros:
        errors.append("RouterOsClient missing auditable close reason log")

    if "/api/setup/router/tcp-check" in setup and "#if RENZFI_ROUTER_TCP_DIAGNOSTIC" not in setup:
        errors.append("TCP diagnostic route must be compile-time gated in production")

    if "RouterApiTransportGate" not in worker:
        errors.append("Worker must use RouterApiTransportGate")

    if "clearKeepCapacity" not in routeros and "record.attrs[j]" not in routeros:
        errors.append(
            "initializeCommandResult must clear all ReplyRecord attrs (clearKeepCapacity)"
        )

    validator = VALIDATOR.read_text(encoding="utf-8")
    if "logStackHighWaterMark" not in validator:
        errors.append("SetupRouterValidator must log stack HWM checkpoints")

    conn = CONNECTION_MANAGER.read_text(encoding="utf-8")
    if "logStackHighWaterMark" not in conn:
        errors.append("SetupRouterConnectionManager must log stack HWM after test/save")

    save_start = conn.find("SetupRouterConnectionManager::saveConnection")
    save_end = conn.find("SetupRouterConnectionManager::", save_start + 1)
    if save_end < 0:
        save_end = len(conn)
    save_body = conn[save_start:save_end]
    if save_start == -1:
        errors.append("Unable to locate saveConnection")
    if "_passwordProtected   = protectedPassword;" not in save_body:
        errors.append("saveConnection must persist encrypted credentials")
    if "resolveRouterCredentials" not in conn:
        errors.append("saveConnection must resolve credentials through canonical path")

    # Every RouterOS API sentence (login and commands) must end with the
    # mandatory zero-length terminating word.
    if "return endSentence();" not in routeros:
        errors.append("writeSentence must always terminate with endSentence()")
    if "writeAndCapture(&zero, 1)" not in routeros:
        errors.append("endSentence must encode the terminator as a single 0x00 byte")

    if errors:
        for err in errors:
            print(f"router-test-save-stability-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("router-test-save-stability-check: OK (worker isolation guards passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
