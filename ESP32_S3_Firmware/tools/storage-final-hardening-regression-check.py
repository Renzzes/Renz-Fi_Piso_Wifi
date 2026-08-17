#!/usr/bin/env python3
"""Static regression guard for SD storage final hardening."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def text(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


storage = text("ESP32_S3_Firmware/src/StorageManager.cpp")
config = text("ESP32_S3_Firmware/src/Config.h")
api = text("ESP32_S3_Firmware/src/ApiServer.cpp")
card = text("src/components/StorageHealthCard.tsx")
types = text("src/types/api.ts")

# Existing health contract remains.
for field in (
    '"mounted"',
    '"mode"',
    '"health"',
    '"totalSpace"',
    '"pendingReplay"',
    '"emergencyUsage"',
    '"warnings"',
):
    assert field in storage, f"missing preserved API field {field}"

# Additive serviceability fields.
for field in (
    '"readable"',
    '"writable"',
    '"pendingConflicts"',
    '"pendingHistory"',
    '"retryState"',
    '"retryRemaining"',
    '"recoveryMode"',
    '"watchMode"',
    '"diagnosticCause"',
    '"internalDiagnosticState"',
    '"lastSuccessfulReplay"',
    '"lastSdVerification"',
    '"replaySummary"',
    '"conflicts"',
):
    assert field in storage, f"missing additive API field {field}"

for cause in (
    "MEDIA_MISSING",
    "WRITE_PROBE_FAILED",
    "WRITE_VERIFICATION_FAILED",
    "TRANSACTION_FAILED",
    "RESTORE_BLOCKED",
    "FILESYSTEM_ERROR",
    "READ_ONLY",
    "UNKNOWN",
):
    assert cause in storage, f"missing diagnostic cause {cause}"

assert "Verifying write capability" in storage
assert "Verification passed" in storage
assert "file.flush()" in storage
assert "WRITE_VERIFICATION_FAILED" in storage
# Option A hotfix: absolute probe path; relative leaf must not be rejoined.
assert '"/temp/.write_probe"' in storage
assert 'joinPath(StoragePaths::Temp, ".write_probe"' not in storage
assert "Entering low-power watch mode" in storage
assert "STORAGE_WATCH_POLL_MS" in config
assert "disableSdPolling = true" in storage  # restore-blocked path retained
assert "SD polling disabled after 3 failed recovery attempts" not in storage
assert "Conflict detected" in storage
assert "no auto-merge" in storage
assert "Replay summary" in storage
assert "History replay" in storage
assert "Recovering fallback files" in storage
assert "Waiting for SD reinsertion" in storage

route_start = api.index('"/api/storage/status"')
route_end = api.index("});", route_start)
route = api[route_start:route_end]
assert '_storage->fillStorageStatus' in route
assert "Router" not in route and "MikroTik" not in route

assert "diagnosticCause" in types
assert "pendingConflicts" in types
assert "watchMode" in types
assert "Storage conflict detected" in card
assert "Storage replay completed" in card
assert "SD storage recovered" in card
assert "previousHealth.current === null" in card
assert "animate-" not in card

print("storage-final-hardening-regression-check: PASS")
