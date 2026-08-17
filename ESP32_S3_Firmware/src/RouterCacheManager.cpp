#include "RouterCacheManager.h"

#include "Config.h"
#include "Logger.h"
#include "StorageManager.h"
#include "StoragePaths.h"

#include <time.h>

namespace {

constexpr size_t kDocCapacity = RenzFiConfig::JSON_DOC_MEDIUM;

/** ArduinoJson 7: is<const char*>() is unreliable for pooled JsonString values.
 *  Always materialize through String so Test/Sync snapshots persist. */
bool copyStringField(JsonObjectConst src, const char *key, JsonObject dst) {
  if (src[key].isNull()) return false;
  const String value = src[key].as<String>();
  if (value.length() == 0) return false;
  dst[key] = value;
  return true;
}

bool copyOptionalStringField(JsonObjectConst src, const char *key, JsonObject dst) {
  if (src[key].isNull()) return false;
  dst[key] = src[key].as<String>();
  return true;
}

}  // namespace

void RouterCacheManager::begin(StorageManager *storage, Logger *logger) {
  _storage = storage;
  _logger  = logger;
  load();
}

bool RouterCacheManager::load() {
  _doc.clear();
  if (!readFromStorage()) {
    return false;
  }
  return isPopulated();
}

bool RouterCacheManager::readFromStorage() {
  if (!_storage || !_storage->exists(StoragePaths::RouterCacheFile)) {
    return false;
  }
  DynamicJsonDocument stored(kDocCapacity);
  if (!_storage->readJson(StoragePaths::RouterCacheFile, stored)) {
    return false;
  }
  _doc = stored;
  if (!_doc["schemaVersion"].is<uint16_t>()) {
    _doc["schemaVersion"] = SCHEMA_VERSION;
  }
  // Migrate legacy cachedAt-only records.
  if (!_doc["lastSynchronizedAt"].is<const char *>() &&
      _doc["cachedAt"].is<const char *>()) {
    _doc["lastSynchronizedAt"] = _doc["cachedAt"];
  }
  normalizeProfilesInDoc();
  return true;
}

void RouterCacheManager::normalizeProfilesInDoc() {
  DynamicJsonDocument scratch(1536);
  JsonArray details = scratch.createNestedArray("profileDetails");
  JsonArray names   = scratch.createNestedArray("profiles");

  auto appendDetail = [&](const char *name, const char *rateLimit) {
    if (!name || strlen(name) == 0) return;
    for (JsonVariantConst existing : names) {
      if (String(existing.as<const char *>()) == name) return;
    }
    JsonObject row = details.createNestedObject();
    row["name"]      = name;
    row["rateLimit"] = rateLimit ? rateLimit : "";
    names.add(name);
  };

  // Prefer structured profileDetails when present.
  if (_doc["profileDetails"].is<JsonArrayConst>()) {
    for (JsonVariantConst item : _doc["profileDetails"].as<JsonArrayConst>()) {
      if (!item.is<JsonObjectConst>()) continue;
      JsonObjectConst obj = item.as<JsonObjectConst>();
      const char *name = obj["name"] | "";
      const char *rate = "";
      if (obj["rateLimit"].is<const char *>()) {
        rate = obj["rateLimit"].as<const char *>();
      } else if (obj["rate-limit"].is<const char *>()) {
        rate = obj["rate-limit"].as<const char *>();
      }
      appendDetail(name, rate);
    }
  }

  // Migrate legacy profiles: string[] or object[].
  if (_doc["profiles"].is<JsonArrayConst>()) {
    for (JsonVariantConst item : _doc["profiles"].as<JsonArrayConst>()) {
      if (item.is<const char *>() || item.is<String>()) {
        appendDetail(item.as<const char *>(), "");
        continue;
      }
      if (item.is<JsonObjectConst>()) {
        JsonObjectConst obj = item.as<JsonObjectConst>();
        const char *name = obj["name"] | "";
        const char *rate = "";
        if (obj["rateLimit"].is<const char *>()) {
          rate = obj["rateLimit"].as<const char *>();
        } else if (obj["rate-limit"].is<const char *>()) {
          rate = obj["rate-limit"].as<const char *>();
        }
        appendDetail(name, rate);
      }
    }
  }

  if (names.size() == 0 && details.size() == 0) {
    if ((_doc["schemaVersion"] | 0) < SCHEMA_VERSION) {
      _doc["schemaVersion"] = SCHEMA_VERSION;
    }
    return;
  }

  _doc["profiles"]       = names;
  _doc["profileDetails"] = details;
  _doc["schemaVersion"]  = SCHEMA_VERSION;
}

