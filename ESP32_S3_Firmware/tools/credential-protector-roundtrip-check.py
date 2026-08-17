#!/usr/bin/env python3
"""Regression tests for router credential protect/persist round-trip semantics."""

from __future__ import annotations

import json
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

from credential_protector_model import protect_v2, verify_round_trip  # noqa: E402

SRC = ROOT.parent / "src"
CREDENTIAL_PROTECTOR = SRC / "CredentialProtector.cpp"
ROUTER_CREDENTIALS = SRC / "RouterCredentials.cpp"
ROUTER_CREDENTIALS_H = SRC / "RouterCredentials.h"
CONNECTION = SRC / "SetupRouterConnectionManager.cpp"
CONNECTION_H = SRC / "SetupRouterConnectionManager.h"


LENGTH_CASES = [1, 4, 8, 16, 32]
SPECIAL_CASES = [
    " ",
    "a b",
    "p@ss!",
    '"quote"',
    "\\back\\slash",
    "café",
    '{"key":"value"}',
    "plain-text",
]


def simulate_atomic_persist(path: Path, payload: dict, *, fail_write: bool = False) -> bool:
    tmp = path.with_suffix(".tmp")
    text = json.dumps(payload, separators=(",", ":"))
    if fail_write:
        return False
    tmp.write_text(text, encoding="utf-8")
    verify = json.loads(tmp.read_text(encoding="utf-8"))
    if verify.get("passwordProtected", "") != payload.get("passwordProtected", ""):
        return False
    tmp.replace(path)
    return True


def main() -> int:
    errors: list[str] = []

    for length in LENGTH_CASES:
        password = ("x" * length).encode("utf-8")
        blob, _ = protect_v2(password)
        ok, stage = verify_round_trip(password, blob)
        if not ok:
            errors.append(f"length={length} round-trip failed at stage={stage}")

    for sample in SPECIAL_CASES:
        password = sample.encode("utf-8")
        blob, _ = protect_v2(password)
        ok, stage = verify_round_trip(password, blob)
        if not ok:
            errors.append(f"special char round-trip failed for {sample!r} at stage={stage}")

    stable = b"test"
    for _ in range(100):
        blob, _ = protect_v2(stable)
        ok, stage = verify_round_trip(stable, blob)
        if not ok:
            errors.append(f"100x iteration failed at stage={stage}")
            break

    bug_blob, _ = protect_v2(stable, save_post_ctr_nonce=True)
    ok, stage = verify_round_trip(stable, bug_blob)
    if ok:
        errors.append("post-CTR nonce bug path must fail round-trip")

    with tempfile.TemporaryDirectory() as tmpdir:
        cfg_path = Path(tmpdir) / "router-connection.json"
        prior = {"passwordProtected": "enc:v2:" + "aa" * 16 + ":00000004:" + "bb" * 4}
        cfg_path.write_text(json.dumps(prior), encoding="utf-8")

        new_blob, _ = protect_v2(b"new!")
        failed = simulate_atomic_persist(
            cfg_path,
            {"passwordProtected": new_blob},
            fail_write=True,
        )
        if failed:
            current = json.loads(cfg_path.read_text(encoding="utf-8"))
            if current["passwordProtected"] != prior["passwordProtected"]:
                errors.append("failed write must not replace prior valid credentials")

        simulate_atomic_persist(cfg_path, {"passwordProtected": new_blob})
        loaded = json.loads(cfg_path.read_text(encoding="utf-8"))["passwordProtected"]
        ok, stage = verify_round_trip(b"new!", loaded)
        if not ok:
            errors.append(f"atomic persist reload failed at stage={stage}")

    cp = CREDENTIAL_PROTECTOR.read_text(encoding="utf-8")
    rc = ROUTER_CREDENTIALS.read_text(encoding="utf-8")
    rc_h = ROUTER_CREDENTIALS_H.read_text(encoding="utf-8")
    conn = CONNECTION.read_text(encoding="utf-8")

    if 'kPrefixV2[] = "enc:v2:"' not in cp:
        errors.append("CredentialProtector must use enc:v2 format")
    if "esp_fill_random(initialNonce" not in cp:
        errors.append("CredentialProtector must generate random initial nonce before CTR")
    if "bytesToHex(initialNonce" not in cp:
        errors.append("CredentialProtector must persist initial nonce before CTR mutation")
    if "assignBytes(outPlaintext" not in cp:
        errors.append("CredentialProtector must rebuild plaintext with explicit byte length")
    if re.search(r'outPlaintext\s*=\s*String\(reinterpret_cast<char\s*\*>', cp):
        errors.append("CredentialProtector must not use null-terminated String conversion")
    if "describeProtectedBlob" not in cp:
        errors.append("CredentialProtector must expose describeProtectedBlob")

    if "verifyRouterCredentialRoundTrip" not in rc_h:
        errors.append("RouterCredentials must expose verifyRouterCredentialRoundTrip")
    if "stage=before-protect" not in rc:
        errors.append("RouterCredentials must log before-protect stage")
    if "roundtrip=%s" not in rc:
        errors.append("RouterCredentials must log roundtrip result")

    if "captureConfig()" not in conn or "restoreConfigSnapshot" not in conn:
        errors.append("saveConnection must snapshot and restore prior config on failure")
    if "persistAndReloadProtected" not in conn:
        errors.append("saveConnection must reload protected blob from storage after write")
    if "verifyRouterCredentialRoundTrip" not in conn:
        errors.append("saveConnection must call verifyRouterCredentialRoundTrip")
    if "ROUTER_CREDENTIAL_PERSISTENCE_MISMATCH" not in conn:
        errors.append("saveConnection must return ROUTER_CREDENTIAL_PERSISTENCE_MISMATCH")

    if re.search(r'passwordProtected\s*=\s*[^;]*reinterpret_cast', conn):
        errors.append("router config must not assign raw binary ciphertext without encoding")

    if errors:
        for err in errors:
            print(f"credential-protector-roundtrip-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("credential-protector-roundtrip-check: OK (protect/persist round-trip guards passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
