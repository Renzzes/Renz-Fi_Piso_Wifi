#include "RouterPlatform.h"

#include <cstring>

#include "Config.h"
#include "EventBus.h"
#include "Logger.h"
#include "RouterCacheManager.h"
#include "RouterDriverManifest.h"
#include "RouterEvents.h"
#include "RouterWirelessAdapter.h"
#include "StorageManager.h"
#include "StoragePaths.h"

void RouterPlatform::begin(StorageManager *storage, Logger *logger,
                           EventBus *events) {
  _storage = storage;
  _logger  = logger;
  _events  = events;

  registerBuiltInDrivers();
  selectActiveDriver();
}

void RouterPlatform::registerBuiltInDrivers() {
  _driverCount = 0;
  registerDriver(&_mikrotikDriver);
  registerDriver(&_genericAPDriver);
  registerDriver(&_tplinkDriver);
  registerDriver(&_ruijieDriver);
  registerDriver(&_openwrtDriver);
}

bool RouterPlatform::registerDriver(IRouterDriver *driver) {
  if (!driver || _driverCount >= kMaxDrivers) return false;

  for (size_t i = 0; i < _driverCount; ++i) {
    if (_drivers[i] == driver) return true;
  }

  driver->begin(_storage, _logger, _events);
  _drivers[_driverCount++] = driver;
  return true;
}

String RouterPlatform::readDriverType() const {
  DynamicJsonDocument stored(RenzFiConfig::JSON_DOC_SMALL);
  if (!_storage || !_storage->readJson(RenzFiConfig::ROUTER_FILE, stored)) {
    return "mikrotik";
  }
  const String driverType = stored["driverType"] | "mikrotik";
  return driverType.length() > 0 ? driverType : String("mikrotik");
}

IRouterDriver *RouterPlatform::findDriver(const String &driverType) const {
  for (size_t i = 0; i < _driverCount; ++i) {
    if (driverType.equalsIgnoreCase(_drivers[i]->driverId())) {
      return _drivers[i];
    }
  }
  return nullptr;
}

bool RouterPlatform::selectActiveDriver() {
  const String requested = readDriverType();
  IRouterDriver *next    = findDriver(requested);

  if (!next) {
    if (_logger) {
      _logger->warn("router",
                    "Unknown driverType '" + requested + "' — falling back to mikrotik");
    }
    next = findDriver("mikrotik");
    emitRouterEvent(RouterEvents::Unavailable,
                    String("{\"requested\":\"") + requested + "\"}");
  }

  if (_active != next) {
    if (_active) _active->disconnect();
    _active = next;
    emitCapabilitiesChanged();
    emitProfileUpdated();
  }

  return _active != nullptr;
}

IRouterDriver *RouterPlatform::activeDriver() { return _active; }

const IRouterDriver *RouterPlatform::activeDriver() const { return _active; }

size_t RouterPlatform::availableDrivers(JsonArray &out) const {
  out.clear();
  for (size_t i = 0; i < _driverCount; ++i) {
    JsonObject entry = out.add<JsonObject>();
    _drivers[i]->manifest().toJson(entry);
    entry["active"] = (_drivers[i] == _active);
  }
  return _driverCount;
}

RouterDriverManifest RouterPlatform::driverManifest() const {
  if (!_active) return RouterDriverManifest{};
  return _active->manifest();
}

RouterDriverManifest RouterPlatform::driverManifest(const String &driverId) const {
  const IRouterDriver *driver = findDriver(driverId);
  if (!driver) return RouterDriverManifest{};
  return driver->manifest();
}