bool RouterCacheManager::isPopulated() const {
  const String host = _doc["routerIp"] | "";
  const String status = _doc["provisionStatus"] | "";
  return host.length() > 0 || status.length() > 0;
}

bool RouterCacheManager::save() const {
  if (!_storage) return false;
  return _storage->writeJson(StoragePaths::RouterCacheFile, _doc, true);
}

String RouterCacheManager::isoTimestampNow() {
  time_t now = time(nullptr);
  // Untethered ESP clocks advance from Unix epoch 0 → 1970-... ISO stamps.
  // Only emit wall-clock ISO when the clock is past a sane threshold.
  static constexpr time_t kMinSaneWallClock = 1577836800;  // 2020-01-01 UTC
  if (now < kMinSaneWallClock) {
    return String();
  }
  struct tm timeinfo {};
  gmtime_r(&now, &timeinfo);
  char buf[32];
  if (strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo) == 0) {
    return String();
  }
  return String(buf);
}

void RouterCacheManager::stampSynchronized() {
  const uint32_t syncedMs = millis();
  _doc["lastSynchronizedMillis"] = syncedMs;

  time_t now = time(nullptr);
  static constexpr time_t kMinSaneWallClock = 1577836800;  // 2020-01-01 UTC
  if (now >= kMinSaneWallClock) {
    const String iso = isoTimestampNow();
    _doc["lastSynchronizedAt"] = iso;
    _doc["cachedAt"]           = iso;
    _doc["lastSynchronizedEpoch"] = static_cast<uint32_t>(now);
    _doc["syncWallClockValid"] = true;
  } else {
    _doc.remove("lastSynchronizedAt");
    _doc.remove("cachedAt");
    _doc.remove("lastSynchronizedEpoch");
    _doc["syncWallClockValid"] = false;
  }
}

uint32_t RouterCacheManager::cacheAgeSeconds() const {
  static constexpr time_t kMinSaneWallClock = 1577836800;
  if (_doc["lastSynchronizedEpoch"].is<uint32_t>()) {
    time_t now = time(nullptr);
    if (now >= kMinSaneWallClock) {
      const uint32_t synced = _doc["lastSynchronizedEpoch"].as<uint32_t>();
      return now >= synced ? static_cast<uint32_t>(now - synced) : 0;
    }
  }
  if (_doc["lastSynchronizedMillis"].is<uint32_t>()) {
    const uint32_t syncedMs = _doc["lastSynchronizedMillis"].as<uint32_t>();
    const uint32_t nowMs = millis();
    if (nowMs >= syncedMs) {
      return (nowMs - syncedMs) / 1000U;
    }
  }
  return 0;
}

bool RouterCacheManager::isStale() const {
  if (!isPopulated()) return false;

  const uint32_t ageSeconds = cacheAgeSeconds();
  if (ageSeconds == 0) return false;

  const uint32_t thresholdSeconds =
      RenzFiConfig::ROUTER_CACHE_STALE_THRESHOLD_HOURS * 3600U;
  return ageSeconds > thresholdSeconds;
}

