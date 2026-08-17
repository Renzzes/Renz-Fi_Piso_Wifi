# SD_CARD_GOLDEN_LAYOUT_FORENSIC.md

**Mode:** Production forensic only — no source changes  
**Date:** 2026-08-10  
**Scenario:** Brand-new / fully formatted FAT32 SD (empty) → flash firmware → power on → writable SD mounts successfully  
**Authority:** `StoragePaths.h`, `StorageManager.cpp`, `FirmwareApp.cpp`, manager `begin()`/`load()` paths  

**Confidence: 94%** for first-boot layout when SD is mounted **and writable**.  
**Confidence: 88%** for post-begin files that depend on optional SPIFFS build-info / first logger write.

---

## Executive summary

On an empty writable SD, firmware itself creates:

1. **28 required directories** (`StoragePaths::kRequiredSdDirectories`)
2. **A fixed set of seed JSON files** (`seedDefaultJsonFiles` + reserved contract stubs)
3. **Two additional config files on manager begin** (`provisioning.json`, `setup-wizard.json`)
4. **Optionally** `/config/build-info.json` (mirror from SPIFFS) and `/history/logs/undated.ndjson` (first log)
5. **Transient** `/temp/.write_probe` during write probe (deleted before layout seed)

It does **not** create on first boot: `router-connection.json`, `router-cache.json`, `router-provisioning.json`, asset binaries, backup zips, sales/session history (except possibly log history), or portal branding under `/www` / `/assets/*/current.*`.

---

## 1. Boot storage timeline (Investigation A)

Exact order from `FirmwareApp::begin()` + `StorageManager::begin()` (source-proven):

```
setup() / FirmwareApp::begin
  Phase 0  RecoveryManager::runBootCheck
  Phase 1  Ethernet begin (NVS network only — no SD)
  Phase 2  SPIFFS.begin(false)     ← not SD; firmware web assets
  Phase 3  StorageManager::begin
             mountSpiffs (reuse)
             SD.begin / mountSdCard
             probeSdWritable
               ensureSdDirectory("/temp")
               create/read/delete "/temp/.write_probe"   ← TRANSIENT
             recoverBootTransactions
             BackupManager::recoverPendingRestore
             ensureLayout                    ★ GOLDEN CREATE
               ensureRequiredDirectories     ★ all dirs
               seedDefaultJsonFiles          ★ seed JSON
             validateLayout
             syncFallbackToSd (only if FB_MANIFEST exists — empty card: skip)
             replayHistorySpools (empty: no-op)
  Phase 4+ managers begin (after storage)
             BuildMetadata::begin → may mirror /config/build-info.json
             Logger::begin (RAM only)
             InstallationStateManager::begin → load seeded installation.json
             AuthManager::begin (NVS — not SD)
             SetupRouterConnectionManager::begin → load; NO file if missing
             RouterProvisioningManager::begin → load; NO file if missing
             NetworkSettingsManager::begin (NVS + may read settings.json)
             CoinManager::begin → read settings.json (no create)
             SetupWizardConfigManager::begin → persist setup-wizard.json if missing ★
             SetupProvisioningManager::begin → persist provisioning.json if missing ★
             RouterCacheManager::begin → load; NO file if missing
             RouterPlatform / RouterWorker
             SessionManager / PromoManager / VoucherManager / PortalSessionManager
             AssetManager / PortalConfigManager (read meta; no asset files)
             RgbController::begin → read settings (no write unless rgb key save later)
             … APIs / boot summary
             Logger.info("Firmware boot complete…") → may create
               /history/logs/undated.ndjson ★
```

**Ready** in the installation sense is **not** reached on empty-card first boot (`installation.json` state = `factory`). Appliance can still serve setup/portal paths; golden layout ≠ production Ready.

---

## 2. Every directory created (Investigation C)

Created by `StorageManager::ensureRequiredDirectories` → `ensureDir` → `SD.mkdir`  
**When:** first successful writable `ensureLayout`  
**Owner registry:** `StoragePaths::ownerForDirectory`