bool RouterPlatform::switchDriver(const String &driverId) {
  IRouterDriver *driver = findDriver(driverId);
  if (!driver) {
    if (_logger) {
      _logger->warn("router", "switchDriver failed — unknown driver: " + driverId);
    }
    return false;
  }

  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_SMALL);
  if (_storage && _storage->readJson(RenzFiConfig::ROUTER_FILE, doc)) {
    doc["driverType"] = driverId;
  } else {
    doc["driverType"] = driverId;
    doc["host"]       = "10.40.0.1";
    doc["profile"]    = "default";
  }

  if (!_storage || !_storage->writeJson(RenzFiConfig::ROUTER_FILE, doc)) {
    return false;
  }

  if (_logger) {
    _logger->info("router", "Switched active router driver to " + driverId);
  }

  return selectActiveDriver();
}

bool RouterPlatform::detectDrivers(JsonDocument &out) const {
  JsonArray drivers = out["drivers"].to<JsonArray>();
  for (size_t i = 0; i < _driverCount; ++i) {
    JsonObject entry = drivers.add<JsonObject>();
    _drivers[i]->detect(entry);
    entry["active"] = (_drivers[i] == _active);
  }
  out["count"] = _driverCount;
  return _driverCount > 0;
}

bool RouterPlatform::isFirmwareSupported(const String &firmware,
                                         const String &version,
                                         String &reasonOut) const {
  if (!_active) {
    reasonOut = "No active router driver";
    return false;
  }

  const RouterDriverManifest manifest = _active->manifest();
  if (manifest.isSupported(firmware, version)) {
    reasonOut = "";
    return true;
  }

  reasonOut = manifest.unsupportedReason(firmware, version);
  return false;
}

RouterProfile RouterPlatform::profile() const {
  if (!_active) {
    RouterProfile p;
    p.status = "unavailable";
    return p;
  }
  return _active->profile();
}

RouterCapabilities RouterPlatform::capabilities() const {
  if (!_active) return RouterCapabilities::none();
  return _active->capabilities();
}

bool RouterPlatform::load(JsonDocument &doc) {
  if (!_active) return false;
  return _active->loadSettings(doc);
}

bool RouterPlatform::save(JsonObjectConst settings) {
  if (!_active) return false;
  const bool ok = _active->saveSettings(settings);
  if (ok) {
    _healthHost = settings["host"] | "";
    _healthConfigured = _healthHost.length() > 0;
    _healthCacheLoaded = true;
    _lastHealthCacheMs = millis();
    selectActiveDriver();
    emitProfileUpdated();
    // saveSettings is local SD only (RouterOS API credentials). Do NOT open a
    // live RouterOS session via refreshRouterCache here — that reconnect can
    // hit connect cooldown and must never run under a blocked AsyncTCP path.
    // Owners validate credentials with Test Connection / Synchronize.
  }
  return ok;
}

bool RouterPlatform::fillPublicSettings(JsonDocument &doc) const {
  if (!_active) return false;
  return _active->fillPublicSettings(doc);
}

