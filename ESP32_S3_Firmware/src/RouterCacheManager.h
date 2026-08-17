#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "Config.h"

class Logger;
class StorageManager;

// Local cache of RouterOS metadata — /config/router-cache.json
// RouterOS API is configuration-only; dashboard reads this file instead of
// polling the router on every page load.
class RouterCacheManager {
 public:
  // v2: structured hotspot user profiles (name + rateLimit) + observational
  // connectivity/hotspot status. v1 string-only profiles still load.
  static constexpr uint16_t SCHEMA_VERSION = 2;

  void begin(StorageManager *storage, Logger *logger = nullptr);

  bool load();
  bool save() const;
  bool isPopulated() const;

  bool fillWireless(JsonDocument &out) const;
  bool fillProfiles(JsonDocument &out) const;
  bool fillPublic(JsonDocument &out) const;
  void fillCacheStatus(JsonObject out) const;
  void fillStatusMetadata(JsonObject out) const;
  void fillObservation(JsonObject out) const;

  uint32_t cacheAgeSeconds() const;
  bool isStale() const;

  bool applyLiveSnapshot(JsonObjectConst snap);
  bool applyWirelessFields(JsonObjectConst wireless);
  bool applyProductionNetworkVerification(JsonObjectConst verifyResult);
  /** Observational connectivity / hotspot health — never invents Online from host alone. */
  bool applyObservation(JsonObjectConst observation);
  void markProvisioned(const char *status = "provisioned");

 private:
  StorageManager *_storage = nullptr;
  Logger         *_logger  = nullptr;
  // Must fit identity + routerOs + wireless + bounded profileDetails +
  // observation. 3072 silently truncated Test/Sync snapshots (empty identity,
  // lost connectivity) while the worker still logged success.
  DynamicJsonDocument _doc{RenzFiConfig::JSON_DOC_MEDIUM};

  bool readFromStorage();
  void stampSynchronized();
  void normalizeProfilesInDoc();
  static String isoTimestampNow();
};