| Directory | Creator | Purpose | Mandatory |
|-----------|---------|---------|-----------|
| `/config` | StorageManager | Config JSON | Yes |
| `/assets` | StorageManager | Asset root | Yes (empty) |
| `/assets/banner` | StorageManager | Banner assets | Yes (empty) |
| `/assets/music` | StorageManager | Music assets | Yes (empty) |
| `/assets/logo` | StorageManager | Logo assets | Yes (empty) |
| `/assets/background` | StorageManager | Background | Yes (empty) |
| `/assets/ads` | StorageManager | Ads | Yes (empty) |
| `/assets/videos` | StorageManager | Videos | Yes (empty) |
| `/assets/icons` | StorageManager | Icons | Yes (empty) |
| `/assets/fonts` | StorageManager | Fonts | Yes (empty) |
| `/assets/downloads` | StorageManager | Downloads | Yes (empty) |
| `/sales` | StorageManager | Sales JSON | Yes |
| `/sessions` | StorageManager | Session JSON | Yes |
| `/logs` | StorageManager | Logs JSON array | Yes |
| `/backups` | StorageManager | Canonical backups | Yes (empty) |
| `/backup` | StorageManager | Legacy BackupManager temp | Yes (empty) |
| `/firmware` | StorageManager | OTA package | Yes (empty) |
| `/reports` | StorageManager | Future reports | Yes (empty) |
| `/exports` | StorageManager | Future exports | Yes (empty) |
| `/cache` | StorageManager | Future cache | Yes (empty) |
| `/temp` | StorageManager (+ early write probe) | Scratch / probes | Yes |
| `/history` | StorageManager | History root | Yes |
| `/history/sales` | StorageManager | Sales NDJSON | Yes (empty until sales) |
| `/history/sessions` | StorageManager | Session NDJSON | Yes (empty until events) |
| `/history/vouchers` | StorageManager | Voucher NDJSON | Yes (empty until vouchers) |
| `/history/logs` | StorageManager | Log NDJSON | Yes (may get first file at boot) |
| `/vouchers` | StorageManager | Legacy voucher store | Yes |
| `/www` | StorageManager | Legacy portal branding | Yes (empty) |

Source: `StoragePaths.cpp` **9–38**; `StorageManager.cpp` **556–565**, **2267–2273**.

---

## 3. Every file created on empty writable SD (Investigation B)

### 3.1 During write probe (before seed; then deleted)

| Path | Who | Fate |
|------|-----|------|
| `/temp/.write_probe` | `probeSdWritable` | Created, verified, **deleted** (`StorageManager.cpp` **2468–2512**) |

### 3.2 Seeded by `seedDefaultJsonFiles` (first boot only if missing)

| Path | Template owner | Line / notes |
|------|----------------|--------------|
| `/config/settings.json` | `kDefaultSettings` | `StorageManager.cpp` **18–25**, **572** |
| `/config/promos.json` | `kDefaultPromos` | **27–29**, **573** |
| `/config/router.json` | `kDefaultRouter` | **31–33**, **574** |
| `/config/wifi.json` | inline string | **575–578** |
| `/vouchers/vouchers.json` | `kDefaultVouchers` `"[]"` | **35**, **580** — **runtime voucher path** |
| `/sales/sales.json` | `"[]"` | **581** |
| `/logs/logs.json` | `"[]"` | **582** |
| `/sessions/users.json` | `"[]"` | **583** |
| `/sessions/admin.json` | `"[]"` | **584** |
| `/sessions/portal_sessions.json` | `kDefaultPortalSessions` | **36**, **585–586** |
| `/config/portal.json` | inline + reserved | **587–589** (also reserved index) |
| `/config/installation.json` | inline factory seed | **590–597** |
| `/config/vouchers.json` | reserved `"[]"` | `StoragePaths.cpp` **45**, **54** — **contract stub; not runtime voucher SoT** |
| `/config/network.json` | reserved `"{}"` | **46**, **56** |
| `/config/auth.json` | reserved `"{}"` | **47**, **57** |
| `/config/system.json` | reserved `"{}"` | **48**, **58** |

Reserved loop: `seedDefaultJsonFiles` **599–604**. Entries with `nullptr` default skip (settings/router/promos already seeded).

### 3.3 Created on manager `begin` / first write (empty card)

| Path | Who | When |
|------|-----|------|
| `/config/provisioning.json` | `SetupProvisioningManager::load` → `persist` | File missing → write defaults (**215–224**) |
| `/config/setup-wizard.json` | `SetupWizardConfigManager::load` → `persist` | File missing → write defaults (**207–208**) |
| `/config/build-info.json` | `BuildMetadata::mirrorToSd` | Only if SPIFFS `/build-info.json` exists (**12–31**, **42–45**) |
| `/history/logs/undated.ndjson` | `Logger::write` → `appendHistory` | First log after storage healthy; bucket `undated` when clock is uptime marker |

