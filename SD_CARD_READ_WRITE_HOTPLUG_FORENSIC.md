# SD Card Read/Write & Hot-Plug Forensic Investigation

## Scope and verdict

This investigation is source-only. No firmware, Admin, Portal, RouterWorker,
RouterOS, API, UI, storage, polling, or fallback behavior was changed.

### Forensic verdict

1. `READ_ONLY` is not reported by FATFS or `SD.begin()`. It is assigned as an
   application health label in exactly one backend decision block:
   `StorageManager::refreshRuntimeSnapshot()`.
2. The exact decision is:
   SD mounted + SD readable + `_sdWritable == false`.
3. `_sdWritable` is a global runtime gate, not a proven filesystem read-only
   attribute. It can become false because:
   - the narrow boot/remount probe failed;
   - two attempts at one transactional JSON write failed;
   - restore recovery failed;
   - the SD was removed/remount failed; or
   - storage was explicitly marked degraded.
4. The boot write probe creates/opens `/temp/.write_probe`, writes `"ok"`,
   closes it, and removes it. It does **not** explicitly flush, reopen, read
   back, compare contents, verify deletion, test append, test overwrite, or test
   rename before declaring the card writable.
5. Therefore the current evidence proves `_sdWritable == false`; it does not
   prove the physical card or FAT filesystem is truly read-only.
6. Existing Windows-visible files do not by themselves prove a current-boot
   write. They may predate the boot. If `/config/build-info.json` was proven to
   have been freshly updated during the same boot, that would be stronger
   evidence of a false-positive classification because `writeSdText()` attempts
   that mirror while mounted/healthy without checking `_sdWritable`.
7. Hot-plug recovery exists, but it is bounded. After removal, firmware retries
   remounting once per minute and disables automatic polling after three failed
   remounts. Reinsertion after that requires the Admin **Retry SD Card** action,
   reboot, or reset.
8. Reinsertion does not guarantee every fallback JSON file migrates. Writes
   made while SD is completely absent record `baseCrc == 0`; reconciliation
   retains a different existing SD file as a conflict.

The exact primitive that produced the observed false `_sdWritable` result
cannot be proven from the supplied excerpts because `probeSdWritable()` returns
false silently at each failure boundary.

## Evidence classification

### Proven by the supplied log excerpts

- `SD.begin OK`: the library accepted the mount request.
- Card type/size output: media enumeration and capacity discovery succeeded.
- `Storage Health = READ_ONLY`: at snapshot time the firmware had
  `_sdMounted == true`, `_sdReadable == true`, and `_sdWritable == false`.
- `SPIFFS fallback write`: an eligible write was routed to bounded SPIFFS
  fallback because SD writes were gated off or a transactional SD write had
  just failed.

### Not proven by the supplied excerpts

- Which line inside `probeSdWritable()` failed.
- Whether the probe file was created.
- Whether `file.print("ok")` returned zero.
- Whether the file could be reopened and read.
- Whether rename, append, overwrite, or deletion worked at that moment.
- Whether Windows-visible files were created during the same firmware boot.
- Whether a transient SPI/card timing fault or a persistent media condition was
  responsible.

No captured runtime log containing the quoted READ_ONLY sequence was found in
the repository terminal records, so the investigation cannot correlate a
specific preceding error beyond the excerpts supplied in the prompt.

## Investigation A — exact READ_ONLY call chain

There is no `StorageHealth` class or enum evaluator in the firmware. The state
is stored as a `String`.

### Boot path

