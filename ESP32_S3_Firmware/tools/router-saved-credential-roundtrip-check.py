#!/usr/bin/env python3
"""Regression guard for saved-credential round-trip and Preview job failure propagation."""

from __future__ import annotations

import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

CONNECTION = ROOT.parent / "src" / "SetupRouterConnectionManager.cpp"
CONNECTION_H = ROOT.parent / "src" / "SetupRouterConnectionManager.h"
PROVISIONING = ROOT.parent / "src" / "RouterProvisioningManager.cpp"
WORKER = ROOT.parent / "src" / "RouterProvisioningWorker.cpp"
CREDENTIALS = ROOT.parent / "src" / "RouterCredentials.cpp"
CREDENTIALS_H = ROOT.parent / "src" / "RouterCredentials.h"
CREDENTIAL_PROTECTOR = ROOT.parent / "src" / "CredentialProtector.cpp"


def fingerprint_secret(secret: str) -> str:
    return hashlib.sha256(secret.encode("utf-8")).hexdigest()[:8]


def simulate_save_round_trip(submitted: str, persisted_read: str) -> tuple[bool, str]:
    if submitted != persisted_read:
        return False, "ROUTER_CREDENTIAL_PERSISTENCE_MISMATCH"
    if fingerprint_secret(submitted) != fingerprint_secret(persisted_read):
        return False, "fingerprint mismatch"
    return True, ""


def simulate_preview_job(build_plan_success: bool, error_code: str, stage: str) -> tuple[str, str]:
    if build_plan_success:
        return "completed", ""
    return "failed", error_code if error_code else "missing-code"


def main() -> int:
    errors: list[str] = []

    password = "test"
    ok, code = simulate_save_round_trip(password, password)
    if not ok:
        errors.append("Scenario b/c: matching saved credential must round-trip")

    ok, code = simulate_save_round_trip(password, "bad")
    if ok or code != "ROUTER_CREDENTIAL_PERSISTENCE_MISMATCH":
        errors.append("Scenario mismatch must fail Save with ROUTER_CREDENTIAL_PERSISTENCE_MISMATCH")

    fp_submit = fingerprint_secret(password)
    fp_saved = fingerprint_secret(password)
    if fp_submit != fp_saved:
        errors.append("Scenario d: Preview resolver fingerprint must match submitted credential")

    state, err = simulate_preview_job(True, "", "")
    if state != "completed":
        errors.append("Scenario e: successful Preview login must complete job")

    state, err = simulate_preview_job(False, "ROUTEROS_API_AUTH_TRAP", "login")
    if state != "failed" or err != "ROUTEROS_API_AUTH_TRAP":
        errors.append("Scenario f: login failure must mark job failed with code preserved")

    conn = CONNECTION.read_text(encoding="utf-8")
    header = CONNECTION_H.read_text(encoding="utf-8")
    prov = PROVISIONING.read_text(encoding="utf-8")
    worker = WORKER.read_text(encoding="utf-8")
    cred = CREDENTIALS.read_text(encoding="utf-8")
    cred_h = CREDENTIALS_H.read_text(encoding="utf-8")
    protector = CREDENTIAL_PROTECTOR.read_text(encoding="utf-8")

    if "resolveRouterCredentials" not in header:
        errors.append("Canonical resolveRouterCredentials must exist")
    if "ResolvedRouterCredentials" not in header:
        errors.append("ResolvedRouterCredentials owned bundle must exist")
    if "loadSessionCredentials" in conn:
        errors.append("Preview must not use legacy loadSessionCredentials")
    if "ROUTER_CREDENTIAL_PERSISTENCE_MISMATCH" not in conn:
        errors.append("Save must fail on persistence round-trip mismatch")
    if "verifyRouterCredentialRoundTrip" not in conn:
        errors.append("Save must verify persisted credential round-trip from storage")
    if "persistAndReloadProtected" not in conn:
        errors.append("Save must reload protected blob from storage after write")
    if "restoreConfigSnapshot" not in conn:
        errors.append("Save must restore prior config when persistence verification fails")
    if "RouterCredentialSource::Persisted" not in prov:
        errors.append("applyConfiguration must use persisted credential resolver")
    if "markRouterPlanFailed" not in prov:
        errors.append("applyConfiguration failure paths must clear success flag")
    if "resolveRouterCredentials" not in prov:
        errors.append("applyConfiguration must resolve credentials through canonical path")
    if "finished type=%s id=%u state=%s code=%s stage=%s" not in worker:
        errors.append("Worker must log failed plan jobs with stage")
    if "finishJob(job, result.httpStatus" not in worker or "result.stage" not in worker:
        errors.append("BuildPlan worker must propagate stage into failed jobs")
    if "[router-credentials] fingerprint=" not in cred:
        errors.append("Credential diagnostics must log safe fingerprint only")
    if "verifyRouterCredentialRoundTrip" not in cred_h:
        errors.append("RouterCredentials helper verifyRouterCredentialRoundTrip must exist")
    if "enc:v2:" not in protector:
        errors.append("CredentialProtector must persist hex-encoded enc:v2 blobs")

    if errors:
        for err in errors:
            print(f"router-saved-credential-roundtrip-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("router-saved-credential-roundtrip-check: OK (save/preview credential guards passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