### 3.4 Explicitly NOT created on empty first boot (source-proven)

| Path | Why |
|------|-----|
| `/config/router-connection.json` | `SetupRouterConnectionManager::load` applies RAM defaults; **no persist** if missing (**98–107**) |
| `/config/router-provisioning.json` | `RouterProvisioningManager::load` returns without writing (**1178–1180**) |
| `/config/router-cache.json` | `RouterCacheManager::load` requires `exists` (**47–48**) |
| `/config/existing-network-scan.json` | No boot create |
| `/config/network-adoption-workflow.json` | No boot create |
| `/assets/**/current.*`, `/www/portal-*` | Created only on asset upload / portal branding write |
| `/backup/renzfi-*.zip|tmp|json` | Backup/export only |
| `/firmware/update.bin` | OTA only |
| `/sales` / `/sessions` / `/vouchers` history NDJSON | First sale/session/voucher event |

---

## 4. Default JSON contents (Investigation D)

### `/config/settings.json` — `kDefaultSettings` (18–25)

```json
{
  "admin": {
    "passwordHash": "",
    "mustChangePassword": true,
    "firstBootCompleted": false
  },
  "network": {
    "ip": "10.40.0.2",
    "gateway": "10.40.0.1",
    "subnet": "255.255.255.0",
    "dns": "10.40.0.1"
  },
  "coin": {
    "pulsesPerPeso": 1,
    "pesoPerPulse": 1,
    "defaultMinutesPerPeso": 5,
    "debounceMs": 35,
    "settleMs": 450,
    "enabled": true
  },
  "device": {
    "name": "Renz-Fi",
    "timezone": "Asia/Manila"
  }
}
```

| Field | Intent |
|-------|--------|
| `admin.passwordHash` empty | Must be set by owner setup |
| `firstBootCompleted` false | Placeholder until auth completes |
| network block | Appliance LAN placeholders (may differ from NVS DHCP/static) |

### `/config/router.json` — `kDefaultRouter` (31–33)

```json
{
  "host": "10.40.0.1",
  "username": "",
  "password": "",
  "profile": "default",
  "ssid": "RenzFi_PesoWifi",
  "wifiPassword": ""
}
```

| Field | Intent |
|-------|--------|
| `username`/`password` empty | **Intentional** — must be overwritten by Finish sync / Admin save |
| `host` | Placeholder |
| `ssid`/`wifiPassword` | Legacy leftover fields; wireless SoT is RouterOS / other files |

### `/config/promos.json` — one default rate row (27–29)

### `/config/wifi.json`

```json
{
  "staSsid": "RenzFi_Admin",
  "useStaticIp": true,
  "staIp": "10.10.10.2",
  "staGateway": "10.10.10.1",
  "staSubnet": "255.255.255.0"
}
```

### `/config/portal.json`

```json
{"revision":0,"hasBanner":false,"hasMusic":false}
```

### `/config/installation.json` (seed)

```json
{
  "state": "factory",
  "updatedAt": 0,
  "completedSteps": [],
  "firmwareVersion": "0.5.0-w5500",
  "installationVersion": 2,
  "session": {
    "sessionId": "",
    "startedAt": 0,
    "lastActivity": 0,
    "installerName": "",
    "deviceId": "",
    "isRecovery": false,
    "attempt": 0
  }
}
```

### Arrays / empties

| File | Default |
|------|---------|
| `/sales/sales.json` | `[]` |
| `/logs/logs.json` | `[]` |
| `/sessions/users.json` | `[]` |
| `/sessions/admin.json` | `[]` |
| `/vouchers/vouchers.json` | `[]` |
| `/config/vouchers.json` | `[]` (contract stub) |
| `/sessions/portal_sessions.json` | `{"sessions":[]}` |
| `/config/network.json` | `{}` |
| `/config/auth.json` | `{}` |
| `/config/system.json` | `{}` |

### `/config/provisioning.json` (first `persist`)

Built by `SetupProvisioningManager::buildDocument`: schemaVersion **2**, `ownerCreated` false, empty owner fields, default setup-unlock hash for `"renzfi-setup"`, timestamps set.

### `/config/setup-wizard.json` (first `persist`)

