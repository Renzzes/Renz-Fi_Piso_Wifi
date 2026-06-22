#include "PortalConfigManager.h"

#include <SD.h>
#include <SPIFFS.h>

#include <ESPAsyncWebServer.h>

#include "Config.h"

namespace {

constexpr const char *kDefaultPortalMeta =
    "{\"revision\":0,\"hasBanner\":false,\"hasMusic\":false}";

}  // namespace

void PortalConfigManager::begin(StorageManager *storage, Logger *logger,
                                EventBus *events) {
  _storage = storage;
  _logger  = logger;
  _events  = events;
  loadMeta();
}

bool PortalConfigManager::loadMeta() {
  _hasBanner = false;
  _hasMusic  = false;
  _revision  = 0;

  if (!_storage) return false;

  DynamicJsonDocument doc(512);
  if (_storage->readJson(RenzFiConfig::PORTAL_CONFIG_FILE, doc)) {
    _hasBanner = doc["hasBanner"] | false;
    _hasMusic  = doc["hasMusic"] | false;
    _revision  = doc["revision"] | 0;
  }

  // Reconcile with actual files (meta may be stale after manual edits).
  if (_hasBanner &&
      !assetExists(RenzFiConfig::PORTAL_BANNER_SD,
                   RenzFiConfig::PORTAL_BANNER_SPIFFS)) {
    _hasBanner = false;
  }
  if (_hasMusic &&
      !assetExists(RenzFiConfig::PORTAL_MUSIC_SD,
                   RenzFiConfig::PORTAL_MUSIC_SPIFFS)) {
    _hasMusic = false;
  }

  return true;
}

bool PortalConfigManager::saveMeta() {
  if (!_storage) return false;

  DynamicJsonDocument doc(512);
  doc["revision"]  = _revision;
  doc["hasBanner"] = _hasBanner;
  doc["hasMusic"]  = _hasMusic;
  if (_hasBanner) {
    doc["bannerPath"] = _storage->healthy()
                            ? RenzFiConfig::PORTAL_BANNER_SD
                            : RenzFiConfig::PORTAL_BANNER_SPIFFS;
  }
  if (_hasMusic) {
    doc["musicPath"] = _storage->healthy()
                           ? RenzFiConfig::PORTAL_MUSIC_SD
                           : RenzFiConfig::PORTAL_MUSIC_SPIFFS;
  }
  return _storage->writeJson(RenzFiConfig::PORTAL_CONFIG_FILE, doc);
}

bool PortalConfigManager::hasCustomBanner() const { return _hasBanner; }
bool PortalConfigManager::hasCustomMusic() const { return _hasMusic; }
uint32_t PortalConfigManager::revision() const { return _revision; }

bool PortalConfigManager::fillBrandingJson(JsonObject out,
                                           const String &baseUrl) const {
  out["revision"]        = _revision;
  out["hasCustomBanner"] = _hasBanner;
  out["hasCustomMusic"]  = _hasMusic;
  out["bannerPath"] =
      _hasBanner ? String(RenzFiConfig::PORTAL_BANNER_SD)
                 : String(RenzFiConfig::PORTAL_BANNER_DEFAULT);
  out["musicPath"] =
      _hasMusic ? String(RenzFiConfig::PORTAL_MUSIC_SD)
                : String(RenzFiConfig::PORTAL_MUSIC_DEFAULT);
  String v = String("?v=") + _revision;
  out["bannerUrl"] = baseUrl + "/api/portal/assets/banner" + v;
  out["musicUrl"]  = baseUrl + "/api/portal/assets/music" + v;
  return true;
}

bool PortalConfigManager::fillSettingsJson(JsonObject out,
                                           const String &baseUrl) const {
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

  if (_hasBanner) {
    out["banner_path"] = _storage && _storage->healthy()
                             ? RenzFiConfig::PORTAL_BANNER_SD
                             : RenzFiConfig::PORTAL_BANNER_SPIFFS;
  }
  if (_hasMusic) {
    out["music_path"] = _storage && _storage->healthy()
                            ? RenzFiConfig::PORTAL_MUSIC_SD
                            : RenzFiConfig::PORTAL_MUSIC_SPIFFS;
  }
  return true;
}

