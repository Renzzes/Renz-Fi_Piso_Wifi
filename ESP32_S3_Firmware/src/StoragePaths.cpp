#include "StoragePaths.h"

#include <string.h>

namespace StoragePaths {

namespace {

constexpr const char *kRequiredSdDirectories[] = {
    Config,
    Assets,
    AssetsBanner,
    AssetsMusic,
    AssetsLogo,
    AssetsBackground,
    AssetsAds,
    AssetsVideos,
    AssetsIcons,
    AssetsFonts,
    AssetsDownloads,
    Sales,
    Sessions,
    Logs,
    Backups,
    LegacyBackup,
    Firmware,
    Reports,
    Exports,
    Cache,
    Temp,
    History,
    HistorySales,
    HistorySessions,
    HistoryVouchers,
    HistoryLogs,
    LegacyVouchers,
    LegacyWww,
};

constexpr const char *kReservedConfigFiles[] = {
    ContractSettingsFile,
    ContractRouterFile,
    ContractPortalFile,
    ContractPromosFile,
    ContractVouchersFile,
    ContractNetworkFile,
    ContractAuthFile,
    ContractSystemFile,
};

constexpr const char *kReservedConfigDefaults[] = {
    nullptr,  // settings — seeded by StorageManager with full default
    nullptr,  // router
    "{\"revision\":0,\"hasBanner\":false,\"hasMusic\":false}",
    nullptr,  // promos — seeded by StorageManager
    "[]",
    "{}",
    "{}",
    "{}",
};

constexpr const char *kSpiffsSystemPrefixes[] = {
    Spiffs::Portal,
    Spiffs::Admin,
    Spiffs::Assets,
    Spiffs::Defaults,
    Spiffs::Fallback,
    Spiffs::Fb,
};

bool copySegment(const char *src, char *out, size_t outSize, size_t &written) {
  if (!src || !out || outSize == 0) return false;
  const size_t len = strlen(src);
  if (written + len + 1 > outSize) return false;
  memcpy(out + written, src, len);
  written += len;
  out[written] = '\0';
  return true;
}

bool joinAssetPath(const char *dir, const char *filename, char *out,
                   size_t outSize) {
  if (!filename || !out || outSize == 0) return false;
  if (!isValidSdPath(filename)) return false;

  size_t written = 0;
  out[0] = '\0';

  if (!copySegment(dir, out, outSize, written)) return false;

  if (filename[0] != '\0') {
    if (written + 1 >= outSize) return false;
    out[written++] = '/';
    out[written] = '\0';
    if (!copySegment(filename, out, outSize, written)) return false;
  }

  return isValidSdPath(out);
}

bool pathStartsWithDir(const char *path, const char *dir) {
  if (!path || !dir) return false;
  const size_t dirLen = strlen(dir);
  if (strncmp(path, dir, dirLen) != 0) return false;
  return path[dirLen] == '\0' || path[dirLen] == '/';
}

}  // namespace

const char *ownerLabel(StorageOwner owner) {
  switch (owner) {
    case StorageOwner::StorageManager: return "StorageManager";
    case StorageOwner::AssetManager: return "AssetManager";
    case StorageOwner::Logger: return "Logger";
    case StorageOwner::SessionManagers: return "SessionManager+PortalSessionManager";
    case StorageOwner::PortalSessionManager: return "PortalSessionManager";
    case StorageOwner::BackupManager: return "BackupManager";
    case StorageOwner::OtaManager: return "OtaManager";
    case StorageOwner::ReportManager: return "ReportManager";
    case StorageOwner::ExportManager: return "ExportManager";
    case StorageOwner::CacheManager: return "CacheManager";
    case StorageOwner::StorageManagerTemp: return "StorageManager(temp)";
    default: return "None";
  }
}

StorageOwner ownerForDirectory(const char *dirPath) {
  if (!dirPath) return StorageOwner::None;
  if (strcmp(dirPath, Config) == 0) return StorageOwner::StorageManager;
  if (strcmp(dirPath, Assets) == 0 ||
      pathStartsWithDir(dirPath, AssetsBanner) ||
      pathStartsWithDir(dirPath, AssetsMusic) ||
      pathStartsWithDir(dirPath, AssetsLogo) ||
      pathStartsWithDir(dirPath, AssetsBackground) ||
      pathStartsWithDir(dirPath, AssetsAds) ||
      pathStartsWithDir(dirPath, AssetsVideos) ||
      pathStartsWithDir(dirPath, AssetsIcons) ||
      pathStartsWithDir(dirPath, AssetsFonts) ||
      pathStartsWithDir(dirPath, AssetsDownloads)) {
    return StorageOwner::AssetManager;
  }
  if (strcmp(dirPath, Sales) == 0) return StorageOwner::SessionManagers;
  if (strcmp(dirPath, Sessions) == 0) return StorageOwner::PortalSessionManager;
  if (strcmp(dirPath, Logs) == 0) return StorageOwner::Logger;
  if (strcmp(dirPath, Backups) == 0 || strcmp(dirPath, LegacyBackup) == 0) {
    return StorageOwner::BackupManager;
  }
  if (strcmp(dirPath, Firmware) == 0) return StorageOwner::OtaManager;
  if (strcmp(dirPath, Reports) == 0) return StorageOwner::ReportManager;
  if (strcmp(dirPath, Exports) == 0) return StorageOwner::ExportManager;
  if (strcmp(dirPath, Cache) == 0) return StorageOwner::CacheManager;
  if (strcmp(dirPath, Temp) == 0) return StorageOwner::StorageManagerTemp;
  if (strcmp(dirPath, History) == 0 ||
      pathStartsWithDir(dirPath, HistorySales) ||
      pathStartsWithDir(dirPath, HistorySessions) ||
      pathStartsWithDir(dirPath, HistoryVouchers) ||
      pathStartsWithDir(dirPath, HistoryLogs)) {
    return StorageOwner::StorageManager;
  }
  if (strcmp(dirPath, LegacyVouchers) == 0 ||
      strcmp(dirPath, LegacyWww) == 0) {
    return StorageOwner::StorageManager;
  }
  return StorageOwner::None;
}

StorageOwner ownerForPath(const char *filePath) {
  if (!filePath) return StorageOwner::None;
  if (pathStartsWithDir(filePath, Config)) return StorageOwner::StorageManager;
  if (pathStartsWithDir(filePath, Assets)) return StorageOwner::AssetManager;
  if (pathStartsWithDir(filePath, Sales)) return StorageOwner::SessionManagers;
  if (pathStartsWithDir(filePath, Sessions)) return StorageOwner::PortalSessionManager;
  if (pathStartsWithDir(filePath, Logs)) return StorageOwner::Logger;
  if (pathStartsWithDir(filePath, Backups) ||
      pathStartsWithDir(filePath, LegacyBackup)) {
    return StorageOwner::BackupManager;
  }
  if (pathStartsWithDir(filePath, Firmware)) return StorageOwner::OtaManager;
  if (pathStartsWithDir(filePath, Reports)) return StorageOwner::ReportManager;
  if (pathStartsWithDir(filePath, Exports)) return StorageOwner::ExportManager;
  if (pathStartsWithDir(filePath, Cache)) return StorageOwner::CacheManager;
  if (pathStartsWithDir(filePath, Temp)) return StorageOwner::StorageManagerTemp;
  if (pathStartsWithDir(filePath, History)) return StorageOwner::StorageManager;
  if (pathStartsWithDir(filePath, LegacyVouchers) ||
      pathStartsWithDir(filePath, LegacyWww)) {
    return StorageOwner::StorageManager;
  }
  return StorageOwner::None;
}

size_t requiredSdDirectoryCount() {
  return sizeof(kRequiredSdDirectories) / sizeof(kRequiredSdDirectories[0]);
}

const char *requiredSdDirectory(size_t index) {
  if (index >= requiredSdDirectoryCount()) return nullptr;
  return kRequiredSdDirectories[index];
}

size_t reservedConfigFileCount() {
  return sizeof(kReservedConfigFiles) / sizeof(kReservedConfigFiles[0]);
}

const char *reservedConfigFile(size_t index) {
  if (index >= reservedConfigFileCount()) return nullptr;
  return kReservedConfigFiles[index];
}

const char *reservedConfigDefaultJson(size_t index) {
  if (index >= reservedConfigFileCount()) return nullptr;
  return kReservedConfigDefaults[index];
}

size_t spiffsSystemPrefixCount() {
  return sizeof(kSpiffsSystemPrefixes) / sizeof(kSpiffsSystemPrefixes[0]);
}

const char *spiffsSystemPrefix(size_t index) {
  if (index >= spiffsSystemPrefixCount()) return nullptr;
  return kSpiffsSystemPrefixes[index];
}

bool isValidSdPath(const char *path) {
  if (!path || path[0] != '/') return false;
  if (strstr(path, "..") != nullptr) return false;
  if (strlen(path) >= 128) return false;
  return true;
}

bool isValidSpiffsPath(const char *path) {
  return isValidSdPath(path);
}

bool joinPath(const char *dir, const char *leaf, char *out, size_t outSize) {
  if (!dir || !out || outSize == 0) return false;
  if (!isValidSdPath(dir)) return false;
  if (leaf == nullptr || leaf[0] == '\0') {
    if (strlen(dir) + 1 > outSize) return false;
    strncpy(out, dir, outSize - 1);
    out[outSize - 1] = '\0';
    return true;
  }
  return joinAssetPath(dir, leaf, out, outSize);
}

bool transactionPath(const char *path, const char *suffix, char *out,
                     size_t outSize) {
  if (!path || !suffix || !out || outSize == 0 || !isValidSdPath(path)) {
    return false;
  }
  const size_t pathLen = strlen(path);
  const size_t suffixLen = strlen(suffix);
  if (pathLen + suffixLen + 1 > outSize) return false;
  memcpy(out, path, pathLen);
  memcpy(out + pathLen, suffix, suffixLen + 1);
  return isValidSdPath(out);
}

bool sdBannerPath(const char *filename, char *out, size_t outSize) {
  return joinAssetPath(AssetsBanner, filename, out, outSize);
}

bool sdMusicPath(const char *filename, char *out, size_t outSize) {
  return joinAssetPath(AssetsMusic, filename, out, outSize);
}

bool sdLogoPath(const char *filename, char *out, size_t outSize) {
  return joinAssetPath(AssetsLogo, filename, out, outSize);
}

bool sdBackgroundPath(const char *filename, char *out, size_t outSize) {
  return joinAssetPath(AssetsBackground, filename, out, outSize);
}

bool sdAdsPath(const char *filename, char *out, size_t outSize) {
  return joinAssetPath(AssetsAds, filename, out, outSize);
}

bool sdVideosPath(const char *filename, char *out, size_t outSize) {
  return joinAssetPath(AssetsVideos, filename, out, outSize);
}

bool sdIconsPath(const char *filename, char *out, size_t outSize) {
  return joinAssetPath(AssetsIcons, filename, out, outSize);
}

bool sdFontsPath(const char *filename, char *out, size_t outSize) {
  return joinAssetPath(AssetsFonts, filename, out, outSize);
}

bool sdDownloadsPath(const char *filename, char *out, size_t outSize) {
  return joinAssetPath(AssetsDownloads, filename, out, outSize);
}

bool contractBannerCurrentPath(char *out, size_t outSize) {
  return joinAssetPath(AssetsBanner, AssetNames::CurrentWebp, out, outSize);
}

bool contractMusicCurrentPath(char *out, size_t outSize) {
  return joinAssetPath(AssetsMusic, AssetNames::CurrentMp3, out, outSize);
}

bool contractLogoCurrentPath(char *out, size_t outSize) {
  return joinAssetPath(AssetsLogo, AssetNames::CurrentWebp, out, outSize);
}

bool contractBackgroundCurrentPath(char *out, size_t outSize) {
  return joinAssetPath(AssetsBackground, AssetNames::CurrentWebp, out, outSize);
}

bool contractFirmwareUpdatePath(char *out, size_t outSize) {
  return joinAssetPath(Firmware, AssetNames::FirmwareBin, out, outSize);
}

}  // namespace StoragePaths
