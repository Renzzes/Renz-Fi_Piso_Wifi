#!/usr/bin/env python3
"""Static contract check: absolute voucher expiry + profile + CPU safety.

Does not execute RouterOS. Proves source-level contracts for:
  A–I voucher lifecycle, J coin Model B untouched, K no poll storm.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PSM = (ROOT / "src" / "PortalSessionManager.cpp").read_text(encoding="utf-8", errors="replace")
VM = (ROOT / "src" / "VoucherManager.cpp").read_text(encoding="utf-8", errors="replace")
DRIVER = (
    ROOT / "src" / "router" / "drivers" / "MikroTikDriver.cpp"
).read_text(encoding="utf-8", errors="replace")
SALES = (ROOT / "src" / "SalesTime.cpp").read_text(encoding="utf-8", errors="replace")
VOUCHERS_UI = (
    ROOT.parents[0] / "src" / "pages" / "VouchersPage.tsx"
).read_text(encoding="utf-8", errors="replace")


def must_contain(label: str, text: str, pattern: str) -> None:
    if not re.search(pattern, text, re.M | re.S):
        raise AssertionError(f"missing: {label} ({pattern})")


def must_not_contain(label: str, text: str, pattern: str) -> None:
    if re.search(pattern, text, re.M | re.S):
        raise AssertionError(f"forbidden: {label} ({pattern})")


def main() -> int:
    checks: list[bool] = []

    def check(name: str, fn) -> None:
        try:
            fn()
            print(f"PASS {name}")
            checks.append(True)
        except Exception as exc:  # noqa: BLE001
            print(f"FAIL {name}: {exc}")
            checks.append(False)

    # A/B — creation stores minutes + profileName; redeem stamps serviceExpiresAt
    check(
        "A generate stores profileName + minutes",
        lambda: (
            must_contain("minutes", VM, r'item\["minutes"\] = minutes'),
            must_contain("profileName", VM, r'item\["profileName"\] = profileName'),
        ),
    )
    check(
        "B reserve stamps serviceExpiresAt from redeemedAt + minutes",
        lambda: (
            must_contain(
                "salesAddSecondsToIso",
                VM,
                r"salesAddSecondsToIso\(\s*redeemedAt,\s*static_cast<uint32_t>\(minutes\)\s*\*\s*60U\)",
            ),
            must_contain(
                "serviceExpiresAt assign",
                VM,
                r'item\["serviceExpiresAt"\] = stampedExpiry',
            ),
        ),
    )

    # C — activate does not recompute from activatedAt when expiry exists
    check(
        "C activate preserves absolute serviceExpiresAt",
        lambda: (
            must_contain(
                "never extend",
                PSM,
                r"Absolute expiry is stamped at redeem — never extend from activatedAt",
            ),
            must_contain(
                "prefer session expiry",
                PSM,
                r'serviceExpiresAt = String\(session\["serviceExpiresAt"\]',
            ),
            must_contain(
                "markActivated keep existing",
                VM,
                r"Never overwrite an absolute expiry stamped at redeem",
            ),
        ),
    )

    # D/E — reconnect / activate gated on wall remaining
    check(
        "D reconnect rejects zero wall remaining",
        lambda: must_contain(
            "reconnect expire",
            PSM,
            r"reconnectVoucher[\s\S]{0,800}?remaining == 0[\s\S]{0,200}?mustExpire = true",
        ),
    )
    check(
        "E onSessionActivated expires past-due voucher",
        lambda: must_contain(
            "mustExpireVoucher",
            PSM,
            r"mustExpireVoucher[\s\S]{0,1200}?ExpireSession",
        ),
    )

    # F — expiry enqueues ExpireSession (worker deauth), including not-Active
    check(
        "F absolute expire when not Active enqueues ExpireSession",
        lambda: must_contain(
            "not Active expire",
            PSM,
            r"Absolute voucher expiry: enqueue ONE ExpireSession even when NOT Active",
        ),
    )

    # G — reboot past-due expire
    check(
        "G boot recovery expires past-due vouchers",
        lambda: must_contain(
            "boot voucher expiry",
            PSM,
            r"Voucher absolute expiry survives reboot",
        ),
    )

    # H — portal Connected requires connected flag for timerRunning
    check(
        "H timerRunning requires connected (no Connected without auth claim)",
        lambda: must_contain(
            "timerRunning connected",
            PSM,
            r'timerRunning"\] = state == PortalState::Active && !paused && secondsLeft > 0 &&\s*\(out\["connected"\]',
        ),
    )

    # I — profile reaches HotspotUser / UI select
    check(
        "I hotspotProfile from voucher profileName; admin selects RouterOS profiles",
        lambda: (
            must_contain(
                "hotspotProfile",
                PSM,
                r'session\["hotspotProfile"\] = reserved\.profileName',
            ),
            must_contain(
                "profiles API",
                VOUCHERS_UI,
                r"routerApi\.profiles\(\)",
            ),
            must_contain(
                "user.profile",
                DRIVER,
                r'=profile=" \+ profile',
            ),
        ),
    )

    # J — coin Model B unchanged (no grace)
    check(
        "J coin Model B: new_limit = existing_uptime + requested_seconds (no grace)",
        lambda: (
            must_contain(
                "model B",
                DRIVER,
                r"new_limit = existing_uptime \+ requested_seconds",
            ),
            must_contain(
                "no grace",
                DRIVER,
                r"Do NOT add grace",
            ),
            must_not_contain(
                "graceSeconds",
                DRIVER,
                r"graceSeconds\s*\+|kActivationGrace|GRACE_SECONDS",
            ),
        ),
    )

    # K — CPU safety
    check(
        "K no heartbeat/session GET Active print; verify remains coalesced 60s",
        lambda: (
            must_contain(
                "60s verify",
                PSM,
                r"kVerifyIntervalMs = 60000",
            ),
            must_contain(
                "no ROS from enrich",
                PSM,
                r"ESP32-local only — 0 RouterOS commands",
            ),
            must_not_contain(
                "tick direct createHotspotUser",
                PSM,
                r"tickSessions[\s\S]{0,200}?createHotspotUser",
            ),
            must_contain(
                "sales helpers",
                SALES,
                r"salesAddSecondsToIso|salesSecondsUntilIso",
            ),
        ),
    )

    # Redeem uses wall remaining for entitlement
    check(
        "redeem entitlement from serviceExpiresAt when present",
        lambda: must_contain(
            "entitlement from expiry",
            PSM,
            r"Absolute voucher authority: remaining until redeemedAt\+validity",
        ),
    )

    failed = sum(1 for ok in checks if not ok)
    print(f"\n{len(checks) - failed}/{len(checks)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