bool PortalConfigManager::assetExists(const char *sdPath,
                                      const char *spiffsPath) const {
  if (!_storage) return false;
  if (_storage->isSdMounted() && sdPath && SD.exists(sdPath)) return true;
  return spiffsPath && SPIFFS.exists(spiffsPath);
}

size_t PortalConfigManager::assetSizeBytes(const char *sdPath,
                                           const char *spiffsPath) const {
  if (_storage && _storage->isSdMounted() && sdPath && SD.exists(sdPath)) {
    File file = SD.open(sdPath, FILE_READ);
    if (file) {
      const size_t bytes = file.size();
      file.close();
      return bytes;
    }
  }
  if (spiffsPath && SPIFFS.exists(spiffsPath)) {
    File file = SPIFFS.open(spiffsPath, "r");
    if (file) {
      const size_t bytes = file.size();
      file.close();
      return bytes;
    }
  }
  return 0;
}

bool PortalConfigManager::verifyAssetOnDisk(const char *sdPath,
                                            const char *spiffsPath,
                                            size_t minBytes) const {
  const size_t bytes = assetSizeBytes(sdPath, spiffsPath);
  if (bytes < minBytes) {
    Serial.printf(
        "[portal] verify failed sd=%s spiffs=%s bytes=%u min=%u sdMounted=%s "
        "healthy=%s\n",
        sdPath ? sdPath : "(null)", spiffsPath ? spiffsPath : "(null)",
        (unsigned)bytes, (unsigned)minBytes,
        (_storage && _storage->isSdMounted()) ? "yes" : "no",
        (_storage && _storage->healthy()) ? "yes" : "no");
    return false;
  }
  return true;
}

void PortalConfigManager::logAssetPaths(const char *action) const {
  const size_t bannerBytes = assetSizeBytes(RenzFiConfig::PORTAL_BANNER_SD,
                                            RenzFiConfig::PORTAL_BANNER_SPIFFS);
  const size_t musicBytes = assetSizeBytes(RenzFiConfig::PORTAL_MUSIC_SD,
                                           RenzFiConfig::PORTAL_MUSIC_SPIFFS);
  Serial.printf(
      "[portal] %s banner sd=%s spiffs=%s bytes=%u hasBanner=%s | music "
      "sd=%s spiffs=%s bytes=%u hasMusic=%s revision=%u\n",
      action, RenzFiConfig::PORTAL_BANNER_SD,
      RenzFiConfig::PORTAL_BANNER_SPIFFS, (unsigned)bannerBytes,
      _hasBanner ? "yes" : "no", RenzFiConfig::PORTAL_MUSIC_SD,
      RenzFiConfig::PORTAL_MUSIC_SPIFFS, (unsigned)musicBytes,
      _hasMusic ? "yes" : "no", (unsigned)_revision);
}

bool PortalConfigManager::writeAsset(const char *sdPath, const char *spiffsPath,
                                     const uint8_t *data, size_t len) {
  if (!_storage || !data || len == 0) return false;

  if (_storage->healthy()) {
    if (!_storage->writeBinary(sdPath, data, len)) return false;
    if (spiffsPath && SPIFFS.exists(spiffsPath)) SPIFFS.remove(spiffsPath);
    return verifyAssetOnDisk(sdPath, spiffsPath, len);
  }

  if (!_storage->writeBinarySpiffs(spiffsPath, data, len)) return false;
  return verifyAssetOnDisk(sdPath, spiffsPath, len);
}

bool PortalConfigManager::removeAsset(const char *sdPath,
                                      const char *spiffsPath) {
  if (!_storage) return false;
  bool ok = true;
  if (_storage->isSdMounted() && sdPath && SD.exists(sdPath))
    ok = SD.remove(sdPath) && ok;
  if (spiffsPath && SPIFFS.exists(spiffsPath))
    ok = SPIFFS.remove(spiffsPath) && ok;
  return ok;
}