```text
FirmwareApp::begin()
  ESP32_S3_Firmware/src/FirmwareApp.cpp:98-108
    |
    +-- StorageManager::begin()
        ESP32_S3_Firmware/src/StorageManager.cpp:121-204
          |
          +-- mountSdCard("SD mount", false)
          |   StorageManager.cpp:90-104
          |     |
          |     +-- SD.begin(CS, dedicated FSPI, frequency)
          |
          +-- _sdMounted = mount result
          +-- _sdReadable = _sdMounted
          +-- _healthy = _sdReadable
          |
          +-- probeSdWritable()
          |   StorageManager.cpp:2154-2174
          |     |
          |     +-- ensureSdDirectory("/temp")
          |     +-- join "/temp/.write_probe"
          |     +-- SD.open(..., FILE_WRITE)
          |     +-- file.print("ok")
          |     +-- file.close()
          |     +-- SD.remove(probe)
          |     +-- _sdWritable = print returned > 0
          |
          +-- _usingFallback = !_sdWritable && SPIFFS mounted
          +-- refreshRuntimeSnapshot()
              StorageManager.cpp:1818-1951
                |
                +-- if mounted && readable && !writable:
                    _snapshotHealth = "READ_ONLY"
```

### Presentation path

```text
StorageManager::_snapshotHealth
  |
  +-- fillStorageStatus()
      StorageManager.cpp:1770-1816
        |
        +-- GET /api/storage/status
            ApiServer.cpp:1120-1134
              |
              +-- systemApi.storageStatus()
                  src/services/system.ts:104-105
                    |
                    +-- useStorageHealth()
                        src/hooks/api/useStorageHealth.ts
                          |
                          +-- StorageHealthCard
                              src/components/StorageHealthCard.tsx
```

### Runtime write-failure path

```text
Any manager calls StorageManager::writeJson()
  StorageManager.cpp:1089-1130
    |
    +-- _sdWritable is true
    +-- writeJsonToSdSerialized()
        StorageManager.cpp:797-818
          |
          +-- writeJsonToSdOnce(), up to two attempts
          +-- both fail
          +-- _sdWritable = false
          +-- _usingFallback = SPIFFS mounted
          +-- refreshRuntimeSnapshot()
                |
                +-- SD remains mounted/readable
                +-- health becomes READ_ONLY
```

## Investigation B — every condition capable of READ_ONLY

Only `refreshRuntimeSnapshot()` assigns the literal `"READ_ONLY"`
(`StorageManager.cpp:1914-1927`). It does so before WARNING, DEGRADED, HEALTHY,
or UNKNOWN when:

```text
_sdMounted && _sdReadable && !_sdWritable
```

The following paths can establish those inputs.

### 1. Boot/remount write probe returns false

- Assignment:
  - boot: `StorageManager.cpp:128-140`
  - remount: `StorageManager.cpp:301-305`
- Purpose: distinguish a mounted/readable card from one that can accept writes.
- Actual check: ensure `/temp`, open `.write_probe`, print two bytes, close,
  remove.
- Trigger evidence: compatible with the supplied boot sequence.
- Proven actual trigger: **not provable** because the probe has no step-level
  log.
- False-positive potential: **yes**. Any directory/path/open/short-write issue
  is collapsed into `_sdWritable=false`; no read-back or broader operation test
  is performed.

### 2. Two transactional JSON attempts fail

- Assignment: `StorageManager.cpp:797-818`.
- Purpose: stop repeatedly writing to an SD path after a durable JSON
  transaction cannot be completed.
- Actual check: staged write, flush, close, read-back, JSON validation, rename,
  and final payload verification in `writeJsonToSdOnce()`
  (`StorageManager.cpp:724-762`), attempted twice.
- Trigger evidence: compatible with a later `SPIFFS fallback write`.
- Proven actual trigger: **not provable** without preceding
  `SD write attempt 1/2 failed` and `2/2 failed` logs.
- False-positive potential: **yes as a global label**. A path-specific rename,
  full-disk condition, malformed target transaction, or transient I/O failure
  disables all SD JSON writes and is presented as READ_ONLY.

A storage-mutex timeout can also make the probe return false before any media
operation because `probeSdWritable()` immediately returns when its recursive
lock is not acquired.

### 3. Pending restore recovery fails while the card remains mounted

- Assignment:
  - boot: `StorageManager.cpp:140-153`
  - remount: `StorageManager.cpp:305-318`
