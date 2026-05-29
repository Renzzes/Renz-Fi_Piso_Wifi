#include "MikroTikManager.h"

#include "Config.h"

void MikroTikManager::begin(StorageManager *storage, Logger *logger) {
  _storage = storage;
  _logger = logger;
}

bool MikroTikManager::load(JsonDocument &doc) {
  return _storage && _storage->readJson(RenzFiConfig::ROUTER_FILE, doc);
}

bool MikroTikManager::save(JsonObjectConst settings) {
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_SMALL);
  load(doc);
  doc["host"] = settings["host"] | doc["host"] | "10.10.10.2";
  doc["username"] = settings["username"] | doc["username"] | "";
  doc["password"] = settings["password"] | doc["password"] | "";
  doc["profile"] = settings["profile"] | doc["profile"] | "default";
  doc["ssid"] = settings["ssid"] | doc["ssid"] | "Renz-Fi";
  bool ok = _storage && _storage->writeJson(RenzFiConfig::ROUTER_FILE, doc);
  if (ok && _logger) _logger->info("router", "Router settings saved");
  return ok;
}

bool MikroTikManager::test(JsonObjectConst overrideSettings) {
  DynamicJsonDocument settings(RenzFiConfig::JSON_DOC_SMALL);
  load(settings);
  const char *host = overrideSettings["host"] | settings["host"] | "";
  bool ok = strlen(host) > 0;
  if (_logger) {
    if (ok) _logger->info("router", String("MikroTik boundary test for ") + host);
    else _logger->warn("router", "MikroTik host is not configured");
  }
  return ok;
}

bool MikroTikManager::createHotspotUser(const HotspotUser &user) {
  if (_logger) {
    _logger->info("router", String("Create hotspot user boundary: ") + user.username);
  }
  return true;
}

bool MikroTikManager::disconnectHotspotUser(const String &mac) {
  if (_logger) _logger->info("router", String("Disconnect hotspot user boundary: ") + mac);
  return true;
}

bool MikroTikManager::assignProfile(const String &username, const String &profile) {
  if (_logger) _logger->info("router", String("Assign profile boundary: ") + username + " -> " + profile);
  return true;
}
