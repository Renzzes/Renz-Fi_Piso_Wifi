# Renz-Fi SD Card Storage Migration & Production Hardening Forensic

Date: 2026-08-09  
Scope: storage, flash wear, RAM allocation, SD failure behavior, backup/restore, and deployment impact  
Method: source-code forensic only; no firmware, portal, Admin, RouterOS, or storage behavior was changed

## Release verdict

**NO-GO for immediate storage migration.**

Most mutable databases are already SD-primary. The release blocker is not their nominal location; it is the current SD-to-SPIFFS fallback contract:

1. SPIFFS is an alternate store, not a continuously maintained last-known-good mirror.
2. An SD-less cold boot can synthesize defaults for installation, router, voucher, sales, and portal-session state.
3. The first write that discovers an SD failure is returned as failed and is not retried against SPIFFS.
4. Recovery copies whole fallback files over SD files without record-level merge, generation checks, or conflict detection.
5. A mounted but read-only SD card can remain classified as healthy.

Moving more state to SD before correcting these boundaries would increase—not reduce—the risk of paid-session, voucher, and configuration loss. The safe order is:

1. Establish a bounded internal last-known-good core checkpoint and write-ahead journal.
2. Make SD failure transitions transactional and deterministic.
3. Move all unbounded history to append/rotated SD storage.
4. Stream reports, backups, uploads, and history reads.
5. Validate card removal, reboot, reinsertion, and power loss on hardware.

## Root cause analysis

The storage contract says SPIFFS is system storage and SD is customer/runtime storage, but runtime fallback paths make SPIFFS mutable. The abstraction chooses one backend based on `_healthy`; it does not mirror healthy SD commits into internal storage. If SD is unavailable and a fallback file is absent, `seedFallbackDefaults()` supplies factory defaults. When SD returns, `syncFallbackToSd()` performs full-file replacement from SPIFFS.

This creates three independent failure modes:

- **Continuity failure:** the appliance can boot without its last known router credentials, installation state, rates, vouchers, and active entitlements.
- **Transition failure:** the write that first detects removal is lost because `writeJson()` does not retry after `writeJsonToSdSerialized()` changes storage state.
- **Reconciliation failure:** fallback data based on defaults can overwrite older, valid SD history.

There is no LittleFS backend in the implementation. Diagnostics sometimes label SPIFFS operations as “LittleFS write,” but actual calls use `SPIFFS`.

## Current storage architecture

### Physical flash partitions

| Region | Size | Purpose | Recommendation |
|---|---:|---|---|
| NVS | 20 KiB | Authentication and boot network settings | Keep internal |
| OTA metadata | 8 KiB | Boot selection/OTA state | Keep internal |
| OTA app0 | 2.5 MiB | Firmware image | Keep internal |
| OTA app1 | 2.5 MiB | Firmware image | Keep internal |
| SPIFFS | 3,080 KiB | Admin SPA, captive-portal recovery assets, mutable emergency fallback | Keep immutable assets; strictly bound emergency state |
| SD/FAT | Device-dependent | Customer configuration, runtime databases, assets, logs, backups | Canonical mutable store |

Runtime SD-card contents and their current byte sizes cannot be measured from the repository. “Current size” below therefore means a source-proven cap, build artifact, fixed partition size, or **device measurement required**.

## Complete persistent-file storage map

### Internal firmware and immutable SPIFFS