- Purpose: prevent mixed-generation configuration after interrupted restore.
- Check: `BackupManager::recoverPendingRestore()`.
- Actual observed trigger: **unlikely for the supplied operational sequence**.
  Boot returns false or is blocked; it does not proceed as an ordinary
  SPIFFS-fallback boot.
- Classification accuracy: READ_ONLY would describe the write gate, but not the
  real cause, which is restore-integrity failure.

### 4. `StorageManager::markDegraded()` is called

- Assignment: `StorageManager.cpp:226-234`.
- Purpose: block storage consumers after an integrity-critical failure.
- Current known caller: incomplete restore handling in
  `BackupManager.cpp:1205-1228` and duplicate boot protection in
  `FirmwareApp.cpp:111-121`.
- Actual observed trigger: no evidence.
- Classification accuracy: if a later snapshot retains mounted/readable flags,
  READ_ONLY can mask the more precise integrity failure.

### 5. Card removal or remount failure

- Assignment:
  - removal: `StorageManager.cpp:244-258`
  - failed remount: `StorageManager.cpp:328-349`
- These paths also set mounted/readable false, so the normal resulting state is
  DEGRADED/WARNING/CRITICAL, not READ_ONLY.
- They can lead back to READ_ONLY if remount succeeds but the subsequent write
  probe fails.

### Presentation inconsistency

`fillSdStatus()` labels every mounted card `"Ready"` even when its separate
`readOnly` Boolean is true (`StorageManager.cpp:1729-1766`). The newer storage
health snapshot reports READ_ONLY correctly, but older consumers of the SD
status object can display contradictory information.

## Investigation C — is a complete write test performed?

### Before the initial READ_ONLY decision

`probeSdWritable()` performs:

- Create `/temp` only if it does not already exist.
- Open/create `/temp/.write_probe`.
- Write `"ok"`.
- Close.
- Attempt delete.

It does not perform:

- explicit `flush()`;
- reopen;
- file-size verification;
- content verification;
- delete-result verification;
- append verification;
- overwrite verification;
- rename verification; or
- verification of a production data path.

The Boolean is based solely on:

```text
file.print("ok") > 0
```

### Stronger checks exist, but occur only after `_sdWritable` is true

- JSON transaction:
  `StorageManager::writeJsonToSdOnce()` performs stage write, flush, close,
  reopen/read, exact comparison, JSON validation, rename, and final verification
  (`StorageManager.cpp:724-762`).
- Binary transaction:
  `StorageManager::writeBinary()` performs stage write, flush, close, reopen,
  byte-for-byte verification, backup rename, and promotion
  (`StorageManager.cpp:1528-1572`).

These stronger checks cannot disprove an initial false READ_ONLY result because
they are gated by `_sdWritable`.

## Investigation D — exact SPIFFS fallback decision

### Boot probe failure

```text
SD.begin succeeds
  |
  +-- _sdMounted = true
  +-- _sdReadable = true
  |
probeSdWritable() returns false
  |
  +-- _sdWritable = false
  +-- _usingFallback = SPIFFS mounted
  |
manager calls writeJson(path)
  |
  +-- if (_sdWritable) block is skipped
  +-- path must be fallback-eligible
  +-- payload CRC is rechecked
  +-- writeJsonToSpiffs()
  |
  +-- "[storage] SPIFFS fallback write ..."
```

### Runtime transaction failure

```text
_sdWritable = true
  |
writeJsonToSdSerialized()
  |
two transactional attempts fail
  |
_sdWritable = false
_usingFallback = true
  |
same writeJson() invocation continues
  |
eligible path is written transactionally to bounded SPIFFS
```

Fallback eligibility is explicit in `StorageManager.cpp:462-474`. It includes
settings, promos, router credentials/config, vouchers, portal sessions, bounded
sales, portal config, installation/provisioning/router-connection/
router-provisioning, and setup wizard state. It does not cover every SD file.

## Investigation E — operation coverage

### Create directory

