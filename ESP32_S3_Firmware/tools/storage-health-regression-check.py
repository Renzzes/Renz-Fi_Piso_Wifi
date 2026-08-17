#!/usr/bin/env python3
"""Static regression guard for owner-visible SD health monitoring."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def text(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


storage = text("ESP32_S3_Firmware/src/StorageManager.cpp")
api = text("ESP32_S3_Firmware/src/ApiServer.cpp")
card = text("src/components/StorageHealthCard.tsx")
events = text("src/hooks/useDashboardEvents.ts")

route_start = api.index('"/api/storage/status"')
route_end = api.index("});", route_start)
route = api[route_start:route_end]

required_fields = (
    '"mounted"',
    '"mode"',
    '"health"',
    '"totalSpace"',
    '"freeSpace"',
    '"usedSpace"',
    '"journalHealthy"',
    '"lastWrite"',
    '"pendingReplay"',
    '"emergencyUsage"',
    '"crcHealthy"',
    '"recoveryQueue"',
    '"warnings"',
)
for field in required_fields:
    assert field in storage, f"missing API telemetry field {field}"

for state in ("HEALTHY", "DEGRADED", "WARNING", "CRITICAL", "READ_ONLY", "UNKNOWN"):
    assert state in storage and state in card, f"missing health state {state}"

assert ">= 70" in storage and ">= 90" in storage, "threshold contract changed"
assert '_storage->fillStorageStatus' in route, "storage route no longer uses cached snapshot"
assert "Router" not in route and "MikroTik" not in route, "storage route gained RouterOS work"
assert '["storage", "status"]' in events, "storage SSE invalidation missing"
assert "previousHealth.current === null" in card, "initial notification suppression missing"
assert "animate-" not in card, "storage health card must not animate"
assert "refetchInterval: fallbackPollMs" in text(
    "src/hooks/api/useStorageHealth.ts"
), "storage health must reuse the existing fallback interval"

print("storage-health-regression-check: PASS")