| Current location | File/path | Purpose | Read frequency | Write frequency | Current size / limit | Growth | Safe to move? | Recommended destination |
|---|---|---|---|---|---|---|---|---|
| OTA app partition | active firmware image | Firmware executable | Continuous execution | OTA only | 2.5 MiB slot | Fixed by build | No | Remain OTA flash |
| OTA app partition | inactive firmware image | OTA target | Boot/OTA only | OTA only | 2.5 MiB slot | Fixed by build | No | Remain OTA flash |
| OTA metadata | boot selection | OTA rollback/selection | Boot | OTA only | 8 KiB partition | Fixed | No | Remain internal |
| SPIFFS | `/index.html` | Admin SPA shell | Admin requests/boot gate | UploadFS only | Build artifact | Immutable | No | Remain SPIFFS |
| SPIFFS | `/assets/D9grvKX5.js` | Admin SPA bundle | Admin requests | UploadFS only | Build artifact | Immutable per build | No | Remain SPIFFS |
| SPIFFS | `/assets/BKTqPgpi.js` | Admin SPA bundle | Admin requests | UploadFS only | Build artifact | Immutable per build | No | Remain SPIFFS |
| SPIFFS | `/assets/tJk2SM1Q.js` | Admin SPA bundle | Admin requests | UploadFS only | Build artifact | Immutable per build | No | Remain SPIFFS |
| SPIFFS | `/assets/63vSE8nA.css` | Admin SPA stylesheet | Admin requests | UploadFS only | Build artifact | Immutable per build | No | Remain SPIFFS |
| SPIFFS | `/favicon.svg` | Admin icon | Browser requests | UploadFS only | Build artifact | None | No | Remain SPIFFS |
| SPIFFS | `/manifest.webmanifest` | Admin PWA manifest | Browser requests | UploadFS only | Build artifact | None | No | Remain SPIFFS |
| SPIFFS | `/sw.js` | Admin service worker | Browser requests | UploadFS only | Build artifact | None | No | Remain SPIFFS |
| SPIFFS | `/build-info.json` | Build identity | Boot/status | UploadFS only | Sub-KiB expected | None | No | Remain SPIFFS |
| SPIFFS | `/DO_NOT_EDIT.txt` | Image marker | Never at runtime | UploadFS only | Tiny | None | Yes, but no benefit | Keep or omit from image |
| SPIFFS | `/portal/login.html` | Recovery captive portal | Portal requests | UploadFS only | Build artifact | None | No | Remain SPIFFS |
| SPIFFS | `/portal/renzfi-app.js` | Recovery portal logic | Portal requests | UploadFS only | Build artifact | None | No | Remain SPIFFS |
| SPIFFS | `/portal/renzfi-style.css` | Recovery portal styles | Portal requests | UploadFS only | Build artifact | None | No | Remain SPIFFS |
| SPIFFS | `/portal/md5.js` | RouterOS CHAP helper | Portal requests | UploadFS only | Build artifact | None | No | Remain SPIFFS |
| SPIFFS | `/portal/Default-Banner.png` | Bundled visual fallback | Portal requests | UploadFS only | Build dependent | None | No | Remain SPIFFS |
| SPIFFS | `/portal/bg_music.mp3` | Optional bundled audio fallback | Portal requests | UploadFS only | Excluded by current staging | None | Prefer no | Keep excluded unless product requires it |

Generated asset names are build-dependent. The table lists every file currently staged in `ESP32_S3_Firmware/data`; future hashed files inherit the same classification.

### NVS

| Current location | Keys | Purpose | Read frequency | Write frequency | Current size | Growth | Safe to move? | Recommended destination |
|---|---|---|---|---|---|---|---|---|
| NVS `renz-auth` | `passwordHash`, `mustChange`, `firstBootDone`, `ownerUsername`, `op_user`, `op_hash` | Boot-safe authentication | Boot/login configuration | Credential changes/reset | Small, fixed; device measurement required | Bounded | No | Remain NVS |
| NVS `renz-network` | `addrMode`, `ip`, `gateway`, `subnet`, `dns`, `dns2`, `provisioned`, `mgmtApKeep`, `syncToSd` | Network boot configuration | Before SD initialization and manager startup | Network save/reset/recovery | Small, fixed | Bounded | No | Remain NVS |
| NVS legacy | `renzfi_wifi` namespace | Reserved legacy contract | No active access found | None found | Unknown | None | Yes after migration proof | Remove in a separate compatibility cleanup |

No EEPROM persistence was found.

### SD configuration and operational databases

