#include "GenericAPDriver.h"

#include "Config.h"
#include "EventBus.h"
#include "Logger.h"
#include "../RouterDriverManifest.h"
#include "StorageManager.h"

void GenericAPDriver::begin(StorageManager *storage, Logger *logger,
                            EventBus *events) {
  _storage = storage;
  _logger  = logger;
  _events  = events;
  refreshConfigured();
}

RouterCapabilities GenericAPDriver::capabilities() const {
  return manifest().capabilities;
}

RouterDriverManifest GenericAPDriver::manifest() const {
  static const char *kFeatures[] = {
      "passive_gateway",
      "appliance_side_sessions",
      "gateway_health_check",
      nullptr,
  };

  RouterDriverManifest m;
  m.driverId          = driverId();
  m.vendor            = vendorName();
  m.model             = "";
  m.supportedFirmware = "";
  m.minimumVersion    = "";
  m.capabilities      = RouterCapabilities{};
  m.capabilities.supportsHealth = true;
  m.supportedFeatures             = kFeatures;
  m.supportedFeatureCount         = 3;
  m.stability                     = DriverStability::Stable;
  m.documentationUrl              = "";
  m.driverVersion                 = "1.0.0";
  return m;
}

RouterProfile GenericAPDriver::profile() const {
  RouterProfile p;
  p.vendor         = vendorName();
  p.driverId       = driverId();
  p.connectionType = "passive-gateway";
  p.status         = _configured ? "configured" : "unconfigured";
  p.capabilities   = capabilities();

  DynamicJsonDocument stored(RenzFiConfig::JSON_DOC_SMALL);
  if (_storage && _storage->readJson(RenzFiConfig::ROUTER_FILE, stored)) {
    p.ipAddress = stored["host"] | "";
    p.model     = stored["ssid"] | "";
  }
  return p;
}

bool GenericAPDriver::refreshConfigured() {
  DynamicJsonDocument stored(RenzFiConfig::JSON_DOC_SMALL);
  if (!_storage || !_storage->readJson(RenzFiConfig::ROUTER_FILE, stored)) {
    _configured = false;
    return false;
  }
  const String host = stored["host"] | "";
  _configured       = host.length() > 0;
  return _configured;
}

bool GenericAPDriver::loadSettings(JsonDocument &doc) {
  return _storage && _storage->readJson(RenzFiConfig::ROUTER_FILE, doc);
}

bool GenericAPDriver::saveSettings(JsonObjectConst settings) {
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_SMALL);
  loadSettings(doc);
  doc["driverType"] = driverId();
  doc["host"]       = settings["host"] | doc["host"] | "10.40.0.1";
  if (!settings["ssid"].isNull()) {
    doc["ssid"] = settings["ssid"].as<const char *>();
  }
  if (settings["wifiPassword"].is<const char *>()) {
    const char *nextWifiPassword = settings["wifiPassword"].as<const char *>();
    if (nextWifiPassword && strlen(nextWifiPassword) > 0) {
      doc["wifiPassword"] = nextWifiPassword;
    }
  }
  bool ok = _storage && _storage->writeJson(RenzFiConfig::ROUTER_FILE, doc);
  if (ok) refreshConfigured();
  return ok;
}

bool GenericAPDriver::fillPublicSettings(JsonDocument &doc) const {
  DynamicJsonDocument stored(RenzFiConfig::JSON_DOC_SMALL);
  if (!_storage || !_storage->readJson(RenzFiConfig::ROUTER_FILE, stored)) {
    return false;
  }
  doc["driverType"]   = stored["driverType"] | driverId();
  doc["host"]         = stored["host"] | "10.40.0.1";
  doc["ssid"]         = stored["ssid"] | "RenzFi_PesoWifi";
  doc["wifiPassword"] = stored["wifiPassword"] | "";
  doc["profile"]      = "default";
  doc["username"]     = "";
  doc["passwordConfigured"] = false;
  return true;
}

bool GenericAPDriver::connect(String &errorOut) {
  if (!refreshConfigured()) {
    errorOut = "Generic AP gateway address is not configured";
    return false;
  }
  errorOut = "";
  return true;
}

void GenericAPDriver::disconnect() {}

bool GenericAPDriver::healthCheck(JsonDocument &out) {
  out["ok"]        = _configured;
  out["connected"] = _configured;
  out["error"]     = _configured ? "" : "Gateway not configured";
  return _configured;
}

bool GenericAPDriver::testSettings(JsonObjectConst overrideSettings,
                                   JsonDocument &out) {
  out["connected"]     = false;
  out["authenticated"] = false;
  out["profileFound"]  = true;
  out["identity"]      = "Generic AP";
  out["error"]         = "";
  out["ok"]            = false;

  DynamicJsonDocument settings(RenzFiConfig::JSON_DOC_SMALL);
  loadSettings(settings);
  if (!overrideSettings.isNull() && !overrideSettings["host"].isNull()) {
    settings["host"] = overrideSettings["host"].as<const char *>();
  }

  const String host = settings["host"] | "";
  if (host.isEmpty()) {
    out["error"] = "Gateway IP is not configured";
    return false;
  }

  out["connected"]     = true;
  out["authenticated"] = true;
  out["ok"]            = true;
  return true;
}

bool GenericAPDriver::listProfiles(JsonDocument &out) {
  out["profiles"] = JsonArray();
  out["profiles"].add("default");
  out["error"] = "";
  return true;
}

bool GenericAPDriver::authorizeUser(const HotspotUser &user) {
  (void)user;
  if (_logger) {
    _logger->info("router",
                  "Generic AP: session authorized on appliance (no router API)");
  }
  return true;
}

bool GenericAPDriver::deauthorizeUser(const String &mac) {
  (void)mac;
  if (_logger) {
    _logger->info("router",
                  "Generic AP: session deauthorized on appliance (no router API)");
  }
  return true;
}

bool GenericAPDriver::assignProfile(const String &username, const String &profile) {
  (void)username;
  (void)profile;
  return true;
}

bool GenericAPDriver::activateProductionNetwork(JsonDocument &result) {
  result["ok"]             = true;
  result["provisionReady"] = true;
  result["reason"]         = "ok";
  result["error"]          = "";
  return true;
}

bool GenericAPDriver::productionNetworkActive(JsonDocument &result) {
  result["ok"]             = true;
  result["provisionReady"] = true;
  result["reason"]         = "ok";
  result["error"]          = "";
  return true;
}

bool GenericAPDriver::fillStatistics(JsonDocument &out) {
  out["activeSessions"] = 0;
  out["error"]          = "Statistics not available for Generic AP";
  return false;
}

void GenericAPDriver::detect(JsonObject out) const {
  out["driverId"] = driverId();

  DynamicJsonDocument stored(RenzFiConfig::JSON_DOC_SMALL);
  const bool configured =
      _storage && _storage->readJson(RenzFiConfig::ROUTER_FILE, stored) &&
      String(stored["host"] | "").length() > 0;

  out["detected"]   = false;
  out["configured"] = configured;
  out["confidence"] = configured ? "configured" : "none";
  out["reason"]     = configured
                          ? "Passive gateway configured; no router API to probe"
                          : "Gateway IP not configured";
  JsonObject manifestObj = out["manifest"].to<JsonObject>();
  manifest().toJson(manifestObj);
}
