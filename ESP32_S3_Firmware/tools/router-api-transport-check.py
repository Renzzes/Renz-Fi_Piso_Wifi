#!/usr/bin/env python3
"""Regression guard for RouterOS API transport safety on constrained routers."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "src" / "Config.h"
ROUTEROS = ROOT / "src" / "RouterOsClient.cpp"
GATE = ROOT / "src" / "RouterApiTransportGate.cpp"
WORKER = ROOT / "src" / "RouterProvisioningWorker.cpp"
SETUP = ROOT / "src" / "web" / "SetupServer.cpp"


def main() -> int:
    errors: list[str] = []
    config = CONFIG.read_text(encoding="utf-8")
    routeros = ROUTEROS.read_text(encoding="utf-8")
    gate = GATE.read_text(encoding="utf-8")
    worker = WORKER.read_text(encoding="utf-8")
    setup = SETUP.read_text(encoding="utf-8")

    # Regression guard: the per-job deadline must comfortably exceed the
    # worst-case connect + login + command budget. It was previously 6000ms,
    # which left no headroom once the login io timeout was corrected from
    # 2000ms to the proven-working 8000ms (see SETUP_ROUTER_IO_TIMEOUT_MS) —
    # a slow-but-legitimate RouterOS login reply on physical hardware (e.g.
    # a MikroTik hAP lite) was cut off by this job-level gate instead of the
    # intended sentence-level one, surfacing as ROUTEROS_API_READ_TIMEOUT.
    if "ROUTER_WORKER_JOB_TIMEOUT_MS = 20000" not in config:
        errors.append("Job timeout must be 20 seconds (headroom over login io timeout)")
    if "ROUTER_API_SENTENCE_TIMEOUT_MS    = 2000" not in config:
        errors.append("Sentence read timeout must be 2 seconds")
    if "ROUTER_API_MIN_CONNECT_INTERVAL_MS = 5000" not in config:
        errors.append("Minimum 5s connect interval required")
    if "#define RENZFI_ROUTER_TCP_DIAGNOSTIC 0" not in config.replace(" ", ""):
        if "RENZFI_ROUTER_TCP_DIAGNOSTIC 0" not in config:
            errors.append("TCP diagnostic must default to disabled")
    if "RENZFI_ROUTER_API_PROTOCOL_DIAGNOSTIC 0" not in config:
        errors.append("API protocol diagnostic must default to disabled")

    if re.search(r"while\s*\(\s*!connected\s*\)", routeros):
        errors.append("RouterOsClient must not retry TCP connect in a loop")
    if "waitUntilConnectAllowed" not in routeros:
        errors.append("RouterOsClient must enforce connect cooldown via gate")
    if "connect_gate_wait=" not in routeros or "tcp_connect=" not in routeros:
        errors.append("RouterOsClient must split connect-gate wait from TCP connect timing")
    if "ros_login=" not in routeros:
        errors.append("RouterOsClient must log RouterOS login elapsed separately from TCP")
    if "login_tx=" not in routeros or "login_rx=" not in routeros:
        errors.append("RouterOsClient must split login write vs login reply wait")
    if "login_challenge=" not in routeros:
        errors.append("RouterOsClient must log whether login used challenge-response")
    if "acquireSession" not in routeros:
        errors.append("RouterOsClient must acquire a single global session")
    if "disconnectInternal" not in routeros:
        errors.append("RouterOsClient must close sockets on all failure paths")
    if "close reason=" not in routeros:
        errors.append("RouterOsClient must log close reason")
    if "job=%u connect attempt=1" not in routeros:
        errors.append("RouterOsClient must log single connect attempt")
    if "job=%u login start" not in routeros:
        errors.append("RouterOsClient must log login start")
    if "delay(" in routeros:
        errors.append("RouterOsClient must not use blocking delay()")
    if "vTaskDelay" not in routeros:
        errors.append("RouterOsClient must yield with vTaskDelay")

    if "recordFailure" not in gate or "g_backoffMs" not in gate:
        errors.append("RouterApiTransportGate must implement exponential backoff")
    if "cooldown remaining=" not in gate:
        errors.append("RouterApiTransportGate must log cooldown remaining")
    if "g_sessionActive" not in gate:
        errors.append("RouterApiTransportGate must track active session count")

    if "tryEnqueueActivateHotspotUser" not in worker:
        errors.append("Worker must reject jobs when another is pending")
    if "busyResult" not in worker:
        errors.append("Worker must return busy when another RouterOS job is pending")
    if "ROUTER_WORKER_QUEUE_DEPTH" not in worker:
        errors.append("Worker queue depth must be configurable to 1")
    if "RouterApiTransportGate::beginJob" not in worker:
        errors.append("Worker must begin/end transport gate jobs")
    if "RouterApiTransportGate::endJob" not in worker:
        errors.append("Worker must end transport gate jobs")
    if re.search(r"tcp-diagnostic iteration", worker) and "#if RENZFI_ROUTER_TCP_DIAGNOSTIC" not in worker:
        errors.append("TCP diagnostic loop must be compile-time gated")

    if "/api/setup/router/tcp-check" in setup and "#if RENZFI_ROUTER_TCP_DIAGNOSTIC" not in setup:
        errors.append("TCP diagnostic HTTP route must be compile-time gated")
    if "/api/setup/router/api-check" in setup and "#if RENZFI_ROUTER_API_PROTOCOL_DIAGNOSTIC" not in setup:
        errors.append("API diagnostic HTTP route must be compile-time gated")
    if "tcpDiagLink" in setup:
        errors.append("TCP diagnostic UI link must be removed from production setup page")
    if "apiDiagLink" in setup:
        errors.append("API diagnostic UI link must be removed from production setup page")

    if errors:
        for err in errors:
            print(f"router-api-transport-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("router-api-transport-check: OK (transport safety guards passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
