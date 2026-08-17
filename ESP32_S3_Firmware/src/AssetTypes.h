#pragma once

#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
//  AssetTypes — frozen metadata and operation result models (Phase 3 contract)
//
//  Every portal asset (banner, music, logo, …) is described by AssetInfo.
//  Every AssetManager write/delete returns AssetOperationResult (never bare bool).
//
//  See docs/ASSET_LIFECYCLE.md § Asset metadata model.
// ─────────────────────────────────────────────────────────────────────────────

enum class AssetType : uint8_t {
  Banner,
  Music,
  Logo,
  Background,
  Ad,
  Video,
  Icon,
  Font,
  Unknown,
};

enum class AssetStorageLocation : uint8_t {
  None,
  Sd,
  Spiffs,
  Bundled,  // firmware SPIFFS default (not user-owned)
};

enum class AssetErrorCode : uint8_t {
  None,
  Unauthorized,
  InvalidUpload,
  InvalidType,
  SizeExceeded,
  TranscodeFailed,
  StorageError,
  SlotInvalid,
  NotFound,
  NotReady,
  IntegrityFailed,
};

// Common metadata for every stored asset — serialized under portal.json "assets".
struct AssetInfo {
  AssetType type = AssetType::Unknown;
  String filename;            // canonical leaf, e.g. "current.webp"
  String mimeType;            // e.g. "image/webp", "audio/mpeg"
  size_t size = 0;            // bytes on disk
  uint32_t lastModified = 0;  // Unix epoch seconds (set at commit time)
  String checksum;            // "md5:" + 32-char hex (lowercase)
  AssetStorageLocation storageLocation = AssetStorageLocation::None;
  String path;                // full path, e.g. "/assets/banner/current.webp"
  uint8_t slot = 0;           // 0 = N/A; 1…N for Ad / Video slots

  bool present() const { return size > 0 && path.length() > 0; }
};

// Structured outcome for upload, delete, reconcile, and integrity checks.
struct AssetOperationResult {
  bool success = false;
  AssetType assetType = AssetType::Unknown;
  String storedPath;
  size_t bytesWritten = 0;
  uint32_t revisionUpdated = 0;
  String warning;  // non-fatal, e.g. "stored on SPIFFS fallback"
  AssetErrorCode errorCode = AssetErrorCode::None;
  String errorMessage;
  AssetInfo asset;  // populated on success (post-commit metadata)

  static AssetOperationResult ok(AssetType type, const AssetInfo &info,
                                 uint32_t revision, size_t bytes = 0) {
    AssetOperationResult r;
    r.success = true;
    r.assetType = type;
    r.asset = info;
    r.storedPath = info.path;
    r.bytesWritten = bytes > 0 ? bytes : info.size;
    r.revisionUpdated = revision;
    return r;
  }

  static AssetOperationResult fail(AssetType type, AssetErrorCode code,
                                   const String &message) {
    AssetOperationResult r;
    r.success = false;
    r.assetType = type;
    r.errorCode = code;
    r.errorMessage = message;
    return r;
  }
};

// String helpers for JSON / logging (Phase 3 AssetManager uses these).
const char *assetTypeLabel(AssetType type);
const char *assetStorageLocationLabel(AssetStorageLocation loc);
const char *assetErrorCodeLabel(AssetErrorCode code);
AssetType assetTypeFromLabel(const char *label);
const char *assetPortalJsonKey(AssetType type);
const char *assetCanonicalMimeType(AssetType type);
