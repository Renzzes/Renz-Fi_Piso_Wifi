#include "AssetResolver.h"

#include <SD.h>
#include <SPIFFS.h>
#include <stdio.h>
#include <string.h>

#include "StorageManager.h"
#include "StoragePaths.h"

const char *assetResolveTierLabel(AssetResolveTier tier) {
  switch (tier) {
    case AssetResolveTier::ContractSd: return "contract_sd";
    case AssetResolveTier::LegacySd: return "legacy_sd";
    case AssetResolveTier::SpiffsCustom: return "spiffs_custom";
    case AssetResolveTier::Bundled: return "bundled";
    default: return "none";
  }
}

void AssetResolver::begin(StorageManager *storage) { _storage = storage; }

bool AssetResolver::sdPathExists(const char *path) const {
  if (!path || !path[0] || !_storage || !_storage->healthy()) return false;
  return _storage->exists(path) && _storage->fileSizeBytes(path) > 0;
}

bool AssetResolver::spiffsPathExists(const char *path) const {
  if (!path || !path[0] || !_storage || !_storage->isSpiffsMounted()) {
    return false;
  }
  if (!SPIFFS.exists(path)) return false;
  File file = SPIFFS.open(path, "r");
  if (!file) return false;
  const size_t bytes = file.size();
  file.close();
  return bytes > 0;
}

ResolvedAsset AssetResolver::makeResult(AssetType type, uint8_t slot,
                                        AssetResolveTier tier, const char *path,
                                        const char *mimeType,
                                        AssetStorageLocation loc,
                                        bool fromMetadata) const {
  ResolvedAsset r;
  r.found = true;
  r.type = type;
  r.slot = slot;
  r.tier = tier;
  r.path = path;
  r.mimeType = mimeType;
  r.storageLocation = loc;
  r.fromMetadata = fromMetadata;
  return r;
}

bool AssetResolver::resolveContractSdPath(AssetType type, uint8_t slot,
                                          String &path) const {
  char buf[128];
  switch (type) {
    case AssetType::Banner:
      return StoragePaths::contractBannerCurrentPath(buf, sizeof(buf)) &&
             (path = buf, true);
    case AssetType::Music:
      return StoragePaths::contractMusicCurrentPath(buf, sizeof(buf)) &&
             (path = buf, true);
    case AssetType::Logo:
      return StoragePaths::contractLogoCurrentPath(buf, sizeof(buf)) &&
             (path = buf, true);
    case AssetType::Background:
      return StoragePaths::contractBackgroundCurrentPath(buf, sizeof(buf)) &&
             (path = buf, true);
    case AssetType::Ad: {
      if (slot < 1 || slot > 5) return false;
      char leaf[16];
      snprintf(leaf, sizeof(leaf), "ad%u.webp", (unsigned)slot);
      return StoragePaths::sdAdsPath(leaf, buf, sizeof(buf)) && (path = buf, true);
    }
    case AssetType::Video: {
      if (slot < 1 || slot > 5) return false;
      char leaf[16];
      snprintf(leaf, sizeof(leaf), "ad%u.mp4", (unsigned)slot);
      return StoragePaths::sdVideosPath(leaf, buf, sizeof(buf)) &&
             (path = buf, true);
    }
    default:
      return false;
  }
}

bool AssetResolver::resolveLegacySdPath(AssetType type, String &path) const {
  switch (type) {
    case AssetType::Banner:
      path = StoragePaths::LegacyPortalBanner;
      return true;
    case AssetType::Music:
      path = StoragePaths::LegacyPortalMusic;
      return true;
    default:
      return false;
  }
}

bool AssetResolver::resolveSpiffsCustomPath(AssetType type, String &path) const {
  switch (type) {
    case AssetType::Banner:
      path = StoragePaths::Spiffs::PortalCustomBanner;
      return true;
    case AssetType::Music:
      path = StoragePaths::Spiffs::PortalCustomMusic;
      return true;
    default:
      return false;
  }
}