bool RouterPlatform::test(JsonObjectConst overrideSettings, JsonDocument &out) {
  if (!_active) {
    out["error"] = "No active router driver";
    return false;
  }

  const bool ok = _active->testSettings(overrideSettings, out);
  const bool authenticated = out["authenticated"] | false;
  const bool connected     = out["connected"] | false;

  if (_cache) {
    // Prefer JSON_DOC_MEDIUM so profileDetails from Test fit in one snapshot.
    DynamicJsonDocument snap(RenzFiConfig::JSON_DOC_MEDIUM);
    bool haveFields = false;

    // Prefer live session host from testSettings; fall back to stored settings.
    String routerIp = out["routerIp"].as<String>();
    String hotspotProfile = out["hotspotProfile"].as<String>();
    if (routerIp.isEmpty() || hotspotProfile.isEmpty()) {
      DynamicJsonDocument settings(RenzFiConfig::JSON_DOC_SMALL);
      if (load(settings)) {
        if (routerIp.isEmpty()) routerIp = settings["host"].as<String>();
        if (hotspotProfile.isEmpty()) {
          hotspotProfile = settings["profile"].as<String>();
        }
      }
    }
    if (routerIp.length() > 0) {
      snap["routerIp"] = routerIp;
      haveFields       = true;
    }
    if (hotspotProfile.length() > 0) {
      snap["hotspotProfile"] = hotspotProfile;
    }

    const String identity = out["identity"].as<String>();
    if (identity.length() > 0) {
      snap["identity"] = identity;
      haveFields       = true;
    }
    if (out["routerOs"].is<JsonObjectConst>()) {
      snap["routerOs"] = out["routerOs"];
      const String version = out["routerOs"]["version"].as<String>();
      if (version.length() > 0) {
        snap["routerOsVersion"] = version;
      }
      haveFields = true;
    }
    if (out["profiles"].is<JsonArrayConst>()) {
      snap["profiles"] = out["profiles"];
      haveFields       = true;
    }
    if (out["profileDetails"].is<JsonArrayConst>()) {
      snap["profileDetails"] = out["profileDetails"];
      haveFields             = true;
    }
    if (out["truncated"] | false) {
      snap["truncated"] = true;
    } else if (!out["profiles"].isNull() || !out["profileDetails"].isNull()) {
      snap["truncated"] = false;
    }
    if (out["observation"].is<JsonObjectConst>()) {
      snap["observation"] = out["observation"];
      haveFields          = true;
    }

    // Prefer live Test wireless fields from the same RouterOS session.
    // Canonical SSID is fallback only — security MUST come from live classify
    // (none/open → Open UI; unknown on failed query; never invent Open).
    const String liveSsid  = out["ssid"].as<String>();
    const String liveSec   = out["security"].as<String>();
    const String liveIface = out["wirelessInterface"].as<String>();
    const String liveBand  = out["band"].as<String>();
    if (liveSsid.length() > 0) {
      snap["ssid"] = liveSsid;
      haveFields   = true;
    }
    if (liveSec.length() > 0) {
      snap["security"] = liveSec;
      haveFields       = true;
    }
    if (liveIface.length() > 0) {
      snap["wirelessInterface"] = liveIface;
    }
    if (liveBand.length() > 0) {
      snap["band"] = liveBand;
    }

    // Fallback SSID/interface from local canonical wireless config when Test
    // did not observe wireless (no extra RouterOS session).
    if (_storage && (liveSsid.isEmpty() || liveIface.isEmpty())) {
      RouterWireless::CanonicalConfig canonical;
      if (RouterWireless::loadCanonicalConfig(_storage, canonical) &&
          canonical.configured) {
        if (liveSsid.isEmpty() && !canonical.ssid.isEmpty()) {
          snap["ssid"] = canonical.ssid;
          haveFields   = true;
        }
        if (liveIface.isEmpty() && !canonical.interfaceId.isEmpty()) {
          snap["wirelessInterface"] = canonical.interfaceId;
        }
      }
    }

    if (ok && haveFields) {
      // Do NOT call refreshRouterCache() — same-session data only.
      _cache->applyLiveSnapshot(snap.as<JsonObjectConst>());
    } else if (connected || authenticated) {
      // Router reachable; Hotspot/profile may have failed.
      if (haveFields) {
        _cache->applyLiveSnapshot(snap.as<JsonObjectConst>());
      } else if (out["observation"].is<JsonObjectConst>()) {
        _cache->applyObservation(out["observation"].as<JsonObjectConst>());
      }
    } else if (out["observation"].is<JsonObjectConst>()) {
      _cache->applyObservation(out["observation"].as<JsonObjectConst>());
    }
  }

  if (ok) {
    emitRouterEvent(RouterEvents::Connected, "{}");
    emitProfileUpdated();
  } else if (connected && !authenticated) {
    emitRouterEvent(RouterEvents::AuthenticationFailed, "{}");
  } else if (!connected) {
    emitRouterEvent(RouterEvents::Disconnected, "{}");
  }
  return ok;
}

