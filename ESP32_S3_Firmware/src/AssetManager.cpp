#include "AssetManager.h"

#include <MD5Builder.h>
#include <SPIFFS.h>
#include <SD.h>
#include <stdio.h>
#include <string.h>

#include "Config.h"
#include "StoragePaths.h"

namespace {

static constexpr size_t kMaxBannerBytes = RenzFiConfig::PORTAL_BANNER_MAX_BYTES;
static constexpr size_t kMaxLogoBytes = 100U * 1024U;
static constexpr size_t kMaxBackgroundBytes = 512U * 1024U;
static constexpr size_t kMaxAdBytes = 200U * 1024U;
static constexpr size_t kMaxVideoBytes = 5U * 1024U * 1024U;
static constexpr uint8_t kMaxAdSlots = 5;
static constexpr uint8_t kMaxVideoSlots = 5;
static constexpr size_t kAssetIoChunkBytes = 4096;

static constexpr const char *kMd5Prefix = "md5:";

bool endsWithIgnoreCase(const String &value, const char *suffix) {
  if (!suffix) return false;
  const size_t suffixLen = strlen(suffix);
  if (value.length() < suffixLen) return false;
  const String tail = value.substring(value.length() - suffixLen);
  return tail.equalsIgnoreCase(suffix);
}

bool isWebpMagic(const uint8_t *data, size_t len) {
  return len >= 12 && memcmp(data, "RIFF", 4) == 0 &&
         memcmp(data + 8, "WEBP", 4) == 0;
}

bool isPngMagic(const uint8_t *data, size_t len) {
  static const uint8_t kSig[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  return len >= sizeof(kSig) && memcmp(data, kSig, sizeof(kSig)) == 0;
}

bool isJpegMagic(const uint8_t *data, size_t len) {
  return len >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
}

bool isMp3Magic(const uint8_t *data, size_t len) {
  if (len < 3) return false;
  if (memcmp(data, "ID3", 3) == 0) return true;
  return data[0] == 0xFF && (data[1] & 0xE0) == 0xE0;
}

bool isMp4Magic(const uint8_t *data, size_t len) {
  return len >= 12 && memcmp(data + 4, "ftyp", 4) == 0;
}

uint32_t epochSecondsNow() { return millis() / 1000U; }

}  // namespace

void AssetManager::begin(StorageManager *storage, Logger *logger,
                         EventBus *events) {
  _storage = storage;
  _logger  = logger;
  _events  = events;
  _ready   = false;
  _revision = 0;
  abortSaveAsset();

  if (!_storage) return;

  _resolver.begin(_storage);
  const AssetOperationResult loaded = loadMetadata();
  _ready = loaded.success || _storage->exists(RenzFiConfig::PORTAL_CONFIG_FILE);
  Serial.printf("[assets] AssetManager begin ready=%s revision=%u\n",
                _ready ? "yes" : "no", (unsigned)_revision);
}

bool AssetManager::ready() const { return _ready && _storage != nullptr; }

uint32_t AssetManager::portalRevision() const { return _revision; }

AssetManager::CachedAsset *AssetManager::cacheFor(AssetType type, uint8_t slot) {
  switch (type) {
    case AssetType::Banner: return &_banner;
    case AssetType::Music: return &_music;
    case AssetType::Logo: return &_logo;
    case AssetType::Background: return &_background;
    case AssetType::Ad:
      if (slot < 1 || slot > kMaxAdSlots) return nullptr;
      return &_ads[slot - 1];
    case AssetType::Video:
      if (slot < 1 || slot > kMaxVideoSlots) return nullptr;
      return &_videos[slot - 1];
    default: return nullptr;
  }
}

const AssetManager::CachedAsset *AssetManager::cacheFor(AssetType type,
                                                        uint8_t slot) const {
  return const_cast<AssetManager *>(this)->cacheFor(type, slot);
}

bool AssetManager::isSupportedType(AssetType type) const {
  switch (type) {
    case AssetType::Banner:
    case AssetType::Music:
    case AssetType::Logo:
    case AssetType::Background:
    case AssetType::Ad:
    case AssetType::Video:
      return true;
    default:
      return false;
  }
}

size_t AssetManager::maxBytesFor(AssetType type) const {
  switch (type) {
    case AssetType::Banner: return kMaxBannerBytes;
    case AssetType::Logo: return kMaxLogoBytes;
    case AssetType::Background: return kMaxBackgroundBytes;
    case AssetType::Music: return RenzFiConfig::PORTAL_MUSIC_MAX_BYTES;
    case AssetType::Ad: return kMaxAdBytes;
    case AssetType::Video: return kMaxVideoBytes;
    default: return 0;
  }
}

bool AssetManager::isRasterWebpTarget(AssetType type) const {
  return type == AssetType::Banner || type == AssetType::Logo ||
         type == AssetType::Background || type == AssetType::Ad ||
         type == AssetType::Icon;
}

bool AssetManager::extensionAllowed(AssetType type,
                                    const String &filename) const {
  switch (type) {
    case AssetType::Banner:
    case AssetType::Logo:
    case AssetType::Background:
    case AssetType::Ad:
      return endsWithIgnoreCase(filename, ".webp") ||
             endsWithIgnoreCase(filename, ".png") ||
             endsWithIgnoreCase(filename, ".jpg") ||
             endsWithIgnoreCase(filename, ".jpeg") ||
             endsWithIgnoreCase(filename, ".mp4");
    case AssetType::Music:
      return endsWithIgnoreCase(filename, ".mp3");
    case AssetType::Video:
      return endsWithIgnoreCase(filename, ".mp4");
    default:
      return false;
  }
}

bool AssetManager::requiresTranscode(AssetType type,
                                     const String &filename) const {
  // Banner may be stored as PNG/JPEG (Phase 3C). Other raster types still
  // expect WebP without on-device transcode.
  if (type == AssetType::Banner) return false;
  if (!isRasterWebpTarget(type)) return false;
  return endsWithIgnoreCase(filename, ".png") ||
         endsWithIgnoreCase(filename, ".jpg") ||
         endsWithIgnoreCase(filename, ".jpeg");
}

bool AssetManager::magicBytesMatch(AssetType type, const uint8_t *data,
                                   size_t len, const String &filename) const {
  if (!data || len == 0) return false;

  switch (type) {
    case AssetType::Banner:
    case AssetType::Logo:
    case AssetType::Background:
    case AssetType::Ad:
      if (endsWithIgnoreCase(filename, ".webp")) return isWebpMagic(data, len);
      if (endsWithIgnoreCase(filename, ".png")) return isPngMagic(data, len);
      if (endsWithIgnoreCase(filename, ".jpg") ||
          endsWithIgnoreCase(filename, ".jpeg")) {
        return isJpegMagic(data, len);
      }
      if (endsWithIgnoreCase(filename, ".mp4")) return isMp4Magic(data, len);
      return isWebpMagic(data, len) || isPngMagic(data, len) ||
             isJpegMagic(data, len) || isMp4Magic(data, len);
    case AssetType::Music:
      return isMp3Magic(data, len);
    case AssetType::Video:
      return isMp4Magic(data, len);
    default:
      return false;
  }
}

bool AssetManager::resolveMediaJsonLocation(AssetType type, uint8_t slot,
                                            const char *&section,
                                            String &key) const {
  section = nullptr;
  switch (type) {
    case AssetType::Banner:
      if (slot != 0) return false;
      section = PortalConfigSchema::Branding;
      key = PortalConfigSchema::KeyBanner;
      return true;
    case AssetType::Logo:
      if (slot != 0) return false;
      section = PortalConfigSchema::Branding;
      key = PortalConfigSchema::KeyLogo;
      return true;
    case AssetType::Background:
      if (slot != 0) return false;
      section = PortalConfigSchema::Branding;
      key = PortalConfigSchema::KeyBackground;
      return true;
    case AssetType::Music:
      if (slot != 0) return false;
      section = PortalConfigSchema::Audio;
      key = PortalConfigSchema::KeyMusic;
      return true;
    case AssetType::Ad:
      if (slot < 1 || slot > kMaxAdSlots) return false;
      section = PortalConfigSchema::Ads;
      key = String("ad") + String(slot);
      return true;
    case AssetType::Video:
      if (slot < 1 || slot > kMaxVideoSlots) return false;
      section = PortalConfigSchema::Videos;
      key = String("video") + String(slot);
      return true;
    default:
      return false;
  }
}

bool AssetManager::resolveStorageTargets(AssetType type, uint8_t slot,
                                         String &sdPath, String &spiffsPath,
                                         String &canonicalFilename,
                                         AssetErrorCode &err) const {
  err = AssetErrorCode::None;
  char pathBuf[128];

  switch (type) {
    case AssetType::Banner:
      canonicalFilename = StoragePaths::AssetNames::CurrentWebp;
      sdPath = StoragePaths::ContractBannerCurrent;
      spiffsPath = StoragePaths::Spiffs::PortalCustomBanner;
      return true;
    case AssetType::Music:
      canonicalFilename = StoragePaths::AssetNames::CurrentMp3;
      sdPath = StoragePaths::ContractMusicCurrent;
      spiffsPath = StoragePaths::Spiffs::PortalCustomMusic;
      return true;
    case AssetType::Logo:
      canonicalFilename = StoragePaths::AssetNames::CurrentWebp;
      sdPath = StoragePaths::ContractLogoCurrent;
      spiffsPath = "";
      return true;
    case AssetType::Background:
      canonicalFilename = StoragePaths::AssetNames::CurrentWebp;
      sdPath = StoragePaths::ContractBackgroundCurrent;
      spiffsPath = "";
      return true;
    case AssetType::Ad: {
      if (slot < 1 || slot > kMaxAdSlots) {
        err = AssetErrorCode::SlotInvalid;
        return false;
      }
      char leaf[16];
      snprintf(leaf, sizeof(leaf), "ad%u.webp", (unsigned)slot);
      canonicalFilename = leaf;
      if (!StoragePaths::sdAdsPath(leaf, pathBuf, sizeof(pathBuf))) {
        err = AssetErrorCode::StorageError;
        return false;
      }
      sdPath = pathBuf;
      spiffsPath = "";
      return true;
    }
    case AssetType::Video: {
      if (slot < 1 || slot > kMaxVideoSlots) {
        err = AssetErrorCode::SlotInvalid;
        return false;
      }
      char leaf[16];
      snprintf(leaf, sizeof(leaf), "ad%u.mp4", (unsigned)slot);
      canonicalFilename = leaf;
      if (!StoragePaths::sdVideosPath(leaf, pathBuf, sizeof(pathBuf))) {
        err = AssetErrorCode::StorageError;
        return false;
      }
      sdPath = pathBuf;
      spiffsPath = "";
      return true;
    }
    default:
      err = AssetErrorCode::InvalidType;
      return false;
  }
}

AssetInfo AssetManager::buildAssetInfo(
    AssetType type, uint8_t slot, const String &sdPath,
    const String &spiffsPath, const String &canonicalFilename, size_t size,
    AssetStorageLocation loc, const String &checksum) const {
  AssetInfo info;
  info.type = type;
  info.filename = canonicalFilename;
  info.mimeType = assetCanonicalMimeType(type);
  info.size = size;
  info.lastModified = epochSecondsNow();
  info.checksum = checksum;
  info.storageLocation = loc;
  info.slot = slot;
  if (loc == AssetStorageLocation::Sd) {
    info.path = sdPath;
  } else if (loc == AssetStorageLocation::Spiffs) {
    info.path = spiffsPath;
  } else {
    info.path = sdPath.length() > 0 ? sdPath : spiffsPath;
  }
  return info;
}

String AssetManager::calculateChecksum(const uint8_t *data, size_t len) {
  if (!data || len == 0) return "";
  MD5Builder md5;
  md5.begin();
  md5.add(data, len);
  md5.calculate();
  return String(kMd5Prefix) + md5.toString();
}

String AssetManager::checksumFile(const char *path) const {
  if (!path || !SD.exists(path)) return "";
  File file = SD.open(path, FILE_READ);
  if (!file) return "";
  MD5Builder md5;
  md5.begin();
  uint8_t chunk[kAssetIoChunkBytes];
  while (file.available()) {
    const size_t got = file.read(chunk, sizeof(chunk));
    if (got == 0) {
      file.close();
      return "";
    }
    md5.add(chunk, got);
  }
  file.close();
  md5.calculate();
  return String(kMd5Prefix) + md5.toString();
}

AssetOperationResult AssetManager::validateAsset(
    AssetType type, const uint8_t *data, size_t len,
    const String &uploadFilename, uint8_t slot) const {
  if (!_storage) {
    return AssetOperationResult::fail(type, AssetErrorCode::NotReady,
                                      "Storage unavailable");
  }
  if (!isSupportedType(type)) {
    return AssetOperationResult::fail(type, AssetErrorCode::InvalidType,
                                      "Unsupported asset type");
  }
  if ((type == AssetType::Ad || type == AssetType::Video) &&
      (slot < 1 || (type == AssetType::Ad && slot > kMaxAdSlots) ||
       (type == AssetType::Video && slot > kMaxVideoSlots))) {
    return AssetOperationResult::fail(type, AssetErrorCode::SlotInvalid,
                                    "Invalid asset slot");
  }
  if (!data || len == 0) {
    return AssetOperationResult::fail(type, AssetErrorCode::InvalidUpload,
                                      "Empty upload");
  }
  const size_t maxBytes = maxBytesFor(type);
  if (len > maxBytes) {
    return AssetOperationResult::fail(type, AssetErrorCode::SizeExceeded,
                                      "Upload exceeds size limit");
  }
  if (uploadFilename.length() == 0) {
    return AssetOperationResult::fail(type, AssetErrorCode::InvalidUpload,
                                      "Missing upload filename");
  }
  if (!extensionAllowed(type, uploadFilename)) {
    return AssetOperationResult::fail(type, AssetErrorCode::InvalidType,
                                      "File extension not allowed");
  }
  if (requiresTranscode(type, uploadFilename)) {
    return AssetOperationResult::fail(
        type, AssetErrorCode::TranscodeFailed,
        "PNG/JPG transcode to WebP is not supported yet");
  }
  if (!magicBytesMatch(type, data, len, uploadFilename)) {
    return AssetOperationResult::fail(type, AssetErrorCode::InvalidUpload,
                                      "File content does not match type");
  }
  if (isRasterWebpTarget(type) && type != AssetType::Banner &&
      !endsWithIgnoreCase(uploadFilename, ".webp")) {
    return AssetOperationResult::fail(
        type, AssetErrorCode::TranscodeFailed,
        "Only WebP may be stored for this asset type");
  }

  AssetOperationResult ok = AssetOperationResult::ok(type, AssetInfo(), 0, len);
  ok.success = true;
  ok.assetType = type;
  ok.bytesWritten = len;
  ok.errorCode = AssetErrorCode::None;
  return ok;
}

bool AssetManager::writeAssetBytes(const String &sdPath,
                                   const String &spiffsPath,
                                   const uint8_t *data, size_t len,
                                   AssetStorageLocation &outLoc,
                                   String &warning) {
  warning = "";
  if (!_storage || !data || len == 0) return false;

  if (_storage->healthy()) {
    if (!_storage->writeBinary(sdPath.c_str(), data, len)) return false;
    outLoc = AssetStorageLocation::Sd;
    if (spiffsPath.length() > 0) {
      _storage->removeBinary(nullptr, spiffsPath.c_str());
    }
    return true;
  }

  // Custom media mutation is intentionally SD-only. Bundled defaults remain
  // available through AssetResolver when SD is absent.
  (void)spiffsPath;
  return false;
}

bool AssetManager::removeAssetFiles(const String &sdPath,
                                    const String &spiffsPath) {
  if (!_storage) return false;
  return _storage->removeBinary(sdPath.c_str(),
                                spiffsPath.length() > 0 ? spiffsPath.c_str()
                                                        : nullptr);
}

bool AssetManager::removeAllTiersForType(AssetType type, uint8_t slot) {
  String sdPath;
  String spiffsPath;
  String canonicalFilename;
  AssetErrorCode err = AssetErrorCode::None;
  if (!resolveStorageTargets(type, slot, sdPath, spiffsPath, canonicalFilename,
                             err)) {
    return false;
  }

  bool ok = removeAssetFiles(sdPath, spiffsPath);
  if (type == AssetType::Banner) {
    ok = removeAssetFiles(StoragePaths::LegacyPortalBanner,
                          StoragePaths::Spiffs::PortalCustomBanner) &&
         ok;
  } else if (type == AssetType::Music) {
    ok = removeAssetFiles(StoragePaths::LegacyPortalMusic,
                          StoragePaths::Spiffs::PortalCustomMusic) &&
         ok;
  }
  return ok;
}

AssetOperationResult AssetManager::commitAsset(
    AssetType type, const uint8_t *data, size_t len,
    const String &uploadFilename, uint8_t slot) {
  const AssetOperationResult validated =
      validateAsset(type, data, len, uploadFilename, slot);
  if (!validated.success) return validated;

  String sdPath;
  String spiffsPath;
  String canonicalFilename;
  AssetErrorCode pathErr = AssetErrorCode::None;
  if (!resolveStorageTargets(type, slot, sdPath, spiffsPath, canonicalFilename,
                             pathErr)) {
    return AssetOperationResult::fail(type, pathErr, "Unable to resolve path");
  }

  if (!_storage->healthy()) {
    return AssetOperationResult::fail(
        type, AssetErrorCode::StorageError,
        "SD card required for asset uploads");
  }

  const String stagingPath = sdPath + ".upload";
  SD.remove(stagingPath);
  File staging = SD.open(stagingPath, FILE_WRITE);
  if (!staging || staging.write(data, len) != len) {
    if (staging) staging.close();
    SD.remove(stagingPath);
    return AssetOperationResult::fail(type, AssetErrorCode::StorageError,
                                      "Unable to stage asset");
  }
  staging.flush();
  staging.close();
  return commitStagedAsset(type, slot, uploadFilename, stagingPath, len);
}

namespace {

String mimeForUploadFilename(const String &filename, AssetType type) {
  if (endsWithIgnoreCase(filename, ".mp4")) return "video/mp4";
  if (endsWithIgnoreCase(filename, ".png")) return "image/png";
  if (endsWithIgnoreCase(filename, ".jpg") ||
      endsWithIgnoreCase(filename, ".jpeg")) {
    return "image/jpeg";
  }
  if (endsWithIgnoreCase(filename, ".webp")) return "image/webp";
  if (type == AssetType::Music) return "audio/mpeg";
  return assetCanonicalMimeType(type);
}

}  // namespace

AssetOperationResult AssetManager::commitStagedAsset(
    AssetType type, uint8_t slot, const String &uploadFilename,
    const String &stagingPath, size_t len) {
  String sdPath;
  String spiffsPath;
  String canonicalFilename;
  AssetErrorCode pathErr = AssetErrorCode::None;
  if (!resolveStorageTargets(type, slot, sdPath, spiffsPath, canonicalFilename,
                             pathErr)) {
    SD.remove(stagingPath);
    return AssetOperationResult::fail(type, pathErr, "Unable to resolve path");
  }
  if (!_storage || !_storage->healthy() || !SD.exists(stagingPath)) {
    SD.remove(stagingPath);
    return AssetOperationResult::fail(type, AssetErrorCode::StorageError,
                                      "Staged asset unavailable");
  }
  File staged = SD.open(stagingPath, FILE_READ);
  const bool stagedSizeOk = staged && staged.size() == len;
  if (staged) staged.close();
  if (!stagedSizeOk) {
    SD.remove(stagingPath);
    return AssetOperationResult::fail(type, AssetErrorCode::IntegrityFailed,
                                      "Staged asset size mismatch");
  }

  const String checksum = checksumFile(stagingPath.c_str());
  if (checksum.length() == 0) {
    SD.remove(stagingPath);
    return AssetOperationResult::fail(type, AssetErrorCode::IntegrityFailed,
                                      "Unable to checksum staged asset");
  }
  const String backupPath = sdPath + ".assetbak";
  SD.remove(backupPath);
  const bool hadOriginal = SD.exists(sdPath);
  if (hadOriginal && !SD.rename(sdPath, backupPath)) {
    SD.remove(stagingPath);
    return AssetOperationResult::fail(type, AssetErrorCode::StorageError,
                                      "Unable to preserve current asset");
  }
  if (!SD.rename(stagingPath, sdPath)) {
    if (hadOriginal) SD.rename(backupPath, sdPath);
    return AssetOperationResult::fail(type, AssetErrorCode::StorageError,
                                      "Unable to promote staged asset");
  }

  const AssetInfo info = [&]() {
    AssetInfo built =
        buildAssetInfo(type, slot, sdPath, spiffsPath, canonicalFilename, len,
                       AssetStorageLocation::Sd, checksum);
    built.mimeType = mimeForUploadFilename(uploadFilename, type);
    return built;
  }();

  CachedAsset *cache = cacheFor(type, slot);
  const CachedAsset previousCache = cache ? *cache : CachedAsset();
  const uint32_t previousRevision = _revision;
  if (cache) {
    cache->info = info;
    cache->loaded = true;
  }

  _revision = refreshPortalRevision();
  const AssetOperationResult saved = saveMetadata();
  if (!saved.success) {
    SD.remove(sdPath);
    if (hadOriginal) SD.rename(backupPath, sdPath);
    if (cache) *cache = previousCache;
    _revision = previousRevision;
    return AssetOperationResult::fail(type, AssetErrorCode::StorageError,
                                      "Unable to persist metadata");
  }
  SD.remove(backupPath);
  if (spiffsPath.length() > 0 && _storage) {
    if (!_storage->mirrorSdFileToSpiffs(sdPath.c_str(), spiffsPath.c_str())) {
      Serial.printf(
          "[assets] SPIFFS mirror skipped/failed type=%s sd=%s spiffs=%s\n",
          assetTypeLabel(type), sdPath.c_str(), spiffsPath.c_str());
    }
  }
  if (type == AssetType::Banner) {
    SD.remove(StoragePaths::LegacyPortalBanner);
  } else if (type == AssetType::Music) {
    SD.remove(StoragePaths::LegacyPortalMusic);
  }

  notifyPortalChanged();
  logAssetAction(String(assetTypeLabel(type)) + " saved " + String(len) +
                 " bytes → " + info.path);

  return AssetOperationResult::ok(type, info, _revision, len);
}

AssetOperationResult AssetManager::saveAsset(AssetType type, const uint8_t *data,
                                             size_t len,
                                             const String &uploadFilename,
                                             uint8_t slot) {
  if (!ready()) {
    return AssetOperationResult::fail(type, AssetErrorCode::NotReady,
                                      "AssetManager not ready");
  }
  if (_upload.active) {
    return AssetOperationResult::fail(type, AssetErrorCode::InvalidUpload,
                                      "Another asset upload is active");
  }
  return commitAsset(type, data, len, uploadFilename, slot);
}

AssetOperationResult AssetManager::beginSaveAsset(AssetType type,
                                                  size_t expectedTotal,
                                                  const String &uploadFilename,
                                                  uint8_t slot) {
  if (!ready()) {
    return AssetOperationResult::fail(type, AssetErrorCode::NotReady,
                                      "AssetManager not ready");
  }
  if (_upload.active) {
    return AssetOperationResult::fail(type, AssetErrorCode::InvalidUpload,
                                      "Another asset upload is active");
  }

  if (!isSupportedType(type)) {
    return AssetOperationResult::fail(type, AssetErrorCode::InvalidType,
                                      "Unsupported asset type");
  }
  if (expectedTotal > maxBytesFor(type)) {
    return AssetOperationResult::fail(type, AssetErrorCode::SizeExceeded,
                                      "Invalid upload size");
  }
  if (uploadFilename.length() == 0) {
    return AssetOperationResult::fail(type, AssetErrorCode::InvalidUpload,
                                      "Missing upload filename");
  }
  if (!extensionAllowed(type, uploadFilename)) {
    return AssetOperationResult::fail(type, AssetErrorCode::InvalidType,
                                      "File extension not allowed");
  }
  if (requiresTranscode(type, uploadFilename)) {
    return AssetOperationResult::fail(
        type, AssetErrorCode::TranscodeFailed,
        "PNG/JPG transcode to WebP is not supported yet");
  }
  if (!_storage->healthy()) {
    return AssetOperationResult::fail(type, AssetErrorCode::StorageError,
                                      "SD card required for asset uploads");
  }

  String sdPath;
  String spiffsPath;
  String canonicalFilename;
  AssetErrorCode pathErr = AssetErrorCode::None;
  if (!resolveStorageTargets(type, slot, sdPath, spiffsPath, canonicalFilename,
                             pathErr)) {
    return AssetOperationResult::fail(type, pathErr, "Unable to resolve path");
  }
  const String stagingPath = sdPath + ".upload";
  SD.remove(stagingPath);
  File staging = SD.open(stagingPath, FILE_WRITE);
  if (!staging) {
    return AssetOperationResult::fail(type, AssetErrorCode::StorageError,
                                      "Unable to open upload staging file");
  }

  _upload.active = true;
  _upload.type = type;
  _upload.slot = slot;
  _upload.uploadFilename = uploadFilename;
  _upload.stagingPath = stagingPath;
  _upload.stagingFile = staging;
  _upload.signatureBytes = 0;
  _upload.expectedTotal = expectedTotal;
  _upload.received = 0;

  AssetOperationResult result;
  result.success = true;
  result.assetType = type;
  result.bytesWritten = 0;
  return result;
}

AssetOperationResult AssetManager::appendSaveChunk(const uint8_t *data,
                                                   size_t len, size_t index,
                                                   bool finalChunk) {
  if (!_upload.active) {
    return AssetOperationResult::fail(_upload.type, AssetErrorCode::InvalidUpload,
                                      "No active upload");
  }
  if (index != _upload.received || (len > 0 && !data)) {
    const AssetType type = _upload.type;
    abortSaveAsset();
    return AssetOperationResult::fail(type, AssetErrorCode::InvalidUpload,
                                      "Upload chunks are missing or out of order");
  }
  if ((_upload.expectedTotal > 0 &&
       _upload.received + len > _upload.expectedTotal) ||
      _upload.received + len > maxBytesFor(_upload.type)) {
    abortSaveAsset();
    return AssetOperationResult::fail(_upload.type, AssetErrorCode::SizeExceeded,
                                      "Upload exceeds expected size");
  }
  if (len > 0) {
    const size_t signatureRoom = sizeof(_upload.signature) - _upload.signatureBytes;
    const size_t signatureCopy = len < signatureRoom ? len : signatureRoom;
    if (signatureCopy > 0) {
      memcpy(_upload.signature + _upload.signatureBytes, data, signatureCopy);
      _upload.signatureBytes += signatureCopy;
    }
    if (!_upload.stagingFile ||
        _upload.stagingFile.write(data, len) != len) {
      const AssetType type = _upload.type;
      Serial.printf(
          "[assets] staging write failed type=%u received=%u chunk=%u "
          "dma_free≈check serial dma lines\n",
          static_cast<unsigned>(type), (unsigned)_upload.received,
          (unsigned)len);
      abortSaveAsset();
      return AssetOperationResult::fail(type, AssetErrorCode::StorageError,
                                        "Unable to write upload staging file");
    }
    _upload.received += len;
  }
  if (finalChunk && _upload.expectedTotal == 0) {
    _upload.expectedTotal = _upload.received;
  } else if (finalChunk && _upload.received != _upload.expectedTotal) {
    const AssetType type = _upload.type;
    abortSaveAsset();
    return AssetOperationResult::fail(type, AssetErrorCode::InvalidUpload,
                                      "Upload size does not match request");
  }
  AssetOperationResult result;
  result.success = true;
  result.assetType = _upload.type;
  result.bytesWritten = _upload.received;
  return result;
}

AssetOperationResult AssetManager::finishSaveAsset() {
  if (!_upload.active) {
    return AssetOperationResult::fail(AssetType::Unknown,
                                      AssetErrorCode::InvalidUpload,
                                      "No active upload");
  }

  const AssetType type = _upload.type;
  const uint8_t slot = _upload.slot;
  const String filename = _upload.uploadFilename;
  const String stagingPath = _upload.stagingPath;
  const size_t received = _upload.received;
  if (_upload.stagingFile) {
    _upload.stagingFile.flush();
    _upload.stagingFile.close();
  }
  if (received == 0 ||
      (_upload.expectedTotal > 0 && received != _upload.expectedTotal)) {
    abortSaveAsset();
    return AssetOperationResult::fail(type, AssetErrorCode::InvalidUpload,
                                      "Empty upload");
  }
  if (!magicBytesMatch(type, _upload.signature, _upload.signatureBytes,
                       filename)) {
    abortSaveAsset();
    return AssetOperationResult::fail(type, AssetErrorCode::InvalidUpload,
                                      "File content does not match type");
  }
  _upload.active = false;
  _upload.stagingPath = "";
  return commitStagedAsset(type, slot, filename, stagingPath, received);
}

void AssetManager::abortSaveAsset() {
  if (_upload.stagingFile) _upload.stagingFile.close();
  if (_upload.stagingPath.length() > 0 && SD.exists(_upload.stagingPath)) {
    SD.remove(_upload.stagingPath);
  }
  _upload.active = false;
  _upload.type = AssetType::Unknown;
  _upload.slot = 0;
  _upload.uploadFilename = "";
  _upload.stagingPath = "";
  _upload.signatureBytes = 0;
  _upload.expectedTotal = 0;
  _upload.received = 0;
}

AssetOperationResult AssetManager::deleteAsset(AssetType type, uint8_t slot) {
  if (!ready()) {
    return AssetOperationResult::fail(type, AssetErrorCode::NotReady,
                                      "AssetManager not ready");
  }
  if (!_storage->healthy()) {
    return AssetOperationResult::fail(type, AssetErrorCode::StorageError,
                                      "SD card required for asset changes");
  }
  if (_upload.active) abortSaveAsset();

  String sdPath;
  String spiffsPath;
  String canonicalFilename;
  AssetErrorCode pathErr = AssetErrorCode::None;
  if (!resolveStorageTargets(type, slot, sdPath, spiffsPath, canonicalFilename,
                             pathErr)) {
    return AssetOperationResult::fail(type, pathErr, "Unable to resolve path");
  }

  CachedAsset *cache = cacheFor(type, slot);
  const bool hadAsset =
      cache && cache->loaded && cache->info.present();

  if (!removeAllTiersForType(type, slot) && !hadAsset) {
    return AssetOperationResult::fail(type, AssetErrorCode::NotFound,
                                      "Asset not found");
  }

  if (cache) {
    cache->info = AssetInfo();
    cache->loaded = false;
  }

  _revision = refreshPortalRevision();
  const AssetOperationResult saved = saveMetadata();
  if (!saved.success) {
    return AssetOperationResult::fail(type, AssetErrorCode::StorageError,
                                      "Unable to persist metadata");
  }

  notifyPortalChanged();
  logAssetAction(String(assetTypeLabel(type)) + " deleted");

  AssetOperationResult result =
      AssetOperationResult::ok(type, AssetInfo(), _revision, 0);
  result.storedPath = sdPath;
  return result;
}

bool AssetManager::assetExists(AssetType type, uint8_t slot) const {
  const CachedAsset *cache = cacheFor(type, slot);
  if (cache && cache->loaded && cache->info.present()) return true;

  String sdPath;
  String spiffsPath;
  String canonicalFilename;
  AssetErrorCode err = AssetErrorCode::None;
  if (!resolveStorageTargets(type, slot, sdPath, spiffsPath, canonicalFilename,
                             err)) {
    return false;
  }
  if (_storage && _storage->healthy() && _storage->exists(sdPath.c_str())) {
    return true;
  }
  if (spiffsPath.length() > 0 && SPIFFS.exists(spiffsPath.c_str())) return true;
  return false;
}

AssetOperationResult AssetManager::getAssetInfo(AssetType type, AssetInfo &out,
                                                uint8_t slot) const {
  if (!ready()) {
    return AssetOperationResult::fail(type, AssetErrorCode::NotReady,
                                      "AssetManager not ready");
  }
  const CachedAsset *cache = cacheFor(type, slot);
  if (cache && cache->loaded && cache->info.present()) {
    out = cache->info;
    return AssetOperationResult::ok(type, out, _revision);
  }
  if (!assetExists(type, slot)) {
    return AssetOperationResult::fail(type, AssetErrorCode::NotFound,
                                      "Asset not found");
  }
  return AssetOperationResult::fail(type, AssetErrorCode::NotFound,
                                    "Asset on disk but not in metadata");
}

AssetOperationResult AssetManager::listAssets(std::vector<AssetInfo> &out) const {
  out.clear();
  if (!ready()) {
    return AssetOperationResult::fail(AssetType::Unknown, AssetErrorCode::NotReady,
                                      "AssetManager not ready");
  }

  auto appendIfPresent = [&](const CachedAsset &entry) {
    if (entry.loaded && entry.info.present()) out.push_back(entry.info);
  };

  appendIfPresent(_banner);
  appendIfPresent(_music);
  appendIfPresent(_logo);
  appendIfPresent(_background);
  for (uint8_t i = 0; i < kMaxAdSlots; ++i) appendIfPresent(_ads[i]);
  for (uint8_t i = 0; i < kMaxVideoSlots; ++i) appendIfPresent(_videos[i]);

  AssetOperationResult result;
  result.success = true;
  result.revisionUpdated = _revision;
  return result;
}

void AssetManager::serializeAssetInfo(JsonObject obj,
                                      const AssetInfo &info) const {
  obj["type"] = assetTypeLabel(info.type);
  obj["filename"] = info.filename;
  obj["mimeType"] = info.mimeType;
  obj["size"] = info.size;
  obj["lastModified"] = info.lastModified;
  obj["checksum"] = info.checksum;
  obj["storageLocation"] = assetStorageLocationLabel(info.storageLocation);
  obj["path"] = info.path;
  if (info.slot > 0) obj["slot"] = info.slot;
}

bool AssetManager::deserializeAssetInfo(JsonObjectConst obj,
                                        AssetInfo &info) const {
  if (obj.isNull()) return false;
  info.type = assetTypeFromLabel(obj["type"] | "");
  info.filename = obj["filename"] | "";
  info.mimeType = obj["mimeType"] | "";
  info.size = obj["size"] | 0;
  info.lastModified = obj["lastModified"] | 0;
  info.checksum = obj["checksum"] | "";
  const char *loc = obj["storageLocation"] | "";
  if (strcmp(loc, "sd") == 0) {
    info.storageLocation = AssetStorageLocation::Sd;
  } else if (strcmp(loc, "spiffs") == 0) {
    info.storageLocation = AssetStorageLocation::Spiffs;
  } else if (strcmp(loc, "bundled") == 0) {
    info.storageLocation = AssetStorageLocation::Bundled;
  } else {
    info.storageLocation = AssetStorageLocation::None;
  }
  info.path = obj["path"] | "";
  info.slot = obj["slot"] | 0;
  return info.present();
}

void AssetManager::syncCacheFromDoc(JsonObjectConst doc) {
  _revision = doc["revision"] | 0;

  auto loadSection = [&](const char *section, const char *key, AssetType type,
                         uint8_t slot) {
    CachedAsset *cache = cacheFor(type, slot);
    if (!cache || !section || !key) return;
    JsonObjectConst sec = doc[section];
    if (sec.isNull()) return;
    JsonObjectConst entry = sec[key];
    if (!entry.isNull()) {
      cache->loaded = deserializeAssetInfo(entry, cache->info);
    }
  };

  loadSection(PortalConfigSchema::Branding, PortalConfigSchema::KeyBanner,
              AssetType::Banner, 0);
  loadSection(PortalConfigSchema::Branding, PortalConfigSchema::KeyLogo,
              AssetType::Logo, 0);
  loadSection(PortalConfigSchema::Branding, PortalConfigSchema::KeyBackground,
              AssetType::Background, 0);
  loadSection(PortalConfigSchema::Audio, PortalConfigSchema::KeyMusic,
              AssetType::Music, 0);

  for (uint8_t slot = 1; slot <= kMaxAdSlots; ++slot) {
    char key[8];
    snprintf(key, sizeof(key), "ad%u", (unsigned)slot);
    loadSection(PortalConfigSchema::Ads, key, AssetType::Ad, slot);
  }
  for (uint8_t slot = 1; slot <= kMaxVideoSlots; ++slot) {
    char key[12];
    snprintf(key, sizeof(key), "video%u", (unsigned)slot);
    loadSection(PortalConfigSchema::Videos, key, AssetType::Video, slot);
    if (!cacheFor(AssetType::Video, slot)->loaded) {
      loadSection(PortalConfigSchema::LegacyVideosSection, key, AssetType::Video,
                  slot);
    }
  }

  // Deprecated flat "assets" map (Phase 3A) — load if sectioned entry absent.
  auto loadLegacyFlat = [&](const char *key, AssetType type, uint8_t slot) {
    CachedAsset *cache = cacheFor(type, slot);
    if (!cache || cache->loaded) return;
    JsonObjectConst assets = doc[PortalConfigSchema::LegacyAssetsMap];
    if (assets.isNull()) return;
    JsonObjectConst entry = assets[key];
    cache->loaded = deserializeAssetInfo(entry, cache->info);
  };

  loadLegacyFlat("banner", AssetType::Banner, 0);
  loadLegacyFlat("music", AssetType::Music, 0);
  loadLegacyFlat("logo", AssetType::Logo, 0);
  loadLegacyFlat("background", AssetType::Background, 0);
  for (uint8_t slot = 1; slot <= kMaxAdSlots; ++slot) {
    char key[8];
    snprintf(key, sizeof(key), "ad%u", (unsigned)slot);
    loadLegacyFlat(key, AssetType::Ad, slot);
  }
  for (uint8_t slot = 1; slot <= kMaxVideoSlots; ++slot) {
    char key[12];
    snprintf(key, sizeof(key), "video%u", (unsigned)slot);
    loadLegacyFlat(key, AssetType::Video, slot);
  }
}

void AssetManager::updateLegacyMirrors(JsonObject doc) const {
  doc["revision"] = _revision;
  doc[PortalConfigSchema::LegacyHasBanner] =
      _banner.loaded && _banner.info.present();
  doc[PortalConfigSchema::LegacyHasMusic] =
      _music.loaded && _music.info.present();
  doc["hasLogo"] = _logo.loaded && _logo.info.present();
  doc["hasBackground"] = _background.loaded && _background.info.present();

  uint8_t adCount = 0;
  for (uint8_t i = 0; i < kMaxAdSlots; ++i) {
    if (_ads[i].loaded && _ads[i].info.present()) adCount++;
  }
  doc["hasAds"] = adCount > 0;
  doc["adCount"] = adCount;

  if (_banner.loaded && _banner.info.present()) {
    doc[PortalConfigSchema::LegacyBannerPath] = _banner.info.path;
  } else {
    doc.remove(PortalConfigSchema::LegacyBannerPath);
  }
  if (_music.loaded && _music.info.present()) {
    doc[PortalConfigSchema::LegacyMusicPath] = _music.info.path;
  } else {
    doc.remove(PortalConfigSchema::LegacyMusicPath);
  }
  if (_logo.loaded && _logo.info.present()) {
    doc["logoPath"] = _logo.info.path;
  } else {
    doc.remove("logoPath");
  }
  if (_background.loaded && _background.info.present()) {
    doc["backgroundPath"] = _background.info.path;
  } else {
    doc.remove("backgroundPath");
  }
}

void AssetManager::writeAssetToPortalJson(JsonObject root, AssetType type,
                                          uint8_t slot,
                                          const AssetInfo &info) const {
  const char *section = nullptr;
  String key;
  if (!resolveMediaJsonLocation(type, slot, section, key)) return;
  JsonObject sec = root[section].to<JsonObject>();
  JsonObject entry = sec[key].to<JsonObject>();
  serializeAssetInfo(entry, info);
}

void AssetManager::removeAssetFromPortalJson(JsonObject root, AssetType type,
                                             uint8_t slot) const {
  const char *section = nullptr;
  String key;
  if (!resolveMediaJsonLocation(type, slot, section, key)) return;
  JsonObject sec = root[section];
  if (!sec.isNull()) sec.remove(key);
}

void AssetManager::writeMediaSections(JsonObject root) {
  auto persist = [&](AssetType type, uint8_t slot) {
    CachedAsset *cache = cacheFor(type, slot);
    if (!cache) return;
    if (cache->loaded && cache->info.present()) {
      writeAssetToPortalJson(root, type, slot, cache->info);
    } else {
      removeAssetFromPortalJson(root, type, slot);
    }
  };

  persist(AssetType::Banner, 0);
  persist(AssetType::Music, 0);
  persist(AssetType::Logo, 0);
  persist(AssetType::Background, 0);
  for (uint8_t slot = 1; slot <= kMaxAdSlots; ++slot) {
    persist(AssetType::Ad, slot);
  }
  for (uint8_t slot = 1; slot <= kMaxVideoSlots; ++slot) {
    persist(AssetType::Video, slot);
  }

  root.remove(PortalConfigSchema::LegacyAssetsMap);
}

AssetOperationResult AssetManager::loadMetadata() {
  if (!_storage) {
    return AssetOperationResult::fail(AssetType::Unknown, AssetErrorCode::NotReady,
                                      "Storage unavailable");
  }

  DynamicJsonDocument doc(4096);
  if (!_storage->readJson(RenzFiConfig::PORTAL_CONFIG_FILE, doc)) {
    _revision = 0;
    return AssetOperationResult::fail(AssetType::Unknown, AssetErrorCode::NotFound,
                                      "portal.json not readable");
  }

  syncCacheFromDoc(doc.as<JsonObjectConst>());
  _revision = doc["revision"] | _revision;

  AssetOperationResult result;
  result.success = true;
  result.revisionUpdated = _revision;
  return result;
}

AssetOperationResult AssetManager::saveMetadata() {
  if (!_storage) {
    return AssetOperationResult::fail(AssetType::Unknown, AssetErrorCode::NotReady,
                                      "Storage unavailable");
  }

  DynamicJsonDocument doc(4096);
  if (!_storage->readJson(RenzFiConfig::PORTAL_CONFIG_FILE, doc)) {
    doc.clear();
  }

  JsonObject root = doc.to<JsonObject>();
  updateLegacyMirrors(root);
  writeMediaSections(root);

  if (!_storage->writeJson(RenzFiConfig::PORTAL_CONFIG_FILE, doc, true)) {
    return AssetOperationResult::fail(AssetType::Unknown, AssetErrorCode::StorageError,
                                      "Unable to write portal.json");
  }

  AssetOperationResult result;
  result.success = true;
  result.revisionUpdated = _revision;
  return result;
}

uint32_t AssetManager::refreshPortalRevision() { return _revision = millis(); }

AssetOperationResult AssetManager::verifyIntegrity(AssetType type,
                                                   uint8_t slot) {
  AssetInfo info;
  const AssetOperationResult got = getAssetInfo(type, info, slot);
  if (!got.success) return got;

  size_t onDisk = 0;
  if (info.storageLocation == AssetStorageLocation::Sd) {
    onDisk = _storage ? _storage->fileSizeBytes(info.path.c_str()) : 0;
  } else if (info.storageLocation == AssetStorageLocation::Spiffs &&
             SPIFFS.exists(info.path.c_str())) {
    File file = SPIFFS.open(info.path.c_str(), "r");
    if (file) {
      onDisk = file.size();
      file.close();
    }
  }

  if (onDisk == 0 || onDisk != info.size) {
    return AssetOperationResult::fail(type, AssetErrorCode::IntegrityFailed,
                                      "Size mismatch on disk");
  }
  if (info.checksum.length() < 4) {
    return AssetOperationResult::fail(type, AssetErrorCode::IntegrityFailed,
                                      "Missing checksum metadata");
  }

  AssetOperationResult result = AssetOperationResult::ok(type, info, _revision);
  result.warning = "checksum recompute deferred to Phase 3B";
  return result;
}

void AssetManager::notifyPortalChanged() {
  if (_events) {
    _events->emit("portal.changed",
                  "{\"revision\":" + String(_revision) + "}");
  }
}

void AssetManager::logAssetAction(const String &msg) {
  if (_logger) _logger->infoLocal("assets", msg);
}

const AssetInfo *AssetManager::cachedInfo(AssetType type, uint8_t slot) const {
  const CachedAsset *cache = cacheFor(type, slot);
  if (cache && cache->loaded && cache->info.present() && _storage &&
      _storage->healthy() &&
      cache->info.storageLocation == AssetStorageLocation::Sd) {
    return &cache->info;
  }
  return nullptr;
}

ResolvedAsset AssetManager::resolveBanner() const {
  return _resolver.resolveBanner(cachedInfo(AssetType::Banner, 0));
}

ResolvedAsset AssetManager::resolveMusic() const {
  return _resolver.resolveMusic(cachedInfo(AssetType::Music, 0));
}

ResolvedAsset AssetManager::resolveLogo() const {
  return _resolver.resolveLogo(cachedInfo(AssetType::Logo, 0));
}

ResolvedAsset AssetManager::resolveBackground() const {
  return _resolver.resolveBackground(cachedInfo(AssetType::Background, 0));
}

ResolvedAsset AssetManager::resolveAd(uint8_t slot) const {
  return _resolver.resolveAd(slot, cachedInfo(AssetType::Ad, slot));
}

ResolvedAsset AssetManager::resolveVideo(uint8_t slot) const {
  return _resolver.resolveVideo(slot, cachedInfo(AssetType::Video, slot));
}

ResolvedAsset AssetManager::resolve(AssetType type, uint8_t slot) const {
  return _resolver.resolve(type, slot, cachedInfo(type, slot));
}