bool AssetResolver::resolveBundledPath(AssetType type, String &path,
                                       const char *&mimeType) const {
  switch (type) {
    case AssetType::Banner:
      path = StoragePaths::Spiffs::PortalDefaultBanner;
      mimeType = "image/png";
      return true;
    case AssetType::Music:
      path = StoragePaths::Spiffs::PortalDefaultMusic;
      mimeType = "audio/mpeg";
      return true;
    default:
      return false;
  }
}

namespace {

AssetResolveTier tierForMetadataPath(const String &path,
                                     AssetStorageLocation loc) {
  if (path.startsWith("/assets/")) return AssetResolveTier::ContractSd;
  if (path.startsWith("/www/")) return AssetResolveTier::LegacySd;
  if (path.startsWith("/portal/custom/")) return AssetResolveTier::SpiffsCustom;
  if (loc == AssetStorageLocation::Spiffs) return AssetResolveTier::SpiffsCustom;
  if (loc == AssetStorageLocation::Bundled) return AssetResolveTier::Bundled;
  return AssetResolveTier::ContractSd;
}

}  // namespace

ResolvedAsset AssetResolver::resolve(AssetType type, uint8_t slot,
                                     const AssetInfo *metadata) const {
  if (!_storage) return ResolvedAsset::notFound(type, slot);

  auto tryPath = [&](AssetResolveTier tier, const String &path,
                     const char *mimeType, AssetStorageLocation loc,
                     bool fromMeta) -> ResolvedAsset {
    if (loc == AssetStorageLocation::Sd && sdPathExists(path.c_str())) {
      return makeResult(type, slot, tier, path.c_str(), mimeType, loc, fromMeta);
    }
    if (loc == AssetStorageLocation::Spiffs && spiffsPathExists(path.c_str())) {
      return makeResult(type, slot, tier, path.c_str(), mimeType, loc, fromMeta);
    }
    if (loc == AssetStorageLocation::Bundled &&
        spiffsPathExists(path.c_str())) {
      return makeResult(type, slot, tier, path.c_str(), mimeType, loc, fromMeta);
    }
    return ResolvedAsset::notFound(type, slot);
  };

  if (metadata && metadata->present() && metadata->path.length() > 0) {
    const char *mime = metadata->mimeType.length() > 0
                           ? metadata->mimeType.c_str()
                           : assetCanonicalMimeType(type);
    ResolvedAsset fromMeta =
        tryPath(AssetResolveTier::ContractSd, metadata->path, mime,
                metadata->storageLocation, true);
    if (fromMeta.found) {
      fromMeta.tier = tierForMetadataPath(metadata->path, metadata->storageLocation);
      return fromMeta;
    }
  }

  String path;
  const char *mime = assetCanonicalMimeType(type);

  if (resolveContractSdPath(type, slot, path)) {
    ResolvedAsset r =
        tryPath(AssetResolveTier::ContractSd, path, mime,
                AssetStorageLocation::Sd, false);
    if (r.found) return r;
  }

  if (resolveLegacySdPath(type, path)) {
    ResolvedAsset r = tryPath(AssetResolveTier::LegacySd, path, mime,
                              AssetStorageLocation::Sd, false);
    if (r.found) return r;
  }

  if (resolveSpiffsCustomPath(type, path)) {
    ResolvedAsset r =
        tryPath(AssetResolveTier::SpiffsCustom, path, mime,
                AssetStorageLocation::Spiffs, false);
    if (r.found) return r;
  }

  const char *bundledMime = mime;
  if (resolveBundledPath(type, path, bundledMime)) {
    ResolvedAsset r =
        tryPath(AssetResolveTier::Bundled, path, bundledMime,
                AssetStorageLocation::Bundled, false);
    if (r.found) return r;
  }

  return ResolvedAsset::notFound(type, slot);
}