bool RouterCacheManager::applyLiveSnapshot(JsonObjectConst snap) {
  if (snap.isNull()) return false;

  // CRITICAL: use as<JsonObject>(), never to<JsonObject>() on the root.
  // ArduinoJson's to<> clears the document before returning — calling it per
  // field wiped routerIp/identity/ssid and left isPopulated() false even when
  // Test logged identity=MikroTik from the live response.
  if (!_doc.is<JsonObject>()) {
    _doc.to<JsonObject>();
  }
  JsonObject doc = _doc.as<JsonObject>();
  doc["schemaVersion"] = SCHEMA_VERSION;
  copyStringField(snap, "routerIp", doc);
  copyStringField(snap, "identity", doc);
  copyStringField(snap, "routerOsVersion", doc);
  copyStringField(snap, "wirelessInterface", doc);
  copyStringField(snap, "ssid", doc);
  copyStringField(snap, "security", doc);
  copyStringField(snap, "band", doc);
  copyStringField(snap, "bridge", doc);
  copyStringField(snap, "hotspotServer", doc);
  copyStringField(snap, "hotspotProfile", doc);
  copyStringField(snap, "htmlDirectory", doc);
  copyStringField(snap, "provisionStatus", doc);
  copyStringField(snap, "provisionTimestamp", doc);

  if (snap["profileDetails"].is<JsonArrayConst>()) {
    _doc["profileDetails"] = snap["profileDetails"];
  }
  if (snap["profiles"].is<JsonArrayConst>()) {
    _doc["profiles"] = snap["profiles"];
  }
  normalizeProfilesInDoc();

  if (snap["routerOs"].is<JsonObjectConst>()) {
    _doc["routerOs"] = snap["routerOs"];
    const char *version = snap["routerOs"]["version"] | "";
    if (version && strlen(version) > 0) {
      _doc["routerOsVersion"] = version;
    }
  }
  if (snap["truncated"] | false) {
    _doc["profilesTruncated"] = true;
  } else if (snap.containsKey("truncated")) {
    _doc["profilesTruncated"] = false;
  }

  if (snap["observation"].is<JsonObjectConst>()) {
    JsonObjectConst observation = snap["observation"].as<JsonObjectConst>();
    // Merge into existing observation — do not wipe prior WAN/config fields
    // when Refresh applies a telemetry-only patch.
    if (!_doc["observation"].is<JsonObject>()) {
      _doc["observation"].to<JsonObject>();
    }
    JsonObject obs = _doc["observation"].as<JsonObject>();
    const String now = isoTimestampNow();
    if (now.length() > 0) {
      obs["lastContactAttemptAt"] = now;
    } else {
      obs["lastContactAttemptAtMs"] = millis();
    }
    const char *connectivity = observation["connectivity"] | "";
    if (connectivity && strlen(connectivity) > 0) {
      obs["connectivity"] = connectivity;
    }
    const char *hotspotStatus = observation["hotspotStatus"] | "";
    if (hotspotStatus && strlen(hotspotStatus) > 0) {
      obs["hotspotStatus"] = hotspotStatus;
    }
    copyOptionalStringField(observation, "lastSuccessfulContactAt", obs);
    copyOptionalStringField(observation, "lastContactAttemptAt", obs);
    copyOptionalStringField(observation, "lastContactError", obs);
    copyOptionalStringField(observation, "hotspotServer", obs);
    copyOptionalStringField(observation, "hotspotInterface", obs);
    if (observation["wan"].is<JsonObjectConst>()) {
      obs["wan"] = observation["wan"];
    }

    const char *obsConnectivity = obs["connectivity"] | "unknown";
    if (strcmp(obsConnectivity, "online") == 0) {
      if (!obs["lastSuccessfulContactAt"].is<const char *>() ||
          strlen(obs["lastSuccessfulContactAt"].as<const char *>() ?
                 obs["lastSuccessfulContactAt"].as<const char *>() : "") == 0) {
        obs["lastSuccessfulContactAt"] = now;
      }
      const char *err = observation["lastContactError"] | "";
      if (!err || strlen(err) == 0) {
        obs["lastContactError"] = "";
      }
    }
  }

  stampSynchronized();
  const bool ok = save();
  if (ok && _logger) {
    if (isPopulated()) {
      const char *identity = _doc["identity"] | "";
      const char *ssid     = _doc["ssid"] | "";
      const char *security = _doc["security"] | "";
      const char *profile  = _doc["hotspotProfile"] | "";
      uint8_t profileCount = 0;
      if (_doc["profiles"].is<JsonArrayConst>()) {
        profileCount = static_cast<uint8_t>(_doc["profiles"].as<JsonArrayConst>().size());
      }
      _logger->info(
          "router",
          String("[router-sync] ok=yes identity=") + identity + " ssid=" + ssid +
              " security=" + security + " hotspotProfile=" + profile +
              " profiles=" + String(profileCount));
    } else {
      const String snapIp = snap["routerIp"].as<String>();
      const String docIp  = _doc["routerIp"].as<String>();
      const String snapId = snap["identity"].as<String>();
      const String docId  = _doc["identity"].as<String>();
      _logger->warn(
          "router",
          String("[router-sync] ok=no stage=persist reason=cache-not-populated") +
              " missing=" +
              (docIp.length() == 0 && snapIp.length() == 0
                   ? "routerIp"
                   : (docIp.length() == 0 ? "routerIp-copy" : "provisionStatus")) +
              " snap.routerIp=" + snapIp + " doc.routerIp=" + docIp +
              " snap.identity=" + snapId + " doc.identity=" + docId);
    }
  }
  // Only report success when the authoritative populated contract is met.
  return ok && isPopulated();
}