bool RouterPlatform::listProfiles(JsonDocument &out) {
  out["profiles"] = JsonArray();
  out["error"]    = "";

  if (_cache && _cache->isPopulated()) {
    return _cache->fillProfiles(out);
  }

  if (!_active) {
    out["error"] = "No active router driver";
    return false;
  }
  out["error"] =
      "Router cache unavailable — complete setup or synchronize router information";
  return false;
}

bool RouterPlatform::adminUserProfileOp(JsonObjectConst request, JsonDocument &out) {
  out["ok"]    = false;
  out["error"] = "";

  const String action = request["action"].as<String>();
  if (action.isEmpty()) {
    out["error"] = "action is required";
    return false;
  }

  if (!_active) {
    out["error"] = "No active router driver";
    return false;
  }

  auto applyProfilesToCache = [this](JsonObjectConst live) {
    if (!_cache) return;
    DynamicJsonDocument snap(RenzFiConfig::JSON_DOC_MEDIUM);
    if (live["profiles"].is<JsonArrayConst>()) {
      snap["profiles"] = live["profiles"];
    }
    if (live["profileDetails"].is<JsonArrayConst>()) {
      snap["profileDetails"] = live["profileDetails"];
    }
    if (live["truncated"] | false) {
      snap["truncated"] = true;
    }
    DynamicJsonDocument settings(RenzFiConfig::JSON_DOC_SMALL);
    if (load(settings)) {
      const String host = settings["host"].as<String>();
      if (host.length() > 0) snap["routerIp"] = host;
      const String profile = settings["profile"].as<String>();
      if (profile.length() > 0) snap["hotspotProfile"] = profile;
    }
    // Preserve identity/ssid already in cache via merge (as<> not wipe).
    _cache->applyLiveSnapshot(snap.as<JsonObjectConst>());
  };

  if (action == "refresh") {
    DynamicJsonDocument live(RenzFiConfig::JSON_DOC_MEDIUM);
    if (!_active->listProfiles(live)) {
      out["error"] = live["error"] | "Failed to refresh profiles";
      return false;
    }
    applyProfilesToCache(live.as<JsonObjectConst>());
    out.set(live.as<JsonObject>());
    out["ok"]     = true;
    out["cached"] = true;
    return true;
  }

  if (strcmp(_active->driverId(), "mikrotik") != 0) {
    out["error"] = "Profile mutations require a MikroTik router";
    return false;
  }

  if (action == "set-rate-limit") {
    const String name      = request["name"].as<String>();
    const String rateLimit = request["rateLimit"].as<String>();
    if (!_mikrotikDriver.setUserProfileRateLimit(name, rateLimit, out)) {
      return false;
    }
    // Patch cache entry locally; refresh names list without a second session
    // by merging the verified row into an in-memory rebuild from current cache.
    if (_cache && _cache->isPopulated()) {
      DynamicJsonDocument cached(RenzFiConfig::JSON_DOC_MEDIUM);
      _cache->fillProfiles(cached);
      JsonArray details = cached["profileDetails"].as<JsonArray>();
      bool found = false;
      for (JsonObject row : details) {
        if (String(row["name"] | "") == name) {
          row["rateLimit"] = out["rateLimit"].as<String>();
          found            = true;
          break;
        }
      }
      if (!found) {
        JsonObject row     = details.createNestedObject();
        row["name"]        = name;
        row["rateLimit"]   = out["rateLimit"].as<String>();
        cached["profiles"].as<JsonArray>().add(name);
      }
      applyProfilesToCache(cached.as<JsonObjectConst>());
    }
    return true;
  }

  if (action == "ensure-managed") {
    const uint16_t downloadMbps =
        static_cast<uint16_t>(request["downloadMbps"] | 0);
    const uint16_t uploadMbps =
        static_cast<uint16_t>(request["uploadMbps"] | 0);
    if (!_mikrotikDriver.ensureManagedSpeedProfile(downloadMbps, uploadMbps,
                                                   out)) {
      return false;
    }
    // One bounded refresh of profile inventory in the same worker job would
    // open a second session — instead upsert the managed row into cache.
    if (_cache) {
      DynamicJsonDocument cached(RenzFiConfig::JSON_DOC_MEDIUM);
      if (_cache->isPopulated()) {
        _cache->fillProfiles(cached);
      } else {
        cached.to<JsonObject>();
        cached["profiles"]       = JsonArray();
        cached["profileDetails"] = JsonArray();
      }
      const String name      = out["name"].as<String>();
      const String rateLimit = out["rateLimit"].as<String>();
      JsonArray details      = cached["profileDetails"].as<JsonArray>();
      JsonArray names        = cached["profiles"].as<JsonArray>();
      bool found             = false;
      for (JsonObject row : details) {
        if (String(row["name"] | "") == name) {
          row["rateLimit"] = rateLimit;
          found            = true;
          break;
        }
      }
      if (!found) {
        JsonObject row   = details.createNestedObject();
        row["name"]      = name;
        row["rateLimit"] = rateLimit;
        names.add(name);
      }
      applyProfilesToCache(cached.as<JsonObjectConst>());
    }
    return true;
  }

  out["error"] = "Unknown profile action";
  return false;
}