void PortalConfigManager::abortUpload() {
  if (_uploadOpen && _uploadFile) {
    _uploadFile.close();
    if (_uploadToSd && _uploadSdPath.length() > 0 &&
        _storage && _storage->isSdMounted()) {
      SD.remove(_uploadSdPath.c_str());
    } else if (_uploadSpiffsPath.length() > 0 &&
               SPIFFS.exists(_uploadSpiffsPath.c_str())) {
      SPIFFS.remove(_uploadSpiffsPath.c_str());
    }
  }
  _uploadFile = File();
  _uploadOpen = false;
  _uploadExpected = 0;
  _uploadReceived = 0;
  _uploadSdPath = "";
  _uploadSpiffsPath = "";
}

bool PortalConfigManager::beginAssetUpload(bool isBanner, size_t expectedTotal,
                                           const String &filename) {
  if (!_storage) return false;

  abortUpload();

  const char *sdPath =
      isBanner ? RenzFiConfig::PORTAL_BANNER_SD : RenzFiConfig::PORTAL_MUSIC_SD;
  const char *spiffsPath =
      isBanner ? RenzFiConfig::PORTAL_BANNER_SPIFFS
               : RenzFiConfig::PORTAL_MUSIC_SPIFFS;
  const size_t maxBytes =
      isBanner ? (200U * 1024U) : RenzFiConfig::PORTAL_MUSIC_MAX_BYTES;

  if (expectedTotal == 0 || expectedTotal > maxBytes) {
    Serial.printf(
        "[portal-upload] reject kind=%s filename=%s expected=%u max=%u\n",
        isBanner ? "banner" : "music", filename.c_str(),
        (unsigned)expectedTotal, (unsigned)maxBytes);
    return false;
  }

  removeAsset(sdPath, spiffsPath);

  _uploadIsBanner = isBanner;
  _uploadExpected = expectedTotal;
  _uploadReceived = 0;
  _uploadSdPath = sdPath;
  _uploadSpiffsPath = spiffsPath;

  if (_storage->healthy()) {
    String path = String(sdPath);
    const int slash = path.lastIndexOf('/');
    if (slash > 0) {
      const String dir = path.substring(0, slash);
      if (!SD.exists(dir.c_str()) && !SD.mkdir(dir.c_str())) {
        Serial.printf("[portal-upload] mkdir failed dir=%s\n", dir.c_str());
        abortUpload();
        return false;
      }
    }
    _uploadFile = SD.open(sdPath, FILE_WRITE);
    _uploadToSd = true;
  } else {
    _uploadFile = SPIFFS.open(spiffsPath, "w");
    _uploadToSd = false;
  }

  if (!_uploadFile) {
    Serial.printf(
        "[portal-upload] open failed kind=%s target=%s sd=%s\n",
        isBanner ? "banner" : "music",
        _uploadToSd ? sdPath : spiffsPath, _uploadToSd ? "yes" : "no");
    abortUpload();
    return false;
  }

  _uploadOpen = true;
  Serial.printf(
      "[portal-upload] begin kind=%s filename=%s expected=%u target=%s sd=%s\n",
      isBanner ? "banner" : "music", filename.c_str(),
      (unsigned)expectedTotal, _uploadToSd ? sdPath : spiffsPath,
      _uploadToSd ? "yes" : "no");
  return true;
}

bool PortalConfigManager::beginBannerUpload(size_t expectedTotal,
                                            const String &filename) {
  return beginAssetUpload(true, expectedTotal, filename);
}

bool PortalConfigManager::beginMusicUpload(size_t expectedTotal,
                                           const String &filename) {
  return beginAssetUpload(false, expectedTotal, filename);
}