bool RouterCacheManager::applyObservation(JsonObjectConst observation) {
  if (observation.isNull()) return false;
  if (!_doc.is<JsonObject>()) {
    _doc.to<JsonObject>();
    _doc["schemaVersion"] = SCHEMA_VERSION;
  }

  JsonObject obs = _doc["observation"].to<JsonObject>();
  const String now = isoTimestampNow();
  obs["lastContactAttemptAt"] = now;

  if (observation["connectivity"].is<const char *>()) {
    obs["connectivity"] = observation["connectivity"].as<const char *>();
  }
  if (observation["hotspotStatus"].is<const char *>()) {
    obs["hotspotStatus"] = observation["hotspotStatus"].as<const char *>();
  }
  if (observation["lastContactError"].is<const char *>()) {
    obs["lastContactError"] = observation["lastContactError"].as<const char *>();
  }
  copyOptionalStringField(observation, "hotspotServer", obs);
  copyOptionalStringField(observation, "hotspotInterface", obs);
  if (observation["wan"].is<JsonObjectConst>()) {
    obs["wan"] = observation["wan"];
  }

  const char *connectivity = obs["connectivity"] | "unknown";
  if (strcmp(connectivity, "online") == 0) {
    obs["lastSuccessfulContactAt"] = now;
    if (!observation["lastContactError"].is<const char *>() ||
        strlen(observation["lastContactError"].as<const char *>() ?
               observation["lastContactError"].as<const char *>() : "") == 0) {
      obs["lastContactError"] = "";
    }
  }

  _doc["schemaVersion"] = SCHEMA_VERSION;
  return save();
}

bool RouterCacheManager::applyWirelessFields(JsonObjectConst wireless) {
  if (wireless.isNull()) return false;
  if (!_doc.is<JsonObject>()) {
    _doc.to<JsonObject>();
    _doc["schemaVersion"] = SCHEMA_VERSION;
  }
  JsonObject doc = _doc.as<JsonObject>();

  copyStringField(wireless, "ssid", doc);
  copyStringField(wireless, "security", doc);
  copyStringField(wireless, "band", doc);
  // Canonical interface key in cache is wirelessInterface.
  const String iface = wireless["interface"].as<String>();
  if (iface.length() > 0) {
    doc["wirelessInterface"] = iface;
  } else {
    copyStringField(wireless, "wirelessInterface", doc);
  }

  stampSynchronized();
  return save();
}

bool RouterCacheManager::applyProductionNetworkVerification(
    JsonObjectConst verifyResult) {
  if (verifyResult.isNull()) return false;
  if (!_doc.is<JsonObject>()) {
    _doc.to<JsonObject>();
    _doc["schemaVersion"] = SCHEMA_VERSION;
  }

  JsonObject production = _doc["productionNetwork"].to<JsonObject>();
  const bool verified =
      verifyResult["ok"] | verifyResult["provisionReady"] | false;
  production["verified"] = verified;

  if (verifyResult["interface"].is<const char *>()) {
    production["interface"] = verifyResult["interface"].as<const char *>();
  }
  if (verifyResult["ssid"].is<const char *>()) {
    production["ssid"] = verifyResult["ssid"].as<const char *>();
  }
  if (verifyResult["mode"].is<const char *>()) {
    production["mode"] = verifyResult["mode"].as<const char *>();
  }
  if (verifyResult["frequency"].is<int>() ||
      verifyResult["frequency"].is<long>() ||
      verifyResult["frequency"].is<const char *>()) {
    production["frequency"] = verifyResult["frequency"];
  }
  if (verifyResult["channel"].is<int>() || verifyResult["channel"].is<long>()) {
    production["channel"] = verifyResult["channel"];
  } else if (verifyResult["channel"].is<const char *>()) {
    const char *channel = verifyResult["channel"].as<const char *>();
    if (channel && strlen(channel) > 0) {
      const long channelNum = strtol(channel, nullptr, 10);
      if (channelNum > 0) {
        production["channel"] = channelNum;
      } else {
        production["channel"] = channel;
      }
    }
  }
  if (verifyResult["expectedSsid"].is<const char *>()) {
    production["expectedSsid"] = verifyResult["expectedSsid"].as<const char *>();
  }

  const char *reason = verifyResult["reason"] | "ok";
  production["reason"]    = reason;
  production["verifiedAt"] = isoTimestampNow();

  stampSynchronized();
  const bool ok = save();
  if (ok && _logger) {
    _logger->info("router",
                  String("Production network verification cached (reason=") +
                      reason + ", verified=" + (verified ? "true" : "false") +
                      ")");
  }
  return ok;
}

