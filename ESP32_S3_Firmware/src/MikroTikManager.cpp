#include "MikroTikManager.h"

#include "Config.h"

void MikroTikManager::begin(StorageManager *storage, Logger *logger) {
  _storage = storage;
  _logger  = logger;
  _routerOs.setTimeouts(RenzFiConfig::ROUTEROS_CONNECT_TIMEOUT_MS,
                        RenzFiConfig::ROUTEROS_IO_TIMEOUT_MS);
}

bool MikroTikManager::load(JsonDocument &doc) {
  return _storage && _storage->readJson(RenzFiConfig::ROUTER_FILE, doc);
}

bool MikroTikManager::save(JsonObjectConst settings) {
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_SMALL);
  load(doc);
  doc["host"] = settings["host"] | doc["host"] | "10.40.0.1";
  doc["username"] = settings["username"] | doc["username"] | "";
  doc["profile"] = settings["profile"] | doc["profile"] | "default";

  if (!settings["ssid"].isNull()) {
    doc["ssid"] = settings["ssid"].as<const char *>();
  }

  if (settings["password"].is<const char *>()) {
    const char *nextPassword = settings["password"].as<const char *>();
    if (nextPassword && strlen(nextPassword) > 0) {
      doc["password"] = nextPassword;
    }
  }

  if (settings["wifiPassword"].is<const char *>()) {
    const char *nextWifiPassword = settings["wifiPassword"].as<const char *>();
    if (nextWifiPassword && strlen(nextWifiPassword) > 0) {
      doc["wifiPassword"] = nextWifiPassword;
    }
  }

  bool ok = _storage && _storage->writeJson(RenzFiConfig::ROUTER_FILE, doc);
  if (ok && _logger) _logger->info("router", "Router settings saved");
  return ok;
}

bool MikroTikManager::fillPublicSettings(JsonDocument &doc) const {
  DynamicJsonDocument stored(RenzFiConfig::JSON_DOC_SMALL);
  if (!_storage || !_storage->readJson(RenzFiConfig::ROUTER_FILE, stored)) {
    return false;
  }

  doc["host"] = stored["host"] | "10.40.0.1";
  doc["username"] = stored["username"] | "";
  doc["profile"] = stored["profile"] | "default";
  doc["ssid"] = stored["ssid"] | "RenzFi_PesoWifi";
  doc["wifiPassword"] = stored["wifiPassword"] | "";

  const String storedPassword = stored["password"] | "";
  doc["passwordConfigured"] = storedPassword.length() > 0;
  return true;
}

bool MikroTikManager::loadRouterCredentials(String &host, String &username,
                                            String &password,
                                            String &profile) const {
  DynamicJsonDocument stored(RenzFiConfig::JSON_DOC_SMALL);
  if (!_storage || !_storage->readJson(RenzFiConfig::ROUTER_FILE, stored)) {
    return false;
  }
  host = stored["host"] | "";
  username = stored["username"] | "";
  password = stored["password"] | "";
  profile = stored["profile"] | "default";
  return true;
}

void MikroTikManager::mergeSettings(JsonObjectConst overrideSettings,
                                    JsonDocument &settings) const {
  if (!overrideSettings.isNull()) {
    if (!overrideSettings["host"].isNull()) {
      settings["host"] = overrideSettings["host"].as<const char *>();
    }
    if (!overrideSettings["username"].isNull()) {
      settings["username"] = overrideSettings["username"].as<const char *>();
    }
    if (overrideSettings["password"].is<const char *>()) {
      const char *nextPassword = overrideSettings["password"].as<const char *>();
      if (nextPassword && strlen(nextPassword) > 0) {
        settings["password"] = nextPassword;
      }
    }
    if (!overrideSettings["profile"].isNull()) {
      settings["profile"] = overrideSettings["profile"].as<const char *>();
    }
    if (!overrideSettings["ssid"].isNull()) {
      settings["ssid"] = overrideSettings["ssid"].as<const char *>();
    }
    if (overrideSettings["wifiPassword"].is<const char *>()) {
      const char *nextWifiPassword =
          overrideSettings["wifiPassword"].as<const char *>();
      if (nextWifiPassword && strlen(nextWifiPassword) > 0) {
        settings["wifiPassword"] = nextWifiPassword;
      }
    }
  }
}

