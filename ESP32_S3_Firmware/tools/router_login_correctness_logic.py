#!/usr/bin/env python3
"""Host-side mirror for RouterOsClient login-attempt state and _loggedIn guards."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum, auto
from typing import Optional

from router_api_protocol_logic import build_login_words, parse_fatal_words, sentence_byte_count


class LoginAttemptMode(Enum):
    NO_LOGIN_REQUEST = auto()
    DIRECT_PASSWORD_LOGIN_SENT = auto()
    CHALLENGE_RESPONSE_SENT = auto()


class LoginCredentialKind(Enum):
    PASSWORD = auto()
    CHALLEN_RESPONSE = auto()


@dataclass
class MockLoginReply:
    trap: bool = False
    fatal: bool = False
    done: bool = False
    trap_message: str = ""
    fatal_message: str = ""
    challenge: str = ""


@dataclass
class LoginFlowOutcome:
    logged_in: bool
    login_mode: LoginAttemptMode
    error_code: str = ""
    error_message: str = ""


def validate_send_login_sentence(
    kind: LoginCredentialKind,
    name_word: str,
    cred_word: str,
) -> tuple[bool, LoginAttemptMode, str]:
    """Mirrors RouterOsClient::sendLoginSentence validation + mode assignment."""
    if not name_word:
        return False, LoginAttemptMode.NO_LOGIN_REQUEST, "ROUTEROS_LOGIN_FAILED"
    if not name_word.startswith("=name=") or name_word == "=name=":
        return False, LoginAttemptMode.NO_LOGIN_REQUEST, "ROUTEROS_LOGIN_FAILED"
    if not cred_word:
        return False, LoginAttemptMode.NO_LOGIN_REQUEST, "ROUTEROS_LOGIN_FAILED"
    if kind == LoginCredentialKind.PASSWORD:
        if not cred_word.startswith("=password=") or cred_word == "=password=":
            return False, LoginAttemptMode.NO_LOGIN_REQUEST, "ROUTEROS_LOGIN_FAILED"
        mode = LoginAttemptMode.DIRECT_PASSWORD_LOGIN_SENT
    else:
        if not cred_word.startswith("=response=") or cred_word == "=response=":
            return False, LoginAttemptMode.NO_LOGIN_REQUEST, "ROUTEROS_LOGIN_FAILED"
        mode = LoginAttemptMode.CHALLENGE_RESPONSE_SENT
    return True, mode, ""


def evaluate_login_result(
    mode: LoginAttemptMode, reply: MockLoginReply
) -> tuple[bool, bool, str, str]:
    """Mirrors evaluateLoginResult. Returns (ok, need_challenge, code, message)."""
    if reply.fatal:
        msg = reply.fatal_message or "RouterOS fatal login error"
        return False, False, "ROUTEROS_API_AUTH_FATAL", msg
    if reply.trap:
        msg = reply.trap_message or "Invalid RouterOS API username or password"
        return False, False, "ROUTEROS_API_AUTH_TRAP", msg
    if reply.challenge:
        return True, True, "", ""
    if reply.done:
        if mode == LoginAttemptMode.NO_LOGIN_REQUEST:
            return (
                False,
                False,
                "ROUTEROS_API_PROTOCOL_ERROR",
                "RouterOS login success without outbound login request",
            )
        return True, False, "", ""
    return False, False, "ROUTEROS_LOGIN_FAILED", "RouterOS login incomplete"


def finalize_login_success(mode: LoginAttemptMode, reply: MockLoginReply) -> LoginFlowOutcome:
    """Mirrors finalizeLoginSuccess."""
    if mode == LoginAttemptMode.NO_LOGIN_REQUEST:
        return LoginFlowOutcome(
            False,
            mode,
            "ROUTEROS_API_PROTOCOL_ERROR",
            "RouterOS login success without outbound login request",
        )
    if reply.trap or reply.fatal or not reply.done:
        return LoginFlowOutcome(
            False,
            mode,
            "ROUTEROS_API_PROTOCOL_ERROR",
            "RouterOS login terminal success preconditions not met",
        )
    if mode not in (
        LoginAttemptMode.DIRECT_PASSWORD_LOGIN_SENT,
        LoginAttemptMode.CHALLENGE_RESPONSE_SENT,
    ):
        return LoginFlowOutcome(
            False,
            mode,
            "ROUTEROS_API_PROTOCOL_ERROR",
            "RouterOS login success with invalid attempt mode",
        )
    return LoginFlowOutcome(True, mode)


def run_direct_password_login(reply: MockLoginReply) -> LoginFlowOutcome:
    ok, mode, code = validate_send_login_sentence(
        LoginCredentialKind.PASSWORD, "=name=admin", "=password=wrong-password"
    )
    if not ok:
        return LoginFlowOutcome(False, LoginAttemptMode.NO_LOGIN_REQUEST, code, "rejected")
    eval_ok, need_challenge, err_code, err_msg = evaluate_login_result(mode, reply)
    if not eval_ok:
        return LoginFlowOutcome(False, mode, err_code, err_msg)
    if need_challenge:
        return LoginFlowOutcome(False, mode, "ROUTEROS_LOGIN_FAILED", "unexpected challenge")
    return finalize_login_success(mode, reply)


def run_challenge_login(
    first: MockLoginReply, second: MockLoginReply
) -> LoginFlowOutcome:
    ok, mode, code = validate_send_login_sentence(
        LoginCredentialKind.PASSWORD, "=name=admin", "=password=secret"
    )
    if not ok:
        return LoginFlowOutcome(False, LoginAttemptMode.NO_LOGIN_REQUEST, code, "rejected")
    eval_ok, need_challenge, err_code, err_msg = evaluate_login_result(mode, first)
    if not eval_ok:
        return LoginFlowOutcome(False, mode, err_code, err_msg)
    if not need_challenge:
        return finalize_login_success(mode, first)

    ok2, mode2, code2 = validate_send_login_sentence(
        LoginCredentialKind.CHALLEN_RESPONSE, "=name=admin", "=response=deadbeef"
    )
    if not ok2:
        return LoginFlowOutcome(False, mode, code2, "challenge send rejected")
    eval_ok2, need_challenge2, err_code2, err_msg2 = evaluate_login_result(mode2, second)
    if not eval_ok2:
        return LoginFlowOutcome(False, mode2, err_code2, err_msg2)
    if need_challenge2:
        return LoginFlowOutcome(
            False, mode2, "ROUTEROS_LOGIN_FAILED", "RouterOS login challenge loop"
        )
    return finalize_login_success(mode2, second)