| Current location | Purpose | Read frequency | Write frequency | Current size / limit | Expected growth | Safe to move? | Recommended destination |
|---|---|---|---|---|---|---|---|
| `/config/settings.json` | Machine, coin, RGB, mirrored Ethernet settings | Boot and settings APIs | Changes; coin stats may save every 5 s | Fallback cap 8 KiB | Bounded | Already SD; not SD-only yet | SD canonical + bounded internal core checkpoint |
| `/config/router.json` | Production router profile/config | Activation and Admin | Router changes/sync | Fallback cap 8 KiB | Bounded | Already SD | SD canonical + protected internal activation subset |
| `/config/portal.json` | Portal/asset metadata | Portal/Admin | Every asset/config mutation | Fallback cap 1 KiB | Bounded | Already SD | SD canonical; bundled defaults for degraded mode |
| `/config/promos.json` | Coin rates/promotions | Coin preview and Admin | Promo CRUD | Fallback cap 32 KiB | User-controlled | Already SD; needs bound | SD canonical + current rate checkpoint internally |
| `/config/wifi.json` | Legacy Wi-Fi compatibility | Compatibility API | Compatibility writes | Device measurement required | Bounded | Yes | Keep SD until compatibility removal |
| `/config/installation.json` | Installation lifecycle | Boot/status | State transition and session touch | Fallback cap 4 KiB | Bounded | Already SD | SD canonical + last-known-good internal checkpoint |
| `/config/provisioning.json` | Setup milestones/unlock | Setup | Setup transitions | Fallback cap 4 KiB | Bounded | Already SD | SD canonical + setup recovery checkpoint |
| `/config/router-connection.json` | Protected RouterOS credentials/verification | Activation/provisioning | Save/test/rollback | Fallback cap 8 KiB | Bounded | Already SD | SD canonical + protected internal activation subset |
| `/config/router-provisioning.json` | SSID/policy/provisioning progress | Setup/recovery | Setup finish stages | Fallback cap 8 KiB | Bounded | Already SD | SD canonical + setup recovery checkpoint |
| `/config/router-cache.json` | Derived router inventory/status | Dashboard | Explicit sync/test/save | Fallback cap 8 KiB | Bounded | Yes | SD-only or RAM-derived cache; no flash fallback |
| `/config/existing-network-scan.json` | Reconstructible scan decision | Setup only | Scan completion | Fallback cap 24 KiB | Bounded | Yes | SD-only or RAM; no flash fallback |
| `/config/setup-wizard.json` | Existing six-step setup choices | Setup | Existing wizard saves | Fallback cap 4 KiB | Bounded | Already SD | SD canonical + setup recovery checkpoint |
| `/config/build-info.json` | Copy of immutable SPIFFS metadata | Status | Rewritten each boot | Sub-KiB expected | None | Yes | Remove duplicate; read SPIFFS source |
| `/config/network-adoption-workflow.json` | Declared workflow artifact | No reader found | Deletion only found | Absent/unknown | None | Yes after external check | Stop reserving if truly unused |
| `/config/vouchers.json` | Reserved target only | No active manager | Seed once | Tiny default | None today | Yes | Migrate active legacy voucher DB here transactionally |
| `/config/network.json` | Reserved | None found | Seed once | Tiny default | None today | Yes | Keep reservation or stop seeding |
| `/config/auth.json` | Reserved | None found | Seed once | Tiny default | None today | Yes | Do not move real auth from NVS |
| `/config/system.json` | Reserved | None found | Seed once | Tiny default | None today | Yes | Keep reservation or stop seeding |
| `/vouchers/vouchers.json` | Active voucher database and archive states | Redemption/Admin | Generate, reserve, activate, expire, disable, archive | Fallback cap 64 KiB; manager JSON nominal 24 KiB | Unbounded records | Yes, already SD | Canonical `/config/vouchers.json`; split active index from append-only `/archives/vouchers-YYYY-MM.ndjson` |
| `/sales/sales.json` | Unified sales/session records | Reports and lifecycle | Every sale/status event | Hard retention: 20 records and 12 KiB | Bounded by current code | Already SD | Keep a bounded active/recent index; append completed records to SD ledger |
| `/sessions/portal_sessions.json` | Paid session recovery/entitlements | Boot, portal status, lifecycle | Event saves + dirty save at 30 s | Fallback cap 128 KiB; no source record cap | Active-set dependent | Already SD | SD active checkpoint + bounded internal entitlement journal |
| `/sessions/users.json` | Legacy active-user records | Admin/session actions | Grant/pause/resume/disconnect/cleanup | Nominal JSON capacity 8 KiB | Active-set dependent | Already SD | Keep SD until consolidation; define degraded behavior |
| `/sessions/admin.json` | Legacy admin-session file | Cleared at boot; RAM sessions authoritative | Every boot clears | `[]` in normal use | None | Yes | Remove after compatibility proof |
| `/logs/logs.json` | Persistent event log | Admin diagnostics/export | Every persisted log rewrites full array | Nominal 256 KiB is not enforced; parser nominal 24 KiB | Unbounded until failure | Must remain SD, architecture must change | Rotated append-only SD segments; RAM ring when SD absent |