- Implemented by `ensureDir()` (`StorageManager.cpp:1969-1975`).
- The probe tests it only when `/temp` is missing.
- If `/temp` already exists, directory creation is not tested.

### Create file

- Attempted by `SD.open("/temp/.write_probe", FILE_WRITE)`.
- Creation is not separately distinguished from opening an existing stale
  probe.

### Write file

- Tested only by checking whether writing `"ok"` reports more than zero bytes.
- No content read-back is performed by the probe.

### Flush/close/reopen/verify

- Probe: close only; no explicit flush/reopen/verify.
- Transactional JSON/binary paths: yes, but only after writable classification.

### Append

- Implemented for NDJSON using `FILE_APPEND`, flush, and close in
  `NdjsonLedger.cpp:138-150`.
- Not tested by the writable probe.
- No reopen/content verification follows an append.

### Overwrite

- Implemented by staging/replacement and by `writeSdText()`.
- Not tested by the writable probe.

### Delete

- The probe attempts `SD.remove()` but ignores its return value.
- Deletion success is not part of writable classification.

### Rename

- Transactional JSON/binary paths rely on rename and verify the promoted data.
- Rename is not tested by the writable probe.

### Read existing data

- Health polling opens `/config` for read once per minute
  (`StorageManager.cpp:261-274`).
- Normal managers read their JSON files through `readJsonFromSd()`.

### Read newly created probe data

- Not performed.

## Why the Windows files do not settle the question

- `/config` and `/temp` may have been created by an earlier successful boot,
  preloaded media, or a previous firmware version.
- The probe file is deleted immediately when it exists, so its absence is not
  evidence either way.
- SPIFFS `/build-info.json` is mirrored to SD as
  `/config/build-info.json` by `BuildMetadata::mirrorToSd()`
  (`BuildMetadata.cpp:42-45`, `StoragePaths.h:105-108`).
- `writeSdText()` checks `_healthy` but does not check `_sdWritable`
  (`StorageManager.cpp:2003-2021`). Therefore it may attempt and possibly
  complete that mirror while the health label says READ_ONLY.
- A same-boot timestamp/content comparison for `/config/build-info.json` would
  be useful hardware evidence, but the source and supplied excerpts do not
  establish it.

## Investigation F/G — hot-plug and remount behavior

### Detection and cadence

- `FirmwareApp::loop()` calls `StorageManager::pollStorageHealth()`
  (`FirmwareApp.cpp:243-250`).
- Media health/remount work is rate-limited to 60,000 ms
  (`Config.h:348-353`, `StorageManager.cpp:379-397`).
- While healthy, detection checks:
  1. `SD.cardType() != CARD_NONE`;
  2. opening `/config` for read.
- There is no card-detect GPIO interrupt or media-insert event.
- Cached owner health is recomputed every two seconds by
  `FirmwareApp::refreshHealthSnapshots()` (`FirmwareApp.cpp:278-300`), but this
  does not remount media.

### Removal

On the next one-minute storage poll:

```text
verifySdHealthy()
  |
cardType NONE or /config open failure
  |
handleSdRemoved()
  |
SD.end()
mounted/readable/writable = false
usingFallback = SPIFFS mounted
```

Removal can therefore remain unnoticed for up to approximately one minute.

### Automatic remount

After removal is detected, the next one-minute poll calls
`attemptSdRecovery()`. Each failure increments `_sdRetryCount`. At three failed
attempts, `_disableSdPolling` becomes true
(`StorageManager.cpp:328-347`).

Consequences:

- Automatic retries are finite, not permanent.
- Once disabled, reinsertion alone is ignored indefinitely.
- Admin **Retry SD Card** calls `POST /api/storage/retry-sd`, clears the disabled
  flag/count, and immediately remounts (`ApiServer.cpp:1105-1118`,
  `StorageManager.cpp:370-376`, `SystemSettingsPage.tsx:113-124,347-354`).
- Reboot or physical reset also reruns the boot mount.

### Scenario findings

#### Scenario 1 — remove, wait 5 minutes, insert