void RouterCacheManager::markProvisioned(const char *status) {
  if (!_doc.is<JsonObject>()) {
    _doc.to<JsonObject>();
  }
  _doc["schemaVersion"]      = SCHEMA_VERSION;
  _doc["provisionStatus"]    = status ? status : "provisioned";
  _doc["provisionTimestamp"] = isoTimestampNow();
  stampSynchronized();
  save();
}

bool RouterCacheManager::fillWireless(JsonDocument &out) const {
  out["ssid"]      = _doc["ssid"] | "";
  out["security"]  = _doc["security"] | "";
  out["interface"] = _doc["wirelessInterface"] | "";
  out["band"]      = _doc["band"] | "";
  out["error"]     = "";
  out["cached"]    = true;

  if (!isPopulated()) {
    out["error"] =
        "Router cache unavailable — complete setup or synchronize router information";
    return false;
  }
  return true;
}

bool RouterCacheManager::fillProfiles(JsonDocument &out) const {
  out["profiles"]       = JsonArray();
  out["profileDetails"] = JsonArray();
  out["error"]          = "";
  out["cached"]         = true;

  if (_doc["profiles"].is<JsonArrayConst>()) {
    JsonArray namesOut = out["profiles"].to<JsonArray>();
    for (JsonVariantConst item : _doc["profiles"].as<JsonArrayConst>()) {
      if (item.is<const char *>() || item.is<String>()) {
        namesOut.add(item.as<const char *>());
      } else if (item.is<JsonObjectConst>()) {
        const char *name = item["name"] | "";
        if (name && strlen(name) > 0) namesOut.add(name);
      }
    }
  }

  if (_doc["profileDetails"].is<JsonArrayConst>()) {
    out["profileDetails"] = _doc["profileDetails"];
  } else if (_doc["profiles"].is<JsonArrayConst>()) {
    // Defensive: synthesize details from names if migration missed a path.
    JsonArray detailsOut = out["profileDetails"].to<JsonArray>();
    for (JsonVariantConst item : out["profiles"].as<JsonArrayConst>()) {
      const char *name = item.as<const char *>();
      if (!name || strlen(name) == 0) continue;
      JsonObject row = detailsOut.createNestedObject();
      row["name"]      = name;
      row["rateLimit"] = "";
    }
  }

  if (_doc["profilesTruncated"] | false) {
    out["truncated"] = true;
  }
  out["stale"] = isStale();
  out["lastSynchronizedAt"] =
      _doc["lastSynchronizedAt"] | _doc["cachedAt"] | "";

  if (!isPopulated()) {
    out["error"] =
        "Router cache unavailable — complete setup or synchronize router information";
    return false;
  }
  return true;
}

void RouterCacheManager::fillCacheStatus(JsonObject out) const {
  out["populated"]           = isPopulated();
  out["identity"]            = _doc["identity"] | "";
  out["routerOsVersion"]     = _doc["routerOsVersion"] | "";
  out["ssid"]                = _doc["ssid"] | "";
  out["security"]            = _doc["security"] | "";
  out["wirelessInterface"]   = _doc["wirelessInterface"] | "";
  out["hotspotProfile"]      = _doc["hotspotProfile"] | "";
  out["band"]                = _doc["band"] | "";
  // Never emit a 1970/unsynced ISO as lastSynchronizedAt.
  if (_doc["syncWallClockValid"] | false) {
    out["lastSynchronizedAt"] = _doc["lastSynchronizedAt"] | _doc["cachedAt"] | "";
  } else {
    out["lastSynchronizedAt"] = "";
  }
  out["provisionStatus"]     = _doc["provisionStatus"] | "";
  out["staleThresholdHours"] = RenzFiConfig::ROUTER_CACHE_STALE_THRESHOLD_HOURS;
  out["stale"]               = isStale();
  out["syncWallClockValid"]  = _doc["syncWallClockValid"] | false;
  if (_doc["lastSynchronizedMillis"].is<uint32_t>()) {
    out["lastSynchronizedMillis"] = _doc["lastSynchronizedMillis"];
    out["cacheAgeSeconds"] = cacheAgeSeconds();
  }
  if (_doc["routerOs"].is<JsonObjectConst>()) {
    out["routerOs"] = _doc["routerOs"];
  }
  if (_doc["productionNetwork"].is<JsonObjectConst>()) {
    out["productionNetwork"] = _doc["productionNetwork"];
  }
  fillObservation(out["observation"].to<JsonObject>());
}