String MikroTikManager::identityFromResult(
    const RouterOsClient::CommandResult &result) {
  for (uint8_t i = 0; i < result.replyCount; ++i) {
    const RouterOsClient::ReplyRecord &record = result.replies[i];
    for (uint8_t j = 0; j < record.attrCount; ++j) {
      String key;
      String value;
      if (RouterOsClient::parseAttr(record.attrs[j], key, value) &&
          key == "name" && !value.isEmpty()) {
        return value;
      }
    }
  }
  return "";
}

void MikroTikManager::profileNamesFromResult(
    const RouterOsClient::CommandResult &result, JsonArray &out) {
  for (uint8_t i = 0; i < result.replyCount; ++i) {
    const RouterOsClient::ReplyRecord &record = result.replies[i];
    for (uint8_t j = 0; j < record.attrCount; ++j) {
      String key;
      String value;
      if (!RouterOsClient::parseAttr(record.attrs[j], key, value) ||
          key != "name" || value.isEmpty()) {
        continue;
      }

      bool exists = false;
      for (JsonVariant item : out) {
        if (String(item.as<const char *>()) == value) {
          exists = true;
          break;
        }
      }
      if (!exists) out.add(value);
    }
  }
}

bool MikroTikManager::profileExistsInResult(
    const RouterOsClient::CommandResult &result, const String &profile) {
  for (uint8_t i = 0; i < result.replyCount; ++i) {
    const RouterOsClient::ReplyRecord &record = result.replies[i];
    for (uint8_t j = 0; j < record.attrCount; ++j) {
      String key;
      String value;
      if (RouterOsClient::parseAttr(record.attrs[j], key, value) &&
          key == "name" && value == profile) {
        return true;
      }
    }
  }
  return false;
}

String MikroTikManager::idFromResult(const RouterOsClient::CommandResult &result) {
  for (uint8_t i = 0; i < result.replyCount; ++i) {
    const RouterOsClient::ReplyRecord &record = result.replies[i];
    for (uint8_t j = 0; j < record.attrCount; ++j) {
      String key;
      String value;
      if (RouterOsClient::parseAttr(record.attrs[j], key, value) &&
          key == ".id" && !value.isEmpty()) {
        return value;
      }
    }
  }
  return "";
}

String MikroTikManager::macToHotspotUsername(const String &mac) {
  String username = mac;
  username.replace(":", "");
  username.toUpperCase();
  return username;
}

String MikroTikManager::formatLimitUptime(uint32_t seconds) {
  if (seconds == 0) return "00:00:00";
  const uint32_t h = seconds / 3600;
  const uint32_t m = (seconds % 3600) / 60;
  const uint32_t s = seconds % 60;
  char buf[16];
  snprintf(buf, sizeof(buf), "%02u:%02u:%02u", h, m, s);
  return String(buf);
}

bool MikroTikManager::openRouterSession(const String &host,
                                        const String &username,
                                        const String &password,
                                        String &errorOut) {
  if (host.isEmpty()) {
    errorOut = "MikroTik Router IP is not configured";
    return false;
  }
  if (username.isEmpty()) {
    errorOut = "RouterOS API username is not configured";
    return false;
  }
  if (password.isEmpty()) {
    errorOut = "RouterOS API password is not configured";
    return false;
  }

  _routerOs.setCredentials(host, username, password,
                           RenzFiConfig::ROUTEROS_API_PORT);

  if (!_routerOs.connect()) {
    errorOut = _routerOs.lastError();
    return false;
  }
  if (!_routerOs.login()) {
    errorOut = _routerOs.lastError();
    _routerOs.disconnect();
    return false;
  }
  return true;
}

void MikroTikManager::closeRouterSession() { _routerOs.disconnect(); }

