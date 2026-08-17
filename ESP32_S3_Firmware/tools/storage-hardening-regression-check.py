#!/usr/bin/env python3
"""Focused behavioral and source-contract regression tests for storage hardening."""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


def read(name: str) -> str:
    return (SRC / name).read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"missing function: {signature}")
    brace = source.find("{", start)
    if brace < 0:
        raise AssertionError(f"missing body: {signature}")
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:index]
    raise AssertionError(f"unterminated body: {signature}")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


@dataclass(frozen=True)
class Candidate:
    exists: bool
    valid: bool
    value: str = ""


def recovery_decision(
    target: Candidate, stage: Candidate, backup: Candidate
) -> tuple[str, str] | None:
    """Executable model of the firmware target/stage/backup recovery policy."""
    if target.exists and target.valid:
        return ("target", target.value)
    ordered = (("backup", backup), ("stage", stage)) if target.exists else (
        ("stage", stage),
        ("backup", backup),
    )
    for name, candidate in ordered:
        if candidate.exists and candidate.valid:
            return (name, candidate.value)
    return None


def test_transaction_recovery_model() -> None:
    old = Candidate(True, True, "old")
    new = Candidate(True, True, "new")
    missing = Candidate(False, False)
    corrupt = Candidate(True, False, "torn")

    power_loss_states = {
        "before_stage": (old, missing, missing, ("target", "old")),
        "stage_written": (old, new, missing, ("target", "old")),
        "old_moved_to_backup": (missing, new, old, ("stage", "new")),
        "new_promoted": (new, missing, old, ("target", "new")),
        "target_torn_with_backup": (corrupt, new, old, ("backup", "old")),
        "target_torn_stage_only": (corrupt, new, missing, ("stage", "new")),
        "stage_torn_backup_valid": (missing, corrupt, old, ("backup", "old")),
        "all_candidates_invalid": (corrupt, corrupt, corrupt, None),
    }
    for stage_name, (target, stage, backup, expected) in power_loss_states.items():
        actual = recovery_decision(target, stage, backup)
        require(actual == expected, f"{stage_name}: expected {expected}, got {actual}")

    storage = read("StorageManager.cpp")
    for signature in (
        "bool StorageManager::recoverSdTransaction",
        "bool StorageManager::recoverSpiffsTransaction",
    ):
        body = function_body(storage, signature)
        require("validateJsonPayload" in body, f"{signature} must validate candidates")
        require("targetExists ? backup : stage" in body,
                f"{signature} must prefer backup for an invalid existing target")
        require("targetExists ? stage : backup" in body,
                f"{signature} must use the alternate valid candidate")
        require(body.find("validateJsonPayload") < body.find("rename(selected"),
                f"{signature} must validate before promoting a candidate")


def test_mount_and_write_fallback_contract() -> None:
    firmware = read("FirmwareApp.cpp")
    storage = read("StorageManager.cpp")
    config = read("Config.h")
    require("SPIFFS.begin(true)" not in firmware + storage,
            "normal boot/mount code must never request SPIFFS formatting")
    require("SPIFFS.begin(false)" in firmware and "SPIFFS.begin(false)" in storage,
            "both SPIFFS mount paths must explicitly disable format-on-failure")

    match = re.search(r"STORAGE_WRITE_ATTEMPTS\s*=\s*(\d+)", config)
    require(match is not None and int(match.group(1)) == 2,
            "SD writes must use exactly two bounded attempts")
    write_sd = function_body(storage, "bool StorageManager::writeJsonToSdSerialized")
    require("writeJsonToSdOnce(path, serialized)" in write_sd,
            "every SD retry must receive the same immutable serialization")
    require(write_sd.find("writeJsonToSdOnce(path, serialized)") <
            write_sd.find("_sdWritable = false"),
            "fallback may only be enabled after SD retries are exhausted")
    write_public = function_body(storage, "bool StorageManager::writeJson(")
    require("const uint32_t originalPayloadCrc = payloadCrc(serialized)" in write_public,
            "fallback must retain an immutable payload fingerprint")
    require("payloadCrc(serialized) != originalPayloadCrc" in write_public,
            "fallback must reject payload mutation after the failed SD attempt")
    require("writeJsonToSpiffs(path, serialized" in write_public,
            "fallback must receive the exact serialization attempted on SD")

    attempted: list[str] = []
    payload = '{"eventId":"same-bytes"}'
    for _ in range(int(match.group(1))):
        attempted.append(payload)
    fallback = payload
    require(attempted == [payload, payload] and fallback == attempted[0],
            "retry/fallback model changed payload bytes")