bool PortalConfigManager::appendUploadChunk(const uint8_t *data, size_t len,
                                            size_t index, bool final) {
  if (!_uploadOpen || !_uploadFile) {
    Serial.printf(
        "[portal-upload] append skipped index=%u len=%u final=%s (no open file)\n",
        (unsigned)index, (unsigned)len, final ? "yes" : "no");
    return false;
  }

  if (len > 0) {
    const size_t written = _uploadFile.write(data, len);
    if (written != len) {
      Serial.printf(
          "[portal-upload] write failed index=%u len=%u wrote=%u final=%s\n",
          (unsigned)index, (unsigned)len, (unsigned)written,
          final ? "yes" : "no");
      abortUpload();
      return false;
    }
    _uploadReceived += written;
  }

  Serial.printf("[portal-upload] chunk index=%u len=%u total=%u/%u final=%s\n",
                (unsigned)index, (unsigned)len, (unsigned)_uploadReceived,
                (unsigned)_uploadExpected, final ? "yes" : "no");

  if (final) {
    _uploadFile.close();
    _uploadOpen = false;
    Serial.printf("[portal-upload] file closed received=%u expected=%u\n",
                  (unsigned)_uploadReceived, (unsigned)_uploadExpected);
  }
  return true;
}

bool PortalConfigManager::finishAssetUpload(bool isBanner) {
  if (_uploadOpen) {
    _uploadFile.close();
    _uploadOpen = false;
    Serial.println("[portal-upload] finish closed open upload file");
  }

  if (_uploadReceived == 0) {
    Serial.println("[portal-upload] finish rejected: zero bytes received");
    abortUpload();
    return false;
  }

  const char *sdPath =
      isBanner ? RenzFiConfig::PORTAL_BANNER_SD : RenzFiConfig::PORTAL_MUSIC_SD;
  const char *spiffsPath =
      isBanner ? RenzFiConfig::PORTAL_BANNER_SPIFFS
               : RenzFiConfig::PORTAL_MUSIC_SPIFFS;

  if (!verifyAssetOnDisk(sdPath, spiffsPath, _uploadReceived)) {
    abortUpload();
    return false;
  }

  if (isBanner) {
    _hasBanner = true;
  } else {
    _hasMusic = true;
  }
  _revision = millis();
  saveMeta();
  logAction(String(isBanner ? "Custom portal banner uploaded ("
                            : "Custom portal music uploaded (") +
            String(_uploadReceived) + " bytes, streamed)");
  logAssetPaths(isBanner ? "banner-upload-complete" : "music-upload-complete");
  notifyChanged();

  _uploadExpected = 0;
  _uploadReceived = 0;
  _uploadSdPath = "";
  _uploadSpiffsPath = "";
  return true;
}

bool PortalConfigManager::finishBannerUpload() {
  return finishAssetUpload(true);
}

bool PortalConfigManager::finishMusicUpload() {
  return finishAssetUpload(false);
}

bool PortalConfigManager::uploadBanner(const uint8_t *data, size_t len) {
  Serial.printf("[portal] uploadBanner enter bytes=%u\n", (unsigned)len);
  if (!data || len == 0) return false;

  removeAsset(RenzFiConfig::PORTAL_BANNER_SD,
              RenzFiConfig::PORTAL_BANNER_SPIFFS);
  if (!writeAsset(RenzFiConfig::PORTAL_BANNER_SD,
                  RenzFiConfig::PORTAL_BANNER_SPIFFS, data, len)) {
    Serial.println("[portal] uploadBanner exit write failed");
    return false;
  }

  _hasBanner = true;
  _revision  = millis();
  saveMeta();
  logAction("Custom portal banner uploaded (" + String(len) + " bytes)");
  logAssetPaths("banner-upload-complete");
  notifyChanged();
  Serial.println("[portal] uploadBanner exit ok");
  return true;
}