bool MikroTikManager::listProfiles(JsonDocument &out) {
  out["profiles"] = JsonArray();
  out["error"]    = "";

  DynamicJsonDocument settings(RenzFiConfig::JSON_DOC_SMALL);
  if (!load(settings)) {
    out["error"] = "Router settings not available";
    return false;
  }

  const String host     = settings["host"] | "";
  const String username = settings["username"] | "";
  const String password = settings["password"] | "";

  String errorOut;
  if (!openRouterSession(host, username, password, errorOut)) {
    out["error"] = errorOut;
    if (_logger) _logger->error("router", "Profile list failed: " + errorOut);
    return false;
  }

  RouterOsClient::CommandResult profileResult;
  if (!_routerOs.executeCommand("/ip/hotspot/user/profile/print",
                                profileResult)) {
    out["error"] = _routerOs.lastError();
    closeRouterSession();
    return false;
  }
  if (profileResult.trapReceived) {
    out["error"] = profileResult.trapMessage.isEmpty()
                       ? "Failed to read hotspot profiles"
                       : profileResult.trapMessage;
    closeRouterSession();
    return false;
  }

  JsonArray profiles = out["profiles"].to<JsonArray>();
  profileNamesFromResult(profileResult, profiles);
  closeRouterSession();

  if (_logger) {
    _logger->info("router",
                  String("Loaded ") + profiles.size() + " hotspot profile(s) from RouterOS");
  }
  return true;
}

bool MikroTikManager::test(JsonObjectConst overrideSettings, JsonDocument &out) {
  out["connected"]      = false;
  out["authenticated"]  = false;
  out["profileFound"]   = false;
  out["identity"]       = "";
  out["error"]          = "";
  out["ok"]             = false;

  DynamicJsonDocument settings(RenzFiConfig::JSON_DOC_SMALL);
  load(settings);
  mergeSettings(overrideSettings, settings);

  const String host     = settings["host"] | "";
  const String username = settings["username"] | "";
  const String password = settings["password"] | "";
  const String profile  = settings["profile"] | "default";

  if (profile.isEmpty()) {
    out["error"] = "Hotspot profile is not configured";
    return false;
  }

  String errorOut;
  if (!openRouterSession(host, username, password, errorOut)) {
    out["error"] = errorOut;
    if (_logger) _logger->error("router", "RouterOS test failed: " + errorOut);
    return false;
  }
  out["connected"]     = true;
  out["authenticated"] = true;

  RouterOsClient::CommandResult identityResult;
  if (!_routerOs.executeCommand("/system/identity/print", identityResult)) {
    out["error"] = _routerOs.lastError();
    closeRouterSession();
    return false;
  }
  if (identityResult.trapReceived) {
    out["error"] = identityResult.trapMessage.isEmpty()
                       ? "Failed to read router identity"
                       : identityResult.trapMessage;
    closeRouterSession();
    return false;
  }

  const String identity = identityFromResult(identityResult);
  out["identity"] = identity.isEmpty() ? "RouterOS" : identity;

  RouterOsClient::CommandResult profileResult;
  const String profileQuery = "?name=" + profile;
  if (!_routerOs.executeCommand("/ip/hotspot/user/profile/print", profileQuery,
                                profileResult)) {
    out["error"] = _routerOs.lastError();
    closeRouterSession();
    return false;
  }
  if (profileResult.trapReceived || !profileExistsInResult(profileResult, profile)) {
    out["error"] = profileResult.trapMessage.isEmpty()
                       ? String("Hotspot profile not found: ") + profile
                       : profileResult.trapMessage;
    closeRouterSession();
    return false;
  }

  out["profileFound"] = true;
  out["ok"]           = true;

  if (_logger) {
    _logger->info("router",
                  String("RouterOS test OK — identity=") + out["identity"].as<const char *>() +
                      " profile=" + profile);
  }

  closeRouterSession();
  return true;
}

