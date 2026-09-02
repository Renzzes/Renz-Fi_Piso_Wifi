#include "PortalConfigManager.h"

#include "AssetManager.h"
#include "AssetResolver.h"
#include "Config.h"

void PortalConfigManager::begin(StorageManager *storage, Logger *logger,
                                EventBus *events, AssetManager *assets) {
  _storage = storage;
  _logger  = logger;
  _events  = events;
  _assets  = assets;
  loadMeta();
}

void PortalConfigManager::refreshMediaFlagsFromAssets() {
  _hasBanner = false;
  _hasMusic  = false;
  if (!_assets) return;

  const ResolvedAsset banner = _assets->resolveBanner();
  const ResolvedAsset music = _assets->resolveMusic();
  _hasBanner = banner.found && banner.tier != AssetResolveTier::Bundled;
  _hasMusic  = music.found && music.tier != AssetResolveTier::Bundled;
  _revision  = _assets->portalRevision();
}

bool PortalConfigManager::loadMeta() {
  _hasBanner = false;
  _hasMusic  = false;
  _revision  = 0;

  if (_assets) {
    _assets->loadMetadata();
    refreshMediaFlagsFromAssets();
    return true;
  }

  if (!_storage) return false;

  DynamicJsonDocument doc(512);
  if (_storage->readJson(RenzFiConfig::PORTAL_CONFIG_FILE, doc)) {
    _hasBanner = doc["hasBanner"] | false;
    _hasMusic  = doc["hasMusic"] | false;
    _revision  = doc["revision"] | 0;
  }
  return true;
}

bool PortalConfigManager::hasCustomBanner() const { return _hasBanner; }
bool PortalConfigManager::hasCustomMusic() const { return _hasMusic; }
uint32_t PortalConfigManager::revision() const { return _revision; }

bool PortalConfigManager::fillBrandingJson(JsonObject out,
                                           const String &baseUrl) const {
  const ResolvedAsset banner =
      _assets ? _assets->resolveBanner() : ResolvedAsset::notFound(AssetType::Banner);
  const ResolvedAsset music =
      _assets ? _assets->resolveMusic() : ResolvedAsset::notFound(AssetType::Music);

  out["revision"] = _revision;
  out["hasCustomBanner"] = _hasBanner;
  out["hasCustomMusic"] = _hasMusic;
  out["bannerPath"] = banner.found ? banner.path : "";
  out["musicPath"] = music.found ? music.path : "";

  const String v = String("?v=") + _revision;
  out["bannerUrl"] = baseUrl + "/api/portal/assets/banner" + v;
  out["musicUrl"]  = baseUrl + "/api/portal/assets/music" + v;
  if (banner.found) {
    out["bannerMime"] = banner.mimeType.length() > 0 ? banner.mimeType
                                                     : assetCanonicalMimeType(AssetType::Banner);
    const bool mimeVideo = banner.mimeType.startsWith("video/");
    const bool pathVideo = banner.path.endsWith(".mp4") || banner.path.endsWith(".MP4");
    out["bannerIsVideo"] = mimeVideo || pathVideo;
  } else {
    out["bannerMime"] = "";
    out["bannerIsVideo"] = false;
  }
  return true;
}

bool PortalConfigManager::fillSettingsJson(JsonObject out,
                                           const String &baseUrl) const {
  const ResolvedAsset banner =
      _assets ? _assets->resolveBanner() : ResolvedAsset::notFound(AssetType::Banner);
  const ResolvedAsset music =
      _assets ? _assets->resolveMusic() : ResolvedAsset::notFound(AssetType::Music);

  out["revision"] = _revision;
  out["has_banner"] = _hasBanner;
  out["has_music"] = _hasMusic;
  out["hasCustomBanner"] = _hasBanner;
  out["hasCustomMusic"] = _hasMusic;
  out["bannerConfigured"] = _hasBanner;
  out["musicConfigured"] = _hasMusic;

  const String v = String("?v=") + _revision;
  out["bannerUrl"] = baseUrl + "/api/portal/assets/banner" + v;
  out["musicUrl"] = baseUrl + "/api/portal/assets/music" + v;

  if (_hasBanner && banner.found) {
    out["bannerMime"] = banner.mimeType.length() > 0 ? banner.mimeType
                                                     : assetCanonicalMimeType(AssetType::Banner);
    const bool mimeVideo = banner.mimeType.startsWith("video/");
    const bool pathVideo = banner.path.endsWith(".mp4") || banner.path.endsWith(".MP4");
    out["bannerIsVideo"] = mimeVideo || pathVideo;
    out["banner_path"] = banner.path;
  } else {
    out["bannerMime"] = "";
    out["bannerIsVideo"] = false;
  }
  if (_hasMusic && music.found) out["music_path"] = music.path;
  return true;
}
