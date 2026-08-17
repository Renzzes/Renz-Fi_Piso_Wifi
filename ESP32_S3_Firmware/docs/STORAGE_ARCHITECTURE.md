# Renz-Fi Storage Architecture (Frozen — Phase 2.3)

This document is the **firmware storage contract**. All current and future managers must use paths and filenames defined in `StoragePaths.h` — never invent ad-hoc directories or versioned filenames.

**Authoritative source:** `ESP32_S3_Firmware/src/StoragePaths.h`  
**Layout enforcement:** `StorageManager::ensureLayout()` at boot  
**Status:** Frozen after Phase 2.3. Phase 3+ managers (AssetManager, OTA, etc.) implement against this contract.

**Companion docs:** [ASSET_LIFECYCLE.md](./ASSET_LIFECYCLE.md), [PORTAL_CONFIG_ARCHITECTURE.md](./PORTAL_CONFIG_ARCHITECTURE.md), [HTTP_ROUTE_CONTRACT.md](./HTTP_ROUTE_CONTRACT.md)

---

## Design principles

1. **Centralized paths** — one header, no scattered string literals.
2. **Folder ownership** — each top-level area has exactly one owning manager.
3. **Canonical filenames** — user assets use fixed names (`current.webp`, `current.mp3`); AssetManager overwrites in place. No versioning in filenames.
4. **Contract vs runtime** — the contract defines the target layout; legacy paths remain active until an explicit migration phase changes manager code.
5. **Backward compatibility** — `/backup`, `/vouchers`, and `/www` are retained during transition.

---

## SD card layout (frozen)

```
/config
    settings.json      ← active runtime
    router.json        ← active runtime
    portal.json        ← active runtime
    promos.json        ← active runtime
    vouchers.json      ← contract (reserved; runtime still uses /vouchers/ today)
    network.json       ← reserved (Phase 3+)
    auth.json          ← reserved (Phase 3+)
    system.json        ← reserved (Phase 3+)
    wifi.json          ← legacy extra (NetworkSettingsManager)

/assets
    /banner
        current.webp   ← contract (Phase 3 AssetManager)
    /logo
        current.webp
    /music
        current.mp3
    /background
        current.webp
    /ads
        ad1.webp
        ad2.webp
        ad3.webp
    /videos
    /icons
    /fonts
    /downloads

/sales
    sales.json

/sessions
    portal_sessions.json
    users.json
    admin.json

/logs
    logs.json

/history
    /sales/YYYY-MM.ndjson
    /sessions/YYYY-MM.ndjson
    /vouchers/YYYY-MM.ndjson
    /logs/YYYY-MM.ndjson
    */undated.ndjson       ← wall clock unavailable

/backups                 ← canonical (timestamped exports)
/backup                  ← legacy (BackupManager temp files today)

/firmware
    update.bin           ← contract OTA package name

/reports
/exports
/cache
/temp

/vouchers                ← legacy (active vouchers.json today)
/www                     ← legacy (portal banner/music today)
```

---

## SPIFFS layout (system storage)

Firmware-owned, read-only after deployment. Bundled with the flash image via `npm run build:esp32`.

```
/portal
/admin
/assets
/defaults
/fallback
/fb
```

Additional bundled prefixes (`/icons`, `/fonts`, `/css`, `/js`) may exist inside the SPIFFS image but are not SD user storage.

---

## Folder ownership

Only the owning manager may create, write, or delete files in its area. Other managers must read through APIs or shared constants — not write directly.

| Folder / area | Owner | Notes |
|---------------|-------|-------|
| `/config` | StorageManager | JSON seeds, layout init; individual config managers read/write their file |
| `/assets` | AssetManager (Phase 3) | Portal branding, ads, icons, fonts, OTA download cache |
| `/logs` | Logger | Append-only event log |
| `/sales` | SessionManager + PortalSessionManager | Revenue / coin sales records |
| `/sessions` | PortalSessionManager | Hotspot session state |
| `/backups` | BackupManager | Timestamped export archives |
| `/backup` | BackupManager | Legacy temp export/restore paths (transition) |
| `/firmware` | OTA Manager (future) | Staged firmware binary |
| `/reports` | Report Manager (future) | Generated reports |
| `/exports` | Export Manager (future) | CSV / data exports |
| `/cache` | Cache Manager (future) | Derived / cached files |
| `/temp` | StorageManager | Transient probes and scratch |
| `/history` | StorageManager + domain managers | Immutable monthly NDJSON ledgers |
| `/vouchers`, `/www` | StorageManager (legacy) | Retained until migration |

Ownership is exposed in storage health JSON via `StoragePaths::ownerForDirectory()` and `ownerLabel()`.

---

## Filename conventions

### User-uploaded assets (canonical — Phase 3)

AssetManager **overwrites** these files. The portal always loads fixed paths. No database lookup. No version suffixes.

| Asset | Path | Filename |
|-------|------|----------|
| Banner | `/assets/banner/` | `current.webp` |
| Logo | `/assets/logo/` | `current.webp` |
| Background | `/assets/background/` | `current.webp` |
| Music | `/assets/music/` | `current.mp3` |
| Ads | `/assets/ads/` | `ad1.webp`, `ad2.webp`, `ad3.webp`, … |
| Firmware OTA | `/firmware/` | `update.bin` |

**Do not use:** `summer_banner_final_v2.png`, `coin_insert_sound_final.mp3`, or any versioned name.

Constants: `StoragePaths::AssetNames::*` and `StoragePaths::Contract*Current` paths.