There is no separate `history.json`, transaction DB, activation DB, reconnect DB, termination DB, daily report, or monthly report in the current source. These events are currently fields in bounded `sales.json` and/or logs. New separate databases are not recommended; one append-only completed-session ledger should be the history SSoT.

### SD assets

| Current location | Purpose | Read frequency | Write frequency | Size limit | Growth | Safe to move? | Recommended destination |
|---|---|---|---|---:|---|---|---|
| `/assets/banner/current.webp` | Portal banner | Portal requests | Owner upload/delete | 200 KiB | Fixed overwrite | Already SD | Keep |
| `/assets/music/current.mp3` | Portal audio | Portal requests | Owner upload/delete | 1,024,000 B | Fixed overwrite | Already SD | Keep |
| `/assets/logo/current.webp` | Branding | UI requests | Owner upload/delete | 100 KiB | Fixed overwrite | Already SD | Keep |
| `/assets/background/current.webp` | Branding | UI requests | Owner upload/delete | 512 KiB | Fixed overwrite | Already SD | Keep |
| `/assets/ads/ad1.webp`–`ad5.webp` | Portal ads | Portal requests | Owner upload/delete | 200 KiB each | Five fixed slots | Already SD | Keep |
| `/assets/videos/ad1.mp4`–`ad5.mp4` | Portal video ads | Portal requests | Owner upload/delete | 5 MiB each | Five fixed slots | Already SD | Keep |
| `/assets/icons/<validated>` | Reserved/dynamic assets | No active writer proven | On demand if implemented | Device-dependent | User-controlled | Yes | SD |
| `/assets/fonts/<validated>` | Reserved/dynamic assets | No active writer proven | On demand if implemented | Device-dependent | User-controlled | Yes | SD |
| `/assets/downloads/<validated>` | Reserved/dynamic assets | No active writer proven | On demand if implemented | Device-dependent | User-controlled | Yes | SD |
| `/www/portal-banner.webp` | Legacy banner | Resolver fallback | Legacy upload/restore | 200 KiB class | Fixed overwrite | Yes | Migrate to canonical banner |
| `/www/portal-bg-music.mp3` | Legacy music | Resolver fallback | Legacy upload/restore | 1,024,000 B | Fixed overwrite | Yes | Migrate to canonical music |

### SD backups, temporary, reports, and reserved areas

| Current location | Purpose | Read/write pattern | Current size | Growth | Safe to move? | Recommended destination |
|---|---|---|---|---|---|---|
| `/backup/renzfi-export.zip` | Manual backup output | Whole overwrite per export | Sum of included JSON/assets + ZIP overhead | One retained file | Yes | `/backups/backup_<timestamp>.zip`, retained ring |
| `/backup/renzfi-export.json` | ZIP-failure fallback | Whole overwrite | Up to 128 KiB JSON document; assets not included | One retained file | Yes | `/backups/` or delete after download |
| `/backup/renzfi-restore.tmp` | Restore upload staging | Whole upload then delete | API cap 3 MiB | Temporary | Yes | `/temp/restore.tmp`, boot cleanup |
| `/temp/.write_probe` | SD writable probe | 2-byte create/delete | 2 B transient | None | Already correct | Keep |
| `<json>.tmp` | Whole-file replacement staging | Every JSON write | Up to replacement size | Transient | Already SD for SD files | Keep but improve recovery protocol |
| `<json>.bad` | Corrupt-file quarantine | On parse failure | Up to original size | Can accumulate one per file | Already SD | Add bounded cleanup/diagnostic retention |
| `/backups` | Reserved canonical backup area | Not active today | Empty unless external files exist | Potentially unbounded | Yes | Activate with retained snapshots |
| `/reports` | Reserved | No active writer | Empty unless external files exist | Potentially unbounded | Yes | Generate/read on demand; apply retention |
| `/exports` | Reserved | No active writer | Empty unless external files exist | Potentially unbounded | Yes | Temporary generated exports; delete after use |
| `/cache` | Reserved | No active writer | Empty unless external files exist | Reconstructible | Yes | SD-only, evictable |
| `/firmware/update.bin` | Reserved OTA package | No active writer; OTA streams to partition | Normally absent | One firmware image if activated | Yes, but not currently used | Leave reserved; do not reroute OTA without a separate design |