bool RouterPlatform::fillWireless(JsonDocument &out) {
  out["ssid"]     = "";
  out["security"] = "";
  out["error"]    = "";

  RouterWireless::CanonicalConfig canonical;
  if (_storage) {
    RouterWireless::loadCanonicalConfig(_storage, canonical);
  }

  if (_cache && _cache->isPopulated()) {
    const bool ok = _cache->fillWireless(out);
    if (canonical.configured) {
      RouterWireless::fillWirelessApiJson(canonical, out.as<JsonObject>());
      if (!canonical.ssid.isEmpty() && out["ssid"].as<String>().isEmpty()) {
        out["ssid"] = canonical.ssid;
      }
      if (!canonical.interfaceId.isEmpty()) {
        out["interface"] = canonical.interfaceId;
      }
      if (!canonical.password.isEmpty() && !out.containsKey("password")) {
        out["password"] = canonical.password;
      }
    }
    return ok;
  }

  if (canonical.configured) {
    RouterWireless::fillWirelessApiJson(canonical, out.as<JsonObject>());
    if (!canonical.ssid.isEmpty()) out["ssid"] = canonical.ssid;
    if (!canonical.interfaceId.isEmpty()) out["interface"] = canonical.interfaceId;
    if (!canonical.password.isEmpty()) out["password"] = canonical.password;
    out["security"] = "wpa2-psk";
  }

  out["error"] =
      "Router cache unavailable — complete setup or synchronize router information";
  return false;
}

bool RouterPlatform::saveWireless(JsonObjectConst settings, JsonDocument &out) {
  out["ssid"]     = "";
  out["security"] = "";
  out["error"]    = "";

  if (!_active) {
    out["error"] = "No active router driver";
    return false;
  }

  const bool ok = _active->saveWireless(settings, out);
  if (ok && _cache) {
    // SET ACK → patch SSID locally. Do not open a second RouterOS session.
    // Empty security in `out` is skipped by copyStringField (preserves Open/etc).
    _cache->applyWirelessFields(out.as<JsonObjectConst>());
  }
  return ok;
}

bool RouterPlatform::refreshRouterCache(bool markProvisioned) {
  _lastCollectError = "";
  if (!_cache || !_active) return false;

  DynamicJsonDocument snap(RenzFiConfig::JSON_DOC_MEDIUM);
  if (!_active->collectCacheSnapshot(snap,
                                     RouterCacheCollectMode::Configuration)) {
    _lastCollectError = snap["error"] | "probe failed";
    if (_logger) {
      _logger->warn("router",
                    String("Router cache sync failed: ") + _lastCollectError);
    }
    return false;
  }

  if (markProvisioned) {
    snap["provisionStatus"] = "provisioned";
  }

  // applyLiveSnapshot already persists + leaves _doc authoritative.
  // Do NOT reload from storage afterward — a failed/partial read would clear
  // in-memory state after a successful sync (false 503).
  const bool ok = _cache->applyLiveSnapshot(snap.as<JsonObjectConst>());
  if (ok && markProvisioned) {
    _cache->markProvisioned("provisioned");
  }
  return ok;
}