Normally **no automatic recovery**. Detection can take up to one minute, then
three once-per-minute remount failures exhaust the retry budget at roughly the
four-minute boundary. Reinsertion at five minutes is normally after polling has
been disabled. Admin Retry, reboot, or reset is then required.

Timing near poll boundaries can shift this by less than a minute, but it does
not make recovery indefinite.

#### Scenario 2 — remove, wait 1 hour, insert

No automatic recovery after the three failed attempts. Admin Retry, reboot, or
reset required.

#### Scenario 3 — remove, wait days, insert

Same as one hour. There is no long-term remount timer after polling is disabled.

#### Scenario 4 — never reinsert

The firmware continues running in fallback/degraded mode while bounded SPIFFS
capacity remains and individual operations are fallback-eligible. Core runtime
state and RouterWorker continue. Durable operations can eventually be rejected
when per-file, aggregate, or minimum-free-space quotas are reached.

#### Scenario 5 — insert while RouterWorker is active

Physical insertion itself is ignored until a storage poll/manual retry/reboot.
The SD uses dedicated FSPI/SPI2 pins; W5500 uses separate SPI3/HSPI pins
(`SdSpi.cpp:9-49`). Remount does not issue RouterOS commands. Storage operations
are serialized by the recursive storage mutex. Source shows no direct
RouterWorker interruption.

Hardware electrical hot-insertion safety is not provable from source and still
requires target testing.

#### Scenario 6 — insert during an active customer session

The session remains in RAM and RouterOS authorization is independent of the SD
mount operation. Reinsertion does not itself terminate or pause a session.
Recovery may flush fallback state/history under the storage mutex. No source
path intentionally interrupts the active session.

#### Scenario 7 — insert during a sales write

- If polling is disabled, insertion is ignored and the sales write continues to
  bounded SPIFFS fallback.
- If an automatic/manual remount occurs, the storage mutex serializes remount,
  fallback sync, and StorageManager-mediated sales writes.
- On successful writable remount, JSON fallback reconciliation and history
  spool replay run.
- If removal occurs during an SD transaction, staged/backup/read-back checks
  reduce partial-commit risk; eligible sales state then attempts bounded
  fallback.

This guarantee is not global. Backup upload/restore, asset staging/serving, and
some downloads access `SD` directly outside the StorageManager mutex and can
race `SD.end()`/remount (`ApiServer.cpp:403-410,2096-2224`,
`BackupManager.cpp:268-540,719-1229`, `AssetManager.cpp:460-765`,
`web/AssetServer.cpp:20-30`).

## Special READ_ONLY recovery behavior

When SD remains mounted/readable but `_sdWritable` is false, the one-minute
health poll calls `probeSdWritable()` again indefinitely. Failed writable probes
do not increment the remount retry count because no remount is attempted.

Therefore READ_ONLY **can** become writable without reboot.

On successful re-probe, `verifySdHealthy()`:

- clears `_usingFallback`;
- synchronizes manifest-tracked JSON fallback; and
- emits a storage-change event.

It does **not** call `replayHistorySpools()` in this path
(`StorageManager.cpp:275-281`). Therefore:

- READ_ONLY can transition to HEALTHY if there are no pending history spools;
- if history spools exist, the snapshot becomes DEGRADED; and
- those history spools are not automatically replayed by this specific
  read-only-to-writable path.

History replay does occur after boot with writable SD and after a successful
remount (`StorageManager.cpp:163-168,352-367`).

## Investigation H — SPIFFS to SD migration

### Automatically reconciled after successful writable remount

- Manifest-tracked fallback JSON:
  settings, promos, router config, voucher DB, portal sessions, bounded sales,
  portal config, installation/provisioning/router connection/router
  provisioning/setup state.
- Sales history spool.
- Completed-session history spool.
- Voucher history spool.

The JSON sync validates payload CRC, detects divergent SD state, writes
transactionally, verifies the SD result, and clears only successfully reconciled
fallback entries (`StorageManager.cpp:1347-1484`).