### Mutable SPIFFS fallback

| SPIFFS path | SD mapping | Limit | Growth/wear | Safe disposition |
|---|---|---:|---|---|
| `/fallback/.manifest.json` | Fallback metadata | Nominal 1–2 KiB document | Rewritten after every fallback write | Keep only for bounded journal generations |
| `/fallback/settings.json` | `/config/settings.json` | 8 KiB | Whole rewrite + manifest rewrite | Replace with compact last-known-good core checkpoint |
| `/fallback/promos.json` | `/config/promos.json` | 32 KiB | Whole rewrite | Retain only current active rates, not history |
| `/fallback/router.json` | `/config/router.json` | 8 KiB | Whole rewrite | Retain protected activation-critical subset |
| `/fallback/vouchers.json` | `/vouchers/vouchers.json` | 64 KiB | Growing whole rewrite | Replace with bounded active voucher index/journal |
| `/fb/ps.json` | `/sessions/portal_sessions.json` | 128 KiB | High-frequency whole rewrite | Replace with bounded active entitlement journal |
| `/fb/sales.json` | `/sales/sales.json` | 128 KiB | Whole rewrite; real sales cap is 12 KiB | Tighten to bounded recent/unflushed journal |
| `/fb/pcfg.json` | `/config/portal.json` | 1 KiB | Low-frequency | Optional compact metadata checkpoint |
| `/fb/installation.json` | `/config/installation.json` | 4 KiB | Transition writes | Keep last-known-good checkpoint |
| `/fb/provisioning.json` | `/config/provisioning.json` | 4 KiB | Setup writes | Keep setup recovery checkpoint |
| `/fb/router-connection.json` | `/config/router-connection.json` | 8 KiB | Setup/Admin writes | Keep protected activation-critical checkpoint |
| `/fb/router-provisioning.json` | `/config/router-provisioning.json` | 8 KiB | Setup writes | Keep setup recovery checkpoint |
| `/fb/router-cache.json` | `/config/router-cache.json` | 8 KiB | Reconstructible writes | Remove from flash fallback |
| `/fb/existing-network-scan.json` | `/config/existing-network-scan.json` | 24 KiB | Reconstructible; omitted from aggregate accounting | Remove from flash fallback |
| `/fb/setup-wizard.json` | `/config/setup-wizard.json` | 4 KiB | Existing wizard actions | Keep setup recovery checkpoint |
| `/portal/custom/banner.webp` | Canonical banner | 200 KiB | Mutable binary bypasses fallback quotas | Prohibit while SD is absent; use bundled default |
| `/portal/custom/bg-music.mp3` | Canonical music | 1,024,000 B | Mutable binary bypasses fallback quotas | Prohibit while SD is absent; omit audio/use bundled default |

Aggregate fallback thresholds are 256 KiB soft, 320 KiB hard, with 128 KiB required free. `existing-network-scan.json` is not included in aggregate accounting. Custom banner/music bypass these controls entirely. Factory fallback cleanup also omits some setup files.

## Recommended SD migration

### Tier 1 — internal boot/core continuity

Retain internally:

