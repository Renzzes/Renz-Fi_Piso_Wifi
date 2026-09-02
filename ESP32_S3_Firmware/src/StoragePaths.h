#pragma once

#include <cstddef>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
//  StoragePaths — FROZEN storage contract for Renz-Fi firmware (Phase 2.3)
//
//  SPIFFS = system storage (firmware-owned, read-only after deployment).
//  SD card = user storage (all persistent customer / runtime data).
//
//  RUNTIME ACTIVE paths (VouchersFile, LegacyBackup*, LegacyPortal*) must not
//  change without an explicit migration phase.  CONTRACT paths define the
//  target layout for Phase 3+ managers (AssetManager, etc.).
//
//  See docs/STORAGE_ARCHITECTURE.md for ownership rules and naming conventions.
// ─────────────────────────────────────────────────────────────────────────────

namespace StoragePaths {

// ── Storage ownership (which manager may write each top-level area) ─────────

enum class StorageOwner : uint8_t {
  StorageManager,       // /config (JSON seeds, layout)
  AssetManager,         // /assets (Phase 3 — not active yet)
  Logger,               // /logs
  SessionManagers,      // /sales — SessionManager + PortalSessionManager
  PortalSessionManager, // /sessions
  BackupManager,        // /backups (+ legacy /backup transition)
  OtaManager,           // /firmware (Phase 3+)
  ReportManager,        // /reports (future)
  ExportManager,        // /exports (future)
  CacheManager,         // /cache (future)
  StorageManagerTemp,   // /temp — transient probes and scratch
  None,
};

const char *ownerLabel(StorageOwner owner);
StorageOwner ownerForDirectory(const char *dirPath);
StorageOwner ownerForPath(const char *filePath);

// ── SD card: top-level directories (frozen layout) ───────────────────────────

static constexpr const char *Config    = "/config";
static constexpr const char *Assets    = "/assets";
static constexpr const char *Sales     = "/sales";
static constexpr const char *Sessions  = "/sessions";
static constexpr const char *Logs      = "/logs";
static constexpr const char *Backups   = "/backups";   // canonical (plural)
static constexpr const char *Firmware  = "/firmware";
static constexpr const char *Reports   = "/reports";
static constexpr const char *Exports   = "/exports";
static constexpr const char *Cache     = "/cache";
static constexpr const char *Temp      = "/temp";
static constexpr const char *History   = "/history";
static constexpr const char *HistorySales = "/history/sales";
static constexpr const char *HistorySessions = "/history/sessions";
static constexpr const char *HistoryVouchers = "/history/vouchers";
static constexpr const char *HistoryLogs = "/history/logs";

// SD asset subdirectories (AssetManager — Phase 3)
static constexpr const char *AssetsBanner     = "/assets/banner";
static constexpr const char *AssetsMusic      = "/assets/music";
static constexpr const char *AssetsLogo       = "/assets/logo";
static constexpr const char *AssetsBackground = "/assets/background";
static constexpr const char *AssetsAds        = "/assets/ads";
static constexpr const char *AssetsVideos     = "/assets/videos";
static constexpr const char *AssetsIcons      = "/assets/icons";
static constexpr const char *AssetsFonts      = "/assets/fonts";
static constexpr const char *AssetsDownloads  = "/assets/downloads";

// Legacy SD directories — retained for backward compatibility (runtime today)
static constexpr const char *LegacyBackup     = "/backup";    // BackupManager active
static constexpr const char *LegacyVouchers   = "/vouchers";  // VoucherManager active
static constexpr const char *LegacyWww        = "/www";       // PortalConfigManager active

// ── Canonical filenames (frozen — AssetManager overwrites, no versioning) ─────

namespace AssetNames {

static constexpr const char *CurrentWebp = "current.webp";
static constexpr const char *CurrentMp3  = "current.mp3";
static constexpr const char *FirmwareBin = "update.bin";

static constexpr const char *Ad1Webp = "ad1.webp";
static constexpr const char *Ad2Webp = "ad2.webp";
static constexpr const char *Ad3Webp = "ad3.webp";

// Backup exports: backup_YYYY-MM-DD_HH-MM.zip (generated at export time)
static constexpr const char *BackupPrefix = "backup_";
static constexpr const char *BackupSuffix = ".zip";

}  // namespace AssetNames

// ── CONTRACT: /config/*.json (frozen firmware contract) ─────────────────────
// Phase 3+ target.  Reserved files are seeded empty on first boot if missing.
// Runtime managers may still read legacy paths until migration.

static constexpr const char *ContractSettingsFile = "/config/settings.json";
static constexpr const char *ContractRouterFile   = "/config/router.json";
static constexpr const char *ContractPortalFile   = "/config/portal.json";
static constexpr const char *ContractPromosFile   = "/config/promos.json";
static constexpr const char *ContractVouchersFile = "/config/vouchers.json";
static constexpr const char *ContractNetworkFile  = "/config/network.json";
static constexpr const char *ContractAuthFile     = "/config/auth.json";
static constexpr const char *ContractSystemFile   = "/config/system.json";
static constexpr const char *ContractSystemBuildInfo = "/config/build-info.json";
static constexpr const char *InstallationFile     = "/config/installation.json";
static constexpr const char *ProvisioningFile     = "/config/provisioning.json";
static constexpr const char *RouterConnectionFile = "/config/router-connection.json";
static constexpr const char *RouterProvisioningFile = "/config/router-provisioning.json";
static constexpr const char *RouterCacheFile        = "/config/router-cache.json";
static constexpr const char *ExistingNetworkScanFile = "/config/existing-network-scan.json";
static constexpr const char *SetupWizardFile          = "/config/setup-wizard.json";
static constexpr const char *NetworkAdoptionWorkflowFile =
    "/config/network-adoption-workflow.json";
static constexpr const char *AccessPointsFile = "/config/access-points.json";
static constexpr const char *ContentFilterFile = "/config/content-filter.json";
static constexpr const char *GamingPriorityFile = "/config/gaming-priority.json";

// ── CONTRACT: canonical asset paths (Phase 3 AssetManager) ─────────────────

static constexpr const char *ContractBannerCurrent     = "/assets/banner/current.webp";
static constexpr const char *ContractMusicCurrent      = "/assets/music/current.mp3";
static constexpr const char *ContractLogoCurrent       = "/assets/logo/current.webp";
static constexpr const char *ContractBackgroundCurrent = "/assets/background/current.webp";
static constexpr const char *ContractFirmwareBin       = "/firmware/update.bin";

// ── RUNTIME ACTIVE: JSON / data files (unchanged — do not migrate in Phase 2) ─

static constexpr const char *SettingsFile       = ContractSettingsFile;
static constexpr const char *PromosFile         = ContractPromosFile;
static constexpr const char *RouterFile         = ContractRouterFile;
static constexpr const char *WifiConfigFile     = "/config/wifi.json";  // legacy extra
static constexpr const char *PortalConfigFile   = ContractPortalFile;
static constexpr const char *VouchersFile       = "/vouchers/vouchers.json";  // legacy active
static constexpr const char *SalesFile          = "/sales/sales.json";
static constexpr const char *LogsFile           = "/logs/logs.json";
static constexpr const char *UsersFile          = "/sessions/users.json";
static constexpr const char *AdminSessionsFile  = "/sessions/admin.json";
static constexpr const char *PortalSessionsFile = "/sessions/portal_sessions.json";

// Legacy portal branding on SD (PortalConfigManager runtime paths)
static constexpr const char *LegacyPortalBanner = "/www/portal-banner.webp";
static constexpr const char *LegacyPortalMusic  = "/www/portal-bg-music.mp3";

// Backup temp files — RUNTIME ACTIVE (BackupManager uses LegacyBackup today)
static constexpr const char *LegacyBackupExportZip   = "/backup/renzfi-export.zip";
static constexpr const char *LegacyBackupExportJson  = "/backup/renzfi-export.json";
static constexpr const char *LegacyBackupRestoreTemp = "/backup/renzfi-restore.tmp";

// Aliases for BackupManager compatibility
static constexpr const char *BackupExportZip   = LegacyBackupExportZip;
static constexpr const char *BackupExportJson  = LegacyBackupExportJson;
static constexpr const char *BackupRestoreTemp = LegacyBackupRestoreTemp;

// ── SPIFFS: system storage (firmware-owned) ─────────────────────────────────

namespace Spiffs {

static constexpr const char *Portal   = "/portal";
static constexpr const char *Admin    = "/admin";
static constexpr const char *Assets   = "/assets";
static constexpr const char *Defaults = "/defaults";
static constexpr const char *Fallback = "/fallback";
static constexpr const char *Fb       = "/fb";

// Additional bundled resource prefixes (informational)
static constexpr const char *Icons = "/icons";
static constexpr const char *Fonts = "/fonts";
static constexpr const char *Css   = "/css";
static constexpr const char *Js    = "/js";

static constexpr const char *PortalDefaultBanner = "/portal/Default-Banner.png";
static constexpr const char *PortalDefaultMusic  = "/portal/bg_music.mp3";
static constexpr const char *PortalCustomBanner    = "/portal/custom/banner.webp";
static constexpr const char *PortalCustomMusic     = "/portal/custom/bg-music.mp3";

static constexpr const char *BuildInfo = "/build-info.json";

static constexpr const char *FbManifest       = "/fallback/.manifest.json";
static constexpr const char *FbSettings       = "/fallback/settings.json";
static constexpr const char *FbPromos         = "/fallback/promos.json";
static constexpr const char *FbRouter         = "/fallback/router.json";
static constexpr const char *FbVouchers       = "/fallback/vouchers.json";
static constexpr const char *FbPortalSessions = "/fb/ps.json";
static constexpr const char *FbSales          = "/fb/sales.json";
static constexpr const char *FbPortalConfig   = "/fb/pcfg.json";
static constexpr const char *FbInstallation       = "/fb/installation.json";
static constexpr const char *FbProvisioning       = "/fb/provisioning.json";
static constexpr const char *FbRouterConnection   = "/fb/router-connection.json";
static constexpr const char *FbRouterProvisioning = "/fb/router-provisioning.json";
static constexpr const char *FbRouterCache        = "/fb/router-cache.json";
static constexpr const char *FbExistingNetworkScan = "/fb/existing-network-scan.json";
static constexpr const char *FbSetupWizard        = "/fb/setup-wizard.json";
static constexpr const char *SalesHistorySpool    = "/fb/hs.ndjson";
static constexpr const char *SessionsHistorySpool = "/fb/he.ndjson";
static constexpr const char *VouchersHistorySpool = "/fb/hv.ndjson";
static constexpr const char *LogsHistorySpool     = "/fb/hl.ndjson";

}  // namespace Spiffs

// Transaction candidates are sidecars so application JSON schemas remain
// untouched. Only one staged and one last-known-good copy exist per file.
// Keep suffixes short: SPIFFS object names are limited to 31 usable chars.
static constexpr const char *TransactionStageSuffix = ".t";
static constexpr const char *TransactionBackupSuffix = ".b";

// ── Layout registry ───────────────────────────────────────────────────────────

size_t requiredSdDirectoryCount();
const char *requiredSdDirectory(size_t index);

size_t reservedConfigFileCount();
const char *reservedConfigFile(size_t index);
const char *reservedConfigDefaultJson(size_t index);

size_t spiffsSystemPrefixCount();
const char *spiffsSystemPrefix(size_t index);

// ── Path helpers ──────────────────────────────────────────────────────────────

bool isValidSdPath(const char *path);
bool isValidSpiffsPath(const char *path);
bool joinPath(const char *dir, const char *leaf, char *out, size_t outSize);
bool transactionPath(const char *path, const char *suffix, char *out,
                     size_t outSize);

// Generic asset join (filename validated)
bool sdBannerPath(const char *filename, char *out, size_t outSize);
bool sdMusicPath(const char *filename, char *out, size_t outSize);
bool sdLogoPath(const char *filename, char *out, size_t outSize);
bool sdBackgroundPath(const char *filename, char *out, size_t outSize);
bool sdAdsPath(const char *filename, char *out, size_t outSize);
bool sdVideosPath(const char *filename, char *out, size_t outSize);
bool sdIconsPath(const char *filename, char *out, size_t outSize);
bool sdFontsPath(const char *filename, char *out, size_t outSize);
bool sdDownloadsPath(const char *filename, char *out, size_t outSize);

// Canonical contract paths (fixed filenames — Phase 3 AssetManager)
bool contractBannerCurrentPath(char *out, size_t outSize);
bool contractMusicCurrentPath(char *out, size_t outSize);
bool contractLogoCurrentPath(char *out, size_t outSize);
bool contractBackgroundCurrentPath(char *out, size_t outSize);
bool contractFirmwareUpdatePath(char *out, size_t outSize);

}  // namespace StoragePaths