This is attempted reconciliation, not guaranteed migration. During complete SD
absence, `writeJson()` cannot read a current SD value and records
`baseCrc == 0` (`StorageManager.cpp:1101-1107`). If the reinserted card already
contains a different version, sync treats it as divergent and retains the dirty
SPIFFS entry (`StorageManager.cpp:1408-1429`).

That conflict can create split-brain behavior: manifest-listed SPIFFS wins
reads (`StorageManager.cpp:1069-1075`), while subsequent writes can target the
writable SD (`StorageManager.cpp:1109-1114`). Sync is not periodically retried.

Fallback payload and manifest are separate commits: the payload is written
before `addToManifest()` (`StorageManager.cpp:1009-1037`). A manifest failure
can leave an orphan fallback payload that is not migrated.

The NDJSON replay is idempotent by event ID, quarantines malformed/torn records,
and clears an active spool only after valid events are processed
(`NdjsonLedger.cpp:240-309`).

### Not automatically migrated

- Logs: the logger calls history append with fallback disabled, and
  `NdjsonLedger::spoolFor(Logs)` returns null.
- Backups: SD-only and manual.
- Restore journal: it is an SD transaction artifact, not a SPIFFS migration.
- RouterWorker jobs: RAM queue/state, not storage migration data.
- Files not listed by `isFallbackEligible()`.
- History spools on the mounted READ_ONLY-to-writable probe-success path, as
  noted above.
- Conflicted or orphaned fallback JSON.

## Investigation I — data safety by domain

### Sales

- Bounded current sales JSON is fallback-eligible.
- Full sales history has a 16 KiB SPIFFS spool.
- Risk: once per-file/aggregate/free-space quotas are exhausted, appends fail.
- Successful remount attempts both paths, but current sales JSON can conflict as
  described above.
- Coin completion is not fully fail-closed: deferred sale persistence failure
  is logged while activation can remain queued
  (`PortalSessionManager.cpp:373-603,1490-1560`).

### Voucher database and voucher history

- Voucher DB is fallback-eligible and transactionally tracked in the manifest.
- Voucher history has a 16 KiB spool.
- Voucher operations return storage errors if the DB save fails.
- Risk: history append is not checked by callers after a successful DB save, so
  history can be absent if its spool is full even though canonical voucher state
  was saved.

### Coin history

- There is no dedicated coin-history ledger.
- Coin counters/settings are stored in the settings document, which is
  fallback-eligible.
- Coin-derived sales use the sales paths above.
- Logs generated during the outage are not durably spooled.

### Portal sessions

- Portal sessions are fallback-eligible and continuously checkpointed.
- Writes are throttled in fallback except forced/immediate cases.
- Active session state remains in RAM while running.
- Risk: the latest deferred mutation may be lost on sudden power loss before a
  successful checkpoint.

### Completed session history

- Uses the 16 KiB sessions spool and is replayed after writable remount.
- Risk begins when spool or aggregate emergency quota is exhausted.

### Pending RouterWorker jobs

- Queue, active slot, last job, and hotspot outcome are FreeRTOS/RAM objects
  (`RouterProvisioningWorker.cpp:192-196,271-300,378-416`).
- SD removal/reinsertion does not clear or persist them.
- They continue while the process remains running.
- They are lost on reboot/power loss regardless of SD state.

### Backups

- Backup creation and restore are SD-only
  (`BackupManager.cpp:702-721,829-848,866-869`).
- Existing backup files remain on the removed card.
- New backups cannot be created while unavailable.
- There is no SPIFFS backup fallback or migration.
- Backup availability checks mounted + `healthy()` but not
  `isSdWritable()` (`BackupManager.cpp:702-704`), so a READ_ONLY card can enter
  backup code and fail during direct SD writes.
- Backups read SD directly and exclude dirty SPIFFS fallback, histories, portal
  sessions, setup/provisioning state, restore journals, and NVS credentials.
  A backup during unresolved split-brain can therefore contain stale SD state.