bool RouterPlatform::synchronizeRouterCache(bool markProvisioned) {
  return refreshRouterCache(markProvisioned);
}

bool RouterPlatform::refreshRouterTelemetry() {
  _lastCollectError = "";
  if (!_cache || !_active) return false;

  DynamicJsonDocument snap(RenzFiConfig::JSON_DOC_MEDIUM);
  if (!_active->collectCacheSnapshot(snap, RouterCacheCollectMode::Telemetry)) {
    _lastCollectError = snap["error"] | "probe failed";
    if (_logger) {
      _logger->warn("router",
                    String("Router telemetry refresh failed: ") +
                        _lastCollectError);
    }
    // Do not wipe prior valid configuration when telemetry fails.
    return false;
  }

  return _cache->applyLiveSnapshot(snap.as<JsonObjectConst>());
}

bool RouterPlatform::fillRouterCache(JsonDocument &out) const {
  if (!_cache) {
    out["populated"] = false;
    out["error"]     = "Router cache unavailable";
    return false;
  }
  return _cache->fillPublic(out);
}

bool RouterPlatform::cachePopulated() const {
  return _cache && _cache->isPopulated();
}

void RouterPlatform::fillRouterCacheStatus(JsonObject out) const {
  if (!_cache) {
    out["populated"] = false;
    return;
  }
  _cache->fillCacheStatus(out);
}

bool RouterPlatform::provisionHotspotUser(const HotspotUser &user) {
  if (!_active) return false;
  return _active->authorizeUser(user);
}

bool RouterPlatform::lastActivateAuthTrace(ActivateAuthTrace &out) const {
  if (!_active) return false;
  return _active->lastActivateAuthTrace(out);
}

bool RouterPlatform::disconnectHotspotUser(const String &mac) {
  if (!_active) return false;
  return _active->deauthorizeUser(mac);
}

bool RouterPlatform::pauseHotspotUser(const String &mac) {
  if (!_active) return false;
  return _active->pauseHotspotUser(mac);
}

bool RouterPlatform::queryHotspotActivePresent(const String &mac,
                                               bool &presentOut) {
  presentOut = false;
  if (!_active) return false;
  return _active->queryHotspotActivePresent(mac, presentOut);
}

bool RouterPlatform::probeApiReady() {
  if (!_active) return false;
  return _active->probeApiReady();
}

String RouterPlatform::lastHotspotError() const {
  if (!_active) return String("No active router driver");
  return _active->lastHotspotError();
}

bool RouterPlatform::assignProfile(const String &username, const String &profile) {
  if (!_active) return false;
  return _active->assignProfile(username, profile);
}

bool RouterPlatform::activateProductionNetwork(JsonDocument &result) {
  result["ok"]             = false;
  result["provisionReady"] = false;
  result["reason"]         = "api-failure";
  result["error"]          = "";
  if (!_active) {
    result["error"] = "No active router driver";
    return false;
  }
  return _active->activateProductionNetwork(result);
}

bool RouterPlatform::activateProductionNetworkForFinish(
    JsonDocument &result, RouterOsClient &session, bool sessionReused,
    bool reconnected) {
  result["ok"]             = false;
  result["provisionReady"] = false;
  result["reason"]         = "api-failure";
  result["error"]          = "";
  if (!_active) {
    result["error"] = "No active router driver";
    return false;
  }
  if (strcmp(_active->driverId(), "mikrotik") != 0) {
    result["error"] = "Finish production activation requires MikroTik driver";
    return false;
  }
  return static_cast<MikroTikDriver *>(_active)->activateProductionNetworkForFinish(
      result, session, sessionReused, reconnected);
}

