# WRITE_PROBE_FAILED Root Cause Forensic

Date: 2026-08-10  
Scope: Investigation only — **no code changes, no behavior changes**  
Symptom: `Health=READ_ONLY` + `Cause=WRITE_PROBE_FAILED` after successful `SD.begin`  
Verdict: **Verification logic false-negative** (path join rejects relative leaf before any SD write I/O)

## Executive conclusion

| Question | Proven answer |
|---|---|
| Exact failing instruction | `StoragePaths::isValidSdPath(".write_probe")` returns `false` because `path[0] != '/'` |
| Exact library/call site | `StoragePaths.cpp` `joinAssetPath()` line that calls `isValidSdPath(filename)` before concatenating |
| Triggered from | `StorageManager::probeSdWritable()` → `StoragePaths::joinPath("/temp", ".write_probe", …)` |
| Does `SD.open` / write / flush / read / remove run? | **No — unreachable on this failure path** |
| Is the SD proven unwritable? | **No.** Mount succeeded; Windows shows files; probe never performed media I/O after the path check |
| Why READ_ONLY? | `_sdWritable` left `false` after probe returns `false`; snapshot maps mounted+readable+!writable → `READ_ONLY` while preserving cause `WRITE_PROBE_FAILED` |
| Confidence | **Very high (static proof).** Runtime log order matches: `Verifying write capability` then cause `WRITE_PROBE_FAILED` with no `Verification passed` and no `Unable to create directory /temp` |

---

## Investigation A — Complete call graph

### Cold boot (observed path)

```
FirmwareApp::begin()
  Phase 1: EthernetManager::begin()          // W5500 / SPI3 — before SD
  Phase 2: SPIFFS.begin(false)
  Phase 3: StorageManager::begin()
            ├─ createStorageMutex()
            ├─ ScopedStorageLock
            ├─ mountSpiffs()
            ├─ mountSdCard("SD mount", false)
            │    ├─ renzFiSdSpiBegin(false)   // FSPI / SPI2 only
            │    └─ SD.begin(cs, renzFiSdSpi(), SD_SPI_FREQ_HZ)
            ├─ _healthy = _sdReadable (= true when begin OK)
            └─ probeSdWritable()             // FAIL → WRITE_PROBE_FAILED
                 ├─ ScopedStorageLock (recursive)
                 ├─ Serial: "[storage] Verifying write capability"
                 ├─ ensureSdDirectory("/temp")
                 │    ├─ ScopedStorageLock
                 │    ├─ isValidSdPath("/temp") → true
                 │    └─ ensureDir("/temp") → SD.exists or SD.mkdir
                 ├─ StoragePaths::joinPath("/temp", ".write_probe", …)  ★ FAILS
                 │    └─ joinAssetPath("/temp", ".write_probe", …)
                 │         └─ isValidSdPath(".write_probe") → FALSE   ★ EXACT FAIL
                 ├─ setDiagnosticCause("WRITE_PROBE_FAILED")
                 └─ return false  (_sdWritable remains false)
            ├─ (cause already WRITE_PROBE_FAILED — not overwritten to READ_ONLY)
            ├─ recoverBootTransactions / restore journal…
            ├─ validateLayout(false)  // read-only path; no write layout create
            ├─ _usingFallback = true if SPIFFS mounted
            └─ refreshRuntimeSnapshot() → health READ_ONLY, cause WRITE_PROBE_FAILED
```

### Other callers of `probeSdWritable()` (same joinPath bug)

| Caller | File | When |
|---|---|---|
| `StorageManager::begin()` | `StorageManager.cpp` | Cold/warm boot Phase 3 |
| `StorageManager::attemptSdRecovery()` | `StorageManager.cpp` | Manual retry / poll remount / watch remount |
| `StorageManager::verifySdHealthy()` | `StorageManager.cpp` | Periodic health when already mounted but `!_sdWritable` |

Every caller uses the same probe; all fail at the same joinPath instruction while the leaf remains `.write_probe`.

### Source files involved