### Configuration

- Listed boot/runtime JSON has bounded, manifest-tracked SPIFFS fallback.
- Divergent SD content is retained and reported as a sync conflict rather than
  blindly overwritten.
- Non-eligible SD files have no general fallback guarantee.
- Active users, logs, admin sessions, Wi-Fi configuration, router cache,
  existing-network scan cache, and network-adoption state are not in
  `isFallbackEligible()` and can fail to persist during complete SD loss.

### Restore journal

- Restore journal/stage/backup files are SD-resident.
- If media disappears during restore, operations fail and incomplete journal
  artifacts are intended to drive rollback/recovery after the card is
  available.
- `markDegraded()` can disable storage polling after an incomplete restore;
  recovery can then require reboot/reset or an explicit retry path.
- Removing media during restore remains a high-risk hardware fault-injection
  case; source safety logic cannot prove the card controller completed writes
  before physical removal.
- Same-card reinsertion can expose the journal for rollback/cleanup; a
  replacement card cannot contain that transaction record.

### Logs

- Runtime ring/SSE logging continues.
- SD log-ledger append does not use a SPIFFS spool.
- Durable logs emitted while SD writes are unavailable can be lost.

## Investigation J — actual owner experience

### SD becomes unavailable

1. Appliance can show stale Healthy/Read Only for up to one minute.
2. Firmware then enters emergency internal storage.
3. Sessions and RouterWorker continue.
4. Supported persistent writes use bounded fallback/spools.
5. Active-user snapshots, logs, admin sessions, Wi-Fi config, router/scan cache,
   network-adoption state, backups, and other unsupported SD-only writes do not
   have the general fallback contract.
6. Health can progress to Warning/Critical as emergency usage grows.

### SD is quickly reinserted

If inserted before the three remount failures are exhausted, the next
one-minute recovery attempt can remount, reconcile JSON, replay history, and
return to Healthy/Degraded/Read Only according to the write probe and pending
work.

### SD is reinserted after retries stop

Nothing automatically remounts it. The owner must use **Retry SD Card**, reboot,
or press reset.

### Mounted READ_ONLY card becomes writable

The firmware retries the writable probe every minute indefinitely. It can
recover without reboot, although history-spool replay is missing from that
specific transition.

## Investigation K — storage health state machine

Health is recomputed from current flags and usage. It is not constrained to a
fixed sequence.

Priority order in `StorageManager.cpp:1914-1927`:

```text
1. CRITICAL
   emergency fallback/replay active and usage >= 90%

2. READ_ONLY
   SD mounted + readable + not writable

3. WARNING
   emergency fallback/replay active and usage >= 70%

4. DEGRADED
   SD absent, fallback active, layout invalid, replay pending,
   or recovery artifacts pending

5. HEALTHY
   storage healthy and SD writable

6. UNKNOWN
   none of the above can be proven
```

### Important transitions

- `UNKNOWN -> HEALTHY`: successful mount, writable probe, valid layout.
- `UNKNOWN/HEALTHY -> READ_ONLY`: mounted card fails write probe or a
  transactional JSON path fails twice.
- `HEALTHY/READ_ONLY -> DEGRADED`: removal/read-health failure.
- `DEGRADED -> WARNING -> CRITICAL`: bounded emergency usage crosses 70%/90%.
- `DEGRADED/WARNING/CRITICAL -> HEALTHY`: successful writable remount plus
  successful reconciliation with no pending work.
- `DEGRADED -> READ_ONLY`: remount succeeds but write probe fails.
- `READ_ONLY -> HEALTHY`: periodic probe succeeds and no pending work remains.
- `READ_ONLY -> DEGRADED`: probe succeeds but pending history/recovery/layout
  evidence remains.

All ordinary transitions are reversible in process except when automatic
polling has been disabled after three failed remounts or an integrity-critical
restore failure. Those require an explicit owner action or reboot/reset.

## Files responsible

Primary firmware:

