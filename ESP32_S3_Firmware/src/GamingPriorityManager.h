#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/semphr.h>

#include "GamingPriorityTypes.h"

class StorageManager;

// Guest-network gaming QoS config. Persist on SD; RouterOS apply via worker only.
class GamingPriorityManager {
 public:
  void begin(StorageManager *storage);

  void fillJson(JsonDocument &doc) const;
  bool updateFromJson(JsonObjectConst body, String &errorOut);
  bool buildSyncPayload(JsonDocument &doc) const;
  bool applySyncResult(bool routerOk, const String &message);

 private:
  StorageManager *_storage = nullptr;
  mutable SemaphoreHandle_t _mutex = nullptr;

  bool _enabled = false;
  char _priority[8] = "normal";
  uint16_t _minimumGamingMbps = 5;
  uint16_t _maximumGamingMbps = 20;
  uint16_t _perUserGamingMbps = 5;
  GamingPriority::GameProfile _profiles[GamingPriority::kMaxProfiles];
  uint8_t _profileCount = 0;
  uint32_t _updatedAt = 0;
  uint32_t _configRevision = 0;
  uint32_t _appliedRevision = 0;
  uint32_t _lastApplyAt = 0;
  bool _lastApplyOk = true;
  String _lastSyncError;

  void lock() const;
  void unlock() const;
  bool loadFromStorage();
  bool persistLocked();
  void seedDefaultsLocked();
  void writeMetaJson(JsonObject obj) const;
  void writeProfilesJson(JsonArray arr, bool syncOnly) const;
  bool ingestProfiles(JsonArrayConst profiles, bool strict, String &errorOut);
};