bool PortalConfigManager::uploadMusic(const uint8_t *data, size_t len) {
  Serial.printf("[portal] uploadMusic enter bytes=%u\n", (unsigned)len);
  if (!data || len == 0 || len > RenzFiConfig::PORTAL_MUSIC_MAX_BYTES)
    return false;

  removeAsset(RenzFiConfig::PORTAL_MUSIC_SD, RenzFiConfig::PORTAL_MUSIC_SPIFFS);
  if (!writeAsset(RenzFiConfig::PORTAL_MUSIC_SD,
                  RenzFiConfig::PORTAL_MUSIC_SPIFFS, data, len)) {
    Serial.println("[portal] uploadMusic exit write failed");
    return false;
  }

  _hasMusic = true;
  _revision = millis();
  saveMeta();
  logAction("Custom portal music uploaded (" + String(len) + " bytes)");
  logAssetPaths("music-upload-complete");
  notifyChanged();
  Serial.println("[portal] uploadMusic exit ok");
  return true;
}

bool PortalConfigManager::deleteBanner() {
  removeAsset(RenzFiConfig::PORTAL_BANNER_SD,
              RenzFiConfig::PORTAL_BANNER_SPIFFS);
  _hasBanner = false;
  _revision  = millis();
  saveMeta();
  logAction("Custom portal banner removed");
  notifyChanged();
  return true;
}

bool PortalConfigManager::deleteMusic() {
  removeAsset(RenzFiConfig::PORTAL_MUSIC_SD, RenzFiConfig::PORTAL_MUSIC_SPIFFS);
  _hasMusic = false;
  _revision = millis();
  saveMeta();
  logAction("Custom portal music removed");
  notifyChanged();
  return true;
}

bool PortalConfigManager::serveBanner(AsyncWebServerRequest *req) const {
  if (!req || !_storage) return false;

  if (_hasBanner) {
    if (_storage->isSdMounted() &&
        SD.exists(RenzFiConfig::PORTAL_BANNER_SD)) {
      req->send(SD, RenzFiConfig::PORTAL_BANNER_SD, "image/webp");
      return true;
    }
    if (SPIFFS.exists(RenzFiConfig::PORTAL_BANNER_SPIFFS)) {
      req->send(SPIFFS, RenzFiConfig::PORTAL_BANNER_SPIFFS, "image/webp");
      return true;
    }
    Serial.printf(
        "[portal] serveBanner missing file hasBanner=yes sd=%s spiffs=%s\n",
        RenzFiConfig::PORTAL_BANNER_SD, RenzFiConfig::PORTAL_BANNER_SPIFFS);
  }

  if (SPIFFS.exists(RenzFiConfig::PORTAL_BANNER_DEFAULT)) {
    req->send(SPIFFS, RenzFiConfig::PORTAL_BANNER_DEFAULT, "image/png");
    return true;
  }

  return false;
}

bool PortalConfigManager::serveMusic(AsyncWebServerRequest *req) const {
  if (!req || !_storage) return false;

  if (_hasMusic) {
    if (_storage->isSdMounted() &&
        SD.exists(RenzFiConfig::PORTAL_MUSIC_SD)) {
      req->send(SD, RenzFiConfig::PORTAL_MUSIC_SD, "audio/mpeg");
      return true;
    }
    if (SPIFFS.exists(RenzFiConfig::PORTAL_MUSIC_SPIFFS)) {
      req->send(SPIFFS, RenzFiConfig::PORTAL_MUSIC_SPIFFS, "audio/mpeg");
      return true;
    }
    Serial.printf(
        "[portal] serveMusic missing file hasMusic=yes sd=%s spiffs=%s\n",
        RenzFiConfig::PORTAL_MUSIC_SD, RenzFiConfig::PORTAL_MUSIC_SPIFFS);
  }

  if (SPIFFS.exists(RenzFiConfig::PORTAL_MUSIC_DEFAULT)) {
    req->send(SPIFFS, RenzFiConfig::PORTAL_MUSIC_DEFAULT, "audio/mpeg");
    return true;
  }

  return false;
}

void PortalConfigManager::notifyChanged() {
  if (_events) _events->emit("portal.changed", "{\"revision\":" + String(_revision) + "}");
}

void PortalConfigManager::logAction(const String &msg) {
  if (_logger) _logger->info("portal", msg);
}