- `ESP32_S3_Firmware/src/StorageManager.cpp`
- `ESP32_S3_Firmware/src/StorageManager.h`
- `ESP32_S3_Firmware/src/FirmwareApp.cpp`
- `ESP32_S3_Firmware/src/Config.h`
- `ESP32_S3_Firmware/src/StoragePaths.h`
- `ESP32_S3_Firmware/src/NdjsonLedger.cpp`
- `ESP32_S3_Firmware/src/NdjsonLedger.h`
- `ESP32_S3_Firmware/src/BackupManager.cpp`
- `ESP32_S3_Firmware/src/BuildMetadata.cpp`
- `ESP32_S3_Firmware/src/SdSpi.cpp`
- `ESP32_S3_Firmware/src/RouterProvisioningWorker.cpp`

API/Admin presentation:

- `ESP32_S3_Firmware/src/ApiServer.cpp`
- `src/services/system.ts`
- `src/hooks/api/useStorageHealth.ts`
- `src/components/StorageHealthCard.tsx`
- `src/pages/SystemSettingsPage.tsx`

Data owners reviewed:

- `ESP32_S3_Firmware/src/SessionManager.cpp`
- `ESP32_S3_Firmware/src/PortalSessionManager.cpp`
- `ESP32_S3_Firmware/src/VoucherManager.cpp`
- `ESP32_S3_Firmware/src/CoinManager.cpp`
- `ESP32_S3_Firmware/src/Logger.cpp`

## Regression risks if later modified

- Treating every write failure as removable-media loss could break a valid
  mounted card and increase fallback writes.
- Unbounded remount attempts could add recurring FSPI/CPU activity and interact
  with active storage operations.
- Changing retry cadence or adding card polling could alter watchdog, DMA, and
  timing behavior.
- Replaying fallback without generation/base-CRC conflict checks could overwrite
  newer SD data.
- Replaying history without event-ID deduplication could duplicate sales,
  vouchers, or completed sessions.
- Removing the storage mutex could permit remount/write races.
- Running SD recovery on the RouterWorker would couple storage faults to
  RouterOS latency and command scheduling.
- Expanding SPIFFS fallback without strict quotas would threaten Admin assets
  and boot-critical checkpoints.
- Automatically treating Windows-visible files as proof of current writability
  would conceal transient or path-specific transaction failures.

## Safe recommendations for discussion only

No recommendation below was implemented.

1. Capture the full serial span from `SD.begin` through the first fallback
   write, including any `SD write attempt` and restore messages.
2. On hardware, compare `/config/build-info.json` content/time before and after
   the same boot to determine whether raw SD writes succeed while health says
   READ_ONLY.
3. Test the existing probe operations individually: temp directory, create,
   write, close, reopen/read, delete, append, overwrite, rename.
4. Distinguish true media read-only, full media, path/rename failure, transient
   bus error, and restore-integrity block in future diagnostics.
5. Validate the three-attempt hot-plug window at poll-boundary extremes.
6. Validate the mounted READ_ONLY-to-writable path with pending history spools,
   because source shows JSON sync but no history replay there.
7. Keep all future recovery independent of RouterWorker and RouterOS.

## Final answers

- Root cause boundary: `_sdWritable == false` while mounted/readable.
- Exact failing primitive: not proven by available logs.
- True physical read-only media: not proven.
- False-positive classification: technically possible and consistent with the
  evidence.
- Automatic hot-plug recovery: implemented but only for three once-per-minute
  failed remount attempts.
- Reinsertion after five minutes/hour/days: normally requires Admin Retry,
  reboot, or reset.
- READ_ONLY to HEALTHY without reboot: yes, via the once-per-minute writable
  re-probe.
- Full fallback replay after physical remount: **not guaranteed**. History
  spools are attempted; JSON with `baseCrc == 0` can remain conflicted.
- Full fallback replay after READ_ONLY becomes writable without remount: JSON
  fallback yes; history spools no.
- RouterOS/RouterWorker changes or command increase: none.
- Source changes made by this investigation: none.