- `ESP32_S3_Firmware/src/FirmwareApp.cpp` — boot ordering
- `ESP32_S3_Firmware/src/StorageManager.cpp` / `.h` — probe + health assignment
- `ESP32_S3_Firmware/src/StoragePaths.cpp` / `.h` — `joinPath` / `isValidSdPath` / `Temp="/temp"`
- `ESP32_S3_Firmware/src/SdSpi.cpp` — FSPI isolation from W5500
- `ESP32_S3_Firmware/src/Config.h` — SD pins / `SD_SPI_FREQ_HZ`
- Arduino `SD` / `FS` — **not reached** on this failing path after mount

---

## Investigation B — Probe sequence (logical instrumentation, no code change)

Printed immediately before work: `[storage] Verifying write capability`

| # | Intended step | Function | Return / check | Failure → cause | Prints? | Reached on current boot? |
|---|---|---|---|---|---|---|
| 0 | Lock + `_healthy` | `probeSdWritable` | lock false / `!_healthy` → return false **without** setting WRITE_PROBE_FAILED | (none / prior cause) | lock timeout only | Yes (healthy true after mount) |
| 1 | Ensure `/temp` | `ensureSdDirectory` → `ensureDir` | `exists` true **or** `mkdir` true | WRITE_PROBE_FAILED | `Unable to create directory /temp` on mkdir fail | Almost certainly **yes** (Windows shows `temp/`) |
| 2 | Build path string | `StoragePaths::joinPath` | false if leaf invalid | WRITE_PROBE_FAILED | **silent** | **YES — FAILS HERE** |
| 3 | `SD.open(FILE_WRITE)` | Arduino SD | falsy File | WRITE_PROBE_FAILED | silent | **No (unreachable)** |
| 4 | `file.print("ok")` | Arduino File | `written != 2` | WRITE_PROBE_FAILED | silent | No |
| 5 | `flush` / `close` | Arduino File | not checked | — | — | No |
| 6 | Re-open READ | `SD.open(FILE_READ)` | falsy | WRITE_VERIFICATION_FAILED | silent | No |
| 7 | `read` + `strcmp` | File / libc | n!=2 or mismatch | WRITE_VERIFICATION_FAILED | silent | No |
| 8 | `SD.remove` + `!exists` | Arduino SD | remove false or still exists | WRITE_VERIFICATION_FAILED | silent | No |
| 9 | Success | sets `_sdWritable=true` | — | OK | `Verification passed` | **Not observed** |

`errno` is never consulted. SD library status beyond boolean File / bool returns is not checked.

**Cause discrimination:** Observed `WRITE_PROBE_FAILED` (not `WRITE_VERIFICATION_FAILED`) proves failure is in steps 1–4 only. Silent failure after “Verifying…” with no mkdir error strongly selects step 2.

---

## Investigation C — Exact failing instruction

```223:228:ESP32_S3_Firmware/src/StoragePaths.cpp
bool isValidSdPath(const char *path) {
  if (!path || path[0] != '/') return false;
  if (strstr(path, "..") != nullptr) return false;
  if (strlen(path) >= 128) return false;
  return true;
}
```

```81:84:ESP32_S3_Firmware/src/StoragePaths.cpp
bool joinAssetPath(const char *dir, const char *filename, char *out,
                   size_t outSize) {
  if (!filename || !out || outSize == 0) return false;
  if (!isValidSdPath(filename)) return false;
```

```234:243:ESP32_S3_Firmware/src/StoragePaths.cpp
bool joinPath(const char *dir, const char *leaf, char *out, size_t outSize) {
  ...
  return joinAssetPath(dir, leaf, out, outSize);
}
```

```2465:2469:ESP32_S3_Firmware/src/StorageManager.cpp
  if (!StoragePaths::joinPath(StoragePaths::Temp, ".write_probe", probePath,
                              sizeof(probePath))) {
    setDiagnosticCause("WRITE_PROBE_FAILED");
    return false;
  }
```

