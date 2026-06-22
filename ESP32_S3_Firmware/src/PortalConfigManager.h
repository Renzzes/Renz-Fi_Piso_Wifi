#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <ESPAsyncWebServer.h>

#include "EventBus.h"
#include "Logger.h"
#include "StorageManager.h"

// Manages captive portal branding assets (banner + background music).
// SD card is preferred; SPIFFS is used when SD is unavailable.
class PortalConfigManager {
 public:
  void begin(StorageManager *storage, Logger *logger, EventBus *events);

  bool loadMeta();
  bool saveMeta();

  bool hasCustomBanner() const;
  bool hasCustomMusic() const;
  uint32_t revision() const;

  bool fillBrandingJson(JsonObject out, const String &baseUrl) const;
  bool fillSettingsJson(JsonObject out, const String &baseUrl) const;

  bool uploadBanner(const uint8_t *data, size_t len);
  bool uploadMusic(const uint8_t *data, size_t len);

  // Chunked upload (multipart or raw body); finish* closes the file and updates meta.
  bool beginBannerUpload(size_t expectedTotal, const String &filename);
  bool beginMusicUpload(size_t expectedTotal, const String &filename);
  bool appendUploadChunk(const uint8_t *data, size_t len, size_t index,
                         bool final);
  bool finishBannerUpload();
  bool finishMusicUpload();
  void abortUpload();

  bool deleteBanner();
  bool deleteMusic();

  bool serveBanner(AsyncWebServerRequest *req) const;
  bool serveMusic(AsyncWebServerRequest *req) const;

 private:
  StorageManager *_storage = nullptr;
  Logger         *_logger  = nullptr;
  EventBus       *_events  = nullptr;

  bool     _hasBanner = false;
  bool     _hasMusic  = false;
  uint32_t _revision  = 0;

  File     _uploadFile;
  bool     _uploadOpen = false;
  bool     _uploadIsBanner = false;
  bool     _uploadToSd = false;
  size_t   _uploadExpected = 0;
  size_t   _uploadReceived = 0;
  String   _uploadSdPath;
  String   _uploadSpiffsPath;

  bool writeAsset(const char *sdPath, const char *spiffsPath,
                  const uint8_t *data, size_t len);
  bool removeAsset(const char *sdPath, const char *spiffsPath);
  bool assetExists(const char *sdPath, const char *spiffsPath) const;
  bool verifyAssetOnDisk(const char *sdPath, const char *spiffsPath,
                         size_t minBytes) const;
  size_t assetSizeBytes(const char *sdPath, const char *spiffsPath) const;
  bool beginAssetUpload(bool isBanner, size_t expectedTotal,
                        const String &filename);
  bool finishAssetUpload(bool isBanner);
  void logAssetPaths(const char *action) const;
  void notifyChanged();
  void logAction(const String &msg);
};
