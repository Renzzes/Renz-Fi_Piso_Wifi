# SD Storage Final Hardening — Implementation Report

Date: 2026-08-09  
Scope: post-forensic stability hardening for SD write verification, diagnostics, hot-plug watch recovery, replay/conflict visibility, and additive storage health API  
Verdict: **IMPLEMENTED — HARDWARE VALIDATION REQUIRED**

## Release verdict

Software integration: **PASS**  
Production release: **NO-GO** until the hardware validation checklist below passes on the ESP32-S3 + SD + MikroTik appliance.

Do not mark Production Ready without cold/warm boot, SD removal/reinsertion, long-removed fallback, replay, and RouterWorker/DMA/heap stability evidence.

## Forensic findings (implemented only)

| Finding | Root cause | Fix |
|---|---|---|
| Weak write probe | Create/write/close/delete without flush/read-back/delete verify | True write verification before `_sdWritable = true` |
| Opaque READ_ONLY | Single owner label hid MEDIA_MISSING / probe / transaction / restore causes | Internal diagnostic cause enum exposed by API |
| Hot-plug dead-end | After 3 retries, polling disabled forever for the boot | Low-power watch mode every 5 minutes |
| Thin recovery logs | Limited operator clarity | Explicit `[storage]` lifecycle logs |
| Silent conflict risk | Divergent SPIFFS≠SD could be retained without owner visibility | Conflict records + replay summary; no auto-merge |
| Incomplete API | Status lacked retry/watch/diagnostic/replay detail | Additive `/api/storage/status` fields only |
| READ_ONLY→writable gap | Probe restore synced JSON but skipped history replay | History spool replay after write capability returns |

## Files modified

- `ESP32_S3_Firmware/src/Config.h` — `STORAGE_WATCH_POLL_MS` (300000), conflict/replay caps
- `ESP32_S3_Firmware/src/StorageManager.h` — watch mode, diagnostics, conflict/replay state
- `ESP32_S3_Firmware/src/StorageManager.cpp` — all 1–8 behaviors
- `src/types/api.ts` — additive StorageStatus types
- `src/components/StorageHealthCard.tsx` — serviceability rows + transition toasts
- `ESP32_S3_Firmware/tools/storage-final-hardening-regression-check.py` — static guard
- `SD_STORAGE_FINAL_HARDENING_IMPLEMENTATION_REPORT.md` — this report

API route handler unchanged except consuming expanded `fillStorageStatus()` output. Existing fields preserved.

## Exact reasoning by implementation

### 1) Write verification
Flow now: create temp → write `"ok"` → flush → close → reopen → read-back compare → delete → verify deletion. Only then `_sdWritable = true`. Failures set `WRITE_PROBE_FAILED` or `WRITE_VERIFICATION_FAILED`. Stack buffer only; no heap growth for probe payload.

### 2) Internal diagnostics
Causes: `MEDIA_MISSING`, `WRITE_PROBE_FAILED`, `WRITE_VERIFICATION_FAILED`, `TRANSACTION_FAILED`, `RESTORE_BLOCKED`, `FILESYSTEM_ERROR`, `READ_ONLY`, `UNKNOWN`, plus `OK` when healthy. Owner UI still maps health to Healthy / Warning / Read Only / Critical / Degraded.

### 3) Hot-plug watch mode
Initial retries remain 1-minute health poll. After 3 failures: enter watch mode (5-minute `SD.cardType()` check → remount if present). Restore-blocked / `markDegraded()` still disables polling (integrity halt). Manual `POST /api/storage/retry-sd` clears watch and retry budget.

### 4) Recovery logging
Examples now present: Detected SD removal, Entering fallback mode, Waiting for SD reinsertion, SD detected, Verifying write capability, Verification passed, Recovering fallback files, Replay complete / Replay summary.

### 5) Replay completeness
JSON sync and history replay produce a RAM-held summary: recovered file labels, history record count, skipped, conflicts. Conflicts are logged and retained; never silently discarded or auto-merged.

### 6) Split-brain detection only
On divergent CRC / generation mismatch: record path, generation, baseCrc, sdCrc, fallbackCrc, timestamp. SD copy retained. Owner decides via Admin visibility.