**Leaf:** `".write_probe"`  
**First character:** `'.'` ≠ `'/'`  
**Return value:** `false` from `isValidSdPath` → `false` from `joinAssetPath` → `false` from `joinPath`  
**Probe result:** `_sdWritable` stays `false`; cause `WRITE_PROBE_FAILED`

That is the **single proven failing operation**. No SD media write API is invoked afterward.

---

## Investigation D — Filesystem path

| Item | Value | Evidence |
|---|---|---|
| Intended probe file | `/temp/.write_probe` | `Temp="/temp"` + leaf `.write_probe` |
| Path actually opened | **none** | joinPath never produces `probePath` |
| Directory | `/temp` required | `StoragePaths::Temp`; listed in `kRequiredSdDirectories` |
| Created beforehand? | `ensureSdDirectory` before join | Would mkdir if missing |
| SPIFFS path? | No | Probe targets SD via `SD.*` after join (never reached) |
| Wrong volume? | No evidence | `SD.begin` OK, cardType=3, cardSize≈14.8 GiB |

Windows-visible `temp/` (and other dirs) is consistent with mount + directory ensure succeeding, then join failing before create.

---

## Investigation E — Filename legality

| Aspect | Assessment |
|---|---|
| Name `.write_probe` on FAT | Dotfiles are generally legal on FAT via Arduino SD / SdFat-style stacks; **not tested** here because open never runs |
| Length | Short; fine |
| Case | Irrelevant |
| Reserved 8.3 | N/A for failure mode |
| **Actual reject reason** | **Application path validator** requires absolute paths (`/` prefix) and incorrectly applies that rule to a **relative leaf** |

Not a FAT reserved-name failure. It is an application-layer validation bug.

---

## Investigation F — FAT / Arduino SD compatibility

Arduino `SD.open` / `print` / `flush` / `read` / `remove` / `exists` / `mkdir` are used elsewhere successfully for JSON/layout. This incident does **not** exercise those APIs in the failing probe branch. Cluster size / LFN / flush semantics are **out of scope for this specific WRITE_PROBE_FAILED** — they cannot be the failing instruction when joinPath returns first.

---

## Investigation G — SD library return values on failing path

| Call | Invoked? | Return used? |
|---|---|---|
| `SD.begin` | Yes (earlier) | OK (runtime evidence) |
| `SD.exists("/temp")` / `SD.mkdir` | Likely | true (inferred; no mkdir error log) |
| `SD.open` probe | **No** | — |
| `File.print` / `flush` / `close` / `read` | **No** | — |
| `SD.remove` / `exists` verify | **No** | — |

Ignored: N/A beyond unreachable steps. Misinterpreted: `joinPath` false is treated as “write probe failed,” which conflates **path construction failure** with **media write failure**.

---

## Investigation H — SPI bus state

Proven by source (`SdSpi.cpp`, `FirmwareApp.cpp`, `W5500Config`):

- W5500: SPI3_HOST (separate pins)
- SD: FSPI / SPI2_HOST (SCK=7, MISO=5, MOSI=6, CS=18)
- Explicit comment: buses are independent; SD init must not touch W5500 pins
- Probe runs after Phase 1 ETH.begin and after SD.begin
- Probe failure path performs **no** SPI transaction after ensureDir

**Contention is not the failing instruction for WRITE_PROBE_FAILED.** Mount already proved the SD SPI path works for card identify + FS mount.

---

## Investigation I — Timing

```
Phase 1 ETH → Phase 2 SPIFFS → Phase 3 StorageManager::begin → mount → probe
```

- Probe is single-threaded under recursive `_storageMutex` on boot task
- No RouterWorker yet (starts later in `FirmwareApp::begin`)
- No concurrent storage consumers during first probe
- Race/scheduler interference: **not indicated** for this deterministic path-validation failure

---

## Investigation J — `_sdWritable = false`

