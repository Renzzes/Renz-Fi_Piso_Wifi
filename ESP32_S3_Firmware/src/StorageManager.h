#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD.h>

class StorageManager {
 public:
  bool begin();
  bool healthy() const;
  bool usingFallback() const;
  String lastError() const;

  void pollStorageHealth();
  bool retrySd();

  bool isSdPollingDisabled() const;
  uint8_t sdRetryCount() const;

  bool ensureLayout();
  bool readJson(const char *path, JsonDocument &doc);
  bool writeJson(const char *path, JsonDocument &doc,
                 bool forcePortalWrite = false);
  bool appendJsonArrayItem(const char *path, JsonObject item, size_t capacity);
  bool clearJsonArray(const char *path);
  bool exists(const char *path) const;
  File openStatic(const String &path);
  String contentType(const String &path) const;
  uint64_t totalBytes() const;
  uint64_t usedBytes() const;
  size_t fileSizeBytes(const char *path) const;

  bool writeBinary(const char *sdPath, const uint8_t *data, size_t len);
  bool writeBinarySpiffs(const char *spiffsPath, const uint8_t *data, size_t len);
  bool removeBinary(const char *sdPath, const char *spiffsPath);

  uint64_t getSpiffsUsedBytes() const;
  uint64_t getSpiffsTotalBytes() const;
  bool isSdPresent() const;
  bool isSdMounted() const;
  uint64_t getSdUsedBytes() const;
  uint64_t getSdTotalBytes() const;
  uint64_t getSdFreeBytes() const;
  void fillSdStatus(JsonObject sd) const;

  // SD-card-only helpers (no SPIFFS fallback).
  bool readSdText(const char *path, String &out) const;
  bool writeSdText(const char *path, const String &content);
  bool deleteSdOnly(const char *path);
  void clearAllFallbackData();

  // Wipe appliance data files and re-seed SD layout defaults.
  bool factoryResetData();

 private:
  bool _healthy = false;
  bool _sdPresent = false;
  bool _sdMountFailed = false;
  bool _spiffsMounted = false;
  bool _usingFallback = false;
  bool _syncInProgress = false;
  bool _disableSdPolling = false;

  uint8_t _sdRetryCount = 0;
  uint32_t _lastHealthPollMs = 0;
  uint32_t _lastFbPortalWriteMs = 0;

  String _lastError;

  void setError(const String &message);
  bool mountSdCard(const char *context);
  bool attemptSdRecovery();
  void onSdRecoveryFailed();
  void onSdRecoverySucceeded();
  bool ensureDir(const char *path);
  bool ensureJsonFile(const char *path, const char *contents);

  bool mountSpiffs();
  bool isFallbackEligible(const char *path) const;
  String toFallbackPath(const char *sdPath) const;
  const char *toSdPath(const char *fbPath) const;
  size_t perFileLimit(const char *sdPath) const;

  bool readJsonFromSd(const char *path, JsonDocument &doc);
  bool writeJsonToSd(const char *path, JsonDocument &doc);
  bool writeJsonToSdSerialized(const char *path, const String &serialized);

  bool readJsonFromSpiffs(const char *sdPath, JsonDocument &doc);
  bool writeJsonToSpiffs(const char *sdPath, const String &serialized,
                         bool forcePortalWrite = false);
  bool seedFallbackDefaults(const char *sdPath, JsonDocument &doc) const;

  bool spiffsReadFile(const String &fbPath, JsonDocument &doc);
  bool spiffsWriteFile(const String &fbPath, const String &data);
  size_t spiffsFreeBytes() const;
  size_t spiffsFileSize(const String &fbPath) const;
  size_t fallbackTotalBytes() const;

  bool checkQuota(const char *sdPath, size_t newSize, size_t oldSize) const;
  bool isPortalWriteThrottled(const char *path, bool force) const;

  bool readManifest(JsonDocument &doc);
  bool writeManifest(const JsonDocument &doc);
  void addToManifest(const String &fbPath, size_t fileBytes);
  void removeFromManifest(const String &fbPath);
  bool syncFallbackToSd();
  bool verifySdMatches(const char *sdPath, const String &expected);
};
