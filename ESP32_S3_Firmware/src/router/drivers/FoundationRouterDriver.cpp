#include "FoundationRouterDriver.h"

#include "Config.h"
#include "EventBus.h"
#include "JsonHeap.h"
#include "Logger.h"
#include "StorageManager.h"

FoundationRouterDriver::FoundationRouterDriver(const RouterDriverManifest &manifest)
    : _manifest(manifest) {}

void FoundationRouterDriver::begin(StorageManager *storage, Logger *logger,
                                   EventBus *events) {
  _storage = storage;
  _logger  = logger;
  _events  = events;
}

RouterProfile FoundationRouterDriver::profile() const {
  RouterProfile p;
  p.vendor         = _manifest.vendor;
  p.driverId       = _manifest.driverId;
  p.connectionType = "foundation";
  p.status         = "unavailable";
  p.capabilities   = _manifest.capabilities;
  return p;
}

bool FoundationRouterDriver::loadSettings(JsonDocument &doc) {
  return _storage && _storage->readJson(RenzFiConfig::ROUTER_FILE, doc);
}

bool FoundationRouterDriver::saveSettings(JsonObjectConst settings) {
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_SMALL);
  loadSettings(doc);
  doc["driverType"] = _manifest.driverId;
  if (!settings["host"].isNull()) {
    doc["host"] = settings["host"].as<const char *>();
  }
  if (!settings["username"].isNull()) {
    doc["username"] = settings["username"].as<const char *>();
  }
  if (settings["password"].is<const char *>()) {
    const char *nextPassword = settings["password"].as<const char *>();
    if (nextPassword && strlen(nextPassword) > 0) {
      doc["password"] = nextPassword;
    }
  }
  return _storage && _storage->writeJson(RenzFiConfig::ROUTER_FILE, doc);
}

bool FoundationRouterDriver::fillPublicSettings(JsonDocument &doc) const {
  DynamicJsonDocument stored(RenzFiConfig::JSON_DOC_SMALL);
  if (!_storage || !_storage->readJson(RenzFiConfig::ROUTER_FILE, stored)) {
    return false;
  }
  doc["driverType"] = stored["driverType"] | _manifest.driverId;
  doc["host"]       = stored["host"] | "";
  doc["username"]   = stored["username"] | "";
  const String storedPassword = stored["password"] | "";
  doc["passwordConfigured"] = storedPassword.length() > 0;
  return true;
}

void FoundationRouterDriver::setNotImplementedError(JsonDocument &out) const {
  out["ok"]    = false;
  out["error"] = String(_manifest.vendor) + " driver is not yet implemented";
}

bool FoundationRouterDriver::connect(String &errorOut) {
  errorOut = String(_manifest.vendor) + " driver is not yet implemented";
  return false;
}

void FoundationRouterDriver::disconnect() {}

bool FoundationRouterDriver::healthCheck(JsonDocument &out) {
  setNotImplementedError(out);
  return false;
}

bool FoundationRouterDriver::testSettings(JsonObjectConst overrideSettings,
                                          JsonDocument &out) {
  (void)overrideSettings;
  setNotImplementedError(out);
  return false;
}

bool FoundationRouterDriver::listProfiles(JsonDocument &out) {
  out["profiles"] = JsonArray();
  setNotImplementedError(out);
  return false;
}

bool FoundationRouterDriver::authorizeUser(const HotspotUser &user) {
  (void)user;
  if (_logger) {
    _logger->warn("router", String(_manifest.vendor) + " authorizeUser not implemented");
  }
  return false;
}

bool FoundationRouterDriver::deauthorizeUser(const String &mac) {
  (void)mac;
  if (_logger) {
    _logger->warn("router", String(_manifest.vendor) + " deauthorizeUser not implemented");
  }
  return false;
}

bool FoundationRouterDriver::assignProfile(const String &username,
                                           const String &profile) {
  (void)username;
  (void)profile;
  return false;
}

bool FoundationRouterDriver::activateProductionNetwork(JsonDocument &result) {
  result["ok"]             = false;
  result["provisionReady"] = false;
  result["reason"]         = "api-failure";
  result["error"]          = "Not Implemented";
  return false;
}

bool FoundationRouterDriver::productionNetworkActive(JsonDocument &result) {
  result["ok"]             = false;
  result["provisionReady"] = false;
  result["reason"]         = "api-failure";
  result["error"]          = "Not Implemented";
  return false;
}

bool FoundationRouterDriver::fillStatistics(JsonDocument &out) {
  setNotImplementedError(out);
  return false;
}

void FoundationRouterDriver::detect(JsonObject out) const {
  out["driverId"]   = _manifest.driverId;
  out["detected"]   = false;
  out["confidence"] = "none";
  out["reason"]     = "Driver registered but protocol not yet implemented";
  JsonObject manifestObj = out["manifest"].to<JsonObject>();
  _manifest.toJson(manifestObj);
}
