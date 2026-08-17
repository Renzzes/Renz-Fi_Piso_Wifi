# STORAGE_RUNTIME_OWNERSHIP_FORENSIC.md

**Mode:** Production forensic only — no code changes, no deletions, no redesign  
**Date:** 2026-08-10  
**Basis:** Golden SD layout + full-firmware reference search (`ESP32_S3_Firmware/src`)  
**Prior docs:** `SD_CARD_GOLDEN_LAYOUT_FORENSIC.md`, credential SoT forensics  

**Confidence: 93%** for active vs dead classification of seeded paths.  
**Confidence: 90%** that `/config/existing-network-scan.json` and `/config/network-adoption-workflow.json` have **no writers**.  
**Confidence: 88%** that `/config/wifi.json` and contract stubs (`network`/`auth`/`system`/`config/vouchers`) are seed-only with no functional readers.

---

## Executive verdict

| Class | Count (approx) | Examples |
|-------|----------------|----------|
| **Actively used** | Majority of money/session/router/setup paths | `settings`, `router`, `router-connection`, `promos`, `portal`, sales/sessions, history, provisioning, setup-wizard, installation, router-cache, router-provisioning, legacy `/www` + `/assets/*/current.*` |
| **Duplicated / split-brain** | Several | Vouchers paths; router credential files; portal assets contract vs legacy; backup archive names vs SD paths |
| **Legacy still used** | Few | `/vouchers/vouchers.json`, `/www/portal-*`, `/backup/*` |
| **Contract stubs / future dirs** | Several | `/config/network.json`, `auth.json`, `system.json`, `/config/vouchers.json`, `/reports`, `/exports`, `/cache`, `/backups`, `/firmware` |
| **Dead / near-dead candidates** | Several | `wifi.json`, `admin.json` (never populated), `logs.json` (size only), `build-info.json` (write-only mirror), scan/adoption SD files (no write) |

**Do not remove anything in production firmware yet.** Retirement is a future, migrated release only.

---

## 1. Runtime ownership table (Investigations A–B)

Legend for **Frequency**: Boot / Setup / Activate / Sales / Voucher / Backup / Restore / Never (functional) / Seed-only  

