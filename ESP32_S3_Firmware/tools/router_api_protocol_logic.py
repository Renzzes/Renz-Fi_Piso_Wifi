#!/usr/bin/env python3
"""Host-side logic mirror for RouterOS API login/inspect classification."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional


@dataclass
class MockLoginExchange:
    sentence_types: str
    trap: bool = False
    fatal: bool = False
    trap_message: str = ""
    fatal_message: str = ""
    challenge: str = ""
    timed_out: bool = False


@dataclass
class MockIdentityExchange:
    sentence_types: str
    trap: bool = False
    fatal: bool = False
    trap_message: str = ""
    fatal_message: str = ""
    attributes: str = ""


@dataclass
class ProtocolOutcome:
    success: bool
    code: str
    message: str
    stage: str


def classify_login(exchange: Optional[MockLoginExchange]) -> ProtocolOutcome:
    if exchange is None:
        return ProtocolOutcome(False, "ROUTEROS_API_UNAVAILABLE", "Login did not run", "login")
    if exchange.timed_out:
        return ProtocolOutcome(
            False,
            "ROUTEROS_API_READ_TIMEOUT",
            "RouterOS API read timeout",
            "login",
        )
    if exchange.fatal:
        return ProtocolOutcome(
            False,
            "ROUTEROS_API_AUTH_FATAL",
            exchange.fatal_message or "RouterOS fatal login error",
            "login",
        )
    if exchange.trap:
        return ProtocolOutcome(
            False,
            "ROUTEROS_API_AUTH_TRAP",
            exchange.trap_message or "RouterOS login failed",
            "login",
        )
    if exchange.challenge:
        return ProtocolOutcome(True, "", "", "login-challenge")
    if "!done" in exchange.sentence_types:
        return ProtocolOutcome(True, "", "", "login")
    return ProtocolOutcome(False, "ROUTEROS_LOGIN_FAILED", "RouterOS login incomplete", "login")


def encoded_word_size(word: str) -> int:
    """Mirrors RouterOsClient::encodedWordSize — RouterOS API length prefix
    (1/2/3 bytes depending on word length) plus the word content itself."""
    length = len(word)
    if length < 0x80:
        return 1 + length
    if length < 0x4000:
        return 2 + length
    if length < 0x200000:
        return 3 + length
    raise ValueError("word exceeds RouterOS API safety bound")


def sentence_byte_count(words: list[str]) -> int:
    """Mirrors RouterOsClient::sentenceByteCount, including the mandatory
    trailing zero-length terminating word (one 0x00 byte)."""
    return 1 + sum(encoded_word_size(w) for w in words)


def build_login_words(username: str, password: str) -> list[str]:
    """Mirrors RouterOsClient::loginWithPassword's sentence construction.
    The password MUST always be included — this is the exact fix for the
    false-password-acceptance bug (previously the first attempt sent only
    "/login" + "=name=<user>" with no password word at all)."""
    return ["/login", f"=name={username}", f"=password={password}"]


def parse_attr(word: str) -> Optional[tuple[str, str]]:
    if not word.startswith("="):
        return None
    eq = word.find("=", 1)
    if eq < 0:
        return None
    return word[1:eq], word[eq + 1:]


def parse_fatal_words(words: list[str]) -> tuple[str, str]:
    """Mirrors RouterOsClient::applySentenceToResult's !fatal branch.
    RouterOS sends !fatal reasons as a bare word (no "=message=" prefix),
    unlike !trap which always uses attribute-value pairs. Preserve it
    verbatim instead of leaving fatalMessage empty."""
    message = ""
    category = ""
    for word in words:
        attr = parse_attr(word)
        if attr:
            key, value = attr
            if key == "message":
                message = value
            if key == "category":
                category = value
        elif not message:
            message = word
    return message, category


def classify_identity(
    command: str, exchange: Optional[MockIdentityExchange]
) -> ProtocolOutcome:
    if exchange is None:
        return ProtocolOutcome(
            False, "API_COMMAND_FAILED", f"{command} command failed", "inspect"
        )
    if exchange.fatal:
        return ProtocolOutcome(
            False,
            "ROUTEROS_API_FATAL",
            exchange.fatal_message or f"{command} fatal reply",
            "inspect",
        )
    if exchange.trap:
        return ProtocolOutcome(
            False,
            "API_TRAP",
            exchange.trap_message or f"{command} trap",
            "inspect",
        )
    if "!re" in exchange.sentence_types and "!done" in exchange.sentence_types:
        return ProtocolOutcome(True, "", "", "inspect")
    return ProtocolOutcome(
        False, "API_COMMAND_FAILED", f"{command} command failed", "inspect"
    )


def run_protocol_diagnostic(
    *,
    login: Optional[MockLoginExchange],
    identity: Optional[MockIdentityExchange] = None,
) -> ProtocolOutcome:
    login_outcome = classify_login(login)
    if not login_outcome.success:
        assert login_outcome.code != "ROUTEROS_API_AUTH_TRAP" or login is None or login.trap
        return login_outcome
    if identity is None:
        return ProtocolOutcome(True, "", "", "complete")
    inspect = classify_identity("/system/identity/print", identity)
    if not inspect.success:
        assert inspect.code not in ("ROUTEROS_API_AUTH_TRAP", "ROUTEROS_API_AUTH_FATAL")
        return inspect
    return ProtocolOutcome(True, "", "", "complete")