- NVS authentication and network boot keys.
- Immutable Admin SPA and recovery portal assets.
- Last-known-good installation mode.
- Protected RouterOS activation credentials/profile.
- Current coin configuration and active promo rates.
- Active paid entitlements.
- Active/single-device voucher reservations needed for reconnect.
- A small, bounded unflushed sales journal.
- Monotonic generation and reconciliation metadata.

This is not a second full database. It is the minimum durable core needed for safe paid operation while SD is absent.

### Tier 2 — SD canonical mutable state

Keep on SD:

- All configuration databases.
- Active voucher index.
- Active session checkpoint.
- Recent sales index.
- Assets and media.
- Logs.
- Backups.

Use generation-stamped, recoverable replacement for mutable JSON. Do not delete the only valid generation before the new generation is flushed and validated.

### Tier 3 — SD append/rotated history

Move or create as append-only, rotated SD records:

- Completed sales/session records.
- Voucher terminal/archive events.
- Activation, reconnect, pause, resume, expiration, and termination events.
- Audit logs.

Recommended representation: bounded NDJSON segment files by month or size, plus a compact index/recent cache. Do not append fragments to the existing JSON arrays; that would corrupt their API-compatible structure.

### Reporting

Daily/monthly/history APIs should:

- Stream/scan only matching SD segments.
- Maintain a bounded recent cache.
- Avoid loading the entire ledger.
- Generate CSV through chunked or file-backed responses.
- Return historical-report unavailable/read-only status when SD is absent, while leaving core payment and activation operational.

No API shape, Admin page, portal flow, session lifecycle, RouterWorker flow, or RouterOS command should change.

## RAM optimization report

Ranked source-proven peaks:

1. **ZIP restore entry:** `std::vector<uint8_t>(compSize)` may allocate nearly the full 3 MiB upload before entry-specific validation.
2. **Asset upload:** a 1,024,000-byte music upload buffer is copied at completion, creating roughly a 2 MiB peak; retained vector capacity persists.
3. **Backup export:** each asset is loaded in full, up to 1,024,000 bytes. JSON fallback reads full assets even though it records only presence and size.
4. **JSON restore:** full-file `String` plus a nominal 128 KiB JSON document.
5. **Sales/log responses:** source JSON, destination JSON/string, and response representations coexist.
6. **Sales CSV:** repeated `String` concatenation and escaping while holding the sales mutex.
7. **Logger RAM ring:** 500 entries, each with four dynamically allocated `String` fields.
8. **Sales chart cache:** three nominal 8 KiB JSON documents, with refresh-time overlap and a 1,980-byte date-key stack array.
9. **RouterOS replies:** multiple arrays of dynamic `String` fields and capacity-preserving overflow vectors.

Safe behavior-preserving reductions:

- Validate ZIP entry sizes before allocation and stream entries.
- Stream uploads to SD temp files; move buffers rather than copy; explicitly release oversized capacity.
- Use file size for backup JSON asset metadata.
- Deserialize from `File` rather than an intermediate full `String`.
- Stream sales/log/CSV responses.
- Snapshot under mutex, then format outside the lock.
- Bound/truncate RAM log strings or use a byte ring.
- Right-size chart caches and copy cache references/revisions outside critical sections.
- Release unusually large RouterOS overflow capacity after scans.

No application allocation explicitly targets PSRAM. PSRAM is enabled/reported, but actual placement of generic `String`, vector, and JSON allocations is allocator-dependent. Networking, task stacks, and DMA-capable buffers still depend on internal memory. The RouterWorker stack is 12,288 words (49,152 bytes on ESP32); the finish heartbeat task is 3,072 words (12,288 bytes). These must be measured on hardware before resizing.

## Flash wear report

Highest wear risks:

- Portal sessions: forced event saves plus periodic dirty saves rewrite the entire file.
- SPIFFS fallback: each domain write also rewrites the manifest.
- Coin statistics/settings: may rewrite `settings.json` every five seconds.
- Vouchers: reserve and activate can perform separate full-database rewrites.
- Sales: each lifecycle mutation rewrites the bounded array.
- Router cache: multiple explicit operations rewrite the complete cache.
- Admin sessions: writes `[]` at every boot despite RAM-only auth sessions.
- Build metadata mirror: rewritten to SD every boot.
- Custom SPIFFS media: large mutable writes bypass quota and recovery synchronization.

