#!/usr/bin/env python3
"""Regression guards for operator NVS persistence and Step 5/8 UI fixes."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
CONFIG = ROOT.parent / "src" / "Config.h"
AUTH_CPP = ROOT.parent / "src" / "AuthManager.cpp"
AUTH_H = ROOT.parent / "src" / "AuthManager.h"
PROV = ROOT.parent / "src" / "SetupProvisioningManager.cpp"
WIZARD = ROOT.parent / "src" / "SetupWizardConfigManager.cpp"
HTML = ROOT.parent / "src" / "web" / "SetupWizardPageHtml.h"

NVS_MAX_KEY_LEN = 15
FORBIDDEN_KEYS = ("operatorUsername", "operatorPasswordHash")
REQUIRED_KEYS = ("op_user", "op_hash")


def extract_putstring_keys(source: str) -> list[str]:
    return re.findall(r'putString\(\s*"([^"]+)"', source)


def main() -> int:
    errors: list[str] = []
    config = CONFIG.read_text(encoding="utf-8")
    auth_cpp = AUTH_CPP.read_text(encoding="utf-8")
    auth_h = AUTH_H.read_text(encoding="utf-8")
    prov = PROV.read_text(encoding="utf-8")
    wizard = WIZARD.read_text(encoding="utf-8")
    html = HTML.read_text(encoding="utf-8")

    for key in REQUIRED_KEYS:
        if key not in config:
            errors.append(f"Config.h must define NVS key constant {key}")
        if len(key) > NVS_MAX_KEY_LEN:
            errors.append(f"NVS key {key} exceeds {NVS_MAX_KEY_LEN} characters")

    for key in FORBIDDEN_KEYS:
        if f'putString("{key}"' in auth_cpp or f'getString("{key}"' in auth_cpp:
            errors.append(f"AuthManager must not use invalid NVS key {key}")

    for key in extract_putstring_keys(auth_cpp):
        if "operator" in key.lower() and key not in REQUIRED_KEYS:
            errors.append(f"Unexpected operator-related NVS key in AuthManager: {key}")
        if len(key) > NVS_MAX_KEY_LEN:
            errors.append(f"AuthManager NVS key too long: {key}")

    if "writeOperatorNvs" not in auth_cpp:
        errors.append("AuthManager must implement writeOperatorNvs with verification")
    if "OPERATOR_PERSISTENCE_FAILED" not in auth_cpp:
        errors.append("AuthManager must return OPERATOR_PERSISTENCE_FAILED on write failure")
    if "OPERATOR_PERSISTENCE_MISMATCH" not in auth_cpp:
        errors.append("AuthManager must return OPERATOR_PERSISTENCE_MISMATCH on verify failure")
    if auth_cpp.count("putString(") < 2 or "savedUser != username" not in auth_cpp:
        errors.append("AuthManager must read back operator values after write")
    if "Operator credentials provisioned" in auth_cpp:
        errors.append("AuthManager must not log false operator provision success")
    if "operator persistence ok usernameLen=" not in auth_cpp:
        errors.append("AuthManager must log safe operator persistence success marker")

    if "OPERATOR_PERSISTENCE_FAILED" not in prov:
        errors.append("SetupProvisioningManager must map OPERATOR_PERSISTENCE_FAILED to HTTP 500")
    if "OPERATOR_PERSISTENCE_MISMATCH" not in prov:
        errors.append("SetupProvisioningManager must map OPERATOR_PERSISTENCE_MISMATCH to HTTP 500")

    if "reconcileOperatorCredentials" not in wizard:
        errors.append("SetupWizardConfigManager must reconcile incomplete operator state")
    if "operator configuration incomplete" not in wizard:
        errors.append("Boot reconcile must log incomplete operator configuration")

    if ".ap-choice" not in html:
        errors.append("Step 5 must use full-width ap-choice cards")
    for rule in ("width:100%", "max-width:100%", "min-width:0", "overflow-wrap:anywhere"):
        if rule not in html:
            errors.append(f"Step 5 CSS must include {rule}")
    if "Both MikroTik Wi-Fi and external AP" not in html:
        errors.append("Step 5 must label the combined AP deployment option correctly")

    overflow_widths = re.findall(r"width:\s*(\d+)px", html[html.find(".ap-choice") : html.find(".hidden-block")])
    for width in overflow_widths:
        if int(width) > 480:
            errors.append(f"Step 5 AP choice CSS must not use fixed desktop width {width}px")

    render_plan = html[html.find("function renderPlan") : html.find("function showRouterFormError")]
    if "DEFER: ' + (a.details" in render_plan:
        errors.append("Step 8 must not render repeated generic DEFER action lines")
    if "Deferred features" not in render_plan:
        errors.append("Step 8 must render a concise Deferred features section")
    if "Attach MikroTik port to guest bridge" not in render_plan:
        errors.append("Step 8 deferred section must list bridge port attachment")

    if "hasOperatorNvsCredentials" not in auth_h:
        errors.append("AuthManager must expose hasOperatorNvsCredentials")

    if errors:
        for err in errors:
            print(f"setup-wizard-operator-persistence-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("setup-wizard-operator-persistence-check: OK (operator persistence guards passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