| Path | Read | Write | Exists/Delete | Backup | Frequency | Owner / status |
|------|------|-------|---------------|--------|-----------|----------------|
| `/config/settings.json` | Coin, Network, Rgb, DeviceIdentity, ApiServer, Installation infer, Wizard apply | Seed; Coin stats; Network; Rgb; Api; Wizard | Factory wipe | Yes | **Boot** + runtime | **ACTIVE SoT** (device/coin/admin flags; auth password mainly NVS) |
| `/config/router.json` | MikroTikDriver (activate + admin), Platform, Installation infer | Seed; saveSettings; syncProduction; switchDriver | Factory wipe | Yes | **Boot** (infer) + **Activate** + Admin | **ACTIVE production router SoT** |
| `/config/router-connection.json` | SetupRouterConnectionManager | Setup save / persist | Factory wipe | Fallback eligible | **Setup** (+ Finish sync source) | **ACTIVE setup SoT** (split-brain vs router.json) |
| `/config/portal.json` | PortalConfig, AssetManager, Installation infer | Seed; AssetManager meta | Factory wipe | Yes (archive name `portal-config.json`) | **Boot** + portal/admin branding | **ACTIVE portal meta SoT** |
| `/config/promos.json` | PromoManager | Seed; Promo CRUD | Factory wipe | Yes | **Boot** + rates / coin resolve | **ACTIVE rates SoT** |
| `/config/installation.json` | InstallationStateManager; Finish verify | Seed; setState/persist | Factory wipe | Fallback | **Boot** + setup | **ACTIVE installation SoT** |
| `/config/provisioning.json` | SetupProvisioningManager | begin persist; owner create | Factory wipe | Fallback | **Boot** + setup owner | **ACTIVE owner/setup-unlock SoT** (partial; Auth NVS too) |
| `/config/setup-wizard.json` | SetupWizardConfigManager | begin persist; step saves | Factory wipe | Fallback | **Boot** + setup steps | **ACTIVE wizard draft SoT** |
| `/config/router-provisioning.json` | RouterProvisioningManager; Finish; WirelessAdapter; MikroTikDriver | Provisioning persist; Finish; wifi selection | Factory wipe | Fallback | **Setup** / Finish / wireless | **ACTIVE wireless/provision SoT** |
| `/config/router-cache.json` | RouterCacheManager; MikroTikDriver (profile fallback) | Cache sync/test | Factory wipe | Fallback | Admin sync / profiles | **ACTIVE cache** (not activate credentials) |
| `/config/wifi.json` | **None found** | Seed; factory wipe only | Wipe | No | **Seed-only** | **DEAD candidate** (`WIFI_CONFIG_FILE` unused) |
| `/config/vouchers.json` | **None** (runtime uses `/vouchers/…`) | Seed reserved `"[]"` | Wipe | Archive *name* only; content from other path | **Seed-only** | **Contract stub / dead reader** |
| `/config/network.json` | **None** | Seed `{}` | Wipe | No | **Seed-only** | **Contract stub** |
| `/config/auth.json` | **None** | Seed `{}` | Wipe | No | **Seed-only** | **Contract stub** (Auth = NVS) |
| `/config/system.json` | **None** | Seed `{}` | Wipe | No | **Seed-only** | **Contract stub** |
| `/config/build-info.json` | **None from SD** | `BuildMetadata::mirrorToSd` | Not wiped in factory list | No | Boot write if SPIFFS has build-info | **Write-only mirror**; SPIFFS `/build-info.json` is real SoT |
| `/config/existing-network-scan.json` | **No readJson** | **No writeJson** | removeBinary (reconfigure); factory delete; fallback path mapped | No content | **Never used** | **Dead path** — scan is **RAM** `ExistingNetworkScanCache` |
| `/config/network-adoption-workflow.json` | **None** | **None** | removeBinary; factory wipe | No | **Never used** | **Dead path** |
| `/vouchers/vouchers.json` | VoucherManager | VoucherManager | Factory wipe | Yes (via `VOUCHERS_FILE`) | **Voucher** ops | **ACTIVE voucher SoT** |
| `/sales/sales.json` | SessionManager | SessionManager | Factory wipe | Yes | **Sales** | **ACTIVE live sales array** |
| `/sessions/portal_sessions.json` | PortalSessionManager | PortalSessionManager | Factory wipe | Yes | **Boot** load + continuous portal | **ACTIVE portal session SoT** |
| `/sessions/users.json` | SessionManager | SessionManager | Factory wipe | Yes | Sales/session lifecycle | **ACTIVE user/session ledger** |
| `/sessions/admin.json` | **No session load** | `clearJsonArray` only (Auth reset) | Factory wipe | Delete only | Near-never | **Near-dead** — Auth sessions are **RAM** |
| `/logs/logs.json` | `fileSizeBytes` only (health UI quota) | Seed only | Factory wipe | Delete | Boot seed; health size | **Near-dead content** — logs go to `/history/logs/*.ndjson` |
| `/history/{sales,sessions,vouchers,logs}/*.ndjson` | History download APIs | Logger / Session / Sales / Voucher appendHistory | Factory wipe tree | Export tools | Runtime events | **ACTIVE history SoT** |
| `/www/portal-banner.webp` | AssetResolver (legacy tier) | AssetManager upload/remove | Factory wipe | Backup asset entry (maps to this SD path) | Portal serve | **ACTIVE legacy asset path** |
| `/www/portal-bg-music.mp3` | Same | Same | Same | Same | Portal serve | **ACTIVE legacy** |
| `/assets/banner/current.webp` | AssetManager / resolver | Asset upload | On replace | Preferred contract | Portal | **ACTIVE contract asset** |
| `/assets/music/current.mp3` | Same | Same | Same | Same | Portal | **ACTIVE contract asset** |
| `/assets/{logo,background,ads,videos,icons,fonts,downloads}/` | Helpers exist; ads/logo not wired as AssetType usage in grep | Dir seed only unless future | — | — | Dir **Boot** | **Mostly reserved empty** |
| `/backup/*` | BackupManager | Export/restore temps & journal | Cleanup | Self | **Backup/Restore** | **ACTIVE legacy backup dir** |
| `/backups/` | **No file writers found** | Dir mkdir only | — | Intended canonical | Dir only | **Future / unused** |
| `/firmware/` | Path helper only | OTA uses flash `Update`, not SD `update.bin` | — | — | Dir only | **Future / unused for OTA today** |
| `/reports/`, `/exports/`, `/cache/` | None | Dir mkdir only | — | — | Dir only | **Future stubs** |
| `/temp/.write_probe` | Probe | Probe | Delete after probe | No | Every writable boot | **Transient ACTIVE** |

---

## 2. Read/write matrix (summary)

```
ACTIVE R/W ──────────────────────────────────────────────
  settings.json, router.json, router-connection.json,
  portal.json, promos.json, installation.json,
  provisioning.json, setup-wizard.json,
  router-provisioning.json, router-cache.json,
  /vouchers/vouchers.json, sales.json,
  portal_sessions.json, users.json,
  history/*.ndjson, /www portal assets, /assets/*/current.*,
  /backup temps

WRITE-ONLY / SEED-ONLY ──────────────────────────────────
  wifi.json, network.json, auth.json, system.json,
  /config/vouchers.json, build-info.json (SD mirror),
  logs.json (content), admin.json (clear only)

PATH DECLARED, NO R/W IMPLEMENTATION ────────────────────
  existing-network-scan.json (SD),
  network-adoption-workflow.json

DIR ONLY (NO MANAGER FILES YET) ─────────────────────────
  /backups, /firmware, /reports, /exports, /cache,
  most /assets subdirs beyond banner/music
```