Logs are already on SD, so they do not wear internal flash, but rewriting the entire log array on every entry is severe SD write amplification and reliability risk.

## Backup strategy

Current backup is manual/on-demand, fixed-name, incomplete, and non-transactional:

- It omits auth NVS, installation/setup state, portal sessions, logs, router cache/scans, and most canonical assets.
- Restore applies entries before complete archive validation.
- Export writes CRC values but restore does not verify them.
- A failed restore can leave mixed generations.
- The JSON fallback does not contain asset bytes.

Recommended offline-first policy:

- Manual export remains supported.
- Create a pre-restore snapshot and a pre-factory-reset snapshot.
- Optionally create a snapshot at successful configuration milestones using the existing event path.
- Retain current plus two previous timestamped SD snapshots.
- Stage restore, validate version/schema/CRC/space completely, then commit one recoverable generation.
- Do not add a timer, worker, Internet dependency, RouterOS read, or cloud backup.

## SD-missing degraded-mode contract

The appliance must:

1. Never reinterpret an installed appliance as factory because SD is absent.
2. Load last-known-good core state internally.
3. Accept coin/voucher payment only when entitlement, accounting, and voucher transitions can be durably committed.
4. Commit payment state before RouterOS authorization.
5. Retry the write that discovers SD failure against the internal journal.
6. Treat a failed writable probe as degraded immediately.
7. Keep Admin login, captive portal, coin GPIO, voucher redemption/reconnect, RouterWorker activation, and expiration operational.
8. Make logs/history/reports/backups/media uploads read-only or unavailable when SD is absent.
9. Reconcile by record ID and generation; never replace complete SD datasets with fallback files derived from defaults.
10. Surface storage readiness independently from network/router readiness.

## Files to modify in a future implementation

No source files were changed by this forensic phase.

| Classification | Expected files | Purpose |
|---|---|---|
| Storage | `StorageManager.h/.cpp`, `StoragePaths.h/.cpp`, `Config.h` | Last-known-good checkpoint, journal, atomic generations, safe fallback/reconciliation |
| Storage | `SessionManager.h/.cpp` | Bounded recent sales + streamed append-only ledger/reports |
| Storage | `PortalSessionManager.h/.cpp` | Durable active-entitlement checkpoint without lifecycle changes |
| Storage | `VoucherManager.h/.cpp` | Active index + terminal archive ledger |
| Storage | `Logger.h/.cpp` | Rotated append SD logs and bounded RAM ring |
| Storage | `BackupManager.h/.cpp` | Streaming, validated transactional snapshots/restores |
| Storage | `AssetManager.h/.cpp` | Stream uploads and prohibit mutable SPIFFS media fallback |
| Firmware API | `ApiServer.cpp` | Stream exports/history; explicit degraded errors without API schema changes |
| Diagnostics | `MemoryDiagnostics.*`, `DmaMemoryMonitor.*` | Measurement only, if existing telemetry is insufficient |
| Tests/scripts | Existing storage/portal test scripts or new host tests | Power-loss generations, SD removal/recovery, schema/API regression |
| Documentation | `STORAGE_ARCHITECTURE.md`, migration report | Update contract after implementation |

The setup wizard step count and order must remain unchanged.

## Deployment impact for the forensic phase

- MikroTik upload required: **No**
- ESP32 firmware upload required: **No**
- UploadFS required: **No**
- Admin rebuild required: **No**
- Portal rebuild required: **No**

Expected implementation impact:

- ESP32 firmware upload: **Yes**
- UploadFS: **Only if SPIFFS fallback layout/default image changes**
- Admin rebuild: **No**, unless degraded-state presentation is added
- Portal rebuild: **No**
- MikroTik upload: **No**

## Regression risks

Highest-risk boundaries:

- Paying while the SD is removed during the commit.
- Reboot with an active coin or voucher session and no SD.
- Voucher reservation followed by activation failure.
- SD reinsertion with newer records on both media.
- Power loss during JSON generation replacement.
- Full/fragmented SPIFFS blocking emergency commits.
- Full/corrupt/read-only SD.
- Restore interrupted between entries.
- Reporting while a lifecycle event appends to the ledger.

