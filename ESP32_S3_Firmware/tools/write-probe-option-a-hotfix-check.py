#!/usr/bin/env python3
"""Static regression guard for WRITE_PROBE_FAILED Option A hotfix."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
storage = (ROOT / "ESP32_S3_Firmware/src/StorageManager.cpp").read_text(
    encoding="utf-8"
)
paths = (ROOT / "ESP32_S3_Firmware/src/StoragePaths.cpp").read_text(encoding="utf-8")

probe_start = storage.index("bool StorageManager::probeSdWritable()")
probe_end = storage.index("bool StorageManager::isSdWritable()", probe_start)
probe = storage[probe_start:probe_end]

assert "ensureSdDirectory(StoragePaths::Temp)" in probe
assert 'kWriteProbePath[] = "/temp/.write_probe"' in probe
assert "isValidSdPath(kWriteProbePath)" in probe
assert "SD.open(probePath, FILE_WRITE)" in probe
assert "file.flush()" in probe
assert "SD.open(probePath, FILE_READ)" in probe
assert "SD.remove(probePath)" in probe
assert "WRITE_PROBE_FAILED" in probe
assert "WRITE_VERIFICATION_FAILED" in probe
assert "Verification passed" in probe
assert 'joinPath(StoragePaths::Temp, ".write_probe"' not in probe
assert "StoragePaths::joinPath(" not in probe

# Shared path APIs must remain unchanged by this hotfix.
assert "if (!isValidSdPath(filename)) return false;" in paths
assert "bool joinPath(const char *dir, const char *leaf" in paths

print("write-probe-option-a-hotfix-check: PASS")
