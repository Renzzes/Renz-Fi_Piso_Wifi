#pragma once

#include <Arduino.h>
#include <SD.h>
#include <vector>

#include "AssetResolver.h"
#include "AssetTypes.h"
#include "EventBus.h"
#include "Logger.h"
#include "PortalConfigSchema.h"
#include "StorageManager.h"

// Phase 3A engine — media files + sectioned portal.json metadata.
// PortalConfigManager = configuration only (Phase 3B). See PORTAL_CONFIG_ARCHITECTURE.md.
class AssetManager {
 public:
  void begin(StorageManager *storage, Logger *logger, EventBus *events);

  bool ready() const;
  uint32_t portalRevision() const;

  // ── Save (buffer) ─────────────────────────────────────────────────────────
  AssetOperationResult saveAsset(AssetType type, const uint8_t *data, size_t len,
                                 const String &uploadFilename,
                                 uint8_t slot = 0);

  // ── Save (stream — staged on SD using caller-provided bounded chunks) ─────
  AssetOperationResult beginSaveAsset(AssetType type, size_t expectedTotal,
                                      const String &uploadFilename,
                                      uint8_t slot = 0);
  AssetOperationResult appendSaveChunk(const uint8_t *data, size_t len,
                                      size_t index, bool finalChunk);
  AssetOperationResult finishSaveAsset();
  void abortSaveAsset();

  // ── Delete / query ────────────────────────────────────────────────────────
  AssetOperationResult deleteAsset(AssetType type, uint8_t slot = 0);
  AssetOperationResult getAssetInfo(AssetType type, AssetInfo &out,
                                    uint8_t slot = 0) const;
  AssetOperationResult listAssets(std::vector<AssetInfo> &out) const;
  bool assetExists(AssetType type, uint8_t slot = 0) const;

  // ── Validation (no write) ─────────────────────────────────────────────────
  AssetOperationResult validateAsset(AssetType type, const uint8_t *data,
                                     size_t len, const String &uploadFilename,
                                     uint8_t slot = 0) const;

  // ── Metadata ──────────────────────────────────────────────────────────────
  AssetOperationResult loadMetadata();
  AssetOperationResult saveMetadata();
  uint32_t refreshPortalRevision();

  // ── Integrity ─────────────────────────────────────────────────────────────
  AssetOperationResult verifyIntegrity(AssetType type, uint8_t slot = 0);
  static String calculateChecksum(const uint8_t *data, size_t len);

  // ── Serve-time resolution (AssetResolver — single fallback chain) ─────────
  ResolvedAsset resolveBanner() const;
  ResolvedAsset resolveMusic() const;
  ResolvedAsset resolveLogo() const;
  ResolvedAsset resolveBackground() const;
  ResolvedAsset resolveAd(uint8_t slot) const;
  ResolvedAsset resolveVideo(uint8_t slot) const;
  ResolvedAsset resolve(AssetType type, uint8_t slot = 0) const;

 private:
  AssetResolver _resolver;
  StorageManager *_storage = nullptr;
  Logger         *_logger  = nullptr;
  EventBus       *_events  = nullptr;

  uint32_t _revision = 0;
  bool     _ready    = false;

  struct CachedAsset {
    AssetInfo info;
    bool loaded = false;
  };
  CachedAsset _banner;
  CachedAsset _music;
  CachedAsset _logo;
  CachedAsset _background;
  CachedAsset _ads[5];
  CachedAsset _videos[5];

  struct UploadState {
    bool active = false;
    AssetType type = AssetType::Unknown;
    uint8_t slot = 0;
    String uploadFilename;
    String stagingPath;
    File stagingFile;
    uint8_t signature[16] = {0};
    size_t signatureBytes = 0;
    size_t expectedTotal = 0;
    size_t received = 0;
  };
  UploadState _upload;

  CachedAsset *cacheFor(AssetType type, uint8_t slot);
  const CachedAsset *cacheFor(AssetType type, uint8_t slot) const;
  const AssetInfo *cachedInfo(AssetType type, uint8_t slot) const;

  AssetOperationResult commitAsset(AssetType type, const uint8_t *data,
                                   size_t len, const String &uploadFilename,
                                   uint8_t slot);
  AssetOperationResult commitStagedAsset(AssetType type, uint8_t slot,
                                         const String &uploadFilename,
                                         const String &stagingPath, size_t len);
  String checksumFile(const char *path) const;

  bool resolveStorageTargets(AssetType type, uint8_t slot, String &sdPath,
                             String &spiffsPath, String &canonicalFilename,
                             AssetErrorCode &err) const;

  bool resolveMediaJsonLocation(AssetType type, uint8_t slot,
                                const char *&section, String &key) const;

  size_t maxBytesFor(AssetType type) const;
  bool isSupportedType(AssetType type) const;
  bool extensionAllowed(AssetType type, const String &filename) const;
  bool magicBytesMatch(AssetType type, const uint8_t *data, size_t len,
                       const String &filename) const;
  bool requiresTranscode(AssetType type, const String &filename) const;
  bool isRasterWebpTarget(AssetType type) const;

  AssetInfo buildAssetInfo(AssetType type, uint8_t slot,
                           const String &sdPath, const String &spiffsPath,
                           const String &canonicalFilename, size_t size,
                           AssetStorageLocation loc,
                           const String &checksum) const;

  void updateLegacyMirrors(JsonObject doc) const;
  void serializeAssetInfo(JsonObject obj, const AssetInfo &info) const;
  bool deserializeAssetInfo(JsonObjectConst obj, AssetInfo &info) const;
  void syncCacheFromDoc(JsonObjectConst doc);
  void writeAssetToPortalJson(JsonObject root, AssetType type, uint8_t slot,
                              const AssetInfo &info) const;
  void removeAssetFromPortalJson(JsonObject root, AssetType type,
                                 uint8_t slot) const;
  void writeMediaSections(JsonObject root);

  bool writeAssetBytes(const String &sdPath, const String &spiffsPath,
                       const uint8_t *data, size_t len,
                       AssetStorageLocation &outLoc, String &warning);
  bool removeAssetFiles(const String &sdPath, const String &spiffsPath);
  bool removeAllTiersForType(AssetType type, uint8_t slot);

  void notifyPortalChanged();
  void logAssetAction(const String &msg);
};
