#!/usr/bin/env python3
"""Regression guard for RouterOsClient login-attempt state and _loggedIn rules."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

from router_api_protocol_logic import build_login_words, parse_fatal_words, sentence_byte_count  # noqa: E402
from router_login_correctness_logic import (  # noqa: E402
    LoginAttemptMode,
    LoginCredentialKind,
    MockLoginReply,
    run_challenge_login,
    run_direct_password_login,
    validate_send_login_sentence,
)

ROUTEROS = ROOT.parent / "src" / "RouterOsClient.cpp"
ROUTEROS_H = ROOT.parent / "src" / "RouterOsClient.h"
SETUP = ROOT.parent / "src" / "web" / "SetupServer.cpp"
HTML = ROOT.parent / "src" / "web" / "SetupWizardPageHtml.h"
CONNECTION = ROOT.parent / "src" / "SetupRouterConnectionManager.cpp"


def main() -> int:
    errors: list[str] = []

    # Direct login sentence includes exactly /login, =name=, =password=, terminator.
    words = build_login_words("admin", "wrong-password")
    expected = ["/login", "=name=admin", "=password=wrong-password"]
    if words != expected:
        errors.append(f"Direct login words mismatch: {words}")
    if sentence_byte_count(words) <= sentence_byte_count(["/login", "=name=admin"]):
        errors.append("Direct login must be larger than username-only payload")

    # Username-only login rejected before socket write.
    ok, mode, code = validate_send_login_sentence(
        LoginCredentialKind.PASSWORD, "=name=admin", ""
    )
    if ok or mode != LoginAttemptMode.NO_LOGIN_REQUEST:
        errors.append("Username-only login must be rejected before write")

    ok, mode, code = validate_send_login_sentence(
        LoginCredentialKind.PASSWORD, "=name=admin", "=password="
    )
    if ok:
        errors.append("Empty password word must be rejected before write")

    # Wrong password !trap never sets _loggedIn.
    outcome = run_direct_password_login(
        MockLoginReply(trap=True, done=True, trap_message="invalid user name or password")
    )
    if outcome.logged_in:
        errors.append("Wrong-password !trap must not set _loggedIn")
    if outcome.error_code != "ROUTEROS_API_AUTH_TRAP":
        errors.append("Wrong-password !trap must map to ROUTEROS_API_AUTH_TRAP")

    # Bare-word !fatal never sets _loggedIn and preserves message.
    outcome = run_direct_password_login(
        MockLoginReply(fatal=True, fatal_message="not logged in")
    )
    if outcome.logged_in:
        errors.append("Bare-word !fatal must not set _loggedIn")
    if outcome.error_message != "not logged in":
        errors.append("Bare-word !fatal must preserve the router message")
    msg, _ = parse_fatal_words(["not logged in"])
    if msg != "not logged in":
        errors.append("parse_fatal_words must preserve bare-word !fatal reason")

    # Direct password login !done sets _loggedIn.
    outcome = run_direct_password_login(MockLoginReply(done=True))
    if not outcome.logged_in:
        errors.append("Direct password login !done must set _loggedIn")
    if outcome.login_mode != LoginAttemptMode.DIRECT_PASSWORD_LOGIN_SENT:
        errors.append("Direct password login must record DirectPasswordLoginSent")

    # Challenge login does not set _loggedIn until second authenticated !done.
    outcome = run_challenge_login(
        MockLoginReply(challenge="abc123", done=True),
        MockLoginReply(done=True),
    )
    if not outcome.logged_in:
        errors.append("Challenge login must set _loggedIn after second !done")
    if outcome.login_mode != LoginAttemptMode.CHALLENGE_RESPONSE_SENT:
        errors.append("Challenge login must record ChallengeResponseSent on final step")

    mid_only = run_direct_password_login(
        MockLoginReply(challenge="abc123", done=True)
    )
    if mid_only.logged_in:
        errors.append("First challenge reply must not set _loggedIn")

    from router_login_correctness_logic import evaluate_login_result, finalize_login_success

    eval_ok, need_challenge, _, _ = evaluate_login_result(
        LoginAttemptMode.DIRECT_PASSWORD_LOGIN_SENT,
        MockLoginReply(challenge="abc123", done=True),
    )
    if not eval_ok or not need_challenge:
        errors.append("Challenge-first reply must defer authentication to second step")

    # !done without prior outbound login is protocol error.
    ok, _, code, _ = evaluate_login_result(
        LoginAttemptMode.NO_LOGIN_REQUEST, MockLoginReply(done=True)
    )
    if ok or code != "ROUTEROS_API_PROTOCOL_ERROR":
        errors.append("!done with NoLoginRequest must return ROUTEROS_API_PROTOCOL_ERROR")

    fin = finalize_login_success(
        LoginAttemptMode.NO_LOGIN_REQUEST, MockLoginReply(done=True)
    )
    if fin.logged_in or fin.error_code != "ROUTEROS_API_PROTOCOL_ERROR":
        errors.append("finalizeLoginSuccess must reject NoLoginRequest")

    # Preview cannot enqueue during page load/navigation/status refresh.
    setup = SETUP.read_text(encoding="utf-8") + HTML.read_text(encoding="utf-8")
    if "loadRouterPlanMeta()" in setup:
        errors.append("Preview must not auto-fetch on panelConfigure")
    if setup.count("fetch('/api/setup/router-plan'") != 1:
        errors.append("Exactly one Preview fetch path allowed")
    if "showPanel('panelReview')" in setup and "fetchRouterPlan" in setup:
        show_panel_idx = setup.find("function showPanel")
        preview_btn_idx = setup.find("previewPlanBtn")
        fetch_in_show = setup.find("fetchRouterPlan", show_panel_idx, preview_btn_idx)
        if fetch_in_show != -1:
            errors.append("fetchRouterPlan must not be called from showPanel")
    if "previewSubmitting" not in setup:
        errors.append("Preview click must guard duplicate jobs with previewSubmitting")

    # Wrong password login-stage 401, never ROUTER_UNREACHABLE.
    conn = CONNECTION.read_text(encoding="utf-8")
    if "isLoginFailureCode" not in conn:
        errors.append("SetupRouterConnectionManager must classify login failures for 401")
    if "ROUTER_UNREACHABLE" in conn and "isLoginFailureCode" not in conn:
        errors.append("Login failures must not map to ROUTER_UNREACHABLE")

    routeros = ROUTEROS.read_text(encoding="utf-8")
    header = ROUTEROS_H.read_text(encoding="utf-8")

    if "LoginAttemptMode" not in header:
        errors.append("RouterOsClient must define LoginAttemptMode")
    if "DirectPasswordLoginSent" not in header:
        errors.append("RouterOsClient must define DirectPasswordLoginSent")
    if "ChallengeResponseSent" not in header:
        errors.append("RouterOsClient must define ChallengeResponseSent")
    if "loginTryNameOnly" in routeros:
        errors.append("Name-only login helper must not exist")
    if "credWord.isEmpty()" in routeros and "writeSentence(words, 2)" in routeros:
        errors.append("Name-only /login write path must not exist")
    if "LoginCredentialKind::Password" not in routeros:
        errors.append("Direct login must use LoginCredentialKind::Password")
    if "LoginCredentialKind::ChallengeResponse" not in routeros:
        errors.append("Challenge login must use LoginCredentialKind::ChallengeResponse")
    if "finalizeLoginSuccess(const CommandResult" not in routeros:
        errors.append("finalizeLoginSuccess must verify CommandResult preconditions")
    if "ROUTEROS_API_PROTOCOL_ERROR" not in routeros:
        errors.append("RouterOsClient must emit ROUTEROS_API_PROTOCOL_ERROR guard")
    if "_loginAttemptMode = LoginAttemptMode::DirectPasswordLoginSent" not in routeros and \
            "LoginAttemptMode::DirectPasswordLoginSent" not in routeros:
        errors.append("DirectPasswordLoginSent must be set after successful write")
    if "logLoginWriteDiagnostics" not in routeros:
        errors.append("Missing logLoginWriteDiagnostics for safe debug mode")

    if errors:
        for err in errors:
            print(f"router-login-correctness-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("router-login-correctness-check: OK (login state guards passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
