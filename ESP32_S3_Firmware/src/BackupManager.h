#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "AuthManager.h"
#include "Config.h"
#include "Logger.h"
#include "PortalConfigManager.h"
#include "StorageManager.h"

class BackupManager {
 public:
  static constexpr int BACKUP_VERSION = 1;
  static constexpr const char *TEMP_ZIP_PATH = "/backup/renzfi-export.zip";
  static constexpr const char *TEMP_JSON_PATH = "/backup/renzfi-export.json";
  static constexpr const char *TEMP_RESTORE_PATH = "/backup/renzfi-restore.tmp";

  void begin(StorageManager *storage, Logger *logger, AuthManager *auth,
             PortalConfigManager *portalConfig);

  bool isSdAvailable() const;

  // Creates backup on SD; returns true when outPath is ready to stream.
  // Prefers ZIP; sets useZip=false when JSON fallback was written instead.
  bool createBackup(String &outPath, bool &useZip, String &error);

  bool restoreFromFile(const char *uploadPath, String &error);

  bool performFactoryReset(String &error);

  static bool mapArchivePathToSd(const char *archivePath, String &sdPath);

 private:
  StorageManager       *_storage       = nullptr;
  Logger               *_logger        = nullptr;
  AuthManager          *_auth          = nullptr;
  PortalConfigManager  *_portalConfig  = nullptr;

  bool ensureBackupDir();
  bool createZipBackup(String &error);
  bool createJsonBackup(String &error);
  bool restoreFromZip(const char *zipPath, String &error);
  bool restoreFromJsonFile(const char *jsonPath, String &error);
  bool validateManifest(JsonObjectConst manifest, String &error);
  bool writeArchiveEntryToSd(const char *archivePath, const uint8_t *data,
                               size_t len, String &error);
  bool wipeUserData(String &error);
};
