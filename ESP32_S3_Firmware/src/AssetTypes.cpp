#include "AssetTypes.h"

namespace {

const char *kUnknown = "unknown";

}  // namespace

const char *assetTypeLabel(AssetType type) {
  switch (type) {
    case AssetType::Banner: return "banner";
    case AssetType::Music: return "music";
    case AssetType::Logo: return "logo";
    case AssetType::Background: return "background";
    case AssetType::Ad: return "ad";
    case AssetType::Video: return "video";
    case AssetType::Icon: return "icon";
    case AssetType::Font: return "font";
    default: return kUnknown;
  }
}

const char *assetStorageLocationLabel(AssetStorageLocation loc) {
  switch (loc) {
    case AssetStorageLocation::Sd: return "sd";
    case AssetStorageLocation::Spiffs: return "spiffs";
    case AssetStorageLocation::Bundled: return "bundled";
    default: return kUnknown;
  }
}

const char *assetErrorCodeLabel(AssetErrorCode code) {
  switch (code) {
    case AssetErrorCode::Unauthorized: return "UNAUTHORIZED";
    case AssetErrorCode::InvalidUpload: return "INVALID_UPLOAD";
    case AssetErrorCode::InvalidType: return "INVALID_TYPE";
    case AssetErrorCode::SizeExceeded: return "SIZE_EXCEEDED";
    case AssetErrorCode::TranscodeFailed: return "TRANSCODE_FAILED";
    case AssetErrorCode::StorageError: return "STORAGE_ERROR";
    case AssetErrorCode::SlotInvalid: return "SLOT_INVALID";
    case AssetErrorCode::NotFound: return "NOT_FOUND";
    case AssetErrorCode::NotReady: return "NOT_READY";
    case AssetErrorCode::IntegrityFailed: return "INTEGRITY_FAILED";
    default: return "NONE";
  }
}

AssetType assetTypeFromLabel(const char *label) {
  if (!label) return AssetType::Unknown;
  if (strcmp(label, "banner") == 0) return AssetType::Banner;
  if (strcmp(label, "music") == 0) return AssetType::Music;
  if (strcmp(label, "logo") == 0) return AssetType::Logo;
  if (strcmp(label, "background") == 0) return AssetType::Background;
  if (strcmp(label, "ad") == 0) return AssetType::Ad;
  if (strcmp(label, "video") == 0) return AssetType::Video;
  if (strcmp(label, "icon") == 0) return AssetType::Icon;
  if (strcmp(label, "font") == 0) return AssetType::Font;
  return AssetType::Unknown;
}

const char *assetPortalJsonKey(AssetType type) {
  return assetTypeLabel(type);
}

const char *assetCanonicalMimeType(AssetType type) {
  switch (type) {
    case AssetType::Music: return "audio/mpeg";
    case AssetType::Video: return "video/mp4";
    case AssetType::Font: return "font/woff2";
    case AssetType::Banner:
    case AssetType::Logo:
    case AssetType::Background:
    case AssetType::Ad:
    case AssetType::Icon:
      return "image/webp";
    default: return "application/octet-stream";
  }
}
