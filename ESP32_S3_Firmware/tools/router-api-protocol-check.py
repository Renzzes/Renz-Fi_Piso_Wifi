#!/usr/bin/env python3
"""Regression guard for RouterOS API login/inspect classification (A–F)."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

from router_api_protocol_logic import (  # noqa: E402
    MockIdentityExchange,
    MockLoginExchange,
    build_login_words,
    parse_fatal_words,
    run_protocol_diagnostic,
    sentence_byte_count,
)

VALIDATOR = ROOT.parent / "src" / "SetupRouterValidator.cpp"
ROUTEROS = ROOT.parent / "src" / "RouterOsClient.cpp"
ROUTEROS_H = ROOT.parent / "src" / "RouterOsClient.h"
WORKER = ROOT.parent / "src" / "RouterProvisioningWorker.cpp"
SETUP = ROOT.parent / "src" / "web" / "SetupServer.cpp"
WIZARD_HTML = ROOT.parent / "src" / "web" / "SetupWizardPageHtml.h"


def main() -> int:
    errors: list[str] = []

    outcome = run_protocol_diagnostic(
        login=MockLoginExchange("!re, !done", challenge="abc123"),
        identity=MockIdentityExchange("!re, !done", attributes="name=RouterOS"),
    )
    if outcome.stage != "complete" or not outcome.success:
        errors.append("Scenario A must succeed after challenge login and identity print")

    outcome = run_protocol_diagnostic(
        login=MockLoginExchange(
            "!trap, !done",
            trap=True,
            trap_message="invalid user name or password",
        ),
    )
    if outcome.code != "ROUTEROS_API_AUTH_TRAP" or outcome.stage != "login":
        errors.append("Scenario B must map login trap to ROUTEROS_API_AUTH_TRAP at login stage")

    outcome = run_protocol_diagnostic(
        login=MockLoginExchange("!re, !done", challenge="abc123"),
        identity=MockIdentityExchange(
            "!fatal",
            fatal=True,
            fatal_message="not logged in",
        ),
    )
    if outcome.code != "ROUTEROS_API_FATAL" or outcome.stage != "inspect":
        errors.append("Scenario C must map identity fatal to ROUTEROS_API_FATAL at inspect")

    outcome = run_protocol_diagnostic(
        login=MockLoginExchange("!re, !done", challenge="abc123"),
        identity=MockIdentityExchange(
            "!trap, !done",
            trap=True,
            trap_message="no such command",
        ),
    )
    if outcome.code != "API_TRAP" or outcome.stage != "inspect":
        errors.append("Scenario D must map identity trap to API_TRAP at inspect")

    outcome = run_protocol_diagnostic(
        login=MockLoginExchange("", timed_out=True),
    )
    if outcome.code != "ROUTEROS_API_READ_TIMEOUT" or outcome.stage != "login":
        errors.append("Scenario E must map login timeout to ROUTEROS_API_READ_TIMEOUT")

    # F. The login sentence must always include the password word. This is
    # the exact regression guard for the false-password-acceptance bug:
    # previously the first login attempt sent only "/login" + "=name=admin"
    # (17 content bytes, matching the reported "bytes=17" log) with no
    # password word at all.
    words = build_login_words("admin", "wrongpass123")
    if "=password=wrongpass123" not in words:
        errors.append("Scenario F: login sentence must include the =password= word")
    name_only_bytes = sentence_byte_count(["/login", "=name=admin"])
    with_password_bytes = sentence_byte_count(words)
    if with_password_bytes <= name_only_bytes:
        errors.append("Scenario F: login payload with password must be larger than name-only")
    expected_bytes = sum(1 + len(w) for w in words) + 1  # +1 terminator
    if with_password_bytes != expected_bytes:
        errors.append("Scenario F: login payload byte count must account for password word")

    # G. !fatal words are bare text (no "=message=" prefix) on real RouterOS
    # replies; the parser must still capture the reason instead of leaving
    # fatalMessage empty (previous bug: "fatalMessage= category=" printed
    # empty even though RouterOS sent a real reason word).
    message, category = parse_fatal_words(["not logged in"])
    if message != "not logged in":
        errors.append("Scenario G: bare !fatal word must be captured as fatalMessage")
    message, category = parse_fatal_words(["=message=no such command", "=category=2"])
    if message != "no such command" or category != "2":
        errors.append("Scenario G: attributed !fatal words must still parse normally")

    validator_text = VALIDATOR.read_text(encoding="utf-8")
    if "/system/identity/print" in validator_text:
        errors.append("SetupRouterValidator must not call /system/identity/print")

    routeros = ROUTEROS.read_text(encoding="utf-8")
    if "evaluateLoginResult" not in routeros:
        errors.append("RouterOsClient must evaluate login before marking success")
    if "logParsedSentence" not in routeros:
        errors.append("RouterOsClient must log parsed sentence types")
    if "runProtocolDiagnostic" in routeros and "#if RENZFI_ROUTER_API_PROTOCOL_DIAGNOSTIC" not in routeros:
        errors.append("runProtocolDiagnostic must be compile-time gated")
    if "ROUTEROS_API_READ_TIMEOUT" not in routeros:
        errors.append("RouterOsClient must emit ROUTEROS_API_READ_TIMEOUT during login")

    # Regression guards for the false-password-acceptance bug: the first
    # login attempt must never omit the password word.
    if "loginTryNameOnly" in routeros:
        errors.append("RouterOsClient must not send a name-only login attempt")
    if "LoginCredentialKind::Password" not in routeros:
        errors.append("RouterOsClient must send direct login via LoginCredentialKind::Password")
    if "LoginAttemptMode" not in ROUTEROS.read_text(encoding="utf-8"):
        errors.append("RouterOsClient must track LoginAttemptMode")
    if "ROUTEROS_API_AUTH_TRAP" not in routeros:
        errors.append("RouterOsClient must classify login traps as ROUTEROS_API_AUTH_TRAP")
    if "ROUTEROS_API_AUTH_FATAL" not in routeros:
        errors.append("RouterOsClient must classify login fatals as ROUTEROS_API_AUTH_FATAL")
    if "ROUTEROS_LOGIN_FAILED" not in routeros:
        errors.append("RouterOsClient must classify incomplete logins as ROUTEROS_LOGIN_FAILED")
    if "_password.isEmpty()" not in routeros:
        errors.append("RouterOsClient::login must reject an empty password before I/O")
    if "out.fatalMessage = _sentenceWords[i];" not in routeros:
        errors.append("RouterOsClient must capture bare-word !fatal messages")
    if "ROUTEROS_API_PROTOCOL_ERROR" not in routeros:
        errors.append("RouterOsClient must guard login success without outbound request")
    if "LoginAttemptMode" not in ROUTEROS_H.read_text(encoding="utf-8"):
        errors.append("RouterOsClient.h must define LoginAttemptMode enum")

    worker = WORKER.read_text(encoding="utf-8")
    if "ApiProtocolDiagnostic" not in worker:
        errors.append("RouterProvisioningWorker must include ApiProtocolDiagnostic job")

    setup = SETUP.read_text(encoding="utf-8")
    wizard = WIZARD_HTML.read_text(encoding="utf-8")
    wizard_ui = wizard
    if "/api/setup/router/tcp-check" in setup and "#if RENZFI_ROUTER_TCP_DIAGNOSTIC" not in setup:
        errors.append("Missing compile-time guard for TCP diagnostic route")
    if "apiDiagLink" in setup:
        errors.append("API diagnostic UI must be removed from production setup page")

    if "formatRouterError" not in wizard_ui:
        errors.append("Setup wizard must format errors as code: stage: message")

    # Regression guards: Preview Configuration Plan must only run from the
    # explicit button click, never automatically (component mount, status
    # refresh, panel navigation, or save completion).
    if "loadRouterPlanMeta()" in wizard_ui:
        errors.append("Preview must not be auto-fetched (loadRouterPlanMeta call found)")
    if wizard_ui.count("fetch('/api/setup/router-plan'") != 1:
        errors.append("Exactly one code path may fetch /api/setup/router-plan (the click handler)")
    if "previewSubmitting" not in wizard_ui:
        errors.append("previewPlanBtn handler must guard against duplicate in-flight jobs")

    if errors:
        for err in errors:
            print(f"router-api-protocol-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("router-api-protocol-check: OK (scenarios A–F + source guards passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