### Historical ledgers

Sales, completed sessions, voucher transitions, and logs are appended as one
JSON object per line under `/history/<kind>/`. The bounded compatibility files
(`sales.json`, `vouchers.json`) and the Logger RAM ring remain authoritative for
existing endpoints and keep their existing retention behavior. Logger does not
rewrite legacy `logs.json`; that file is preserved in place for compatibility.

Files use `YYYY-MM.ndjson`; events without a usable wall-clock timestamp use
`undated.ndjson`. Each event carries an `eventId`. Financial, voucher, and
session dedupe is bounded to the 24 most recent IDs in RAM plus the final 32 KiB
of the target monthly file. Since each spool is at most 16 KiB and recovery is
serialized, the 32 KiB tail also covers all events from an interrupted spool
replay after reboot. Guaranteed-unique log IDs contain the device ID, a random
boot-instance component, and a per-boot sequence, so log appends do no ledger
read. Readers must ignore an invalid final line caused by power loss.

During SD outage, sales, completed-session, and voucher events use separate
bounded 16 KiB SPIFFS append spools. Spool bytes count toward the existing
320 KiB aggregate fallback hard limit and may not consume the reserved 128 KiB
of SPIFFS free space. They replay only during the existing SD boot/recovery
path. Every valid complete line is verified through the ledger append; a torn
final fragment is moved to the spool's `.q` quarantine and the active spool is
cleared. Logs remain in the unchanged RAM ring when SD is unavailable.

Owner-only downloads stream one monthly SD file:

- `/api/history/sales/download?month=YYYY-MM`
- `/api/history/sessions/download?month=YYYY-MM`
- `/api/history/vouchers/download?month=YYYY-MM`
- `/api/history/logs/download?month=YYYY-MM`

Use `month=undated` for the undated bucket.

### Backups (timestamped)

Exports stored under `/backups/` use:

```
backup_YYYY-MM-DD_HH-MM.zip
```

Example: `backup_2026-06-29_20-35.zip`

Legacy temp files during export/restore remain under `/backup/` until BackupManager migrates:

- `/backup/renzfi-export.zip`
- `/backup/renzfi-export.json`
- `/backup/renzfi-restore.tmp`

### Config JSON

All config lives under `/config/` with fixed basenames matching the domain:

| File | Purpose |
|------|---------|
| `settings.json` | Coin rates, machine settings |
| `router.json` | MikroTik connection |
| `portal.json` | Shared: PortalConfigManager (config) + AssetManager (media) |
| `portal.json` → `branding` / `audio` / `ads` / `media` | AssetInfo per leaf — see PORTAL_CONFIG_ARCHITECTURE.md |
| `promos.json` | Promo definitions |
| `vouchers.json` | Voucher codes (contract; legacy at `/vouchers/vouchers.json` today) |
| `network.json` | Network / VLAN settings (reserved) |
| `auth.json` | Auth credentials policy (reserved) |
| `system.json` | System health / feature flags (reserved) |

---

## Runtime vs contract (transition map)

These paths are **active today**. Phase 3 migrations will switch managers to contract paths without renaming on disk until a migration step copies data.

| Domain | Runtime (active) | Contract (target) |
|--------|------------------|-------------------|
| Portal banner | `/www/portal-banner.webp` | `/assets/banner/current.webp` |
| Portal music | `/www/portal-bg-music.mp3` | `/assets/music/current.mp3` |
| Vouchers | `/vouchers/vouchers.json` | `/config/vouchers.json` |
| Backup temp | `/backup/renzfi-*.zip` | `/backups/backup_*.zip` |

`RenzFiConfig::*` aliases in `Config.h` point at **runtime** paths. New code should reference `StoragePaths::Contract*` for forward-compatible targets.

---

## Boot-time layout

`StorageManager::ensureLayout()`:

1. **`ensureRequiredDirectories()`** — creates all paths in `StoragePaths::requiredSdDirectory()`.
2. **`seedDefaultJsonFiles()`** — creates missing JSON with safe defaults; seeds reserved contract files (`network.json`, `auth.json`, `system.json`, `/config/vouchers.json`) only if absent.

Does **not** migrate or delete legacy files.

---

## Path safety

- All paths must start with `/`.
- No `..` segments.
- Max path length: 128 bytes.
- Use `StoragePaths::joinPath()` / `StorageManager::joinSdPath()` for composition.
- Use `StoragePaths::isValidSdPath()` before SD operations.

---

## Phase 3 checklist (for future work)

- [ ] Implement AssetManager; write only under `/assets/` using canonical filenames — follow [ASSET_LIFECYCLE.md](./ASSET_LIFECYCLE.md)
- [ ] Portal serves banner/music from contract paths with legacy fallback.
- [ ] Migrate vouchers to `/config/vouchers.json`.
- [ ] BackupManager writes timestamped archives to `/backups/`; retire `/backup/` temp names.
- [ ] OTA stages `update.bin` under `/firmware/`.
- [ ] Wire `fillStorageHealth()` to admin API if not already exposed.

---

## Related files

| File | Role |
|------|------|
| `StoragePaths.h` / `StoragePaths.cpp` | Frozen constants, ownership, helpers |
| `StorageManager.h` / `StorageManager.cpp` | Mount, layout, health, read/write abstraction |
| `Config.h` | Runtime aliases (`SETTINGS_FILE`, etc.) |
| `BackupManager.*` | Legacy `/backup/` (unchanged in Phase 2) |
