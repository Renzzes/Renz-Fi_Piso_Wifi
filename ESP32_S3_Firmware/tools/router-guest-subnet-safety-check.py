#!/usr/bin/env python3
"""Regression guards for dedicated guest subnet 10.20.20.0/24 safety."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
TYPES = ROOT.parent / "src" / "RouterProvisioningTypes.h"
PROV = ROOT.parent / "src" / "RouterProvisioningManager.cpp"
SETUP = ROOT.parent / "src" / "web" / "SetupServer.cpp"
HTML = ROOT.parent / "src" / "web" / "SetupWizardPageHtml.h"

GUEST_DEFAULTS = (
    "10.20.20.0/24",
    "10.20.20.1",
    "10.20.20.10-10.20.20.254",
)
UI_GUEST_DEFAULTS = GUEST_DEFAULTS
TYPES_GUEST_DEFAULTS = GUEST_DEFAULTS + ("10.20.20.1/24",)


def main() -> int:
    errors: list[str] = []
    types = TYPES.read_text(encoding="utf-8")
    prov = PROV.read_text(encoding="utf-8")
    setup = SETUP.read_text(encoding="utf-8")
    ui = HTML.read_text(encoding="utf-8") + setup

    for value in TYPES_GUEST_DEFAULTS:
        if value not in types:
            errors.append(f"RouterProvisioningTypes must default guest subnet to {value}")
    for value in UI_GUEST_DEFAULTS:
        if value not in ui:
            errors.append(f"Setup UI must expose default guest subnet value {value}")

    if "192.168.88.0/24" in types or "192.168.88.0/24" in ui:
        errors.append("Guest subnet defaults must not use 192.168.88.0/24")

    if "validateGuestSubnetLocal" not in prov:
        errors.append("RouterProvisioningManager must validate guest subnet locally")
    if "Guest subnet overlaps the ESP32 Ethernet subnet" not in prov:
        errors.append("Must reject guest subnet overlap with ESP32 Ethernet subnet")
    if "Guest subnet overlaps the MikroTik management/router subnet" not in prov:
        errors.append("Must reject guest subnet overlap with router management subnet")
    if "Apply will stop safely if this guest subnet conflicts" not in prov:
        errors.append("Must warn when router subnet is unknown locally")

    get_start = setup.find('server.on("/api/setup/router-plan", HTTP_GET')
    get_end = setup.find('server.on("/api/setup/router-plan", HTTP_POST', get_start)
    get_handler = setup[get_start:get_end] if get_start >= 0 else ""
    if "buildLocalPlan" not in get_handler:
        errors.append("GET /api/setup/router-plan must call buildLocalPlan")
    for forbidden in ("RouterOsClient", "enqueueBuildPlan", "inspectRouter("):
        if forbidden in get_handler:
            errors.append(f"Local preview GET handler must not reference {forbidden}")

    if "CONFLICT_DETECTED" not in prov:
        errors.append("Apply must use CONFLICT_DETECTED for guest subnet conflicts")
    if '"?address=" + settings.guestNetwork' not in prov:
        errors.append("Apply preflight must query exact guest network address")
    if '"?address=" + settings.guestGatewayCidr' not in prov:
        errors.append("Apply preflight must query exact guest gateway address")
    if "exists without a RENZFI: comment" not in prov:
        errors.append("Apply must stop on non-RENZFI guest subnet object without writes")

    external_markers = (
        "Configure every external AP in Access Point mode.",
        "Connect the MikroTik guest-side port to the AP LAN/uplink port.",
        "Renz-Fi does not automatically detect or configure TP-Link EAP225",
    )
    for marker in external_markers:
        if marker not in ui:
            errors.append(f"External AP instructions must include: {marker}")
    if "external_only" not in ui or "updateExternalApCard" not in ui:
        errors.append("External AP card must toggle for external_only/both modes")

    if "safetySummary" not in prov or "Apply creates the guest-network foundation only." not in ui:
        errors.append("Step 8 must include apply safety summary")

    if errors:
        for err in errors:
            print(f"router-guest-subnet-safety-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("router-guest-subnet-safety-check: OK (guest subnet safety guards passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