schemaVersion **1**, ethernet/ap/coin not configured, default coin rates 1→5 / 5→25 / 10→50 min, abuse 5 / ban 10 / resetWindow 60, guest portal display name `"Renz-Fi WiFi"`.

---

## 5. Ownership table (Investigation E)

| File | Writes | Reads | Updates | Deletes | Migrate | Backup | Restore |
|------|--------|-------|---------|---------|---------|--------|---------|
| settings.json | Storage seed; Coin/Network/Rgb/Wizard/Api | Coin, Network, Auth-related, Admin | Same writers | Factory wipe | Wizard/network merges | BackupManager | Restore / reseed |
| router.json | Seed; MikroTik/Foundation/Generic save; syncProduction; switchDriver | MikroTikDriver activate + admin | Same | Factory wipe | — | Yes | Yes / default empty user |
| portal.json | Seed; PortalConfig/AssetManager | PortalConfig, Installation infer | Asset uploads bump meta | Factory wipe | — | Yes | Yes |
| promos.json | Seed; PromoManager | Promo/Coin/Portal | Promo CRUD | Factory wipe | — | Yes | Yes |
| wifi.json | Seed; wifi config API | Network/wifi config | API | Factory wipe | — | Not in main kJsonEntries list as wifi — check BackupManager | — |
| installation.json | Seed; InstallationStateManager | Setup, boot, sales time gate | setState/advance | Factory wipe | migrateDocument | Fallback eligible | Wipe + reseed |
| provisioning.json | SetupProvisioningManager | Setup status | Owner create | Factory wipe | migrateDocument | Fallback | Wipe |
| setup-wizard.json | SetupWizardConfigManager | Wizard steps | Step saves | Factory wipe | — | Fallback | Wipe |
| router-connection.json | Setup save only | Setup resolve/Finish sync | Save | Factory wipe | migrateDocument | Fallback | Wipe |
| router-provisioning.json | Provisioning/Finish/Wifi selection | RouterProvisioningManager | Apply/Finish | Factory wipe | migrate | Fallback | Wipe |
| router-cache.json | RouterCacheManager | Admin profiles/cache UI | Sync/test | Factory wipe | normalize | Fallback | Wipe |
| sales.json | SessionManager | Sales/history APIs | Sales record | Factory wipe | — | Yes | Yes |
| portal_sessions.json | PortalSessionManager | Portal/API | Tick/save | Factory wipe | — | Yes | Yes |
| users.json | SessionManager | Sessions | Session lifecycle | Factory wipe | — | Yes | Yes |
| admin.json | Auth sessions path | Auth | Login sessions | Factory wipe | — | — | Wipe |
| vouchers.json (legacy `/vouchers/`) | VoucherManager | VoucherManager | CRUD/redeem | Factory wipe | — | Yes | Yes |
| vouchers.json (`/config/`) | Seed only today | Contract reserved | — | Factory wipe | — | — | Reseed |
| logs.json | Seed; rarely used vs history | Possible list paths | — | Factory wipe | — | — | Reseed |
| history/*/*.ndjson | Logger/Session/Sales/Voucher appendHistory | History download | Append-only | Factory wipe tree | Spool replay | Export | Wipe |
| build-info.json | BuildMetadata mirror | System build API | Each boot if SPIFFS present | Not in wipe list explicitly — survives unless deleted | — | — | — |

---

## 6. Source of truth (Investigation F)

| Domain | Authoritative store | Split-brain? |
|--------|---------------------|--------------|
| Router API credentials (production activate) | `/config/router.json` | **Yes** vs `/config/router-connection.json` (setup) |
| Router connection (setup) | `/config/router-connection.json` | Dual with production |
| Portal branding meta | `/config/portal.json` | Assets also under `/assets/*` + legacy `/www/*` |
| Coin rates (runtime) | `/config/settings.json` `coin` + `/config/promos.json` | Wizard also in `setup-wizard.json` until applied |
| Sales | `/sales/sales.json` + `/history/sales/*.ndjson` | Dual (live array + ledger) |
| Vouchers | **`/vouchers/vouchers.json`** (runtime) | Contract `/config/vouchers.json` is stub only |
| Installation | `/config/installation.json` | Can disagree with inferFromStorage |
| Owner / setup unlock | `/config/provisioning.json` (+ Auth NVS) | Auth passwords primarily NVS |
| Settings / device | `/config/settings.json` | Network also NVS |
| WiFi STA (legacy file) | `/config/wifi.json` | Ethernet mode also NVS |
| Provisioning plan | `/config/router-provisioning.json` | Created later |
| Assets | `/assets/.../current.*` when uploaded; SPIFFS defaults until then | Dual with SPIFFS portal defaults |

---

## 7. Classification (Investigation G)

| Class | Paths |
|-------|-------|
| **Required (boot reseed if missing)** | All dirs in §2; all files in §3.2 via `ensureJsonFile` / `ensureLayout` |
| **Required (manager recreate if missing)** | `provisioning.json`, `setup-wizard.json` |
| **Optional / later** | `router-connection.json`, `router-cache.json`, `router-provisioning.json`, scan/adoption files, assets, backups, OTA |
| **Generated** | History NDJSON, build-info mirror |
| **Transient** | `/temp/.write_probe`, `*.t` / `*.b` transaction sidecars, backup temp under `/backup` |
| **Cache** | `router-cache.json`, `/cache` (empty dir) |
| **History** | `/history/**` |

**If missing after first seed:** next boot recreates seed files/dirs if writable.  
**If SD read-only:** no `ensureLayout` seed; SPIFFS fallback for eligible paths.  
**If SD missing:** no golden SD layout; SPIFFS fallback seeds only `FB_PORTAL_SESSIONS` / `FB_SALES` (`StorageManager.cpp` **226–240**).

---

## 8. Transient files (Investigation H) — never preload

| Artifact | When appears | When disappears |
|----------|--------------|-----------------|
| `/temp/.write_probe` | Every writable mount probe | Immediately after verify |
| `path.t` / `path.b` | During transactional SD writes | Commit/rollback / boot recover |
| `/backup/renzfi-restore.tmp` | Restore in progress | Restore complete / recover |
| `/backup/renzfi-export.zip` | Export | After download/cleanup |
| SPIFFS `/fallback/*`, `/fb/*`, `.manifest.json` | Degraded SD writes | After `syncFallbackToSd` |
| SPIFFS history spools `/fb/h*.ndjson` | Fallback append | Replay to SD |

**Do not put these on a golden card image.**

---

## 9. Golden SD directory tree (Investigation I)

Legend: **B** = first boot (writable empty SD) · **L** = later lifecycle · **T** = transient

```
/
├── config/                          B
│   ├── settings.json                B  seed
│   ├── router.json                  B  seed (empty username/password)
│   ├── portal.json                  B  seed
│   ├── promos.json                  B  seed
│   ├── wifi.json                    B  seed
│   ├── installation.json            B  seed (factory)
│   ├── vouchers.json                B  contract stub []
│   ├── network.json                 B  {}
│   ├── auth.json                    B  {}
│   ├── system.json                  B  {}
│   ├── provisioning.json            B  manager begin
│   ├── setup-wizard.json            B  manager begin
│   ├── build-info.json              B? if SPIFFS build-info exists
│   ├── router-connection.json       L  after setup router save
│   ├── router-provisioning.json     L  after provisioning/finish
│   ├── router-cache.json            L  after router sync/test cache
│   ├── existing-network-scan.json   L  after scan
│   └── network-adoption-workflow.json L after adoption workflow
├── assets/… (all subdirs empty)     B
│   └── …/current.webp|mp3           L  after asset upload
├── sales/
│   └── sales.json                   B  []
├── sessions/
│   ├── portal_sessions.json         B
│   ├── users.json                   B
│   └── admin.json                   B
├── vouchers/
│   └── vouchers.json                B  []  ← runtime vouchers
├── logs/
│   └── logs.json                    B  []
├── history/
│   ├── sales/                       B empty; L first sale → YYYY-MM.ndjson
│   ├── sessions/                    B empty; L session events
│   ├── vouchers/                    B empty; L voucher events
│   └── logs/
│       └── undated.ndjson           B? first Logger write (no wall clock)
├── backups/                         B empty; L timestamped exports
├── backup/                          B empty; L export/restore temps
├── firmware/                        B empty; L update.bin
├── reports/                         B empty
├── exports/                         B empty
├── cache/                           B empty
├── temp/                            B; T .write_probe during probe only
├── www/                             B empty; L legacy portal banner/music
└── (no preload of *.t *.b)
```

---

## 10–16. Mandatory / optional / edit / delete / recreate / backup

### Mandatory after first successful writable boot

All §2 directories + §3.2 seed files + `provisioning.json` + `setup-wizard.json`.

### Optional

Everything in §3.4 / later lifecycle markers.

### Never manually edit (production risk)

| File | Why |
|------|-----|
| `router.json` / `router-connection.json` | Credential split-brain; activate depends on exact fields |
| `installation.json` | Skews setup gating |
| `provisioning.json` | Owner hashes / unlock |
| `portal_sessions.json` / `sales.json` | Live money/time state |
| Transaction `*.t`/`*.b`, write probe | Corruption / false health |
| Dirty SPIFFS fallback copies | Can override SD reads |

### Safe to delete (firmware recreates or tolerates)

| Path | Result |
|------|--------|
| Empty optional dirs’ contents | OK |
| Missing seed JSON | Recreated on next `ensureLayout` if writable |
| `router-cache.json` | Regenerated on sync |
| History NDJSON | Loss of history only |
| `/temp/*` | OK |

### Files requiring backup before field service

`settings.json`, `router.json`, `router-connection.json`, `promos.json`, `portal.json`, `sales.json`, `vouchers/vouchers.json`, `portal_sessions.json`, `installation.json`, `provisioning.json`, `setup-wizard.json`, asset binaries, history ledgers.

### Recreated automatically

Seed set (§3.2), directories (§2), `provisioning.json` / `setup-wizard.json` if deleted, write probe each boot.

---

## 17. Factory reset behavior

`BackupManager::wipeUserData` + `StorageManager::factoryResetData`:

- Deletes listed config/session/sales/logs/users/portal/installation/provisioning/router-connection/router-provisioning/router-cache/scan/setup-wizard/adoption paths (+ tx sidecars)
- Clears history tree + SPIFFS spools
- Calls `ensureLayout()` → **recreates golden dirs + seed JSON**
- Installation forced to Factory
- Does **not** by itself recreate `provisioning.json` / `setup-wizard.json` until managers `begin`/`load` again (next boot or same boot if managers reload)

---

## 18. Firmware upgrade behavior

- **Does not wipe SD** on normal flash.
- Existing files kept (`ensureJsonFile` skips if exists).
- New reserved files appear only if missing.
- Schema migrations may rewrite `installation.json` / provisioning on load.
- SPIFFS web assets replace independently of SD.

---

## 19. Migration behavior

- Contract vs legacy: `/config/vouchers.json` stub **and** `/vouchers/vouchers.json` runtime.
- Legacy `/backup`, `/www`, `/vouchers` dirs always created.
- Fallback SPIFFS ↔ SD sync only when dirty manifest exists (not on clean empty card).

---

## 20. Verification matrix (Investigation J)

Assume empty FAT32 SD, writable mount, SPIFFS OK:

| Check | Expected |
|-------|----------|
| 28 required dirs exist | Yes |
| Seed JSON list §3.2 | Yes |
| `username` in router.json | `""` |
| `installation.state` | `factory` |
| `router-connection.json` | **Absent** |
| Asset current.* | **Absent** |
| `.write_probe` | **Absent** after boot |
| `provisioning.json` + `setup-wizard.json` | **Present** after full begin |
| Duplicate voucher paths | Both `/config/vouchers.json` and `/vouchers/vouchers.json` as `[]` |
| Unexpected split-brain | Production router empty vs no setup connection file yet — expected for factory |

---

## 21. Regression safety (Investigation K)

If future firmware changes defaults/paths for:

| Change | Affects |
|--------|---------|
| `router.json` seed / schema | All new cards; old cards keep old until migrate; activate path |
| `settings.json` | Coin/network/admin; may break firstBoot flags |
| `portal.json` | Branding meta; AssetManager |
| Sales/sessions/history | Ledger tools, restore, disk growth |
| Backup entry list | Field restore completeness |
| Directory registry | `ensureLayout` / health UI |

Existing appliances: **non-destructive** if only additive seeds; **destructive** if factory wipe or if migrations rewrite credentials/sessions without care.

---

## Success criterion

This document is the **source-proven Golden SD Card Layout** for first boot on an empty writable card. Use it to validate every production flash: directories, seed files, intentional empty credentials, and files that must remain absent until setup/ops lifecycle.

**No code modified. No storage redesign. No RouterOS/CPU impact.**

**END OF FORENSIC REPORT**