All existing coin, voucher, portal, Admin, RouterWorker, provisioning, recovery, RBAC, and RouterOS paths must be regression-tested. Storage work must not alter their external behavior.

## CPU and RouterOS impact

RouterOS CPU/API impact of the recommended design: **zero additional commands and zero idle traffic**.

Storage commits originate only from existing product events. No RouterOS polling, scheduler, script, periodic sync, or new background worker is required. SD report scanning may consume ESP32 CPU during an explicit Admin request, but it has no RouterOS effect.

ESP32 CPU impact:

- Lower JSON parse/serialize cost after replacing whole-history rewrites with appends.
- Lower heap-copy cost after streaming uploads/backups/reports.
- Small CRC/index overhead only on storage events or explicit report requests.
- No new idle work.

## Capacity estimates

### Internal flash savings

Direct avoidable mutable SPIFFS exposure:

- Custom music: up to 1,024,000 B
- Custom banner: up to 200 KiB
- Router cache: up to 8 KiB
- Existing network scan: up to 24 KiB
- Excess sales fallback allowance: up to 116 KiB above the real 12 KiB sales cap

Theoretical exposure removed/tightened: approximately **1.34 MiB**, before accounting for `.tmp` duplication. Actual savings require device filesystem measurements.

### RAM savings

Source-proven peak opportunities:

- Asset upload copy removal: approximately **1.0 MiB**
- Streamed backup asset: up to **1.0 MiB**
- Streamed restore entry: up to **3.0 MiB**
- JSON restore intermediate removal: file size plus nominal **128 KiB**
- Smaller response/cache/log allocations: tens of KiB

These peaks do not necessarily occur simultaneously. Hardware heap traces are required for a release claim.

### SD usage

Configuration and active indexes are expected to remain below a few hundred KiB. Media can consume approximately:

- 5 video slots × 5 MiB = 25 MiB
- 5 ad slots × 200 KiB = 1 MiB
- music + background + banner + logo ≈ 1.8 MiB

Configured media ceiling is therefore approximately **27.8 MiB**, plus logs, ledgers, backups, and filesystem overhead. Recommend at least a 1 GiB high-endurance SD card, with explicit free-space thresholds and retention policies.

## Hardware validation checklist

1. Boot installed appliance with healthy SD.
2. Boot installed appliance with SD absent; verify it does not enter factory/setup mode.
3. Verify Admin login and captive portal assets without SD.
4. Insert coins, finish payment, activate Internet, expire, pause/resume, and terminate without SD.
5. Redeem and reconnect one-device voucher without SD.
6. Remove SD immediately before, during, and after each durable commit.
7. Use a mounted read-only/failing SD and verify immediate degraded classification.
8. Reinsert SD with records created on both media; verify ID/generation merge and no data loss.
9. Power-cycle during new-file write, flush, rename, and reconciliation.
10. Fill SD and SPIFFS to each warning/hard threshold.
11. Corrupt each mutable JSON generation and verify rollback to the last valid generation.
12. Validate log and ledger rotation at boundaries.
13. Export a backup containing maximum-size assets.
14. Interrupt restore at every entry and verify all-old or all-new state, never mixed state.
15. Measure free heap, minimum heap, largest free block, DMA largest block, PSRAM, and task stack high-water marks through all cases.
16. Confirm RouterOS command count remains zero at idle and unchanged for each lifecycle action.
17. Run firmware build, Admin tests/lint/build, portal tests/build, and SPIFFS validation.

## Final recommendation

Proceed only as a staged production-hardening implementation:

- Phase 1: safe core checkpoint, write retry, generation reconciliation, and read-only SD detection.
- Phase 2: append/rotated SD logs and completed-session/voucher ledgers.
- Phase 3: streaming uploads, backups, restore, reports, and API responses.
- Phase 4: hardware fault-injection qualification.

Until Phase 1 passes hardware validation, the required “core functions continue when SD is temporarily missing” guarantee is not met.