def test_checkpoint_and_fallback_accounting() -> None:
    storage = read("StorageManager.cpp")
    checkpoint = function_body(
        storage, "bool StorageManager::isContinuousCheckpointEligible"
    )
    allowed = {
        "SETTINGS_FILE", "PROMOS_FILE", "ROUTER_FILE", "VOUCHERS_FILE",
        "PORTAL_SESSIONS_FILE", "InstallationFile", "ProvisioningFile",
        "RouterConnectionFile", "RouterProvisioningFile", "SetupWizardFile",
    }
    actual = set(re.findall(
        r"(?:RenzFiConfig::|StoragePaths::)([A-Za-z_][A-Za-z0-9_]*)", checkpoint
    ))
    require(actual == allowed,
            f"healthy checkpoint allowlist drifted: {sorted(actual ^ allowed)}")
    for forbidden in ("SALES_FILE", "LOGS_FILE", "RouterCacheFile",
                      "ExistingNetworkScanFile"):
        require(forbidden not in checkpoint,
                f"healthy checkpoints must exclude {forbidden}")

    accounting = function_body(storage, "size_t StorageManager::fallbackTotalBytes")
    for spool in ("SalesHistorySpool", "SessionsHistorySpool",
                  "VouchersHistorySpool"):
        require(spool in accounting, f"fallback accounting missing {spool}")
    require("base + StoragePaths::TransactionStageSuffix" in accounting,
            "fallback accounting must include every registered stage sidecar")
    require("base + StoragePaths::TransactionBackupSuffix" in accounting,
            "fallback accounting must include every registered backup sidecar")
    require(accounting.count(' + ".q"') == 3,
            "fallback accounting must include all spool quarantines")

    clear = function_body(storage, "void StorageManager::clearAllFallbackData")
    reset = function_body(storage, "bool StorageManager::factoryResetData")
    sync = function_body(storage, "bool StorageManager::syncFallbackToSd")
    require("NdjsonLedger::clearSpools()" in clear,
            "fallback clear must remove active and quarantine spools")
    require("NdjsonLedger::clearSpools()" in reset,
            "factory reset must remove active and quarantine spools")
    require("TransactionStageSuffix" in clear and "TransactionBackupSuffix" in clear,
            "fallback clear must remove transaction sidecars")
    require("TransactionStageSuffix" in sync and "TransactionBackupSuffix" in sync,
            "successful sync cleanup must remove fallback sidecars")


def test_divergent_sync_protection() -> None:
    storage = read("StorageManager.cpp")
    sync = function_body(storage, "bool StorageManager::syncFallbackToSd")
    conflict = "if (baseCrc == 0 || currentCrc != baseCrc)"
    write = "writeJsonToSdSerialized(sdPath, payload)"
    require(conflict in sync and write in sync and sync.find(conflict) < sync.find(write),
            "divergence check must happen before any fallback overwrite")
    conflict_block = sync[sync.find(conflict):sync.find(write)]
    require("continue;" in conflict_block and "divergent SD retained" in conflict_block,
            "divergent SD data must be retained and skipped")


def test_ledger_contract() -> None:
    ledger = read("NdjsonLedger.cpp")
    header = read("NdjsonLedger.h")
    bucket = function_body(ledger, "String NdjsonLedger::bucketFor")
    path = function_body(ledger, "bool NdjsonLedger::pathFor")
    require('return "undated"' in bucket and 'bucket.length() != 7' in ledger,
            "ledger must route malformed/absent dates to the undated bucket")
    require('"/" + bucket + ".ndjson"' in path and "validBucket(bucket)" in path,
            "ledger paths must be validated monthly or undated NDJSON paths")
    require("kTailDedupeBytes = 32U * 1024U" in header,
            "ledger dedupe scan must remain bounded to 32 KiB")
    dedupe = function_body(ledger, "bool NdjsonLedger::containsRecentEventId")
    require("size - kTailDedupeBytes" in dedupe and "discard a partial first line" in dedupe,
            "dedupe must scan only a line-aligned bounded tail")
    append = function_body(ledger, "bool NdjsonLedger::appendLineSd")
    require("file.read() != '\\n'" in append and "file.print('\\n')" in append,
            "append must tolerate and separate a torn ledger tail")
    spool = function_body(ledger, "bool NdjsonLedger::appendSpool")
    require("kMaxSpoolBytesPerKind" in spool and "FB_HARD_LIMIT_BYTES" in spool and
            "SPIFFS_MIN_FREE_BYTES" in spool,
            "spooling must enforce per-kind, aggregate, and free-space quotas")
    replay = function_body(ledger, "bool NdjsonLedger::replaySpools")
    require("appendSd(kind, eventId, eventAt" in replay,
            "spool replay must use idempotent ledger append")
    require("SPIFFS.remove(path)" in replay and 'String(path) + ".q"' in replay,
            "replay must clear completed spools and quarantine torn records")


def test_logger_and_history_api() -> None:
    logger = read("Logger.cpp")
    api = read("ApiServer.cpp")
    require("LOGS_FILE" not in logger and "logs.json" not in logger,
            "Logger must not rewrite the legacy logs.json array")
    write = function_body(logger, "void Logger::write")
    require("appendHistory(NdjsonLedger::Kind::Logs" in write,
            "Logger must append to the NDJSON history ledger")

    register = function_body(api, "auto registerHistoryDownload")
    require("requireOwnerAuth(req)" in register,
            "history downloads must be owner-only")
    require("req->beginResponse(SD, path.c_str(), \"application/x-ndjson\")" in register,
            "history downloads must stream directly from the SD file")
    require("readString" not in register and "serializeJson" not in register,
            "history downloads must not materialize files into RAM")
    for route in ("sales", "sessions", "vouchers"):
        require(f'"/api/history/{route}/download"' in api,
                f"missing owner-streamed {route} history endpoint")