---

## 3. Duplicate storage analysis (Investigation C)

| Pair | Primary | Secondary | Classification |
|------|---------|-----------|----------------|
| `/vouchers/vouchers.json` vs `/config/vouchers.json` | **`/vouchers/vouchers.json`** (VoucherManager) | `/config/vouchers.json` seed stub | Primary + **contract stub / dead** |
| `/config/router.json` vs `/config/router-connection.json` | Activate: **router.json**; Setup: **router-connection.json** | Each other | **Split-brain ACTIVE** |
| `/config/portal.json` vs asset binaries | Meta: portal.json; Bytes: `/assets/.../current.*` then `/www/...` | SPIFFS defaults | Layered SoT (meta + files) |
| `/sales/sales.json` vs `/history/sales/*.ndjson` | Live array: sales.json | Append ledger: history | Dual ACTIVE by design |
| `/sessions/portal_sessions.json` vs `/history/sessions/*.ndjson` | Live: portal_sessions | History ledger | Dual ACTIVE |
| `/logs/logs.json` vs `/history/logs/*.ndjson` | **History NDJSON** | logs.json seed/size | Primary ledger + **legacy empty array** |
| `/backup` vs `/backups` | **`/backup`** (BackupManager) | `/backups` empty dir | Legacy active + contract unused |
| `/assets/.../current.*` vs `/www/portal-*` | Resolver prefers contract then legacy | Both may exist | Dual ACTIVE during migration |
| Backup zip entry `/config/vouchers.json` vs SD `/vouchers/vouchers.json` | SD path = runtime vouchers | Zip *name* misleading | Archive naming quirk |
| Backup zip entry `/config/portal-config.json` vs SD `/config/portal.json` | SD = portal.json | Zip *name* legacy | Archive naming quirk |
| Auth: NVS vs `/config/auth.json` vs `admin.json` | **NVS + RAM sessions** | SD stubs | Stub + near-dead SD |
| Network: NVS vs `settings.json` network vs `wifi.json` vs `network.json` | **NVS** (+ settings.network) | wifi/network.json unused/stub | Split / dead extras |
| Build info: SPIFFS `/build-info.json` vs SD `/config/build-info.json` | **SPIFFS** | SD mirror write-only | Mirror not read |

---

## 4. Dead / legacy / retirement candidates (Investigations D, G)

### Dead file candidates (zero functional readers; safe only after future migration)

| Path | Evidence | Future retirement risk |
|------|----------|------------------------|
| `/config/wifi.json` | Only seed + factoryReset list; `WIFI_CONFIG_FILE` never read | Low if no external tools depend on it |
| `/config/network.json` | Seed only | Low |
| `/config/auth.json` | Seed only | Low |
| `/config/system.json` | Seed only | Low |
| `/config/vouchers.json` | Seed only; vouchers use `/vouchers/` | Medium — contract name reserved for Phase 3 |
| `/config/existing-network-scan.json` | No R/W; RAM cache used | Low — path reserved for fallback map |
| `/config/network-adoption-workflow.json` | Delete only, no write | Low |

### Near-dead (keep until cleaned)

| Path | Why keep for now |
|------|------------------|
| `/sessions/admin.json` | `clearJsonArray` on auth reset; removing breaks wipe assumptions |
| `/logs/logs.json` | Health UI `logsUsedKb` uses file size; seed/wipe |
| `/config/build-info.json` | Written each boot; unused read — harmless |

### Legacy but ACTIVE (do not retire without migration)

| Path | Reason |
|------|--------|
| `/vouchers/vouchers.json` | Runtime voucher SoT |
| `/www/portal-*` | Still resolved by AssetResolver |
| `/backup/*` | Only working backup/restore workspace |
| `/config/router.json` empty seed fields | Still production activate SoT |

### Future dirs (empty — not dead files, reserved)

`/backups`, `/firmware`, `/reports`, `/exports`, `/cache`, unused asset subdirs.

---

## 5. Source-of-truth table (Investigation F)