bool RouterPlatform::productionNetworkActive(JsonDocument &result) {
  result["ok"]             = false;
  result["provisionReady"] = false;
  result["reason"]         = "api-failure";
  result["error"]          = "";
  if (!_active) {
    result["error"] = "No active router driver";
    return false;
  }
  return _active->productionNetworkActive(result);
}

bool RouterPlatform::saveProductionNetworkVerification(
    JsonObjectConst verifyResult) {
  if (!_cache) return false;
  return _cache->applyProductionNetworkVerification(verifyResult);
}

bool RouterPlatform::persistFinishRouterCache(JsonObjectConst verifyResult) {
  if (!_cache) return false;
  if (!saveProductionNetworkVerification(verifyResult)) return false;
  _cache->markProvisioned("provisioned");
  return true;
}

void RouterPlatform::emitRouterEvent(const char *event, const String &json) {
  if (_events) _events->emit(event, json);
}

void RouterPlatform::emitProfileUpdated() {
  if (!_active || !_events) return;
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_SMALL);
  JsonObject obj = doc.to<JsonObject>();
  profile().toJson(obj);
  emitRouterEvent(RouterEvents::ProfileUpdated, doc.as<String>());
}

void RouterPlatform::emitCapabilitiesChanged() {
  if (!_active || !_events) return;
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_SMALL);
  JsonObject obj = doc.to<JsonObject>();
  capabilities().toJson(obj);
  emitRouterEvent(RouterEvents::CapabilitiesChanged, doc.as<String>());
}

void RouterPlatform::refreshHealthCache() {
  if (!_active) {
    _healthConfigured = false;
    _healthHost = "";
    _healthCacheLoaded = true;
    _lastHealthCacheMs = millis();
    return;
  }

  // Called every 2s from FirmwareApp::refreshHealthSnapshots. load() →
  // readJson(ROUTER_FILE) under STORAGE_LOCK on that cadence starves async_tcp
  // on shared CPU1 during Admin login/dashboard HTTP storms.
  const uint32_t now = millis();
  if (_healthCacheLoaded &&
      (now - _lastHealthCacheMs) <
          RenzFiConfig::STORAGE_SNAPSHOT_HEAVY_INTERVAL_MS) {
    return;
  }

  DynamicJsonDocument routerDoc(RenzFiConfig::JSON_DOC_SMALL);
  if (load(routerDoc)) {
    _healthHost = routerDoc["host"] | "";
    _healthConfigured = _healthHost.length() > 0;
  } else {
    _healthConfigured = false;
    _healthHost = "";
  }
  _healthCacheLoaded = true;
  _lastHealthCacheMs = now;
}

void RouterPlatform::fillHealthStatus(JsonObject routerObj) const {
  routerObj["configured"] = _healthConfigured;
  if (_healthHost.length() > 0) routerObj["host"] = _healthHost;
  if (_cache && _cache->isPopulated()) {
    _cache->fillStatusMetadata(routerObj);
  }
  if (_active) {
    const RouterDriverManifest manifest = driverManifest();
    routerObj["driverId"] = manifest.driverId;
    JsonObject product = routerObj["product"].to<JsonObject>();
    if (manifest.productName && strlen(manifest.productName) > 0) {
      product["name"] = manifest.productName;
    } else if (manifest.vendor && strlen(manifest.vendor) > 0) {
      product["name"] = manifest.vendor;
    }
    if (manifest.productSubtitle && strlen(manifest.productSubtitle) > 0) {
      product["subtitle"] = manifest.productSubtitle;
    }
    manifest.capabilities.toHealthJson(
        routerObj["capabilities"].to<JsonObject>());
    routerObj["status"] = _healthConfigured ? "connected" : "detected";
  } else {
    routerObj["driverId"] = nullptr;
    routerObj["status"] = "unavailable";
  }
}