bool MikroTikManager::createHotspotUser(const HotspotUser &user) {
  String host;
  String username;
  String password;
  String configuredProfile;
  if (!loadRouterCredentials(host, username, password, configuredProfile)) {
    if (_logger) _logger->error("router", "Hotspot user provisioning failed: settings unavailable");
    return false;
  }

  const String profile =
      user.profile.length() > 0 ? user.profile : configuredProfile;
  const String hotspotUser =
      user.username.length() > 0 ? user.username : macToHotspotUsername(user.mac);
  const String hotspotPassword = hotspotUser;
  const String limitUptime = formatLimitUptime(user.timeoutSeconds);

  String errorOut;
  if (!openRouterSession(host, username, password, errorOut)) {
    if (_logger) _logger->error("router", "Hotspot user provisioning failed: " + errorOut);
    return false;
  }

  RouterOsClient::CommandResult printResult;
  const String printQuery = "?name=" + hotspotUser;
  if (!_routerOs.executeCommand("/ip/hotspot/user/print", printQuery, printResult)) {
    if (_logger) _logger->error("router", "Hotspot user lookup failed: " + _routerOs.lastError());
    closeRouterSession();
    return false;
  }

  const String existingId = idFromResult(printResult);
  bool ok = false;

  if (!existingId.isEmpty()) {
    const String setAttrs[] = {
        "=.id=" + existingId,
        "=profile=" + profile,
        "=limit-uptime=" + limitUptime,
        "=disabled=no",
        "=comment=" + user.mac,
    };
    RouterOsClient::CommandResult setResult;
    ok = _routerOs.executeCommand("/ip/hotspot/user/set", setAttrs,
                                  sizeof(setAttrs) / sizeof(setAttrs[0]),
                                  setResult);
    if (!ok || setResult.trapReceived) {
      if (_logger) {
        _logger->error("router",
                       "Hotspot user update failed: " +
                           (setResult.trapMessage.isEmpty()
                                ? _routerOs.lastError()
                                : setResult.trapMessage));
      }
      closeRouterSession();
      return false;
    }
  } else {
    const String addAttrs[] = {
        "=name=" + hotspotUser,
        "=password=" + hotspotPassword,
        "=profile=" + profile,
        "=limit-uptime=" + limitUptime,
        "=comment=" + user.mac,
    };
    RouterOsClient::CommandResult addResult;
    ok = _routerOs.executeCommand("/ip/hotspot/user/add", addAttrs,
                                  sizeof(addAttrs) / sizeof(addAttrs[0]),
                                  addResult);
    if (!ok || addResult.trapReceived) {
      if (_logger) {
        _logger->error("router",
                       "Hotspot user add failed: " +
                           (addResult.trapMessage.isEmpty()
                                ? _routerOs.lastError()
                                : addResult.trapMessage));
      }
      closeRouterSession();
      return false;
    }
  }

  closeRouterSession();
  if (_logger) {
    _logger->info("router",
                  String("Hotspot user provisioned: ") + hotspotUser +
                      " profile=" + profile + " limit=" + limitUptime);
  }
  return true;
}

bool MikroTikManager::provisionHotspotUser(const HotspotUser &user) {
  return createHotspotUser(user);
}

bool MikroTikManager::disconnectHotspotUser(const String &mac) {
  String host;
  String username;
  String password;
  String profile;
  if (!loadRouterCredentials(host, username, password, profile)) {
    return false;
  }

  String errorOut;
  if (!openRouterSession(host, username, password, errorOut)) {
    if (_logger) _logger->error("router", "Hotspot disconnect failed: " + errorOut);
    return false;
  }

  RouterOsClient::CommandResult activeResult;
  const String macQuery = "?mac-address=" + mac;
  bool ok = true;

  if (_routerOs.executeCommand("/ip/hotspot/active/print", macQuery, activeResult)) {
    const String activeId = idFromResult(activeResult);
    if (!activeId.isEmpty()) {
      const String removeAttrs[] = {"=.id=" + activeId};
      RouterOsClient::CommandResult removeResult;
      if (!_routerOs.executeCommand("/ip/hotspot/active/remove", removeAttrs, 1,
                                    removeResult) ||
          removeResult.trapReceived) {
        ok = false;
      }
    }
  }

  const String hotspotUser = macToHotspotUsername(mac);
  RouterOsClient::CommandResult printResult;
  if (_routerOs.executeCommand("/ip/hotspot/user/print", "?name=" + hotspotUser,
                               printResult)) {
    const String userId = idFromResult(printResult);
    if (!userId.isEmpty()) {
      const String removeAttrs[] = {"=.id=" + userId};
      RouterOsClient::CommandResult removeResult;
      if (!_routerOs.executeCommand("/ip/hotspot/user/remove", removeAttrs, 1,
                                    removeResult) ||
          removeResult.trapReceived) {
        ok = false;
      }
    }
  }

  closeRouterSession();
  if (_logger) {
    _logger->info("router", String("Hotspot user disconnected: ") + mac);
  }
  return ok;
}

bool MikroTikManager::assignProfile(const String &username, const String &profile) {
  HotspotUser user;
  user.username = username;
  user.profile = profile;
  return createHotspotUser(user);
}
