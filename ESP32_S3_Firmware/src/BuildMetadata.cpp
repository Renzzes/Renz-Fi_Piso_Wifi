#include "BuildMetadata.h"

#include <FS.h>
#include <SPIFFS.h>

#include "Config.h"
#include "ContractVersions.h"
#include "RenzFiDebug.h"
#include "StorageManager.h"
#include "StoragePaths.h"

void BuildMetadata::begin(StorageManager *storage) {
  _storage = storage;
  _loaded  = false;
  _doc.clear();

  if (!SPIFFS.exists(StoragePaths::Spiffs::BuildInfo)) {
    return;
  }

  File file = SPIFFS.open(StoragePaths::Spiffs::BuildInfo, "r");
  if (!file) return;

  String raw = file.readString();
  file.close();

  DeserializationError err = deserializeJson(_doc, raw);
  if (err) return;

  _loaded = true;
  mirrorToSd(raw);

#if RENZFI_DEBUG_BOOT
  Serial.printf("[build] admin=%s portal=%s git=%s #%u\n",
                _doc["adminBuild"] | "",
                _doc["portalRevision"] | "",
                _doc["gitCommit"] | "",
                static_cast<unsigned>(_doc["buildNumber"] | 0));
#endif
}

void BuildMetadata::mirrorToSd(const String &rawJson) {
  if (!_storage || !_storage->isSdMounted()) return;
  _storage->writeSdText(StoragePaths::ContractSystemBuildInfo, rawJson);
}

void BuildMetadata::fillJson(JsonObject out) const {
  if (_loaded) {
    for (JsonPairConst kv : _doc.as<JsonObjectConst>()) {
      out[kv.key().c_str()] = kv.value();
    }
    return;
  }

  out["firmwareVersion"]         = RenzFiConfig::FIRMWARE_VERSION;
  out["deviceProfileVersion"]    = RenzFiContract::DEVICE_PROFILE_VERSION;
  out["storageContractVersion"]  = RenzFiContract::STORAGE_CONTRACT_VERSION;
  out["httpContractVersion"]     = RenzFiContract::HTTP_CONTRACT_VERSION;
  out["gitCommit"]               = "unknown";
  out["portalRevision"]          = "unknown";
  out["buildNumber"]             = 0;
}