void RouterCacheManager::fillObservation(JsonObject out) const {
  const char *connectivity = "unknown";
  const char *hotspotStatus = "unknown";
  if (_doc["observation"].is<JsonObjectConst>()) {
    JsonObjectConst obs = _doc["observation"].as<JsonObjectConst>();
    connectivity  = obs["connectivity"] | "unknown";
    hotspotStatus = obs["hotspotStatus"] | "unknown";
    out["lastSuccessfulContactAt"] = obs["lastSuccessfulContactAt"] | "";
    out["lastContactAttemptAt"]    = obs["lastContactAttemptAt"] | "";
    out["lastContactError"]        = obs["lastContactError"] | "";
    out["hotspotServer"]           = obs["hotspotServer"] | "";
    out["hotspotInterface"]        = obs["hotspotInterface"] | "";
    if (obs["wan"].is<JsonObjectConst>()) {
      out["wan"] = obs["wan"];
    }
  }
  out["connectivity"]  = connectivity;
  out["hotspotStatus"] = hotspotStatus;
}

bool RouterCacheManager::fillPublic(JsonDocument &out) const {
  if (!isPopulated()) {
    out["error"]     = "Router cache unavailable";
    out["populated"] = false;
    fillCacheStatus(out.to<JsonObject>());
    return false;
  }

  JsonObject obj = out.to<JsonObject>();
  obj["populated"]           = true;
  obj["routerIp"]            = _doc["routerIp"] | "";
  obj["identity"]            = _doc["identity"] | "";
  obj["routerOsVersion"]     = _doc["routerOsVersion"] | "";
  obj["wirelessInterface"]   = _doc["wirelessInterface"] | "";
  obj["ssid"]                = _doc["ssid"] | "";
  obj["security"]            = _doc["security"] | "";
  obj["bridge"]              = _doc["bridge"] | "";
  obj["hotspotServer"]       = _doc["hotspotServer"] | "";
  obj["hotspotProfile"]      = _doc["hotspotProfile"] | "";
  obj["htmlDirectory"]       = _doc["htmlDirectory"] | "";
  obj["provisionTimestamp"]  = _doc["provisionTimestamp"] | "";
  obj["provisionStatus"]     = _doc["provisionStatus"] | "";
  obj["lastSynchronizedAt"]  = _doc["lastSynchronizedAt"] | _doc["cachedAt"] | "";
  obj["cachedAt"]            = obj["lastSynchronizedAt"];
  obj["cacheAgeSeconds"]     = cacheAgeSeconds();
  obj["staleThresholdHours"] = RenzFiConfig::ROUTER_CACHE_STALE_THRESHOLD_HOURS;
  obj["stale"]               = isStale();
  if (_doc["routerOs"].is<JsonObjectConst>()) {
    obj["routerOs"] = _doc["routerOs"];
  }
  if (_doc["profiles"].is<JsonArrayConst>()) {
    obj["profiles"] = _doc["profiles"];
  }
  if (_doc["profileDetails"].is<JsonArrayConst>()) {
    obj["profileDetails"] = _doc["profileDetails"];
  }
  if (_doc["productionNetwork"].is<JsonObjectConst>()) {
    obj["productionNetwork"] = _doc["productionNetwork"];
  }
  fillObservation(obj["observation"].to<JsonObject>());
  return true;
}

void RouterCacheManager::fillStatusMetadata(JsonObject out) const {
  if (!isPopulated()) return;

  fillCacheStatus(out);

  out["version"]  = _doc["routerOsVersion"] | "";
  out["ssid"]     = _doc["ssid"] | "";
  out["cachedAt"] = _doc["lastSynchronizedAt"] | _doc["cachedAt"] | "";

  if (_doc["routerOs"].is<JsonObjectConst>()) {
    JsonObject routerOs = out["routerOs"].to<JsonObject>();
    routerOs["version"]     = _doc["routerOs"]["version"] | _doc["routerOsVersion"] | "";
    routerOs["cpuLoad"]     = _doc["routerOs"]["cpuLoad"] | "";
    routerOs["freeMemory"]  = _doc["routerOs"]["freeMemory"] | "";
    routerOs["totalMemory"] = _doc["routerOs"]["totalMemory"] | "";
    routerOs["uptime"]      = _doc["routerOs"]["uptime"] | "";
  }
}