### 7) Storage health API (additive)
`GET /api/storage/status` still returns all prior fields and additionally: readable, writable, pendingConflicts, pendingHistory, retryState, retryRemaining, lastSuccessfulWrite, lastSuccessfulReplay, lastSdVerification, recoveryMode, watchMode, diagnosticCause / internalDiagnosticState, replaySummary, conflicts.

### 8) Owner notifications
Transition-only toasts: health changes, recovered, replay completed, conflict detected. No new poll interval; existing SSE/`useStorageHealth` fallback interval unchanged.

## Regression analysis

| Area | Result |
|---|---|
| Captive Portal | No path changes |
| Coin / Voucher / Sales / Session | No manager redesign; durability still via existing StorageManager/NdjsonLedger |
| RouterWorker / Router sync / MikroTik cmds | Unchanged; storage route has no RouterOS work |
| Idle RouterOS commands | Still 0 |
| SPIFFS fallback | Preserved; watch mode improves remount chance |
| SD corruption risk | Reduced by stronger probe; conflicts never auto-overwrite |
| Admin Dashboard | Additive fields only |
| Installation state | Unchanged |
| Continuous polling | Reduced after retry budget (60s → 300s watch) |
| TWDT / DMA / RouterWorker timing | No new continuous tasks; watch interval is coarser |

Static guards:
- `storage-health-regression-check.py`: PASS
- `storage-final-hardening-regression-check.py`: PASS

## Memory / flash / CPU / RouterOS comparison

Production env `freenove_esp32_s3_wroom` after this change:

| Metric | Prior health release | This build | Delta |
|---|---|---|---|
| Static RAM | 105,948 / 327,680 (32.3%) | 106,292 / 327,680 (32.4%) | **+344 bytes** |
| Flash | 2,376,203 / 2,621,440 (90.6%) | 2,381,931 / 2,621,440 (90.9%) | **+5,728 bytes** |
| CPU idle tasks | health poll 60s | healthy/retry 60s; watch 300s | same or lower after failures |
| DMA | unchanged architecture | unchanged | expected same/better (no new buffers) |
| RouterOS idle cmds | 0 | 0 | unchanged |
| Router sync | unchanged | unchanged | unchanged |

Heap impact: probe uses stack buffer; conflict/replay summaries are fixed static members (no unbounded allocations).

## Hardware validation checklist

Must pass on real ESP32-S3 + SD + MikroTik appliance:

- [ ] Cold boot with healthy SD → HEALTHY, diagnosticCause=OK, verification log present
- [ ] Warm reboot → same
- [ ] SD removal during operation → MEDIA_MISSING / fallback logs; portal/coin/voucher continue on SPIFFS
- [ ] SD reinsertion within first 3 minutes → remount, verify, replay, HEALTHY
- [ ] SD removed 5 minutes → watch mode entered; remount succeeds without reboot
- [ ] SD removed 1 hour → watch-mode remount + replay still works
- [ ] SD removed 24 hours → emergency quota still bounded; remount + replay; no TWDT
- [ ] Forced read-only / probe fail → READ_ONLY with WRITE_* or READ_ONLY cause; sales/voucher spool
- [ ] Read-only recovery to writable → history replay runs; Replay summary populated
- [ ] Deliberate SPIFFS≠SD conflict → conflicts[] exposed; no auto-overwrite
- [ ] Sales / voucher / coin / portal session after fallback and after recovery
- [ ] RouterWorker timing unchanged; idle RouterOS command count = 0
- [ ] DMA / heap / PSRAM margins stable under removal+reinsert soak
- [ ] CPU stable; no continuous SD polling; no TWDT
- [ ] Admin: Healthy→Read Only→Critical→Recovered / Replay Completed / Conflict Detected toasts only on transitions
- [ ] Manual Retry SD Card still recovers restore-non-blocked cases

## Release statement

**IMPLEMENTED — HARDWARE VALIDATION REQUIRED**

Software criteria for this stability release are met. Production candidate status requires completing the hardware checklist above.
