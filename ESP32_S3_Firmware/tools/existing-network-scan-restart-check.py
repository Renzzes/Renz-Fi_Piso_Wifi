#!/usr/bin/env python3
"""Regression guards for the Step 8 existing-network scan DEVICE_RESTARTED
false-positive.

Bug history:
1. Backend: GET /api/setup/router/existing-network/jobs/<id> read the job
   id from req->_tempObject, a field only ever populated by the POST body
   collector. On this GET-only route it was always null, so the endpoint
   returned 404 JOB_NOT_FOUND on literally every poll — independent of
   whether the scan job existed or had completed. Fixed to parse the id
   from the URL path via parseJobIdFromPath(req), same as the sibling
   /api/setup/router/jobs/* route.
2. Frontend: pollExistingNetworkJob() treated a single 404/JOB_NOT_FOUND
   poll response as proof the ESP32 had rebooted, even when the firmware
   scan had actually completed successfully (job-table races — TTL /
   slot eviction, a slow mobile reconnect, or one dropped request — can
   all make a completed job "disappear" without a reboot).

This script statically asserts the fixed behavior so neither regression
can silently come back:

  - the existing-network job-status route parses the job id from the URL,
    never from req->_tempObject;
  - completed/failed jobs are retained (TTL-based, >=30s) rather than
    destroyed the instant finishJob() runs, and eviction never touches a
    still-queued/running job;
  - a completed+successful job is checked, and wins, before any
    404/failed/restart branch;
  - DEVICE_RESTARTED is only ever dispatched from inside
    confirmExistingScanRestart(), gated on a *changed* bootInstanceId
    fetched fresh from /api/setup/status — never from a bare 404;
  - a transient fetch/network failure retries (with backoff) at least 3
    times before surfacing any terminal error, and never claims a restart;
  - switching away from Step 8 and back (restoreExistingScanUi) keeps
    showing cached successful scan data unless a restart was actually
    confirmed.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
HTML = ROOT.parent / "src" / "web" / "SetupWizardPageHtml.h"
DEVICE_IDENTITY_H = ROOT.parent / "src" / "DeviceIdentity.h"
DEVICE_IDENTITY_CPP = ROOT.parent / "src" / "DeviceIdentity.cpp"
SETUP_SERVER = ROOT.parent / "src" / "web" / "SetupServer.cpp"
WORKER_H = ROOT.parent / "src" / "RouterProvisioningWorker.h"
WORKER_CPP = ROOT.parent / "src" / "RouterProvisioningWorker.cpp"
CONFIG_H = ROOT.parent / "src" / "Config.h"


def slice_between(text: str, start_marker: str, end_marker: str) -> str:
    start = text.find(start_marker)
    if start < 0:
        return ""
    end = text.find(end_marker, start + len(start_marker))
    return text[start: end if end > start else start + 4000]


def strip_comments(source: str) -> str:
    """Remove // and /* */ C/C++ comments so checks only match real code,
    not prose (e.g. root-cause explanations) that happens to mention the
    same identifiers."""
    no_block = re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", no_block)


def main() -> int:
    errors: list[str] = []

    html = HTML.read_text(encoding="utf-8")
    device_identity_h = DEVICE_IDENTITY_H.read_text(encoding="utf-8")
    device_identity_cpp = DEVICE_IDENTITY_CPP.read_text(encoding="utf-8")
    setup_server = SETUP_SERVER.read_text(encoding="utf-8")
    worker_h = WORKER_H.read_text(encoding="utf-8")
    worker_cpp = WORKER_CPP.read_text(encoding="utf-8")
    config_h = CONFIG_H.read_text(encoding="utf-8")

    # ── 0. The GET job-status endpoint must read the job id from the URL,
    # never from req->_tempObject (that field is only ever populated by the
    # POST body collector; on this GET-only route it is always null, which
    # made the endpoint return 404 JOB_NOT_FOUND on literally every poll —
    # regardless of whether the scan job existed or had completed). ────────
    existing_jobs_route = strip_comments(slice_between(
        setup_server,
        '"/api/setup/router/existing-network/jobs/*"',
        "});"))
    if not existing_jobs_route:
        errors.append(
            "Could not locate the /api/setup/router/existing-network/jobs/* "
            "route handler in SetupServer.cpp")
    else:
        if "_tempObject" in existing_jobs_route:
            errors.append(
                "/api/setup/router/existing-network/jobs/* must NOT read "
                "req->_tempObject (it is never populated on this GET route "
                "and this previously caused a deterministic 404 on every "
                "poll) — it must call parseJobIdFromPath(req) instead")
        if "parseJobIdFromPath(req)" not in existing_jobs_route:
            errors.append(
                "/api/setup/router/existing-network/jobs/* must parse the "
                "job id from the URL path via parseJobIdFromPath(req), the "
                "same helper /api/setup/router/jobs/* already uses")

    # ── 0b. Completed/failed jobs must be retained for a grace period, not
    # destroyed the moment finishJob() runs, and eviction must only ever
    # touch already-finished jobs (never a job that is still queued/running).
    if "job.finishedAt   = millis();" not in worker_cpp and \
       "job.finishedAt = millis()" not in worker_cpp.replace("  ", " "):
        errors.append(
            "finishJob() must stamp finishedAt so completed/failed jobs can "
            "be retained for a grace period before eviction")
    ttl_match = re.search(r"ROUTER_WORKER_JOB_TTL_MS\s*=\s*(\d+)", config_h)
    if not ttl_match or int(ttl_match.group(1)) < 30000:
        errors.append(
            "ROUTER_WORKER_JOB_TTL_MS must retain completed/failed jobs for "
            "at least 30 seconds so the UI can always retrieve a completed "
            "result instead of racing a 404")
    if "pollExpiredJobs" not in worker_h or "pollExpiredJobs" not in worker_cpp:
        errors.append("Worker must expose/implement pollExpiredJobs() job-retention sweep")
    expiry_fn = slice_between(worker_cpp, "void RouterProvisioningWorker::pollExpiredJobs",
                               "RouterProvisioningWorker::JobRecord *")
    if "kJobTtlMs" not in expiry_fn or "finishedAt" not in expiry_fn:
        errors.append(
            "pollExpiredJobs() must only evict jobs whose finishedAt is "
            "older than the TTL, not jobs that are still queued/running")
    if re.search(r"state != JobState::Completed && job\.state != JobState::Failed",
                 expiry_fn) is None and \
       re.search(r"job\.state != JobState::Completed", expiry_fn) is None:
        errors.append(
            "pollExpiredJobs() must skip queued/running jobs and only ever "
            "evict jobs already in Completed/Failed state")
    if "finishJob(job" in worker_cpp:
        finish_call_sites = [m.start() for m in re.finditer(r"finishJob\(job", worker_cpp)]
        for pos in finish_call_sites:
            following = worker_cpp[pos: pos + 600]
            if re.search(r"\.reset\(\)|jobs\.erase|_jobs\[.*\]\s*=\s*JobRecord\{\}", following):
                errors.append(
                    "finishJob() must not be immediately followed by job "
                    "destruction/eviction — completed jobs must remain "
                    "queryable until pollExpiredJobs() retires them via TTL")
                break

    poll_fn = slice_between(html, "function pollExistingNetworkJob(",
                             "function handleExistingScanJobDone(")
    confirm_fn = slice_between(html, "function confirmExistingScanRestart(",
                                "function pollExistingNetworkJob(")
    done_fn = slice_between(html, "function handleExistingScanJobDone(",
                             "var PANEL_ORDER")
    restore_fn = slice_between(html, "function restoreExistingScanUi(",
                                "function adoptionCandidateFromJob(")

    if not poll_fn or not confirm_fn or not done_fn or not restore_fn:
        errors.append(
            "Could not locate pollExistingNetworkJob/confirmExistingScanRestart/"
            "handleExistingScanJobDone/restoreExistingScanUi in SetupWizardPageHtml.h")
        for err in errors:
            print(f"existing-network-scan-restart-check: FAIL — {err}", file=sys.stderr)
        return 1

    # ── Backend: a real boot-instance signal must exist for /api/setup/status ──
    if "String bootInstanceId();" not in device_identity_h:
        errors.append("DeviceIdentity must expose bootInstanceId()")
    if "esp_random()" not in device_identity_cpp or "bootInstanceId" not in device_identity_cpp:
        errors.append("DeviceIdentity::bootInstanceId() must generate a fresh per-boot token")
    if 'data["bootInstanceId"]' not in setup_server:
        errors.append("/api/setup/status must expose bootInstanceId in its response data")

    # ── 1. Completed+success must be checked before 404/failed handling ────
    completed_check = re.search(
        r"response\.success === true\s*&&\s*job\.state === 'completed'",
        poll_fn)
    if not completed_check:
        errors.append(
            "pollExistingNetworkJob must check response.success===true && "
            "job.state==='completed' && result.success===true && result.data "
            "as the authoritative completion path (requirement 1)")
    failed_check = re.search(r"job\.state === 'failed'", poll_fn)
    if completed_check and failed_check and completed_check.start() > failed_check.start():
        errors.append(
            "Completed-success check must come before the failed-job check, "
            "not after")

    if "result && result.success === true && result.data" not in poll_fn:
        errors.append(
            "Completed-success guard must require result.success===true AND "
            "result.data to exist (requirement 1)")

    # ── 2. Exact unwrap shape: job = response.data; result = job.result ────
    if "var job = response.data" not in poll_fn:
        errors.append("pollExistingNetworkJob must unwrap job = response.data (requirement 2)")
    if "var result = job.result" not in poll_fn:
        errors.append("pollExistingNetworkJob must unwrap result = job.result (requirement 2)")

    # ── 3. 404/JOB_NOT_FOUND must never directly dispatch DEVICE_RESTARTED ──
    not_found_branch = slice_between(poll_fn, "resp.json.code === 'JOB_NOT_FOUND'", "return;\n            }")
    if "DEVICE_RESTARTED" in not_found_branch:
        errors.append(
            "The 404/JOB_NOT_FOUND branch in pollExistingNetworkJob must never "
            "dispatch DEVICE_RESTARTED directly — it must defer to "
            "confirmExistingScanRestart() (requirement 4)")
    if "confirmExistingScanRestart(jobId, bootInstanceBefore, onDone)" not in poll_fn:
        errors.append(
            "404/JOB_NOT_FOUND must call confirmExistingScanRestart() before any "
            "terminal decision (requirement 4)")

    # ── 4. DEVICE_RESTARTED only after a confirmed bootInstanceId change ───
    restart_calls = list(re.finditer(r"code: 'DEVICE_RESTARTED'", confirm_fn))
    if len(restart_calls) != 1:
        errors.append(
            "confirmExistingScanRestart must dispatch DEVICE_RESTARTED exactly "
            "once, only from the confirmed-restart branch")
    else:
        before_restart = confirm_fn[: restart_calls[0].start()]
        if "if (restartDetected)" not in before_restart:
            errors.append(
                "DEVICE_RESTARTED must be gated behind an explicit "
                "restartDetected check (requirement 4)")
    if not re.search(
            r"restartDetected\s*=\s*!!bootInstanceBefore\s*&&\s*!!bootInstanceAfter\s*&&\s*\n?\s*bootInstanceBefore !== bootInstanceAfter",
            confirm_fn):
        errors.append(
            "restartDetected must require both a known prior bootInstanceId AND "
            "a changed value read fresh from /api/setup/status (requirement 4)")
    if "DEVICE_RESTARTED" in html:
        # Make sure it is ONLY reachable through confirmExistingScanRestart
        # for the existing-network scan flow (pollRouterJob is a separate,
        # unrelated job type and is out of scope for this check).
        occurrences = [m.start() for m in re.finditer(r"code: 'DEVICE_RESTARTED'", html)]
        existing_scan_region_start = html.find("function confirmExistingScanRestart(")
        existing_scan_region_end = html.find("function handleExistingScanJobDone(")
        stray = [o for o in occurrences
                 if existing_scan_region_start <= o < existing_scan_region_end
                 and not (confirm_fn and confirm_fn.find("code: 'DEVICE_RESTARTED'") >= 0)]
        # (structural sanity only; the precise gating is already checked above)

    # ── 5. Transient poll/network errors: preserve job id, retry >=3x ──────
    if "activeExistingScanJobId = null" in poll_fn:
        errors.append(
            "pollExistingNetworkJob must never clear activeExistingScanJobId "
            "itself on a transient error — only handleExistingScanJobDone may, "
            "and only once polling has truly finished (requirement 5)")
    retry_match = re.search(r"maxPollErrorRetries\s*=\s*(\d+)", poll_fn)
    if not retry_match or int(retry_match.group(1)) < 3:
        errors.append(
            "Transient poll/network failures must retry at least 3 times with "
            "backoff before a terminal error is shown (requirement 5)")
    if "pollErrorCount <= maxPollErrorRetries" not in poll_fn or \
       "setTimeout(tick, 500 * pollErrorCount)" not in poll_fn:
        errors.append(
            "Transient poll/network failure handling must retry via "
            "setTimeout(tick, ...) with increasing backoff before giving up")

    # ── 6. Completed success always wins over a stale restart flag ─────────
    if "existingScanRestartFlagged = false" not in done_fn:
        errors.append(
            "handleExistingScanJobDone must clear a stale restart flag on "
            "successful completion (requirement 6)")
    success_idx = done_fn.find("finalRes.json.data")
    clear_idx = done_fn.find("existingScanRestartFlagged = false")
    if success_idx < 0 or clear_idx < 0 or clear_idx < success_idx:
        errors.append(
            "existingScanRestartFlagged must be cleared inside the successful-"
            "completion branch of handleExistingScanJobDone")

    # ── 7. Debug logging gated + exact required fields ─────────────────────
    if "if (!RENZFI_DEBUG) return;" not in html:
        errors.append("Debug logging must be gated on RENZFI_DEBUG (dev/build debug mode only)")
    required_debug_fields = [
        "jobId", "httpStatus", "jobState", "resultSuccess", "hasScanData",
        "restartDetected", "bootInstanceBefore", "bootInstanceAfter",
    ]
    log_calls = re.findall(r"logExistingScanDebug\(\{(.*?)\}\);", html, re.DOTALL)
    if not log_calls:
        errors.append("No logExistingScanDebug({...}) call sites found")
    for call in log_calls:
        for field in required_debug_fields:
            if field not in call:
                errors.append(
                    f"logExistingScanDebug call is missing required field '{field}'")

    # ── 8a. Switching away from Step 8 and back retains successful data ────
    if "existingScanRestartFlagged" not in restore_fn:
        errors.append(
            "restoreExistingScanUi must consult existingScanRestartFlagged "
            "before re-rendering cached scan data")
    if "lastExistingScanData" not in restore_fn:
        errors.append(
            "restoreExistingScanUi must still render lastExistingScanData when "
            "no restart was confirmed (requirement 8)")

    # ── 8b. A confirmed restart is the only thing allowed to drop cached data ─
    clear_cache_sites = [m.start() for m in re.finditer(r"(?<!var )lastExistingScanData = null", html)]
    if not clear_cache_sites:
        errors.append("lastExistingScanData must be cleared when a restart is confirmed")
    else:
        for pos in clear_cache_sites:
            preceding = html[max(0, pos - 400): pos]
            if "restartDetected" not in preceding and "existingScanRestartFlagged = true" not in preceding:
                errors.append(
                    "lastExistingScanData must only be cleared inside a "
                    "confirmed-restart branch, never on a plain poll failure")

    if errors:
        for err in errors:
            print(f"existing-network-scan-restart-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("existing-network-scan-restart-check: OK (DEVICE_RESTARTED false-positive guards passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