| Domain | Single SoT? | Authoritative store |
|--------|-------------|---------------------|
| Router API (activate Internet) | Intended yes; **split with setup** | `/config/router.json` |
| Router API (setup connect) | Separate | `/config/router-connection.json` |
| Portal branding meta | Yes | `/config/portal.json` |
| Portal banner/music bytes | Prefer contract, fall back legacy | `/assets/.../current.*` → `/www/...` → SPIFFS defaults |
| Voucher inventory | Yes (path legacy) | `/vouchers/vouchers.json` |
| Sales live | Yes + history | `/sales/sales.json` |
| Sales history | Ledger | `/history/sales/*.ndjson` |
| Portal sessions | Yes + history | `/sessions/portal_sessions.json` |
| Coin rates (runtime) | Dual intentional | `promos.json` + `settings.json` coin |
| Installation state | Yes | `/config/installation.json` |
| Owner / setup unlock | Mostly | `/config/provisioning.json` (+ Auth NVS) |
| Admin password / sessions | NVS + RAM | Not SD `auth.json` / `admin.json` |
| Ethernet addressing | NVS primary | `NetworkSettingsManager` NVS; settings.network secondary |
| Wireless guest selection | Yes | `/config/router-provisioning.json` |
| Router profile cache | Yes | `/config/router-cache.json` |
| Logs | History ledger | `/history/logs/*.ndjson` |
| Build metadata | SPIFFS | `/build-info.json` (SPIFFS) |

---

## 6. Migration risk if removed (Investigation E)

| If removed | Breaks |
|------------|--------|
| `router.json` | **Router activate**, admin settings, Finish sync target |
| `router-connection.json` | **Setup** test/save/Finish credential resolve |
| `settings.json` | Coin, RGB, device name, network settings persistence |
| `portal.json` | Portal branding meta, AssetManager ready gate |
| `promos.json` | Rates / coin minutes |
| `portal_sessions.json` | Captive portal sessions |
| `sales.json` / history | Sales / accounting / downloads |
| `/vouchers/vouchers.json` | **Voucher** subsystem |
| `installation.json` | Setup gating, Ready checks |
| `provisioning.json` / `setup-wizard.json` | Setup owner/wizard |
| `router-provisioning.json` | Finish wireless / provision |
| `router-cache.json` | Admin profiles UX (not Internet grant) |
| `/www` or `/assets` currents | Portal media |
| `/backup` | **Backup/Restore** |
| Seed-only stubs (`wifi`, `network`, `auth`, `system`, config vouchers) | **Likely nothing functional today**; wipe/seed code still expects recreate |
| `admin.json` / `logs.json` | Minor wipe/clear/size paths only |
| Scan/adoption SD paths | **Nothing** (already unused for R/W) |
| `/backups`, `/firmware` dirs | Nothing today |

---

## 7. Files safe for *future* retirement (not now)

**Candidate wave (after explicit migration PR + hardware soak):**

1. Stop seeding `/config/wifi.json`, `/config/network.json`, `/config/auth.json`, `/config/system.json` once no external tooling depends on them.  
2. Either implement or drop SD paths for `existing-network-scan.json` / `network-adoption-workflow.json` (today RAM-only).  
3. Migrate voucher SoT to `/config/vouchers.json` **or** stop seeding the unused contract copy.  
4. Collapse `/backup` → `/backups` with BackupManager rewrite.  
5. Stop mirroring unread `/config/build-info.json` **or** start reading it.  
6. Remove `admin.json` / `logs.json` after Auth/Logger no longer reference them.

**Not safe to retire without large projects:**  
`router.json`, `router-connection.json`, `settings.json`, portal/sales/sessions/voucher runtime paths, history ledgers, installation/provisioning/setup-wizard, `/www` until AssetResolver drops legacy tier.

---

## 8. Regression risk

| Change class | Risk |
|--------------|------|
| Delete seed stubs on live devices | Low functional; medium if tools expect files |
| Point vouchers to `/config/vouchers.json` without migrate | **High** — empty inventory |
| Delete `router-connection` or stop sync | **High** — setup/Finish |
| Delete `router.json` | **Critical** — no Internet activate |
| Move backup to `/backups` without code | **High** — restore/export fail |
| Remove `/www` before resolver update | Portal branding break |
| Remove `logs.json` without health UI update | Cosmetic health metric |

Stability rule: **no removal in current release**; any retirement needs additive migrate + dual-read window.

---

## 9. Confidence score

| Claim | Confidence |
|-------|------------|
| Active core config/session/sales/voucher paths correctly identified | **95%** |
| `wifi.json` / contract stubs have no functional readers | **92%** |
| Existing-network / adoption SD files have no writers | **95%** |
| Scan cache is RAM-only | **98%** |
| `/backups` and `/firmware/update.bin` unused for current backup/OTA | **90%** |
| Safe future retirement list | **85%** (tooling/docs outside firmware unknown) |

---

## Success criteria

- Runtime ownership of Golden SD files mapped with read/write evidence.  
- Dead, legacy, duplicate, and SoT classifications without deleting anything.  
- Clear retirement *candidates* only — **no implementation**.

**END OF FORENSIC REPORT**
