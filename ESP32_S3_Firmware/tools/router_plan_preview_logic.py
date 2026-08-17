#!/usr/bin/env python3
"""Host-side logic mirror for router plan preview error classification."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional


@dataclass
class MockLoginResult:
    done: bool = False
    trap: bool = False
    fatal: bool = False
    trap_message: str = ""
    challenge: str = ""
    authenticated: bool = True


@dataclass
class MockInspectResult:
    ok: bool = True
    trap: bool = False
    fatal: bool = False
    trap_message: str = ""
    reply_count: int = 0


@dataclass
class PreviewOutcome:
    success: bool
    code: str
    message: str
    stage: str


def classify_session_open(*, tcp_connected: bool, login: Optional[MockLoginResult]) -> PreviewOutcome:
    if not tcp_connected:
        return PreviewOutcome(
            success=False,
            code="ROUTER_UNREACHABLE",
            message="TCP connect to RouterOS API failed",
            stage="tcp-connect",
        )
    if login is None:
        return PreviewOutcome(
            success=False,
            code="ROUTEROS_API_UNAVAILABLE",
            message="Login did not run",
            stage="session-open",
        )
    if login.fatal:
        return PreviewOutcome(
            success=False,
            code="ROUTEROS_API_AUTH_FATAL",
            message="RouterOS fatal login error",
            stage="login",
        )
    if login.trap and login.done:
        return PreviewOutcome(
            success=False,
            code="ROUTEROS_API_AUTH_TRAP",
            message=login.trap_message or "RouterOS login failed",
            stage="login",
        )
    if login.challenge and login.done and not login.authenticated:
        return PreviewOutcome(success=True, code="", message="", stage="login-challenge")
    if login.done and login.authenticated and not login.trap:
        return PreviewOutcome(success=True, code="", message="", stage="login")
    return PreviewOutcome(
        success=False,
        code="ROUTEROS_LOGIN_FAILED",
        message="RouterOS login incomplete",
        stage="login",
    )


def classify_inspection(command: str, result: MockInspectResult) -> PreviewOutcome:
    if result.fatal:
        return PreviewOutcome(
            success=False,
            code="ROUTEROS_API_FATAL",
            message=f"{command} fatal reply",
            stage="inspect",
        )
    if result.trap:
        return PreviewOutcome(
            success=False,
            code="API_TRAP",
            message=result.trap_message or f"{command} trap",
            stage="inspect",
        )
    if not result.ok:
        return PreviewOutcome(
            success=False,
            code="API_COMMAND_FAILED",
            message=f"{command} command failed",
            stage="inspect",
        )
    return PreviewOutcome(success=True, code="", message="", stage="inspect")


def run_preview_plan(
    *,
    tcp_connected: bool,
    login: Optional[MockLoginResult],
    inspection: Optional[MockInspectResult] = None,
) -> PreviewOutcome:
    session = classify_session_open(tcp_connected=tcp_connected, login=login)
    if not session.success:
        assert session.code != "ROUTER_UNREACHABLE" or not tcp_connected
        return session
    if inspection is None:
        return PreviewOutcome(success=True, code="", message="", stage="complete")
    inspect = classify_inspection("/system/identity/print", inspection)
    if not inspect.success:
        assert inspect.code != "ROUTER_UNREACHABLE"
        return inspect
    return PreviewOutcome(success=True, code="", message="", stage="complete")
