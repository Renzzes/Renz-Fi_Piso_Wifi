#!/usr/bin/env python3
"""Static contract check: Pause/Resume stay on RouterWorker, no polling, idempotent."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PSM = (ROOT / "src" / "PortalSessionManager.cpp").read_text(encoding="utf-8", errors="replace")
DRIVER = (
    ROOT / "src" / "router" / "drivers" / "MikroTikDriver.cpp"
).read_text(encoding="utf-8", errors="replace")
WORKER = (
    ROOT / "src" / "RouterProvisioningWorker.cpp"
).read_text(encoding="utf-8", errors="replace")


def must_contain(label: str, text: str, pattern: str) -> None:
    if not re.search(pattern, text, re.M):
        raise AssertionError(f"missing: {label} ({pattern})")


def must_not_contain(label: str, text: str, pattern: str) -> None:
    if re.search(pattern, text, re.M):
        raise AssertionError(f"forbidden: {label} ({pattern})")


def main() -> int:
    checks = []

    def check(name: str, fn):
        try:
            fn()
            print(f"PASS {name}")
            checks.append(True)
        except Exception as exc:  # noqa: BLE001
            print(f"FAIL {name}: {exc}")
            checks.append(False)

    check(
        "pause enqueues PauseHotspotUser via RouterWorker",
        lambda: must_contain(
            "tryEnqueuePauseHotspotUser",
            PSM,
            r"tryEnqueuePauseHotspotUser\(",
        ),
    )
    check(
        "pause driver removes active + cookies, keeps user",
        lambda: (
            must_contain("removeHotspotActiveByMac", DRIVER, r"removeHotspotActiveByMac"),
            must_contain("removeHotspotCookiesByMac", DRIVER, r"removeHotspotCookiesByMac"),
            must_contain("keep user comment", DRIVER, r"Keep /ip/hotspot/user"),
        ),
    )
    check(
        "worker dispatches PauseHotspotUser",
        lambda: must_contain(
            "PauseHotspotUser",
            WORKER,
            r"OpType::PauseHotspotUser",
        ),
    )
    check(
        "duplicate pause is idempotent while pending/paused",
        lambda: must_contain(
            "pause already",
            PSM,
            r"routerPausePending[\s\S]{0,120}already\s*=\s*true",
        ),
    )
    check(
        "duplicate resume is idempotent while resumePending",
        lambda: must_contain(
            "resume already",
            PSM,
            r"resumePending[\s\S]{0,80}already\s*=\s*true",
        ),
    )
    check(
        "tick does not decrement secondsLeft while paused",
        lambda: must_contain(
            "active && !paused",
            PSM,
            r"if \(isActive && !isPaused\)",
        ),
    )
    check(
        "late activation cannot revive unexpected states",
        lambda: must_contain(
            "expected guard",
            PSM,
            r"A late activation result must never revive",
        ),
    )
    check(
        "pause path does not call RouterOS from heartbeat/session GET",
        lambda: must_not_contain(
            "heartbeat RouterOS",
            PSM,
            r"void PortalSessionManager::heartbeat[\s\S]{0,800}pauseHotspotUser",
        ),
    )

    failed = checks.count(False)
    print(
        f"pause-resume-contract-check: {len(checks) - failed}/{len(checks)} passed"
    )
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
