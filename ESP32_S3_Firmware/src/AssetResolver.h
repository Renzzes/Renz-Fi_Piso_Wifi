#pragma once

#include <Arduino.h>

#include "AssetTypes.h"

class StorageManager;

// ─────────────────────────────────────────────────────────────────────────────
//  AssetResolver — internal serve-time fallback chain (owned by AssetManager)
//
//  Single implementation of: contract SD → legacy SD → SPIFFS custom → bundled
//  Portal, ApiServer, and Admin must call AssetManager::resolve*(), never
//  duplicate fallback logic or hardcode StoragePaths in PortalConfigManager.
//
//  See docs/PORTAL_CONFIG_ARCHITECTURE.md
// ─────────────────────────────────────────────────────────────────────────────

enum class AssetResolveTier : uint8_t {
  None,
  ContractSd,
  LegacySd,
  SpiffsCustom,
  Bundled,
};

const char *assetResolveTierLabel(AssetResolveTier tier);

struct ResolvedAsset {
  bool found = false;
  AssetType type = AssetType::Unknown;
  AssetResolveTier tier = AssetResolveTier::None;
  String path;
  String mimeType;
  AssetStorageLocation storageLocation = AssetStorageLocation::None;
  uint8_t slot = 0;
  bool fromMetadata = false;

  static ResolvedAsset notFound(AssetType type, uint8_t slot = 0) {
    ResolvedAsset r;
    r.type = type;
    r.slot = slot;
    return r;
  }
};

class AssetResolver {
 public:
  void begin(StorageManager *storage);

  ResolvedAsset resolve(AssetType type, uint8_t slot,
                        const AssetInfo *metadata) const;

  ResolvedAsset resolveBanner(const AssetInfo *metadata) const {
    return resolve(AssetType::Banner, 0, metadata);
  }
  ResolvedAsset resolveMusic(const AssetInfo *metadata) const {
    return resolve(AssetType::Music, 0, metadata);
  }
  ResolvedAsset resolveLogo(const AssetInfo *metadata) const {
    return resolve(AssetType::Logo, 0, metadata);
  }
  ResolvedAsset resolveBackground(const AssetInfo *metadata) const {
    return resolve(AssetType::Background, 0, metadata);
  }
  ResolvedAsset resolveAd(uint8_t slot, const AssetInfo *metadata) const {
    return resolve(AssetType::Ad, slot, metadata);
  }
  ResolvedAsset resolveVideo(uint8_t slot, const AssetInfo *metadata) const {
    return resolve(AssetType::Video, slot, metadata);
  }

 private:
  StorageManager *_storage = nullptr;

  bool sdPathExists(const char *path) const;
  bool spiffsPathExists(const char *path) const;

  ResolvedAsset makeResult(AssetType type, uint8_t slot, AssetResolveTier tier,
                           const char *path, const char *mimeType,
                           AssetStorageLocation loc, bool fromMetadata) const;

  bool resolveContractSdPath(AssetType type, uint8_t slot, String &path) const;
  bool resolveLegacySdPath(AssetType type, String &path) const;
  bool resolveSpiffsCustomPath(AssetType type, String &path) const;
  bool resolveBundledPath(AssetType type, String &path,
                          const char *&mimeType) const;
};