def test_restore_boot_and_journal_contract() -> None:
    firmware = read("FirmwareApp.cpp")
    backup = read("BackupManager.cpp")
    recovery = function_body(backup, "bool BackupManager::recoverPendingRestore")

    recovery_pos = firmware.find("BackupManager::recoverPendingRestore")
    first_manager_pos = firmware.find("_buildMetadata.begin")
    require(0 <= recovery_pos < first_manager_pos,
            "restore recovery must finish before any subsystem manager begins")
    require("_bootBlocked = true" in firmware[recovery_pos:first_manager_pos],
            "failed restore recovery must block subsystem startup")

    for candidate in ("RESTORE_JOURNAL_PATH", "RESTORE_JOURNAL_STAGE_PATH",
                      "RESTORE_JOURNAL_BACKUP_PATH"):
        require(candidate in recovery, f"restore recovery missing {candidate}")
    load = function_body(backup, "bool loadJournalCandidate")
    for state in ("commit_pending", "commit_complete", "rollback_complete"):
        require(f'"{state}"' in load, f"journal parser missing {state}")
    require("rollbackAction" in load and "hadOriginal" in load,
            "journal recovery must validate explicit rollback actions")
    require("commitComplete ? finalizeCommittedRestore(entries)" in recovery and
            ": rollbackRestore(entries)" in recovery,
            "journal state must select cleanup versus rollback")


def test_restore_limits_entries_and_upload_auth() -> None:
    backup = read("BackupManager.cpp")
    api = read("ApiServer.cpp")
    require("kMaxJsonBackupBytes = 128U * 1024U" in backup,
            "JSON restore input cap must remain 128 KiB")
    restore_json = function_body(backup, "bool BackupManager::restoreFromJsonFile")
    require("source.size() > kMaxJsonBackupBytes" in restore_json,
            "JSON restore cap must be checked before parsing")
    require(restore_json.find("source.size() > kMaxJsonBackupBytes") <
            restore_json.find("DynamicJsonDocument doc"),
            "oversized JSON must be rejected before allocating the restore document")

    entries_block = backup[
        backup.find("constexpr BackupEntry kJsonEntries[]"):
        backup.find("constexpr BackupEntry kAssetEntries[]")
    ]
    actual_entries = set(re.findall(r'\{"(/[^"]+\.json)"', entries_block))
    required_entries = {
        "/config/settings.json", "/config/router.json",
        "/config/portal-config.json", "/config/promos.json",
        "/config/vouchers.json", "/config/sales.json", "/config/users.json",
    }
    require(actual_entries == required_entries,
            f"v1 JSON entry set drifted: {sorted(actual_entries ^ required_entries)}")
    require("requiredCount != sizeof(kJsonEntries) / sizeof(kJsonEntries[0])" in backup,
            "JSON backups must contain every required v1 entry exactly once")

    upload = function_body(api, "auto restoreUploadHandler")
    auth_pos = upload.find("requireOwnerAuth(req)")
    staging_pos = upload.find('if (!SD.exists("/backup")) SD.mkdir("/backup")')
    open_pos = upload.find("SD.open(BackupManager::TEMP_RESTORE_PATH, FILE_WRITE)")
    require(auth_pos >= 0 and staging_pos > auth_pos and open_pos > auth_pos,
            "restore upload authentication must precede directory/file staging")
    require("gRestoreUpload.file.write(data, len)" in upload,
            "restore uploads must stream chunks to SD, not accumulate in RAM")
    require("gRestoreUpload.received > 3 * 1024 * 1024" in upload,
            "streamed restore upload must have a bounded archive cap")
    restore_route_start = api.find('"/api/settings/restore", HTTP_POST')
    restore_route = api[restore_route_start:restore_route_start + 3200]
    require("restoreUploadHandler, bodyCollect" in restore_route,
            "restore route must use the small bounded body collector, never largeBodyCollect")
    body_collect = function_body(api, "void bodyCollect")
    require("total > 8192" in body_collect,
            "non-multipart restore payload vector must remain capped at 8 KiB")


TESTS = (
    test_transaction_recovery_model,
    test_mount_and_write_fallback_contract,
    test_checkpoint_and_fallback_accounting,
    test_divergent_sync_protection,
    test_ledger_contract,
    test_logger_and_history_api,
    test_restore_boot_and_journal_contract,
    test_restore_limits_entries_and_upload_auth,
)


def main() -> int:
    failures: list[str] = []
    for test in TESTS:
        try:
            test()
            print(f"storage-hardening-regression-check: PASS — {test.__name__}")
        except (AssertionError, OSError) as exc:
            failures.append(f"{test.__name__}: {exc}")
    if failures:
        for failure in failures:
            print(f"storage-hardening-regression-check: FAIL — {failure}",
                  file=sys.stderr)
        return 1
    print(
        "storage-hardening-regression-check: OK "
        f"({len(TESTS)} focused storage invariants passed)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