| Where | Why |
|---|---|
| `probeSdWritable` entry | Unconditionally clears before testing |
| Failed joinPath / open / short write | Leaves false + WRITE_PROBE_* |
| Failed verify/remove | Leaves false + WRITE_VERIFICATION_FAILED |
| `writeJsonToSdSerialized` 2× fail | TRANSACTION_FAILED |
| Removal / recovery fail / markDegraded / restore | other causes |

**Reversible?** Yes in design: later `verifySdHealthy` / `retrySd` / remount re-call `probeSdWritable()`. But while leaf remains `.write_probe`, **every** re-probe hits the same joinPath failure — effectively sticky READ_ONLY for the boot until code changes or path argument changes.

---

## Investigation K — Genuine unwritable media vs bad verification

| Evidence | Implication |
|---|---|
| `SD.begin OK`, cardType=3, non-zero cardSize | Media readable / mountable |
| Windows lists directories/files | Filesystem readable on host |
| Cause = WRITE_PROBE_FAILED not WRITE_VERIFICATION_FAILED | Failed before read-back stage |
| Source: joinPath rejects `.write_probe` before `SD.open` | **No write I/O attempted** |
| Conclusion | **Verification logic incorrectly reports write failure.** Hardware writability is **unproven by this probe**, not proven false. |

---

## Investigation L — Evidence agreement

| Source | Expected if joinPath bug | Observed |
|---|---|---|
| Serial | `Verifying write capability` then no `Verification passed` | Matches user report |
| Diagnostic | `WRITE_PROBE_FAILED` | Matches |
| Health | `READ_ONLY` (mounted && !writable) | Matches |
| SD contents | No new `.write_probe` required | Compatible (never created) |
| Windows files | Pre-existing layout visible | Compatible |
| Conflict? | None for this cause | Log + source + host view agree |

---

## Investigation M — Hot-plug / retry interaction

| Scenario | Probe behavior |
|---|---|
| Cold boot | Fails at joinPath |
| Warm reboot | Same |
| Manual Retry SD | `attemptSdRecovery` → same probe → same fail |
| Watch / hot-plug remount | Same probe → same fail |
| Mounted READ_ONLY periodic re-probe | `verifySdHealthy` → same probe → same fail |

Recovery paths cannot clear WRITE_PROBE_FAILED while the leaf string remains `.write_probe`.

---

## Investigation N — Regression risk if probe later modified (discussion only)

A future fix must not:

- Auto-merge SPIFFS/SD conflicts
- Increase RouterOS traffic / RouterWorker work
- Disable SPIFFS fallback
- Skip transactional write semantics for real JSON writes
- Poll continuously
- Treat path-build errors as permanent hardware RO without distinguishing causes

Safest **discussion-only** fix directions (do not implement here):

1. Pass a full absolute path constant `"/temp/.write_probe"` directly to `SD.open`, bypassing relative-leaf `joinPath`; or
2. Change `joinPath`/`joinAssetPath` so relative leaves are validated without requiring a leading `/` (still reject `..`); or
3. Keep joinPath but use a leaf that is already absolute — **avoid** concatenating absolute leaf into `/temp` + `/` + `/.write_probe` without adjusting join logic.

Then re-run hardware validation: probe must reach `SD.open` and prefer `WRITE_VERIFICATION_FAILED` only for real I/O mismatches.

---

## Root cause statement

**Root cause:** `StorageManager::probeSdWritable()` calls `StoragePaths::joinPath("/temp", ".write_probe", …)`. `joinPath` delegates to `joinAssetPath`, which calls `isValidSdPath` on the **relative leaf**. `isValidSdPath` requires `path[0] == '/'`. `".write_probe"` fails that check, so `joinPath` returns `false`, the probe sets `WRITE_PROBE_FAILED`, leaves `_sdWritable == false`, and never opens or writes the probe file. Runtime health then reports `READ_ONLY` with that cause despite a successful SD mount.

**Confidence:** Very high — proven by source control flow; runtime logs match the silent post-“Verifying” WRITE_PROBE_FAILED branch; `WRITE_VERIFICATION_FAILED` path is excluded by the observed cause string.
