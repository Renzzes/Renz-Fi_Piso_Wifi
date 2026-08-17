# WRITE_PROBE_FAILED Hotfix (Option A) — Implementation Report

Date: 2026-08-10  
Scope: Probe-local absolute path only — no StoragePaths / asset / RouterWorker changes  
Verdict: **IMPLEMENTED — HARDWARE VALIDATION REQUIRED**

## Release verdict

Software integration: **PASS**  
Production ready: **NO** until appliance hardware checklist below passes.

## Why Option A

Forensic + pre-implementation verification proved `joinPath("/temp", ".write_probe")` fails because `isValidSdPath(".write_probe")` requires a leading `/`, so the probe never reached `SD.open` and falsely set `WRITE_PROBE_FAILED` → `READ_ONLY`.

Option A replaces only path construction with the absolute path `"/temp/.write_probe"`.

## Why Options B/C/D were rejected

| Option | Rejected because |
|---|---|
| B — change `joinPath` for relative leaves | Changes shared composition semantics |
| C — change `joinAssetPath` validation | Would alter AssetResolver contract-tier success / fallthrough |
| D — new `joinLeaf()` | Extra API surface; unnecessary when absolute path is known |

## Files modified

| File | Change |
|---|---|
| `ESP32_S3_Firmware/src/StorageManager.cpp` | Option A path construction in `probeSdWritable()` |
| `ESP32_S3_Firmware/tools/storage-final-hardening-regression-check.py` | Assert absolute probe path; ban relative join |
| `ESP32_S3_Firmware/tools/write-probe-option-a-hotfix-check.py` | New static guard for probe-only scope |
| `WRITE_PROBE_OPTION_A_HOTFIX_IMPLEMENTATION_REPORT.md` | This report |

**Unchanged:** `StoragePaths.cpp/.h`, AssetManager, AssetResolver, RouterWorker, fallback, replay, conflict, restore, journal, backup, history, Admin UI.

## Exact code change

In `StorageManager::probeSdWritable()`:

**Before:** `StoragePaths::joinPath(StoragePaths::Temp, ".write_probe", probePath, …)`

**After:**

```cpp
static constexpr const char kWriteProbePath[] = "/temp/.write_probe";
if (!StoragePaths::isValidSdPath(kWriteProbePath)) {
  setDiagnosticCause("WRITE_PROBE_FAILED");
  return false;
}
const char *probePath = kWriteProbePath;
```

Preserved:

- `ensureSdDirectory(StoragePaths::Temp)`
- Full-path `isValidSdPath` on the absolute probe path
- Write → flush → close → reopen → read → compare → delete → verify deletion
- `WRITE_PROBE_FAILED` / `WRITE_VERIFICATION_FAILED` cause mapping
- Success: `_sdWritable = true` + `Verification passed`

## Software verification

| Check | Result |
|---|---|
| `write-probe-option-a-hotfix-check.py` | PASS |
| `storage-final-hardening-regression-check.py` | PASS |
| `pio run -e freenove_esp32_s3_wroom` | SUCCESS |

## Resource comparison

Env: `freenove_esp32_s3_wroom`

| Metric | Pre-hotfix (final hardening build) | This build | Delta |
|---|---|---|---|
| Static RAM | 106,292 / 327,680 (32.4%) | 106,292 / 327,680 (32.4%) | **0** |
| Flash | 2,381,931 / 2,621,440 (90.9%) | 2,381,791 / 2,621,440 (90.9%) | **−140 bytes** |
| DMA architecture | unchanged | unchanged | same |
| CPU idle tasks | unchanged | unchanged | same |
| Idle RouterOS cmds | 0 | 0 (no code path added) | same |

Heap: no new dynamic allocation in the probe path (string literal + stack File ops as before).

## Boot log — before vs after (expected)

### Before (observed / forensic)

```
SD.begin OK
[storage] Verifying write capability
… (silent joinPath reject)
Cause=WRITE_PROBE_FAILED
Health=READ_ONLY
Sales storage = SPIFFS fallback
```

### After (expected on writable media — hardware TBD)

```
SD.begin OK
[storage] Verifying write capability
[storage] Verification passed
[storage] Sales storage = SD
[storage] Health=HEALTHY   (and diagnosticCause=OK)
```

Unexpected after fix on healthy media: `WRITE_PROBE_FAILED`, `READ_ONLY`, or `WRITE_VERIFICATION_FAILED` (latter only if real I/O fails).

## Regression analysis (Option A scope)

| Area | Impact |
|---|---|
| RouterWorker / Router sync / MikroTik / idle ROS cmds | None |
| Coin / Voucher / Portal / Session / Sales managers | None (benefit if SD becomes writable) |
| SPIFFS fallback / replay / restore / backup / conflicts | Logic unchanged; success path already clears fallback |
| Asset loading / banner / music / downloads | Unchanged (`joinAssetPath` untouched) |
| Admin / EventBus / SSE | Unchanged |
| Hot-plug / watch mode | Unchanged; probe now can succeed on remount |
| Polling / new background tasks | None |

## Hardware validation checklist (required)

Run on ESP32-S3 + W5500 + SD + MikroTik:

- [ ] Cold boot → `Verification passed`, `Sales storage = SD`, `Health=HEALTHY`
- [ ] Warm reboot → same
- [ ] Probe file created then deleted (`/temp/.write_probe` absent after boot)
- [ ] Sales / sessions / history land on SD; fallback inactive; replay idle when clean
- [ ] Remove SD → fallback; reinsert → watch remount → replay → SD mode without reboot
- [ ] Genuine RO media (if available) fails at real I/O, not path construction
- [ ] Idle RouterOS = 0; no TWDT; DMA/heap stable; coin/voucher/portal/done-paying OK

## Final statement

**IMPLEMENTED — HARDWARE VALIDATION REQUIRED**

Do not mark Production Ready or RELEASE CANDIDATE until the checklist above passes on the physical appliance.
