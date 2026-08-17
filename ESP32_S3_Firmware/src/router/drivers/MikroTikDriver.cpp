#include "MikroTikDriver.h"

#include "Config.h"
#include "EventBus.h"
#include "JsonHeap.h"
#include "Logger.h"
#include "StoragePaths.h"
#include "../RouterDriverManifest.h"
#include "StorageManager.h"
#include "RouterWirelessAdapter.h"
#include "RouterCommandScratch.h"
#include "RouterApiTransportGate.h"
#include "ActivationLatencyTrace.h"
#include "ProductionNetworkTrace.h"
#include "RouterProvisioningTypes.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static void logRouterStack(const char *label) {
  UBaseType_t stackWords = uxTaskGetStackHighWaterMark(NULL);
  Serial.printf("[STACK] router %s free=%u bytes\n", label,
                static_cast<unsigned>(stackWords * sizeof(StackType_t)));
}

bool productionNetworkExecute(RouterOsClient &client, const char *statement,
                              const String &commandPath,
                              RouterOsClient::CommandResult &out,
                              const String *attributes = nullptr,
                              size_t attributeCount = 0) {
  const uint32_t startMs = millis();
  ProductionNetworkTrace::logCmdBegin(commandPath.c_str());
  ProductionNetworkTrace::logSessionState(client);
  const bool ok = attributes
                      ? client.executeCommand(commandPath, attributes,
                                              attributeCount, out)
                      : client.executeCommand(commandPath, out);
  ProductionNetworkTrace::logCmdEnd(client, commandPath.c_str(),
                                    millis() - startMs, out, ok);
  if (!ok && ProductionNetworkTrace::active()) {
    ProductionNetworkTrace::logReturnFalse(
        statement, "executeCommand failed", client.lastErrorCode().c_str(),
        client.lastError().c_str(), nullptr);
  }
  return ok;
}

void MikroTikDriver::begin(StorageManager *storage, Logger *logger,
                           EventBus *events) {
  _storage = storage;
  _logger  = logger;
  _events  = events;
  _routerOs.setTimeouts(RenzFiConfig::ROUTEROS_CONNECT_TIMEOUT_MS,
                        RenzFiConfig::ROUTEROS_IO_TIMEOUT_MS);
}

RouterCapabilities MikroTikDriver::capabilities() const {
  return manifest().capabilities;
}

RouterDriverManifest MikroTikDriver::manifest() const {
  static const char *kFeatures[] = {
      "hotspot_user_provisioning",
      "hotspot_session_disconnect",
      "profile_management",
      "router_identity",
      "router_health_test",
      "hotspot_statistics",
      nullptr,
  };

  RouterDriverManifest m;
  m.driverId          = driverId();
  m.vendor            = vendorName();
  m.productName       = "Renz-Fi Gateway";
  m.productSubtitle   = "Powered by MikroTik RouterOS";
  m.model             = "RouterBOARD / CCR / hAP / wAP";
  m.supportedFirmware = "RouterOS";
  m.minimumVersion    = "6.0";
  m.capabilities      = RouterCapabilities{};
  m.capabilities.supportsVoucherControl  = true;
  m.capabilities.supportsBandwidthLimit  = true;
  m.capabilities.supportsHotspot         = true;
  m.capabilities.supportsPauseResume     = true;
  m.capabilities.supportsQueueManagement = true;
  m.capabilities.supportsApi            = true;
  m.capabilities.supportsIdentity       = true;
  m.capabilities.supportsHealth         = true;
  m.capabilities.supportsStatistics     = true;
  m.capabilities.supportsRemoteConfig   = false;
  m.supportedFeatures                   = kFeatures;
  m.supportedFeatureCount               = 6;
  m.stability                           = DriverStability::Stable;
  m.documentationUrl                    = "https://help.mikrotik.com/docs/";
  m.driverVersion                       = "1.0.0";
  return m;
}

RouterProfile MikroTikDriver::profile() const {
  RouterProfile p;
  p.vendor         = vendorName();
  p.driverId       = driverId();
  p.connectionType = "routeros-api";
  p.apiVersion     = "RouterOS API";
  p.capabilities   = capabilities();

  String host;
  String username;
  String password;
  String profileName;
  if (loadRouterCredentials(host, username, password, profileName)) {
    p.ipAddress = host;
    p.username  = username;
  }

  p.status   = _routerOs.isConnected() ? "connected" : "disconnected";
  p.identity = _cachedIdentity;
  return p;
}

bool MikroTikDriver::loadSettings(JsonDocument &doc) {
  return _storage && _storage->readJson(RenzFiConfig::ROUTER_FILE, doc);
}

bool MikroTikDriver::saveSettings(JsonObjectConst settings) {
  // SSID/WiFi password are intentionally NOT stored here — RouterOS is the
  // single source of truth for wireless configuration (see fillWireless /
  // saveWireless). This file only holds RouterOS API connection details.
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_SMALL);
  loadSettings(doc);
  doc["driverType"] = driverId();
  doc["host"]       = settings["host"] | doc["host"] | "10.40.0.1";
  doc["username"]   = settings["username"] | doc["username"] | "";
  doc["profile"]    = settings["profile"] | doc["profile"] | "default";

  if (settings["password"].is<const char *>()) {
    const char *nextPassword = settings["password"].as<const char *>();
    if (nextPassword && strlen(nextPassword) > 0) {
      doc["password"] = nextPassword;
    }
  }

  bool ok = _storage && _storage->writeJson(RenzFiConfig::ROUTER_FILE, doc);
  if (ok && _logger) _logger->info("router", "Router settings saved");
  return ok;
}

bool MikroTikDriver::fillPublicSettings(JsonDocument &doc) const {
  DynamicJsonDocument stored(RenzFiConfig::JSON_DOC_SMALL);
  if (!_storage || !_storage->readJson(RenzFiConfig::ROUTER_FILE, stored)) {
    return false;
  }

  doc["driverType"] = stored["driverType"] | driverId();
  doc["host"]       = stored["host"] | "10.40.0.1";
  doc["username"]   = stored["username"] | "";
  doc["profile"]    = stored["profile"] | "default";

  const String storedPassword = stored["password"] | "";
  const String storedUsername = stored["username"] | "";
  doc["passwordConfigured"] = storedPassword.length() > 0;
  doc["usernameConfigured"] = storedUsername.length() > 0;
  return true;
}

bool MikroTikDriver::loadRouterCredentials(String &host, String &username,
                                           String &password,
                                           String &profile) const {
  HeapJsonDocument heap(RenzFiConfig::JSON_DOC_SMALL);
  if (!_storage || !_storage->readJson(RenzFiConfig::ROUTER_FILE, heap.doc())) {
    return false;
  }
  JsonDocument &stored = heap;
  host     = stored["host"] | "";
  username = stored["username"] | "";
  password = stored["password"] | "";
  profile  = stored["profile"] | "default";
  return true;
}

void MikroTikDriver::mergeSettings(JsonObjectConst overrideSettings,
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
  }
}

String MikroTikDriver::identityFromResult(
    const RouterOsClient::CommandResult &result) {
  for (uint8_t i = 0; i < result.replyCount; ++i) {
    const RouterOsClient::ReplyRecord &record = result.replyAt(i);
    for (uint8_t j = 0; j < record.attrCount; ++j) {
      String key;
      String value;
      if (RouterOsClient::parseAttr(record.attr(j), key, value) &&
          key == "name" && !value.isEmpty()) {
        return value;
      }
    }
  }
  return "";
}

void MikroTikDriver::profileNamesFromResult(
    const RouterOsClient::CommandResult &result, JsonArray &out) {
  for (uint8_t i = 0; i < result.replyCount; ++i) {
    const RouterOsClient::ReplyRecord &record = result.replyAt(i);
    for (uint8_t j = 0; j < record.attrCount; ++j) {
      String key;
      String value;
      if (!RouterOsClient::parseAttr(record.attr(j), key, value) ||
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

void MikroTikDriver::profileDetailsFromResult(
    const RouterOsClient::CommandResult &result, JsonArray &detailsOut,
    JsonArray &namesOut) {
  for (uint8_t i = 0; i < result.replyCount; ++i) {
    const RouterOsClient::ReplyRecord &record = result.replyAt(i);
    String name;
    String rateLimit;
    for (uint8_t j = 0; j < record.attrCount; ++j) {
      String key;
      String value;
      if (!RouterOsClient::parseAttr(record.attr(j), key, value)) continue;
      if (key == "name") {
        name = value;
      } else if (key == "rate-limit") {
        rateLimit = value;
      }
    }
    if (name.isEmpty()) continue;

    bool exists = false;
    for (JsonVariant item : namesOut) {
      if (String(item.as<const char *>()) == name) {
        exists = true;
        break;
      }
    }
    if (exists) continue;

    JsonObject row = detailsOut.createNestedObject();
    row["name"]      = name;
    row["rateLimit"] = rateLimit;
    namesOut.add(name);
  }
}

bool MikroTikDriver::profileExistsInResult(
    const RouterOsClient::CommandResult &result, const String &profile) {
  for (uint8_t i = 0; i < result.replyCount; ++i) {
    const RouterOsClient::ReplyRecord &record = result.replyAt(i);
    for (uint8_t j = 0; j < record.attrCount; ++j) {
      String key;
      String value;
      if (RouterOsClient::parseAttr(record.attr(j), key, value) &&
          key == "name" && value == profile) {
        return true;
      }
    }
  }
  return false;
}

String MikroTikDriver::idFromResult(const RouterOsClient::CommandResult &result) {
  for (uint8_t i = 0; i < result.replyCount; ++i) {
    const RouterOsClient::ReplyRecord &record = result.replyAt(i);
    for (uint8_t j = 0; j < record.attrCount; ++j) {
      String key;
      String value;
      if (RouterOsClient::parseAttr(record.attr(j), key, value) &&
          key == ".id" && !value.isEmpty()) {
        return value;
      }
    }
  }
  return "";
}

String MikroTikDriver::attrFromResult(const RouterOsClient::CommandResult &result,
                                      const char *attrName) {
  for (uint8_t i = 0; i < result.replyCount; ++i) {
    const RouterOsClient::ReplyRecord &record = result.replyAt(i);
    for (uint8_t j = 0; j < record.attrCount; ++j) {
      String key;
      String value;
      if (RouterOsClient::parseAttr(record.attr(j), key, value) && key == attrName) {
        return value;
      }
    }
  }
  return "";
}

static String attrFromReply(const RouterOsClient::CommandResult &result,
                            uint8_t replyIdx, const char *attrName) {
  if (replyIdx >= result.replyCount) return "";
  const RouterOsClient::ReplyRecord &record = result.replyAt(replyIdx);
  for (uint8_t j = 0; j < record.attrCount; ++j) {
    String key;
    String value;
    if (RouterOsClient::parseAttr(record.attr(j), key, value) && key == attrName) {
      return value;
    }
  }
  return "";
}

String MikroTikDriver::macToHotspotUsername(const String &mac) {
  String username = mac;
  username.replace(":", "");
  username.toUpperCase();
  return username;
}

String MikroTikDriver::formatLimitUptime(uint32_t seconds) {
  if (seconds == 0) return "00:00:00";
  const uint32_t h = seconds / 3600;
  const uint32_t m = (seconds % 3600) / 60;
  const uint32_t s = seconds % 60;
  char buf[16];
  snprintf(buf, sizeof(buf), "%02u:%02u:%02u", h, m, s);
  return String(buf);
}

uint32_t MikroTikDriver::parseRouterOsDurationSeconds(const String &raw) {
  // Accepts RouterOS duration forms used by HotSpot user/active prints:
  //   "18s", "5m", "5m1s", "1h2m3s", "00:05:00", "0s", empty → 0
  if (raw.isEmpty()) return 0;

  // HH:MM:SS or H:MM:SS
  const int c1 = raw.indexOf(':');
  if (c1 > 0) {
    const int c2 = raw.indexOf(':', c1 + 1);
    if (c2 > c1) {
      const long h = raw.substring(0, c1).toInt();
      const long m = raw.substring(c1 + 1, c2).toInt();
      const long s = raw.substring(c2 + 1).toInt();
      if (h < 0 || m < 0 || s < 0) return 0;
      return static_cast<uint32_t>(h * 3600L + m * 60L + s);
    }
  }

  uint32_t total = 0;
  uint32_t current = 0;
  bool haveDigit = false;
  for (size_t i = 0; i < raw.length(); ++i) {
    const char c = raw.charAt(i);
    if (c >= '0' && c <= '9') {
      haveDigit = true;
      current = current * 10u + static_cast<uint32_t>(c - '0');
      continue;
    }
    if (!haveDigit) continue;
    if (c == 'w' || c == 'W') {
      total += current * 604800u;
    } else if (c == 'd' || c == 'D') {
      total += current * 86400u;
    } else if (c == 'h' || c == 'H') {
      total += current * 3600u;
    } else if (c == 'm' || c == 'M') {
      total += current * 60u;
    } else if (c == 's' || c == 'S') {
      total += current;
    } else {
      // Unknown unit — abort rather than invent a value.
      return 0;
    }
    current = 0;
    haveDigit = false;
  }
  // Bare integer seconds (rare).
  if (haveDigit) total += current;
  return total;
}

bool MikroTikDriver::openRouterSession(const String &host, const String &username,
                                       const String &password, String &errorOut) {
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
  _routerOs.setCredentialSource("production-router-json");

  if (!_routerOs.connect()) {
    errorOut = _routerOs.lastError();
    return false;
  }
  if (!_routerOs.login()) {
    errorOut = _routerOs.lastError();
    _routerOs.disconnect();
    return false;
  }

  _cachedHost     = host;
  _cachedUsername = username;
  return true;
}

void MikroTikDriver::closeRouterSession() { _routerOs.disconnect(); }

bool MikroTikDriver::connect(String &errorOut) {
  String host;
  String username;
  String password;
  String profile;
  if (!loadRouterCredentials(host, username, password, profile)) {
    errorOut = "Router settings not available";
    return false;
  }
  return openRouterSession(host, username, password, errorOut);
}

void MikroTikDriver::disconnect() { closeRouterSession(); }

bool MikroTikDriver::healthCheck(JsonDocument &out) {
  return testSettings(JsonObject(), out);
}

bool MikroTikDriver::probeApiReady() {
  // Minimal readiness: one connect+login+identity/print+disconnect.
  // Never used as an idle heartbeat — only health FSM PROBING.
  String host;
  String username;
  String password;
  String profile;
  if (!loadRouterCredentials(host, username, password, profile)) {
    return false;
  }
  String errorOut;
  const uint32_t t0 = millis();
  if (!openRouterSession(host, username, password, errorOut)) {
    Serial.printf("[ros-health] probe login_failed reason=%s\n",
                  errorOut.c_str());
    return false;
  }
  RouterOsClient::CommandResult &identityResult =
      RouterCommandScratchContext::acquire();
  const bool ok =
      _routerOs.executeCommand("/system/identity/print", identityResult) &&
      !identityResult.trapReceived && identityResult.doneReceived;
  closeRouterSession();
  Serial.printf("[ros-health] probe %s latency=%lums\n", ok ? "ok" : "fail",
                (unsigned long)(millis() - t0));
  return ok;
}

bool MikroTikDriver::listProfiles(JsonDocument &out) {
  out["profiles"] = JsonArray();
  out["error"]    = "";

  DynamicJsonDocument settings(RenzFiConfig::JSON_DOC_SMALL);
  if (!loadSettings(settings)) {
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

  RouterOsClient::CommandResult &profileResult = RouterCommandScratchContext::acquire();
  // Bounded inventory — Admin UI needs name + rate-limit only.
  const String profileAttrs[] = {"=.proplist=name,rate-limit"};
  if (!_routerOs.executeCommand("/ip/hotspot/user/profile/print", profileAttrs, 1,
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
  JsonArray details  = out["profileDetails"].to<JsonArray>();
  profileDetailsFromResult(profileResult, details, profiles);
  closeRouterSession();

  // Surface (never mask) truncation instead of silently returning a
  // partial list — the RouterOS reply-record cap was hit before the whole
  // profile table was read.
  if (profileResult.replyLimitReached) {
    out["truncated"] = true;
    if (_logger) {
      _logger->warn("router",
                    String("Hotspot profile list truncated at ") +
                        profiles.size() + " entries (RouterOS reply limit)");
    }
  }

  if (_logger) {
    _logger->info("router",
                  String("Loaded ") + profiles.size() +
                      " hotspot profile(s) from RouterOS");
  }
  return true;
}

String MikroTikDriver::formatRateLimitMbps(uint16_t downloadMbps,
                                           uint16_t uploadMbps) {
  // Hotspot user-profile rate-limit: rx/tx = client download/upload.
  return String(downloadMbps) + "M/" + String(uploadMbps) + "M";
}

String MikroTikDriver::managedSpeedProfileName(uint16_t downloadMbps,
                                               uint16_t uploadMbps) {
  return String("renzfi-speed-") + downloadMbps + "m-" + uploadMbps + "m";
}

bool MikroTikDriver::setUserProfileRateLimit(const String &name,
                                             const String &rateLimit,
                                             JsonDocument &out) {
  out["ok"]        = false;
  out["name"]      = name;
  out["rateLimit"] = rateLimit;
  out["error"]     = "";

  if (name.isEmpty()) {
    out["error"] = "Profile name is required";
    return false;
  }

  String host, username, password, configuredProfile;
  if (!loadRouterCredentials(host, username, password, configuredProfile)) {
    out["error"] = "Router settings not available";
    return false;
  }
  String errorOut;
  if (!openRouterSession(host, username, password, errorOut)) {
    out["error"] = errorOut;
    return false;
  }

  RouterOsClient::CommandResult &printResult = RouterCommandScratchContext::acquire();
  const String filter[] = {
      "?name=" + name,
      "=.proplist=.id,name,rate-limit,comment",
  };
  if (!_routerOs.executeCommand("/ip/hotspot/user/profile/print", filter, 2,
                                printResult) ||
      printResult.trapReceived || printResult.replyCount == 0) {
    out["error"] = printResult.trapMessage.isEmpty()
                       ? "Hotspot user profile not found"
                       : printResult.trapMessage;
    closeRouterSession();
    return false;
  }

  const String id = idFromResult(printResult);
  if (id.isEmpty()) {
    out["error"] = "Profile id missing";
    closeRouterSession();
    return false;
  }

  RouterOsClient::CommandResult &setResult = RouterCommandScratchContext::acquire();
  const String setAttrs[] = {"=.id=" + id, "=rate-limit=" + rateLimit};
  if (!_routerOs.executeCommand("/ip/hotspot/user/profile/set", setAttrs, 2,
                                setResult) ||
      setResult.trapReceived) {
    out["error"] = setResult.trapMessage.isEmpty() ? "Failed to set rate-limit"
                                                   : setResult.trapMessage;
    closeRouterSession();
    return false;
  }

  // Same-session verify.
  RouterOsClient::CommandResult &verify = RouterCommandScratchContext::acquire();
  if (!_routerOs.executeCommand("/ip/hotspot/user/profile/print", filter, 2,
                                verify) ||
      verify.trapReceived || verify.replyCount == 0) {
    out["error"] = "Rate-limit set, but verification failed";
    closeRouterSession();
    return false;
  }
  const String verified = attrFromResult(verify, "rate-limit");
  closeRouterSession();

  out["rateLimit"] = verified;
  out["ok"]        = true;
  out["name"]      = name;
  if (_logger) {
    _logger->info("router", String("User profile rate-limit updated name=") +
                                name + " rate-limit=" + verified);
  }
  return true;
}

bool MikroTikDriver::ensureManagedSpeedProfile(uint16_t downloadMbps,
                                               uint16_t uploadMbps,
                                               JsonDocument &out) {
  out["ok"]        = false;
  out["error"]     = "";
  out["created"]   = false;
  out["updated"]   = false;
  out["unchanged"] = false;

  if (downloadMbps == 0 || uploadMbps == 0 || downloadMbps > 1000 ||
      uploadMbps > 1000) {
    out["error"] = "Download and upload must be 1–1000 Mbps";
    return false;
  }

  const String name      = managedSpeedProfileName(downloadMbps, uploadMbps);
  const String rateLimit = formatRateLimitMbps(downloadMbps, uploadMbps);

  out["name"]      = name;
  out["rateLimit"] = rateLimit;
  out["managed"]   = true;

  String host, username, password, configuredProfile;
  if (!loadRouterCredentials(host, username, password, configuredProfile)) {
    out["error"] = "Router settings not available";
    return false;
  }
  String errorOut;
  if (!openRouterSession(host, username, password, errorOut)) {
    out["error"] = errorOut;
    return false;
  }

  RouterOsClient::CommandResult &printResult = RouterCommandScratchContext::acquire();
  const String filter[] = {
      "?name=" + name,
      "=.proplist=.id,name,rate-limit",
  };
  if (!_routerOs.executeCommand("/ip/hotspot/user/profile/print", filter, 2,
                                printResult) ||
      printResult.trapReceived) {
    out["error"] = printResult.trapMessage.isEmpty()
                       ? "Failed to read hotspot user profiles"
                       : printResult.trapMessage;
    closeRouterSession();
    return false;
  }

  if (printResult.replyCount > 0) {
    const String id           = idFromResult(printResult);
    const String existingRate = attrFromResult(printResult, "rate-limit");
    // Ownership is the deterministic renzfi-speed-* name — do not send
    // unsupported RouterOS "comment" on user-profile add/set.
    if (existingRate == rateLimit) {
      closeRouterSession();
      out["ok"]        = true;
      out["unchanged"] = true;
      out["rateLimit"] = existingRate;
      return true;
    }
    RouterOsClient::CommandResult &setResult = RouterCommandScratchContext::acquire();
    const String setAttrs[] = {
        "=.id=" + id,
        "=rate-limit=" + rateLimit,
    };
    if (!_routerOs.executeCommand("/ip/hotspot/user/profile/set", setAttrs, 2,
                                  setResult) ||
        setResult.trapReceived) {
      String trap = setResult.trapMessage;
      if (trap.indexOf("comment") >= 0 || trap.indexOf("unknown parameter") >= 0) {
        out["error"] =
            String("RouterOS rejected profile configuration: ") + trap;
      } else {
        out["error"] = trap.isEmpty() ? "Failed to update managed profile" : trap;
      }
      closeRouterSession();
      return false;
    }
    out["updated"] = true;
  } else {
    RouterOsClient::CommandResult &addResult = RouterCommandScratchContext::acquire();
    const String addAttrs[] = {
        "=name=" + name,
        "=rate-limit=" + rateLimit,
    };
    if (!_routerOs.executeCommand("/ip/hotspot/user/profile/add", addAttrs, 2,
                                  addResult) ||
        addResult.trapReceived) {
      String trap = addResult.trapMessage;
      if (trap.indexOf("comment") >= 0 || trap.indexOf("unknown parameter") >= 0) {
        out["error"] =
            String("RouterOS rejected profile configuration: ") + trap;
      } else {
        out["error"] = trap.isEmpty() ? "Failed to create managed profile" : trap;
      }
      closeRouterSession();
      return false;
    }
    out["created"] = true;
  }

  // Same-session verify.
  RouterOsClient::CommandResult &verify = RouterCommandScratchContext::acquire();
  if (!_routerOs.executeCommand("/ip/hotspot/user/profile/print", filter, 2,
                                verify) ||
      verify.trapReceived || verify.replyCount == 0) {
    out["error"] = "Managed profile write succeeded, but verification failed";
    closeRouterSession();
    return false;
  }
  const String verified = attrFromResult(verify, "rate-limit");
  closeRouterSession();
  out["rateLimit"] = verified;
  out["ok"]        = true;
  if (_logger) {
    _logger->info("router", String("Managed speed profile ok name=") + name +
                                " rate-limit=" + verified);
  }
  return true;
}

bool MikroTikDriver::fillWireless(JsonDocument &out) {
  out["ssid"]      = "";
  out["security"]  = "";
  out["interface"] = "";
  out["error"]     = "";

  RouterWireless::CanonicalConfig canonical;
  if (_storage) {
    RouterWireless::loadCanonicalConfig(_storage, canonical);
    if (canonical.configured) {
      RouterWireless::fillWirelessApiJson(canonical, out.as<JsonObject>());
      if (!canonical.ssid.isEmpty()) out["ssid"] = canonical.ssid;
      if (!canonical.interfaceId.isEmpty()) out["interface"] = canonical.interfaceId;
      if (!canonical.password.isEmpty()) out["password"] = canonical.password;
      out["security"] = "wpa2-psk";
    }
  }

  String host;
  String username;
  String password;
  String profile;
  if (!loadRouterCredentials(host, username, password, profile)) {
    if (canonical.configured && !out["ssid"].as<String>().isEmpty()) {
      return true;
    }
    out["error"] = "Router settings not available";
    return false;
  }

  String errorOut;
  if (!openRouterSession(host, username, password, errorOut)) {
    if (canonical.configured && !out["ssid"].as<String>().isEmpty()) {
      return true;
    }
    out["error"] = errorOut;
    if (_logger) _logger->error("router", "Wireless read failed: " + errorOut);
    return false;
  }

  if (canonical.configured && !canonical.interfaceId.isEmpty()) {
    DynamicJsonDocument live(RenzFiConfig::JSON_DOC_SMALL);
    JsonObject liveObj = live.to<JsonObject>();
    if (RouterWireless::readInterface(_routerOs, canonical.interfaceId, liveObj,
                                      errorOut)) {
      out["ssid"]      = liveObj["ssid"] | out["ssid"].as<String>();
      out["security"]  = liveObj["security"] | out["security"].as<String>();
      out["interface"] = liveObj["interface"] | canonical.interfaceId;
      if (liveObj.containsKey("password")) {
        out["password"] = liveObj["password"];
      } else if (!canonical.password.isEmpty()) {
        out["password"] = canonical.password;
      }
      if (liveObj.containsKey("band")) out["band"] = liveObj["band"];
      closeRouterSession();
      return true;
    }
  }

  RouterOsClient::CommandResult &wirelessResult = RouterCommandScratchContext::acquire();
  const String wirelessAttrs[] = {
      "=.proplist=.id,name,ssid,security-profile,band,frequency",
  };
  if (!_routerOs.executeCommand("/interface/wireless/print", wirelessAttrs, 1,
                                wirelessResult) ||
      wirelessResult.trapReceived) {
    out["error"] = wirelessResult.trapMessage.isEmpty()
                       ? "Failed to read wireless interface"
                       : wirelessResult.trapMessage;
    closeRouterSession();
    return canonical.configured && !out["ssid"].as<String>().isEmpty();
  }
  if (wirelessResult.replyCount == 0) {
    out["error"] = "No wireless interface found on RouterOS";
    closeRouterSession();
    return canonical.configured && !out["ssid"].as<String>().isEmpty();
  }

  const String ssid             = attrFromResult(wirelessResult, "ssid");
  const String interfaceName    = attrFromResult(wirelessResult, "name");
  const String securityProfile  = attrFromResult(wirelessResult, "security-profile");
  const String bandRaw          = attrFromResult(wirelessResult, "band");
  const String frequency        = attrFromResult(wirelessResult, "frequency");
  out["ssid"]      = ssid;
  out["interface"] = interfaceName;
  if (!bandRaw.isEmpty()) {
    out["band"] = RouterWireless::formatBandLabel(bandRaw);
  } else if (!frequency.isEmpty()) {
    const String derived =
        RouterWireless::formatBandFromFrequencyMhz(frequency.toInt());
    if (!derived.isEmpty()) out["band"] = derived;
  }

  if (securityProfile.isEmpty() || securityProfile == "default") {
    out["security"] = "none";
    closeRouterSession();
    return true;
  }

  RouterOsClient::CommandResult &secResult = RouterCommandScratchContext::acquire();
  const String secQuery = "?name=" + securityProfile;
  if (!_routerOs.executeCommand("/interface/wireless/security-profiles/print",
                                secQuery, secResult) ||
      secResult.trapReceived) {
    out["security"] = "unknown";
    closeRouterSession();
    return true;
  }

  const String authTypes = attrFromResult(secResult, "authentication-types");
  const String psk        = attrFromResult(secResult, "wpa2-pre-shared-key");
  out["security"] = authTypes.isEmpty() ? "none" : authTypes;
  if (!psk.isEmpty()) out["password"] = psk;

  closeRouterSession();
  return true;
}

bool MikroTikDriver::resolveProductionWirelessInterface(String &ifaceOut,
                                                        String &errorOut) const {
  ifaceOut.clear();
  errorOut.clear();
  if (_storage) {
    DynamicJsonDocument prov(RenzFiConfig::JSON_DOC_SMALL);
    if (_storage->readJson(StoragePaths::RouterProvisioningFile, prov)) {
      ifaceOut = prov["selectedWirelessInterface"] | "";
    }
  }
  if (ifaceOut.isEmpty()) {
    errorOut = "Production wireless interface is not configured";
    return false;
  }
  return true;
}

namespace {

bool isApWirelessMode(const String &mode) {
  String normalized = mode;
  normalized.trim();
  normalized.toLowerCase();
  return normalized == "ap-bridge" || normalized == "ap";
}

}  // namespace

bool MikroTikDriver::resolveExpectedProductionSsid(String &ssidOut) const {
  ssidOut = "";
  if (!_storage) return true;

  String policy = "keep";
  DynamicJsonDocument prov(RenzFiConfig::JSON_DOC_SMALL);
  if (_storage->readJson(StoragePaths::RouterProvisioningFile, prov)) {
    policy = prov["ssidPolicy"] | "keep";
    if (policy != "keep") {
      ssidOut = prov["targetSsid"] | "";
    }
  }
  if (ssidOut.isEmpty() && policy != "keep") {
    DynamicJsonDocument cache(RenzFiConfig::JSON_DOC_SMALL);
    if (_storage->readJson(StoragePaths::RouterCacheFile, cache)) {
      ssidOut = cache["ssid"] | "";
    }
  }
  return true;
}

static bool replyHasAttr(const RouterOsClient::CommandResult &result,
                         uint8_t replyIdx, const char *attrName) {
  if (replyIdx >= result.replyCount) return false;
  const RouterOsClient::ReplyRecord &record = result.replyAt(replyIdx);
  for (uint8_t j = 0; j < record.attrCount; ++j) {
    String key;
    String value;
    if (RouterOsClient::parseAttr(record.attr(j), key, value) && key == attrName) {
      return true;
    }
  }
  return false;
}

static const char *wirelessRuntimeLabel(uint8_t runtime) {
  switch (runtime) {
    case 1:  // Running
      return "yes";
    case 2:  // NotRunning
      return "no";
    case 0:  // Unknown
    default:
      return "unknown";
  }
}

bool MikroTikDriver::queryWirelessInterfaceState(RouterOsClient &client,
                                                 const String &ifaceName,
                                                 WirelessInterfaceState &out,
                                                 String &errorOut) {
  out = WirelessInterfaceState{};
  RouterOsClient::CommandResult &wirelessResult = RouterCommandScratchContext::acquire();
  // Bounded proplist keeps essential admin fields inside the 24-attr reply cap.
  // Do NOT rely on a truncated full print for "running".
  const String filter[] = {
      "?name=" + ifaceName,
      "=.proplist=.id,name,disabled,running,ssid,mode,frequency,channel",
  };
  const bool cmdOk = ProductionNetworkTrace::active()
                         ? productionNetworkExecute(
                               client,
                               "queryWirelessInterfaceState:executeCommand",
                               "/interface/wireless/print", wirelessResult, filter,
                               2)
                         : client.executeCommand("/interface/wireless/print", filter,
                                                 2, wirelessResult);
  if (!cmdOk || wirelessResult.trapReceived || wirelessResult.replyCount == 0) {
    errorOut = "Wireless interface not found: " + ifaceName;
    if (ProductionNetworkTrace::active()) {
      String summary = String("replyCount=") + wirelessResult.replyCount +
                       " trap=" + (wirelessResult.trapReceived ? "yes" : "no") +
                       " fatal=" + (wirelessResult.fatalReceived ? "yes" : "no");
      ProductionNetworkTrace::logReturnFalse(
          "queryWirelessInterfaceState:interface lookup",
          cmdOk ? (wirelessResult.trapReceived ? "trap" : "not found")
                : "read failed",
          client.lastErrorCode().c_str(), errorOut.c_str(), summary.c_str());
    }
    return false;
  }

  out.attrLimitReached = wirelessResult.replyLimitReached;
  out.attrsStored =
      wirelessResult.replyCount > 0 ? wirelessResult.replyAt(0).attrCount : 0;
  out.id       = attrFromReply(wirelessResult, 0, ".id");
  out.disabled = attrFromReply(wirelessResult, 0, "disabled");
  out.running  = attrFromReply(wirelessResult, 0, "running");
  out.ssid     = attrFromReply(wirelessResult, 0, "ssid");
  out.mode     = attrFromReply(wirelessResult, 0, "mode");
  out.frequency = attrFromReply(wirelessResult, 0, "frequency");
  out.channel   = attrFromReply(wirelessResult, 0, "channel");
  out.disabledAttrPresent = replyHasAttr(wirelessResult, 0, "disabled");
  out.runningAttrPresent  = replyHasAttr(wirelessResult, 0, "running");
  out.runtimeSource       = "print";
  out.runtimeStatus       = "";

  if (out.runningAttrPresent) {
    if (out.running == "true") {
      out.runtime = WirelessInterfaceState::Runtime::Running;
    } else if (out.running == "false") {
      out.runtime = WirelessInterfaceState::Runtime::NotRunning;
    } else {
      out.runtime = WirelessInterfaceState::Runtime::Unknown;
    }
  } else {
    // Missing "running" is UNKNOWN — never coerce to false.
    out.runtime = WirelessInterfaceState::Runtime::Unknown;
  }

  if (ProductionNetworkTrace::active()) {
    Serial.printf("[wireless-debug] attrsStored=%u\n",
                  static_cast<unsigned>(out.attrsStored));
    Serial.printf("[wireless-debug] attrLimitReached=%s\n",
                  out.attrLimitReached ? "yes" : "no");
    Serial.printf("[wireless-debug] disabledPresent=%s\n",
                  out.disabledAttrPresent ? "yes" : "no");
    Serial.printf("[wireless-debug] disabledValue=%s\n",
                  out.disabledAttrPresent ? out.disabled.c_str() : "(absent)");
    Serial.printf("[wireless-debug] runningPresent=%s\n",
                  out.runningAttrPresent ? "yes" : "no");
    Serial.printf("[wireless-debug] runningValue=%s\n",
                  out.runningAttrPresent ? out.running.c_str() : "(absent)");
    Serial.printf("[wireless-debug] runtimeSource=%s\n", out.runtimeSource.c_str());
  }

  if (out.id.isEmpty()) {
    errorOut = "Wireless interface record is missing on RouterOS";
    if (ProductionNetworkTrace::active()) {
      ProductionNetworkTrace::logReturnFalse(
          "queryWirelessInterfaceState:missing .id", "missing-id", nullptr,
          errorOut.c_str(), "replyCount=1 but .id empty");
    }
    return false;
  }
  return true;
}

bool MikroTikDriver::queryWirelessRuntimeOnce(RouterOsClient &client,
                                              const String &ifaceId,
                                              const String &ifaceName,
                                              WirelessInterfaceState &inout,
                                              String &errorOut) {
  errorOut.clear();
  if (ifaceId.isEmpty()) {
    errorOut = "Wireless interface id missing for monitor";
    return false;
  }

  // ONE bounded "once" snapshot — no streaming/long monitor.
  RouterOsClient::CommandResult &monitorResult = RouterCommandScratchContext::acquire();
  const String attrs[] = {"=.id=" + ifaceId, "=once="};
  if (ProductionNetworkTrace::active()) {
    Serial.println(F("[production-network] WIRELESS RUNTIME QUERY=monitor-once"));
  }
  const bool cmdOk = ProductionNetworkTrace::active()
                         ? productionNetworkExecute(
                               client, "queryWirelessRuntimeOnce:monitor",
                               "/interface/wireless/monitor", monitorResult, attrs,
                               2)
                         : client.executeCommand("/interface/wireless/monitor", attrs,
                                                 2, monitorResult);
  if (!cmdOk || monitorResult.trapReceived || monitorResult.fatalReceived ||
      monitorResult.replyCount == 0) {
    errorOut = monitorResult.trapMessage.isEmpty()
                   ? (client.lastError().isEmpty()
                          ? "Wireless monitor-once unavailable"
                          : client.lastError())
                   : monitorResult.trapMessage;
    inout.runtimeSource = "unknown";
    inout.runtime       = WirelessInterfaceState::Runtime::Unknown;
    if (ProductionNetworkTrace::active()) {
      Serial.printf("[wireless-debug] runtimeSource=unknown\n");
      Serial.printf("[production-network] WIRELESS RUNTIME VERIFICATION=unavailable\n");
    }
    return false;
  }

  inout.runtimeStatus = attrFromReply(monitorResult, 0, "status");
  inout.runtimeSource = "monitor";
  String statusLower  = inout.runtimeStatus;
  statusLower.toLowerCase();

  if (statusLower == "running-ap" || statusLower.startsWith("running-ap")) {
    inout.runtime = WirelessInterfaceState::Runtime::Running;
  } else if (statusLower == "disabled" || statusLower.indexOf("disabled") >= 0) {
    inout.runtime = WirelessInterfaceState::Runtime::NotRunning;
  } else if (statusLower.isEmpty()) {
    inout.runtime = WirelessInterfaceState::Runtime::Unknown;
  } else if (statusLower.indexOf("running") >= 0) {
    // Other legitimate operational running-* statuses.
    inout.runtime = WirelessInterfaceState::Runtime::Running;
  } else {
    // Explicit but non-running status from monitor — treat as not running.
    inout.runtime = WirelessInterfaceState::Runtime::NotRunning;
  }

  if (ProductionNetworkTrace::active()) {
    Serial.printf("[production-network] WIRELESS RUNTIME STATUS=%s\n",
                  inout.runtimeStatus.isEmpty() ? "(empty)"
                                                : inout.runtimeStatus.c_str());
    Serial.printf("[wireless-debug] runtimeSource=monitor\n");
    Serial.printf("[wireless-debug] iface=%s\n", ifaceName.c_str());
  }
  return true;
}

bool MikroTikDriver::queryWirelessInterfaceState(const String &ifaceName,
                                                 WirelessInterfaceState &out,
                                                 String &errorOut) {
  return queryWirelessInterfaceState(_routerOs, ifaceName, out, errorOut);
}

namespace {

void setProductionNetworkFailure(JsonDocument &result, const char *reason,
                                 const String &error) {
  result["ok"]             = false;
  result["provisionReady"] = false;
  result["reason"]         = reason;
  result["error"]          = error;
}

}  // namespace

bool MikroTikDriver::fillProductionNetworkDiagnostics(
    JsonDocument &result, const String &ifaceName,
    const WirelessInterfaceState &state, const String &expectedSsid) const {
  const bool enabled = state.disabled != "true";
  const bool running =
      state.runtime == WirelessInterfaceState::Runtime::Running;
  const bool runtimeUnknown =
      state.runtime == WirelessInterfaceState::Runtime::Unknown;
  const bool ssidPresent = state.ssid.length() > 0;
  const bool ssidMatches =
      expectedSsid.isEmpty() ? ssidPresent : (state.ssid == expectedSsid);
  const bool apMode = isApWirelessMode(state.mode);

  result["interface"]     = ifaceName;
  result["enabled"]       = enabled;
  result["running"]       = running;
  result["runtimeUnknown"] = runtimeUnknown;
  result["runtimeStatus"]  = state.runtimeStatus;
  result["runtimeSource"]  = state.runtimeSource;
  result["ssid"]          = state.ssid;
  result["mode"]          = state.mode;
  result["channel"]       = state.channel;
  result["expectedSsid"]  = expectedSsid;
  result["ssidMatches"]   = ssidMatches;
  result["apMode"]        = apMode;
  result["disabled"]      = !enabled;

  if (state.frequency.length() > 0) {
    const long freqMhz = state.frequency.toInt();
    if (freqMhz > 0) {
      result["frequency"] = freqMhz;
    } else {
      result["frequency"] = state.frequency;
    }
  }

  // Accept Running, or Unknown when admin-enabled AP with SSID (CASE D warning path).
  const bool provisionReady =
      enabled && apMode && ssidMatches &&
      (running || runtimeUnknown);
  result["provisionReady"] = provisionReady;
  result["ok"]             = provisionReady;

  if (provisionReady) {
    if (runtimeUnknown) {
      result["reason"] = "ok-runtime-unknown";
      result["warning"] =
          "Wireless runtime status unavailable; admin-enabled AP accepted";
    } else {
      result["reason"] = "ok";
      result["error"]  = "";
    }
    return true;
  }

  if (!enabled) {
    setProductionNetworkFailure(result, "interface-disabled",
                                "Production wireless interface is disabled");
  } else if (state.runtime == WirelessInterfaceState::Runtime::NotRunning) {
    setProductionNetworkFailure(result, "interface-not-running",
                                "Production wireless interface is not running");
  } else if (!ssidMatches) {
    if (expectedSsid.isEmpty() && !ssidPresent) {
      setProductionNetworkFailure(result, "ssid-empty",
                                  "Production SSID is not broadcasting");
    } else {
      setProductionNetworkFailure(
          result, "ssid-mismatch",
          String("SSID mismatch — expected \"") + expectedSsid +
              "\", found \"" + state.ssid + "\"");
    }
  } else if (!apMode) {
    setProductionNetworkFailure(
        result, "invalid-mode",
        String("Wireless mode is not AP/AP-bridge (mode=") + state.mode + ")");
  } else {
    setProductionNetworkFailure(result, "routeros-error",
                                "Production wireless network is not provision-ready");
  }
  return false;
}

bool MikroTikDriver::activateProductionNetworkForFinish(
    JsonDocument &result, RouterOsClient &session, bool sessionReused,
    bool reconnected) {
  result["ok"]     = false;
  result["error"]  = "";
  result["reason"] = "api-failure";

  const bool wirelessQueryRequired = true;

  if (ProductionNetworkTrace::active()) {
    ProductionNetworkTrace::logSessionState(session);
  }

  String iface;
  String errorOut;
  if (!resolveProductionWirelessInterface(iface, errorOut)) {
    if (ProductionNetworkTrace::active()) {
      ProductionNetworkTrace::logReturnFalse(
          "activateProductionNetworkForFinish:resolveProductionWirelessInterface",
          "missing-interface", "missing-interface", errorOut.c_str(), nullptr);
      ProductionNetworkTrace::logWirelessEnableOutcome(false, false, false, "no");
      ProductionNetworkTrace::logActivationSummary(sessionReused, reconnected,
                                                     wirelessQueryRequired, false);
    }
    setProductionNetworkFailure(result, "missing-interface", errorOut);
    return false;
  }

  WirelessInterfaceState state;
  if (!queryWirelessInterfaceState(session, iface, state, errorOut)) {
    const char *reason = errorOut.startsWith("Wireless interface not found")
                             ? "missing-interface"
                             : "routeros-error";
    if (ProductionNetworkTrace::active()) {
      ProductionNetworkTrace::logReturnFalse(
          "activateProductionNetworkForFinish:queryWirelessInterfaceState", reason,
          reason, errorOut.c_str(), nullptr);
      ProductionNetworkTrace::logWirelessEnableOutcome(false, false, false, "no");
      ProductionNetworkTrace::logActivationSummary(sessionReused, reconnected,
                                                     wirelessQueryRequired, false);
    }
    setProductionNetworkFailure(result, reason, errorOut);
    return false;
  }

  if (ProductionNetworkTrace::active()) {
    Serial.println(F("[production-network] WIRELESS EXISTS=yes"));
  }

  const bool disabledAfterConfig = state.disabled == "true";
  if (disabledAfterConfig) {
    const String setAttrs[] = {"=.id=" + state.id, "=disabled=no"};
    RouterOsClient::CommandResult &setResult = RouterCommandScratchContext::acquire();
    const bool setOk =
        ProductionNetworkTrace::active()
            ? productionNetworkExecute(
                  session, "activateProductionNetworkForFinish:wireless/set",
                  "/interface/wireless/set", setResult, setAttrs, 2)
            : session.executeCommand("/interface/wireless/set", setAttrs, 2,
                                     setResult);
    if (!setOk || setResult.trapReceived) {
      const String setErr =
          setResult.trapMessage.isEmpty()
              ? "Unable to enable production wireless interface"
              : setResult.trapMessage;
      if (ProductionNetworkTrace::active()) {
        ProductionNetworkTrace::logReturnFalse(
            "activateProductionNetworkForFinish:wireless/set", "routeros-error",
            "routeros-error", setErr.c_str(),
            setResult.trapReceived ? "trap" : "command failed");
        ProductionNetworkTrace::logWirelessEnableOutcome(true, false, false, "no");
        ProductionNetworkTrace::logActivationSummary(sessionReused, reconnected,
                                                     wirelessQueryRequired, false);
      }
      setProductionNetworkFailure(result, "routeros-error", setErr);
      return false;
    }
    delay(300);
    if (!queryWirelessInterfaceState(session, iface, state, errorOut)) {
      const char *reason = errorOut.startsWith("Wireless interface not found")
                               ? "missing-interface"
                               : "routeros-error";
      if (ProductionNetworkTrace::active()) {
        ProductionNetworkTrace::logReturnFalse(
            "activateProductionNetworkForFinish:queryWirelessInterfaceState(retry)",
            reason, reason, errorOut.c_str(), nullptr);
        ProductionNetworkTrace::logWirelessEnableOutcome(true, false, false, "no");
        ProductionNetworkTrace::logActivationSummary(sessionReused, reconnected,
                                                     wirelessQueryRequired, false);
      }
      setProductionNetworkFailure(result, reason, errorOut);
      return false;
    }
  }

  const bool enableVerified = state.disabled != "true";
  const bool enableCommandSent = disabledAfterConfig;
  if (!enableVerified) {
    if (ProductionNetworkTrace::active()) {
      ProductionNetworkTrace::logWirelessEnableOutcome(
          disabledAfterConfig, enableCommandSent, enableVerified, "no");
      ProductionNetworkTrace::logReturnFalse(
          "activateProductionNetworkForFinish:interface still disabled",
          "interface-disabled", "interface-disabled",
          "Production wireless interface is still disabled", nullptr);
      ProductionNetworkTrace::logActivationSummary(sessionReused, reconnected,
                                                     wirelessQueryRequired, false);
    }
    setProductionNetworkFailure(result, "interface-disabled",
                                "Production wireless interface is still disabled");
    return false;
  }

  // Prefer monitor-once whenever print does not affirmatively report running=true.
  // Missing or ambiguous print "running" must not be treated as down without monitor.
  if (state.runtime != WirelessInterfaceState::Runtime::Running) {
    String monitorError;
    queryWirelessRuntimeOnce(session, state.id, iface, state, monitorError);
    // Failure leaves runtime=Unknown — handled below (CASE D warning path).
  }

  if (ProductionNetworkTrace::active()) {
    ProductionNetworkTrace::logWirelessEnableOutcome(
        disabledAfterConfig, enableCommandSent, enableVerified,
        wirelessRuntimeLabel(static_cast<uint8_t>(state.runtime)));
    if (state.runtime == WirelessInterfaceState::Runtime::Unknown) {
      Serial.println(F("[production-network] WIRELESS RUNTIME VERIFICATION=unavailable"));
      Serial.println(F("[production-network] WIRELESS ADMIN ENABLED=yes"));
    }
  }

  if (state.runtime == WirelessInterfaceState::Runtime::NotRunning) {
    if (ProductionNetworkTrace::active()) {
      ProductionNetworkTrace::logReturnFalse(
          "activateProductionNetworkForFinish:interface not running",
          "interface-not-running", "interface-not-running",
          "Production wireless interface is not running", nullptr);
      ProductionNetworkTrace::logActivationSummary(sessionReused, reconnected,
                                                     wirelessQueryRequired, false);
    }
    setProductionNetworkFailure(result, "interface-not-running",
                                "Production wireless interface is not running");
    return false;
  }

  String expectedSsid;
  resolveExpectedProductionSsid(expectedSsid);
  if (expectedSsid.isEmpty() && _storage) {
    DynamicJsonDocument prov(RenzFiConfig::JSON_DOC_SMALL);
    if (_storage->readJson(StoragePaths::RouterProvisioningFile, prov)) {
      expectedSsid = prov["targetSsid"] | "";
    }
  }

  result["activationSource"] = "finish-provisioned";
  result["sessionReused"]    = sessionReused;

  bool ready =
      fillProductionNetworkDiagnostics(result, iface, state, expectedSsid);
  if (!ready && ProductionNetworkTrace::active()) {
    ProductionNetworkTrace::logReturnFalse(
        "activateProductionNetworkForFinish:fillProductionNetworkDiagnostics",
        result["reason"] | "routeros-error", result["reason"] | "routeros-error",
        result["error"] | "Production network verification failed", nullptr);
  }

  if (ready && ProductionNetworkTrace::active()) {
    if (state.runtime == WirelessInterfaceState::Runtime::Unknown) {
      Serial.println(F("[production-network] HOTSPOT BRIDGE VALIDATION=pass"));
      Serial.println(
          F("[production-network] PRODUCTION ACTIVATION=SUCCESS_WITH_WARNING"));
    }
  }

  if (ProductionNetworkTrace::active()) {
    ProductionNetworkTrace::logActivationSummary(sessionReused, reconnected,
                                                 wirelessQueryRequired, ready);
  }
  return ready;
}

bool MikroTikDriver::activateProductionNetwork(JsonDocument &result) {
  result["ok"]    = false;
  result["error"] = "";
  result["reason"] = "api-failure";

  String iface;
  String errorOut;
  if (!resolveProductionWirelessInterface(iface, errorOut)) {
    if (ProductionNetworkTrace::active()) {
      ProductionNetworkTrace::logReturnFalse(
          "activateProductionNetwork:resolveProductionWirelessInterface",
          "missing-interface", "missing-interface", errorOut.c_str(), nullptr);
    }
    setProductionNetworkFailure(result, "missing-interface", errorOut);
    return false;
  }

  String host;
  String username;
  String password;
  String profile;
  if (!loadRouterCredentials(host, username, password, profile)) {
    if (ProductionNetworkTrace::active()) {
      ProductionNetworkTrace::logReturnFalse(
          "activateProductionNetwork:loadRouterCredentials", "api-failure",
          "api-failure", "Router settings not available", nullptr);
    }
    setProductionNetworkFailure(result, "api-failure",
                                "Router settings not available");
    return false;
  }

  if (ProductionNetworkTrace::active()) {
    ProductionNetworkTrace::logSessionState(_routerOs);
  }
  if (!openRouterSession(host, username, password, errorOut)) {
    if (ProductionNetworkTrace::active()) {
      ProductionNetworkTrace::logReturnFalse(
          "activateProductionNetwork:openRouterSession", "api-failure",
          _routerOs.lastErrorCode().c_str(), errorOut.c_str(), nullptr);
    }
    setProductionNetworkFailure(result, "api-failure", errorOut);
    return false;
  }
  if (ProductionNetworkTrace::active()) {
    ProductionNetworkTrace::logSessionState(_routerOs);
  }

  WirelessInterfaceState state;
  if (!queryWirelessInterfaceState(iface, state, errorOut)) {
    const char *reason = errorOut.startsWith("Wireless interface not found")
                             ? "missing-interface"
                             : "routeros-error";
    if (ProductionNetworkTrace::active()) {
      ProductionNetworkTrace::logReturnFalse(
          "activateProductionNetwork:queryWirelessInterfaceState", reason,
          reason, errorOut.c_str(), nullptr);
    }
    setProductionNetworkFailure(result, reason, errorOut);
    closeRouterSession();
    return false;
  }

  if (state.disabled == "true") {
    const String setAttrs[] = {"=.id=" + state.id, "=disabled=no"};
    RouterOsClient::CommandResult &setResult = RouterCommandScratchContext::acquire();
    const bool setOk =
        ProductionNetworkTrace::active()
            ? productionNetworkExecute(_routerOs,
                                       "activateProductionNetwork:wireless/set",
                                       "/interface/wireless/set", setResult, setAttrs,
                                       2)
            : _routerOs.executeCommand("/interface/wireless/set", setAttrs, 2,
                                       setResult);
    if (!setOk || setResult.trapReceived) {
      const String setErr =
          setResult.trapMessage.isEmpty()
              ? "Unable to enable production wireless interface"
              : setResult.trapMessage;
      if (ProductionNetworkTrace::active()) {
        ProductionNetworkTrace::logReturnFalse(
            "activateProductionNetwork:wireless/set", "routeros-error",
            "routeros-error", setErr.c_str(),
            setResult.trapReceived ? "trap" : "command failed");
      }
      setProductionNetworkFailure(result, "routeros-error", setErr);
      closeRouterSession();
      return false;
    }
    delay(500);
    if (!queryWirelessInterfaceState(iface, state, errorOut)) {
      const char *reason = errorOut.startsWith("Wireless interface not found")
                               ? "missing-interface"
                               : "routeros-error";
      if (ProductionNetworkTrace::active()) {
        ProductionNetworkTrace::logReturnFalse(
            "activateProductionNetwork:queryWirelessInterfaceState(retry)",
            reason, reason, errorOut.c_str(), nullptr);
      }
      setProductionNetworkFailure(result, reason, errorOut);
      closeRouterSession();
      return false;
    }
  }

  result["interface"] = iface;
  result["ssid"]      = state.ssid;
  result["enabled"]   = state.disabled != "true";
  result["mode"]      = state.mode;

  if (state.disabled == "true") {
    if (ProductionNetworkTrace::active()) {
      ProductionNetworkTrace::logReturnFalse(
          "activateProductionNetwork:interface still disabled",
          "interface-disabled", "interface-disabled",
          "Production wireless interface is still disabled", nullptr);
    }
    setProductionNetworkFailure(result, "interface-disabled",
                                "Production wireless interface is still disabled");
    closeRouterSession();
    return false;
  }

  if (state.runtime != WirelessInterfaceState::Runtime::Running) {
    String monitorError;
    queryWirelessRuntimeOnce(_routerOs, state.id, iface, state, monitorError);
  }

  result["running"] =
      state.runtime == WirelessInterfaceState::Runtime::Running;
  result["runtimeUnknown"] =
      state.runtime == WirelessInterfaceState::Runtime::Unknown;
  result["runtimeStatus"] = state.runtimeStatus;
  result["runtimeSource"] = state.runtimeSource;

  if (state.runtime == WirelessInterfaceState::Runtime::NotRunning) {
    if (ProductionNetworkTrace::active()) {
      ProductionNetworkTrace::logReturnFalse(
          "activateProductionNetwork:interface not running",
          "interface-not-running", "interface-not-running",
          "Production wireless interface is not running", nullptr);
    }
    setProductionNetworkFailure(result, "interface-not-running",
                                "Production wireless interface is not running");
    closeRouterSession();
    return false;
  }

  // Running or Unknown (admin-enabled): accept.
  result["ok"]             = true;
  result["provisionReady"] = true;
  result["reason"] =
      state.runtime == WirelessInterfaceState::Runtime::Unknown
          ? "ok-runtime-unknown"
          : "ok";
  result["error"] = "";

  closeRouterSession();
  return true;
}

bool MikroTikDriver::productionNetworkActive(JsonDocument &result) {
  result["ok"]     = false;
  result["error"]  = "";
  result["reason"] = "api-failure";

  String iface;
  String errorOut;
  if (!resolveProductionWirelessInterface(iface, errorOut)) {
    setProductionNetworkFailure(result, "missing-interface", errorOut);
    return false;
  }

  String host;
  String username;
  String password;
  String profile;
  if (!loadRouterCredentials(host, username, password, profile)) {
    setProductionNetworkFailure(result, "api-failure",
                                "Router settings not available");
    return false;
  }

  if (!openRouterSession(host, username, password, errorOut)) {
    setProductionNetworkFailure(result, "api-failure", errorOut);
    return false;
  }

  WirelessInterfaceState state;
  if (!queryWirelessInterfaceState(iface, state, errorOut)) {
    const char *reason = errorOut.startsWith("Wireless interface not found")
                             ? "missing-interface"
                             : "routeros-error";
    setProductionNetworkFailure(result, reason, errorOut);
    closeRouterSession();
    return false;
  }

  if (state.runtime != WirelessInterfaceState::Runtime::Running) {
    String monitorError;
    queryWirelessRuntimeOnce(_routerOs, state.id, iface, state, monitorError);
  }

  String expectedSsid;
  resolveExpectedProductionSsid(expectedSsid);
  const bool ok =
      fillProductionNetworkDiagnostics(result, iface, state, expectedSsid);

  closeRouterSession();
  return ok;
}

void MikroTikDriver::observeAndRepairWan(JsonObject observationOut) {
  // Explicit Sync/Test only (caller already holds one RouterOS session).
  // Command budget target: ~3–6 targeted prints/sets. No reconnect. No loops.
  constexpr const char *kWanIface = "ether1-WAN";
  constexpr const char *kTempRouteComment = "Temporary upstream default route";
  constexpr const char *kWanDhcpComment = "RENZFI: WAN DHCP client";

  JsonObject wan = observationOut["wan"].to<JsonObject>();
  wan["known"] = true;
  wan["interface"] = kWanIface;
  wan["link"] = "unknown";
  wan["dhcp"] = "unknown";
  wan["ip"] = "";
  wan["gateway"] = "";
  wan["defaultRoute"] = "unknown";
  wan["internet"] = "unknown";
  wan["dns"] = "unknown";
  wan["note"] = "";
  uint8_t commands = 0;

  // --- Interface presence / link ---
  {
    RouterOsClient::CommandResult &ifaceResult =
        RouterCommandScratchContext::acquire();
    const String ifaceFilter[] = {
        String("?name=") + kWanIface,
        "=.proplist=name,running,disabled",
    };
    if (_routerOs.executeCommand("/interface/print", ifaceFilter, 2, ifaceResult) &&
        !ifaceResult.trapReceived) {
      commands++;
      if (ifaceResult.replyCount == 0) {
        wan["link"] = "unknown";
        wan["note"] = "WAN interface ether1-WAN not present";
        Serial.printf("[router-budget] operation=wan-observe commands=%u "
                      "session=reuse note=no-iface\n",
                      (unsigned)commands);
        return;
      }
      const String disabled = attrFromResult(ifaceResult, "disabled");
      const String running  = attrFromResult(ifaceResult, "running");
      if (disabled == "true") {
        wan["link"] = "down";
      } else if (running == "true") {
        wan["link"] = "up";
      } else if (running == "false") {
        wan["link"] = "down";
      } else {
        wan["link"] = "unknown";
      }
    }
  }

  // --- DHCP client (observe + optional ensure) ---
  String dhcpId;
  String dhcpStatus;
  String dhcpAddress;
  String dhcpGateway;
  String dhcpDisabled;
  String dhcpComment;
  bool dhcpFound = false;

  {
    RouterOsClient::CommandResult &dhcpResult =
        RouterCommandScratchContext::acquire();
    const String dhcpAttrs[] = {
        "=.proplist=.id,interface,status,address,gateway,add-default-route,"
        "disabled,comment",
    };
    if (_routerOs.executeCommand("/ip/dhcp-client/print", dhcpAttrs, 1,
                                 dhcpResult) &&
        !dhcpResult.trapReceived) {
      commands++;
      bool dhcpAddDefaultRoute = false;
      for (uint8_t i = 0; i < dhcpResult.replyCount; ++i) {
        const String iface = attrFromReply(dhcpResult, i, "interface");
        if (iface != kWanIface) continue;
        dhcpFound = true;
        dhcpId = attrFromReply(dhcpResult, i, ".id");
        dhcpStatus = attrFromReply(dhcpResult, i, "status");
        dhcpAddress = attrFromReply(dhcpResult, i, "address");
        dhcpGateway = attrFromReply(dhcpResult, i, "gateway");
        dhcpDisabled = attrFromReply(dhcpResult, i, "disabled");
        dhcpComment = attrFromReply(dhcpResult, i, "comment");
        const String addDef = attrFromReply(dhcpResult, i, "add-default-route");
        dhcpAddDefaultRoute = (addDef == "true" || addDef == "yes");
        (void)dhcpAddDefaultRoute;
        break;
      }
    }
  }

  if (dhcpFound) {
    wan["ip"] = dhcpAddress;
    wan["gateway"] = dhcpGateway;
    if (dhcpDisabled == "true") {
      wan["dhcp"] = "disabled";
      // Enable only Renz-Fi-owned disabled clients.
      if (dhcpComment.startsWith("RENZFI:") && !dhcpId.isEmpty()) {
        RouterOsClient::CommandResult &setResult =
            RouterCommandScratchContext::acquire();
        const String setAttrs[] = {"=.id=" + dhcpId, "=disabled=no"};
        if (_routerOs.executeCommand("/ip/dhcp-client/set", setAttrs, 2,
                                     setResult) &&
            !setResult.trapReceived) {
          commands++;
          wan["dhcp"] = "searching";
          wan["note"] = "Enabled Renz-Fi WAN DHCP client";
        }
      }
    } else if (dhcpStatus == "bound") {
      wan["dhcp"] = "bound";
    } else if (dhcpStatus.length() > 0) {
      wan["dhcp"] = "searching";
    } else {
      wan["dhcp"] = "unknown";
    }
  } else {
    // No DHCP client on ether1-WAN — skip create if a static address exists.
    bool staticWan = false;
    RouterOsClient::CommandResult &addrResult =
        RouterCommandScratchContext::acquire();
    const String addrFilter[] = {
        String("?interface=") + kWanIface,
        "=.proplist=address,dynamic,disabled",
    };
    if (_routerOs.executeCommand("/ip/address/print", addrFilter, 2, addrResult) &&
        !addrResult.trapReceived) {
      commands++;
      for (uint8_t i = 0; i < addrResult.replyCount; ++i) {
        const String dyn = attrFromReply(addrResult, i, "dynamic");
        const String dis = attrFromReply(addrResult, i, "disabled");
        if (dis == "true") continue;
        if (dyn != "true") {
          staticWan = true;
          wan["ip"] = attrFromReply(addrResult, i, "address");
          break;
        }
      }
    }

    if (staticWan) {
      wan["dhcp"] = "disabled";
      wan["note"] = "Static WAN address present — DHCP client not created";
    } else {
      RouterOsClient::CommandResult &addResult =
          RouterCommandScratchContext::acquire();
      const String addAttrs[] = {
          String("=interface=") + kWanIface,
          "=add-default-route=yes",
          "=use-peer-dns=yes",
          String("=comment=") + kWanDhcpComment,
      };
      if (_routerOs.executeCommand("/ip/dhcp-client/add", addAttrs, 4, addResult) &&
          !addResult.trapReceived) {
        commands++;
        wan["dhcp"] = "searching";
        wan["note"] = "Created Renz-Fi WAN DHCP client";
        Serial.println("[wan] dhcp-client added interface=ether1-WAN "
                       "add-default-route=yes");
      } else {
        wan["dhcp"] = "unknown";
        wan["note"] = "Unable to create WAN DHCP client";
      }
    }
  }

  // --- Default route (active vs merely present) ---
  // A RouterOS *read failure* is NOT proof that no default route exists.
  String tempRouteId;
  bool activeDefault = false;
  String activeGateway;
  bool routeQueryOk = false;
  {
    RouterOsClient::CommandResult &routeResult =
        RouterCommandScratchContext::acquire();
    const String routeFilter[] = {
        "?dst-address=0.0.0.0/0",
        "=.proplist=.id,gateway,active,dynamic,comment,distance",
    };
    auto runRouteQuery = [&]() -> bool {
      return _routerOs.executeCommand("/ip/route/print", routeFilter, 2,
                                      routeResult) &&
             !routeResult.trapReceived;
    };
    routeQueryOk = runRouteQuery();
    if (!routeQueryOk) {
      // One bounded retry — same filtered query, same session.
      routeResult.clearKeepCapacity();
      routeQueryOk = runRouteQuery();
    }
    if (routeQueryOk) {
      commands++;
      for (uint8_t i = 0; i < routeResult.replyCount; ++i) {
        const String active = attrFromReply(routeResult, i, "active");
        const String gateway = attrFromReply(routeResult, i, "gateway");
        const String comment = attrFromReply(routeResult, i, "comment");
        const String id = attrFromReply(routeResult, i, ".id");
        if (comment == kTempRouteComment && !id.isEmpty()) {
          tempRouteId = id;
        }
        if (active == "true") {
          activeDefault = true;
          activeGateway = gateway;
        }
      }
    }
  }

  if (routeQueryOk) {
    wan["defaultRoute"] = activeDefault ? "available" : "unavailable";
  } else {
    wan["defaultRoute"] = "unknown";
    if (String(wan["note"] | "").length() == 0) {
      wan["note"] = "Unable to verify default route (RouterOS query failed)";
    }
  }
  if (activeDefault) {
    if (wan["gateway"].as<String>().length() == 0 && activeGateway.length() > 0) {
      wan["gateway"] = activeGateway;
    }
  }

  // Remove ONLY the exact Renz-Fi-legacy temporary static when DHCP owns routing.
  if (!tempRouteId.isEmpty() &&
      (strcmp(wan["dhcp"] | "", "bound") == 0) &&
      activeDefault) {
    RouterOsClient::CommandResult &rm = RouterCommandScratchContext::acquire();
    const String rmAttrs[] = {"=.id=" + tempRouteId};
    if (_routerOs.executeCommand("/ip/route/remove", rmAttrs, 1, rm) &&
        !rm.trapReceived) {
      commands++;
      Serial.println("[wan] removed stale Temporary upstream default route");
      if (String(wan["note"] | "").length() == 0) {
        wan["note"] = "Removed stale temporary default route";
      }
    }
  }

  // Bounded reachability — one ICMP only when an active default route exists
  // AND the worker still has enough job budget. Sync often spends most of
  // ROUTER_WORKER_JOB_TIMEOUT_MS before WAN observe; starting /ping near the
  // deadline yields (read failed) and must not be treated as Internet down.
  // Prefer skipping ping over raising the global timeout (which hides Sync cost).
  static constexpr uint32_t kPingMinBudgetMs = 3000;
  if (activeDefault) {
    const uint32_t budgetMs = RouterApiTransportGate::remainingJobBudgetMs();
    if (budgetMs < kPingMinBudgetMs) {
      wan["internet"] = "unknown";
      if (String(wan["note"] | "").length() == 0) {
        wan["note"] = "Reachability unverified (insufficient sync budget for ping)";
      }
      Serial.printf("[wan] ping skipped remaining_budget_ms=%u need=%u\n",
                    static_cast<unsigned>(budgetMs),
                    static_cast<unsigned>(kPingMinBudgetMs));
    } else {
      RouterOsClient::CommandResult &pingResult =
          RouterCommandScratchContext::acquire();
      const String pingAttrs[] = {"=address=8.8.8.8", "=count=1"};
      if (_routerOs.executeCommand("/ping", pingAttrs, 2, pingResult) &&
          !pingResult.trapReceived) {
        commands++;
        const String received = attrFromResult(pingResult, "received");
        if (received.length() > 0) {
          wan["internet"] = (received.toInt() > 0) ? "online" : "offline";
        } else if (pingResult.replyCount > 0) {
          wan["internet"] = "online";
        } else {
          wan["internet"] = "offline";
        }
      } else {
        // Ping command failed — not proof that Internet is down.
        wan["internet"] = "unknown";
        if (String(wan["note"] | "").length() == 0) {
          wan["note"] = "Reachability unverified (ping probe failed)";
        }
      }
    }
  } else if (!routeQueryOk) {
    // Observation failure — do not claim Internet is down.
    wan["internet"] = "unknown";
  } else {
    wan["internet"] = "offline";
  }

  // DNS: without a second probe, infer only when DHCP peer-DNS path is bound
  // and internet ICMP succeeded. Otherwise leave unknown (no continuous DNS poll).
  if (strcmp(wan["internet"] | "", "online") == 0 &&
      strcmp(wan["dhcp"] | "", "bound") == 0) {
    wan["dns"] = "working";
  } else if (strcmp(wan["defaultRoute"] | "", "unavailable") == 0) {
    wan["dns"] = "failed";
  } else if (strcmp(wan["defaultRoute"] | "", "unknown") == 0) {
    wan["dns"] = "unknown";
  }

  Serial.printf(
      "[wan] link=%s dhcp=%s route=%s internet=%s gateway=%s cmds=%u\n",
      wan["link"] | "?", wan["dhcp"] | "?", wan["defaultRoute"] | "?",
      wan["internet"] | "?", wan["gateway"] | "", (unsigned)commands);
  Serial.printf(
      "[router-budget] operation=wan-observe commands=%u session=reuse\n",
      (unsigned)commands);
}

bool MikroTikDriver::collectCacheSnapshot(JsonDocument &out,
                                          RouterCacheCollectMode mode) {
  out["error"] = "";
  const bool telemetryOnly = (mode == RouterCacheCollectMode::Telemetry);
  const uint32_t t0 = millis();
  Serial.printf("[router-%s] mode=%s opening session\n",
                telemetryOnly ? "refresh" : "sync",
                telemetryOnly ? "telemetry" : "configuration");

  String host;
  String username;
  String password;
  String profileName;
  if (!loadRouterCredentials(host, username, password, profileName)) {
    out["error"] = "Router settings not available";
    return false;
  }

  Serial.printf(
      "[router-config] host=%s port=%u usernameConfigured=%s "
      "usernameLength=%u passwordConfigured=%s passwordLength=%u "
      "source=production-router-json\n",
      host.c_str(), (unsigned)RenzFiConfig::ROUTEROS_API_PORT,
      username.isEmpty() ? "no" : "yes", (unsigned)username.length(),
      password.isEmpty() ? "no" : "yes", (unsigned)password.length());

  String bridgeHint;
  String selectedWireless;
  if (_storage) {
    DynamicJsonDocument provDoc(RenzFiConfig::JSON_DOC_SMALL);
    if (_storage->readJson(StoragePaths::RouterProvisioningFile, provDoc)) {
      bridgeHint       = provDoc["guestBridgeName"] | "";
      selectedWireless = provDoc["selectedWirelessInterface"] | "";
      if (!telemetryOnly) {
        const char *storedIdentity = provDoc["routerIdentity"] | "";
        if (storedIdentity && strlen(storedIdentity) > 0) {
          out["identity"] = storedIdentity;
        }
        const char *storedVersion = provDoc["routerVersion"] | "";
        if (storedVersion && strlen(storedVersion) > 0) {
          out["routerOsVersion"] = storedVersion;
        }
      }
    }
  }

  // Always stamp host so applyLiveSnapshot keeps isPopulated() true.
  out["routerIp"] = host;
  if (!telemetryOnly) {
    out["hotspotProfile"] = profileName;
  }

  String errorOut;
  if (!openRouterSession(host, username, password, errorOut)) {
    out["error"] = errorOut;
    return false;
  }

  // Shared: resource print (version for Sync; CPU/mem/uptime for Refresh).
  RouterOsClient::CommandResult &resourceResult =
      RouterCommandScratchContext::acquire();
  bool resourceOk = false;
  if (_routerOs.executeCommand("/system/resource/print", resourceResult) &&
      !resourceResult.trapReceived) {
    resourceOk = true;
    JsonObject routerOs = out["routerOs"].to<JsonObject>();
    const String version = attrFromResult(resourceResult, "version");
    routerOs["version"]     = version;
    routerOs["cpuLoad"]     = attrFromResult(resourceResult, "cpu-load");
    routerOs["freeMemory"]  = attrFromResult(resourceResult, "free-memory");
    routerOs["totalMemory"] = attrFromResult(resourceResult, "total-memory");
    routerOs["uptime"]      = attrFromResult(resourceResult, "uptime");
    if (!version.isEmpty()) out["routerOsVersion"] = version;
  }

  if (telemetryOnly) {
    Serial.println("[router-refresh] resource");
    if (!resourceOk) {
      out["error"] = "Unable to read RouterOS resource telemetry";
      closeRouterSession();
      return false;
    }

    // Light HotSpot status only — read-only, no mutations.
    RouterOsClient::CommandResult &hotspotResult =
        RouterCommandScratchContext::acquire();
    const String hotspotAttrs[] = {"=.proplist=name,interface,disabled"};
    JsonObject observation = out["observation"].to<JsonObject>();
    observation["connectivity"] = "online";
    if (_routerOs.executeCommand("/ip/hotspot/print", hotspotAttrs, 1,
                                 hotspotResult) &&
        !hotspotResult.trapReceived && hotspotResult.replyCount > 0) {
      const String hsName = attrFromResult(hotspotResult, "name");
      const String hsIface = attrFromResult(hotspotResult, "interface");
      if (!hsName.isEmpty()) {
        out["hotspotServer"] = hsName;
        observation["hotspotStatus"] = "available";
        observation["hotspotServer"] = hsName;
        if (!hsIface.isEmpty()) observation["hotspotInterface"] = hsIface;
      } else {
        observation["hotspotStatus"] = "unavailable";
      }
    } else {
      observation["hotspotStatus"] = "unknown";
    }
    Serial.println("[router-refresh] hotspot-status");
    closeRouterSession();
    Serial.printf("[router-refresh] completed duration=%lums\n",
                  static_cast<unsigned long>(millis() - t0));
    return true;
  }

  // —— Configuration Sync (no WAN repair, no captive reconcile) ——
  Serial.println("[router-sync] identity");
  RouterOsClient::CommandResult &identityResult =
      RouterCommandScratchContext::acquire();
  if (_routerOs.executeCommand("/system/identity/print", identityResult) &&
      !identityResult.trapReceived) {
    const String identity = identityFromResult(identityResult);
    if (!identity.isEmpty()) out["identity"] = identity;
  }

  Serial.println("[router-sync] wireless");
  RouterWireless::CanonicalConfig canonical;
  if (_storage) {
    RouterWireless::loadCanonicalConfig(_storage, canonical);
  }

  bool wirelessCached = false;
  if (canonical.configured && !canonical.interfaceId.isEmpty()) {
    DynamicJsonDocument wirelessLive(RenzFiConfig::JSON_DOC_SMALL);
    JsonObject wirelessObj = wirelessLive.to<JsonObject>();
    String readError;
    if (RouterWireless::readInterface(_routerOs, canonical.interfaceId, wirelessObj,
                                      readError)) {
      wirelessCached = true;
      const char *iface = wirelessObj["interface"] | canonical.interfaceId.c_str();
      const char *ssid  = wirelessObj["ssid"] | "";
      const char *sec   = wirelessObj["security"] | "";
      out["wirelessInterface"] = iface;
      out["ssid"]              = ssid;
      out["security"]          = sec;
      if (wirelessObj.containsKey("band")) {
        out["band"] = wirelessObj["band"];
      }
    }
  }

  if (!wirelessCached) {
    RouterOsClient::CommandResult &wirelessResult =
        RouterCommandScratchContext::acquire();
    const String wirelessAttrs[] = {
        "=.proplist=.id,name,ssid,security-profile,band,frequency,disabled,running",
    };
    if (_routerOs.executeCommand("/interface/wireless/print", wirelessAttrs, 1,
                                 wirelessResult) &&
        !wirelessResult.trapReceived && wirelessResult.replyCount > 0) {
      uint8_t chosenIdx = 0;
      if (!selectedWireless.isEmpty()) {
        for (uint8_t i = 0; i < wirelessResult.replyCount; ++i) {
          const String name = attrFromReply(wirelessResult, i, "name");
          if (name == selectedWireless) {
            chosenIdx = i;
            break;
          }
        }
      }

      const String ifaceName = attrFromReply(wirelessResult, chosenIdx, "name");
      const String ssid      = attrFromReply(wirelessResult, chosenIdx, "ssid");
      const String secProfile =
          attrFromReply(wirelessResult, chosenIdx, "security-profile");
      const String bandRaw = attrFromReply(wirelessResult, chosenIdx, "band");
      const String frequency =
          attrFromReply(wirelessResult, chosenIdx, "frequency");
      out["wirelessInterface"] = ifaceName;
      out["ssid"]              = ssid;
      if (!bandRaw.isEmpty()) {
        out["band"] = RouterWireless::formatBandLabel(bandRaw);
      } else if (!frequency.isEmpty()) {
        const long freqMhz = frequency.toInt();
        const String derived =
            RouterWireless::formatBandFromFrequencyMhz(freqMhz);
        if (!derived.isEmpty()) out["band"] = derived;
      }

      if (secProfile.isEmpty()) {
        out["security"] = "unknown";
      } else {
        RouterOsClient::CommandResult &secResult =
            RouterCommandScratchContext::acquire();
        const String secFilter[] = {
            "?name=" + secProfile,
            "=.proplist=name,mode,authentication-types,wpa-pre-shared-key,"
            "wpa2-pre-shared-key",
        };
        if (_routerOs.executeCommand(
                "/interface/wireless/security-profiles/print", secFilter, 2,
                secResult) &&
            !secResult.trapReceived && secResult.replyCount > 0) {
          const String authTypes =
              attrFromResult(secResult, "authentication-types");
          out["security"] = authTypes.isEmpty() ? "none" : authTypes;
        } else {
          out["security"] = "unknown";
        }
      }
    }
  }

  if (!bridgeHint.isEmpty()) {
    out["bridge"] = bridgeHint;
  }

  // Intentionally NOT calling reconcileCaptiveHotspotPath — retained for
  // future Diagnostics/Repair only (Phase 3).

  Serial.println("[router-sync] hotspot");
  {
    RouterOsClient::CommandResult &hotspotResult =
        RouterCommandScratchContext::acquire();
    const String hotspotAttrs[] = {"=.proplist=name,interface,disabled"};
    if (_routerOs.executeCommand("/ip/hotspot/print", hotspotAttrs, 1,
                                 hotspotResult) &&
        !hotspotResult.trapReceived && hotspotResult.replyCount > 0) {
      out["hotspotServer"] = attrFromResult(hotspotResult, "name");
      const String hsInterface = attrFromResult(hotspotResult, "interface");
      if (bridgeHint.isEmpty() && !hsInterface.isEmpty()) {
        out["bridge"] = hsInterface;
      }
    }
  }

  // Intentionally NOT fetching html-directory via hotspot profile print.

  Serial.println("[router-sync] profiles");
  RouterOsClient::CommandResult &userProfiles =
      RouterCommandScratchContext::acquire();
  const String userProfileAttrs[] = {"=.proplist=name,rate-limit"};
  if (_routerOs.executeCommand("/ip/hotspot/user/profile/print", userProfileAttrs,
                               1, userProfiles) &&
      !userProfiles.trapReceived) {
    JsonArray profiles = out["profiles"].to<JsonArray>();
    JsonArray details  = out["profileDetails"].to<JsonArray>();
    profileDetailsFromResult(userProfiles, details, profiles);
    if (userProfiles.replyLimitReached) out["truncated"] = true;
  }

  JsonObject observation = out["observation"].to<JsonObject>();
  observation["connectivity"] = "online";
  const char *hsServer = out["hotspotServer"] | "";
  const String hsIfaceObs = out["bridge"] | "";
  if (hsServer && strlen(hsServer) > 0) {
    observation["hotspotStatus"] = "available";
    observation["hotspotServer"] = hsServer;
    if (!hsIfaceObs.isEmpty()) {
      observation["hotspotInterface"] = hsIfaceObs;
    }
  } else {
    observation["hotspotStatus"] = "unavailable";
  }

  // Intentionally NOT calling observeAndRepairWan — retained for future
  // Diagnostics/Repair only (Phase 3). No /ip/route/print on Sync/Refresh.

  closeRouterSession();
  Serial.println("[router-sync] cache-save pending");
  Serial.printf("[router-sync] completed duration=%lums\n",
                static_cast<unsigned long>(millis() - t0));
  return true;
}

bool MikroTikDriver::saveWireless(JsonObjectConst settings, JsonDocument &out) {
  out["ssid"]         = "";
  out["security"]     = "";
  out["error"]        = "";
  out["applied"]      = false;
  out["verified"]     = false;
  out["verification"] = "failed";

  const String nextSsid     = settings["ssid"].is<const char *>()
                                  ? settings["ssid"].as<const char *>()
                                  : "";
  const String nextPassword = settings["password"].is<const char *>()
                                  ? settings["password"].as<const char *>()
                                  : "";
  (void)nextPassword;  // SSID-only mutation — password not applied to RouterOS.

  if (nextSsid.isEmpty()) {
    out["error"] = "SSID is required";
    return false;
  }

  RouterWireless::CanonicalConfig canonical;
  if (!_storage || !RouterWireless::loadCanonicalConfig(_storage, canonical)) {
    out["error"] = "Wireless is not configured — complete setup first";
    return false;
  }

  String host;
  String username;
  String password;
  String profile;
  if (!loadRouterCredentials(host, username, password, profile)) {
    out["error"] = "Router settings not available";
    return false;
  }

  String errorOut;
  if (!openRouterSession(host, username, password, errorOut)) {
    out["error"] = errorOut;
    if (_logger) _logger->error("router", "Wireless save failed: " + errorOut);
    return false;
  }

  // Targeted SSID transaction — no post-set inventory / wifiwave2 fallback.
  DynamicJsonDocument mutation(RenzFiConfig::JSON_DOC_SMALL);
  JsonObject mutationObj = mutation.to<JsonObject>();
  const bool applied =
      RouterWireless::applySsidOnly(_routerOs, canonical.interfaceId, nextSsid,
                                    mutationObj, errorOut);
  closeRouterSession();

  if (!applied) {
    out["error"] = errorOut;
    if (_logger) _logger->error("router", "Wireless SSID set failed: " + errorOut);
    return false;
  }

  // Requested SSID is authoritative after SET ACK (verification deferred).
  const String resultSsid = nextSsid;

  // Persist configured SSID when RouterOS accepted the set (applied).
  // Immediate post-SET readback is intentionally omitted (deferred verify).
  canonical.ssid = resultSsid;
  if (!RouterWireless::saveCanonicalFields(_storage, canonical)) {
    out["error"] = "Router updated, but local wireless settings were not saved";
    return false;
  }

  out["applied"]      = true;
  out["verified"]     = false;
  out["verification"] = "deferred";
  out["ssid"]         = resultSsid;
  out["interface"]    = canonical.interfaceId;
  // SSID-only: do not invent or rewrite security — leave blank so cache merge
  // preserves existing security via copyStringField (skips empty).
  out["security"]     = "";
  RouterWireless::fillWirelessApiJson(canonical, out.as<JsonObject>());

  Serial.printf("[router-wireless] cache patched ssid=\"%s\"\n", resultSsid.c_str());
  Serial.println("[router-wireless] verification=deferred");
  Serial.println("[router-wireless] ssid-save complete");
  if (_logger) {
    _logger->info("router",
                  "Wireless SSID change applied (verification=deferred)");
  }
  return true;
}

bool MikroTikDriver::testSettings(JsonObjectConst overrideSettings,
                                  JsonDocument &out) {
  out["connected"]     = false;
  out["authenticated"] = false;
  out["profileFound"]  = false;
  out["identity"]      = "";
  out["error"]         = "";
  out["ok"]            = false;
  out["profiles"]       = JsonArray();
  out["profileDetails"] = JsonArray();

  DynamicJsonDocument settings(RenzFiConfig::JSON_DOC_SMALL);
  loadSettings(settings);
  mergeSettings(overrideSettings, settings);

  const String host     = settings["host"] | "";
  const String username = settings["username"] | "";
  const String password = settings["password"] | "";
  const String profile  = settings["profile"] | "default";

  JsonObject observation = out["observation"].to<JsonObject>();
  observation["connectivity"]  = "offline";
  observation["hotspotStatus"] = "unknown";

  if (profile.isEmpty()) {
    out["error"] = "Hotspot profile is not configured";
    observation["lastContactError"] = out["error"];
    return false;
  }

  String errorOut;
  if (!openRouterSession(host, username, password, errorOut)) {
    out["error"] = errorOut;
    observation["lastContactError"] = errorOut;
    if (_logger) _logger->error("router", "RouterOS test failed: " + errorOut);
    return false;
  }
  out["connected"]     = true;
  out["authenticated"] = true;
  // Canonical cache keys — same host/profile that opened this session.
  // Without routerIp, RouterCacheManager::isPopulated() stays false and Test
  // logs cache-not-populated despite RouterOS success.
  out["routerIp"]       = host;
  out["hotspotProfile"] = profile;
  // Session established — router reachability is online even if hotspot
  // profile inventory later fails.
  observation["connectivity"] = "online";

  RouterOsClient::CommandResult &identityResult = RouterCommandScratchContext::acquire();
  if (!_routerOs.executeCommand("/system/identity/print", identityResult)) {
    out["error"] = _routerOs.lastError();
    observation["lastContactError"] = out["error"];
    closeRouterSession();
    return false;
  }
  if (identityResult.trapReceived) {
    out["error"] = identityResult.trapMessage.isEmpty()
                       ? "Failed to read router identity"
                       : identityResult.trapMessage;
    observation["lastContactError"] = out["error"];
    closeRouterSession();
    return false;
  }

  const String identity = identityFromResult(identityResult);
  out["identity"] = identity.isEmpty() ? "RouterOS" : identity;
  _cachedIdentity = out["identity"].as<const char *>();

  // One-shot resource snapshot for System Configuration status panel.
  RouterOsClient::CommandResult &resourceResult = RouterCommandScratchContext::acquire();
  if (_routerOs.executeCommand("/system/resource/print", resourceResult) &&
      !resourceResult.trapReceived) {
    JsonObject routerOs = out["routerOs"].to<JsonObject>();
    routerOs["version"]     = attrFromResult(resourceResult, "version");
    routerOs["cpuLoad"]     = attrFromResult(resourceResult, "cpu-load");
    routerOs["freeMemory"]  = attrFromResult(resourceResult, "free-memory");
    routerOs["totalMemory"] = attrFromResult(resourceResult, "total-memory");
    routerOs["uptime"]      = attrFromResult(resourceResult, "uptime");
  }

  // Same-session bounded Hotspot User Profile inventory (name + rate-limit).
  RouterOsClient::CommandResult &profileResult = RouterCommandScratchContext::acquire();
  const String profileAttrs[] = {"=.proplist=name,rate-limit"};
  if (!_routerOs.executeCommand("/ip/hotspot/user/profile/print", profileAttrs, 1,
                                profileResult)) {
    out["error"] = _routerOs.lastError();
    observation["hotspotStatus"]    = "unknown";
    observation["lastContactError"] = out["error"];
    closeRouterSession();
    return false;
  }
  if (profileResult.trapReceived) {
    out["error"] = profileResult.trapMessage.isEmpty()
                       ? "Failed to read hotspot user profiles"
                       : profileResult.trapMessage;
    observation["hotspotStatus"]    = "unavailable";
    observation["lastContactError"] = out["error"];
    closeRouterSession();
    return false;
  }

  JsonArray profiles = out["profiles"].to<JsonArray>();
  JsonArray details  = out["profileDetails"].to<JsonArray>();
  profileDetailsFromResult(profileResult, details, profiles);
  if (profileResult.replyLimitReached) {
    out["truncated"] = true;
  }

  if (!profileExistsInResult(profileResult, profile)) {
    out["error"] = String("Hotspot profile not found: ") + profile;
    observation["hotspotStatus"]    = "unavailable";
    observation["lastContactError"] = out["error"];
    closeRouterSession();
    return false;
  }

  out["profileFound"] = true;

  // Observational Hotspot server presence (bridge-aware: any interface).
  // Failure here does not fail Test — profiles already prove Hotspot package.
  RouterOsClient::CommandResult &hotspotResult = RouterCommandScratchContext::acquire();
  const String hotspotAttrs[] = {"=.proplist=name,interface,disabled"};
  bool hotspotAvailable = false;
  if (_routerOs.executeCommand("/ip/hotspot/print", hotspotAttrs, 1, hotspotResult) &&
      !hotspotResult.trapReceived) {
    for (uint8_t i = 0; i < hotspotResult.replyCount; ++i) {
      const String disabled = attrFromReply(hotspotResult, i, "disabled");
      if (disabled == "true") continue;
      const String hsName = attrFromReply(hotspotResult, i, "name");
      const String hsIface = attrFromReply(hotspotResult, i, "interface");
      if (!hsName.isEmpty()) observation["hotspotServer"] = hsName;
      if (!hsIface.isEmpty()) observation["hotspotInterface"] = hsIface;
      hotspotAvailable = true;
      break;
    }
  }
  observation["hotspotStatus"] = hotspotAvailable ? "available" : "unavailable";
  if (!hotspotAvailable && profiles.size() > 0) {
    // Profiles exist but no enabled hotspot server — still report unavailable.
    observation["hotspotStatus"] = "unavailable";
  }

  // Same-session production wireless observation. Without this, Test populates
  // SSID from local canonical config only and leaves cache security blank
  // (hardware: security=) even when MikroTik is Open.
  RouterWireless::CanonicalConfig canonical;
  if (_storage) {
    RouterWireless::loadCanonicalConfig(_storage, canonical);
  }
  if (canonical.configured && !canonical.interfaceId.isEmpty()) {
    DynamicJsonDocument wirelessLive(RenzFiConfig::JSON_DOC_SMALL);
    JsonObject wirelessObj = wirelessLive.to<JsonObject>();
    String readError;
    if (RouterWireless::readInterface(_routerOs, canonical.interfaceId, wirelessObj,
                                      readError)) {
      const String iface = wirelessObj["interface"].as<String>();
      const String ssid  = wirelessObj["ssid"].as<String>();
      const String sec   = wirelessObj["security"].as<String>();
      out["wirelessInterface"] =
          iface.length() > 0 ? iface : canonical.interfaceId;
      if (ssid.length() > 0) out["ssid"] = ssid;
      // readInterfaceSecurity: empty auth-types after successful read → "none";
      // failed security query → "unknown". Never invent Open on failure.
      out["security"] = sec.length() > 0 ? sec : "unknown";
      if (wirelessObj["band"].as<String>().length() > 0) {
        out["band"] = wirelessObj["band"].as<String>();
      }
    } else {
      out["security"] = "unknown";
    }
  }

  out["ok"] = true;
  observation["lastContactError"] = "";

  observeAndRepairWan(observation);

  if (_logger) {
    _logger->info("router",
                  String("RouterOS test OK — identity=") +
                      out["identity"].as<const char *>() + " profile=" + profile +
                      " profiles=" + profiles.size());
  }

  closeRouterSession();
  return true;
}

bool MikroTikDriver::removeHotspotActiveByMac(const String &mac) {
  if (mac.isEmpty()) return true;
  RouterOsClient::CommandResult &activeResult = RouterCommandScratchContext::acquire();
  if (!_routerOs.executeCommand("/ip/hotspot/active/print", "?mac-address=" + mac,
                                activeResult)) {
    return false;
  }
  const String activeId = idFromResult(activeResult);
  if (activeId.isEmpty()) return true;
  const String removeAttrs[] = {"=.id=" + activeId};
  RouterOsClient::CommandResult &removeResult = RouterCommandScratchContext::acquire();
  return _routerOs.executeCommand("/ip/hotspot/active/remove", removeAttrs, 1,
                                  removeResult) &&
         !removeResult.trapReceived;
}

bool MikroTikDriver::removeHotspotCookiesByMac(const String &mac) {
  // Targeted cookie cleanup only — never clears the whole cookie table.
  // Prefer user= (stripped MAC credential contract); fall back to mac-address=.
  if (mac.isEmpty()) return true;
  const String hotspotUser = macToHotspotUsername(mac);
  String ids[8];
  uint8_t idCount = 0;

  auto collectIds = [&](const String &query) {
    RouterOsClient::CommandResult &cookieResult =
        RouterCommandScratchContext::acquire();
    if (!_routerOs.executeCommand("/ip/hotspot/cookie/print", query, cookieResult)) {
      return;
    }
    for (uint8_t i = 0; i < cookieResult.replyCount && idCount < 8; ++i) {
      const String id = attrFromReply(cookieResult, i, ".id");
      if (id.isEmpty()) continue;
      bool dup = false;
      for (uint8_t j = 0; j < idCount; ++j) {
        if (ids[j] == id) {
          dup = true;
          break;
        }
      }
      if (!dup) ids[idCount++] = id;
    }
  };

  collectIds("?user=" + hotspotUser);
  if (idCount == 0) {
    collectIds("?mac-address=" + mac);
  }

  bool ok = true;
  for (uint8_t i = 0; i < idCount; ++i) {
    const String removeAttrs[] = {"=.id=" + ids[i]};
    RouterOsClient::CommandResult &removeResult = RouterCommandScratchContext::acquire();
    if (!_routerOs.executeCommand("/ip/hotspot/cookie/remove", removeAttrs, 1,
                                  removeResult) ||
        removeResult.trapReceived) {
      ok = false;
    }
  }
  return ok;
}

bool MikroTikDriver::lastActivateAuthTrace(ActivateAuthTrace &out) const {
  out = _lastActivateTrace;
  return _lastActivateTrace.authorizedAtMs != 0 ||
         _lastActivateTrace.activeLoginSuccess ||
         _lastActivateTrace.usedActiveSet;
}

bool MikroTikDriver::loginHotspotActive(const HotspotUser &user,
                                        const String &hotspotUser,
                                        const String &hotspotPassword) {
  // Prefer in-place active update when already authorized (Add Time / leftover
  // Active). RouterOS Active limit-uptime is a session-start cap:
  //   session-time-left ≈ limit-uptime − active.uptime
  // so the write must be Model B, same as the user object:
  //   new_active_limit = active.uptime + requested_seconds
  String activeId;
  uint32_t existingActiveUptime = 0;
  uint32_t existingActiveLeft = 0;
  if (!user.mac.isEmpty()) {
    RouterOsClient::CommandResult &activePrint =
        RouterCommandScratchContext::acquire();
    if (_routerOs.executeCommand("/ip/hotspot/active/print",
                                 "?mac-address=" + user.mac, activePrint)) {
      activeId = idFromResult(activePrint);
      existingActiveUptime =
          parseRouterOsDurationSeconds(attrFromResult(activePrint, "uptime"));
      existingActiveLeft = parseRouterOsDurationSeconds(
          attrFromResult(activePrint, "session-time-left"));
    }
  }

  _lastActivateTrace.activeUptime = existingActiveUptime;
  _lastActivateTrace.activeSessionTimeLeft = existingActiveLeft;
  _lastActivateTrace.grantedSeconds = user.timeoutSeconds;

  if (!activeId.isEmpty()) {
    // Active already exists = Internet authorization already granted.
    // Do NOT send /ip/hotspot/active/set limit-uptime — this RouterOS
    // rejects that parameter (TRAP unknown parameter limit-uptime).
    // Entitlement is the user object (Model B user/set already applied).
    _lastActivateTrace.usedActiveSet = false;
    _lastActivateTrace.activeLoginSuccess = true;
    _lastActivateTrace.activeVerifySuccess = true;
    _lastActivateTrace.authorizedAtMs = millis();
    Serial.printf(
        "[activate] operation=active_present mac=%s active_id=%s "
        "active_uptime=%u session_time_left=%u requested=%u "
        "(no active/set)\n",
        user.mac.c_str(), activeId.c_str(),
        (unsigned)existingActiveUptime, (unsigned)existingActiveLeft,
        (unsigned)user.timeoutSeconds);
    return true;
  }

  String loginAttrs[4];
  uint8_t attrCount = 0;
  loginAttrs[attrCount++] = "=user=" + hotspotUser;
  loginAttrs[attrCount++] = "=password=" + hotspotPassword;
  if (!user.mac.isEmpty()) {
    loginAttrs[attrCount++] = "=mac-address=" + user.mac;
  }
  if (!user.ip.isEmpty()) {
    loginAttrs[attrCount++] = "=ip=" + user.ip;
  }

  RouterOsClient::CommandResult &loginResult = RouterCommandScratchContext::acquire();
  const bool ok =
      _routerOs.executeCommand("/ip/hotspot/active/login", loginAttrs, attrCount,
                               loginResult) &&
      !loginResult.trapReceived;
  if (!ok) {
    const String reason = loginResult.trapMessage.isEmpty() ? _routerOs.lastError()
                                                            : loginResult.trapMessage;
    Serial.printf("[activate] operation=active_login mac=%s result=fail reason=%s\n",
                  user.mac.c_str(), reason.c_str());
    if (_logger) {
      _logger->error("router", "Hotspot active login failed: " + reason);
    }
    return false;
  }

  _lastActivateTrace.activeLoginSuccess = true;
  _lastActivateTrace.authorizedAtMs = millis();

  // Confirm authorization actually produced an Active row (same API session).
  if (!user.mac.isEmpty()) {
    RouterOsClient::CommandResult &verify =
        RouterCommandScratchContext::acquire();
    if (_routerOs.executeCommand("/ip/hotspot/active/print",
                                 "?mac-address=" + user.mac, verify)) {
      const String verifiedId = idFromResult(verify);
      if (verifiedId.isEmpty()) {
        Serial.printf(
            "[activate] operation=active_login mac=%s result=fail "
            "reason=no_active_after_login\n",
            user.mac.c_str());
        if (_logger) {
          _logger->error("router",
                         "Hotspot active login returned ok but no active row for " +
                             user.mac);
        }
        return false;
      }
      _lastActivateTrace.activeVerifySuccess = true;
      _lastActivateTrace.activeUptime =
          parseRouterOsDurationSeconds(attrFromResult(verify, "uptime"));
      _lastActivateTrace.activeSessionTimeLeft = parseRouterOsDurationSeconds(
          attrFromResult(verify, "session-time-left"));
      Serial.printf(
          "[activate] operation=active_login mac=%s result=ok active_id=%s "
          "session_limit=%s active_uptime=%u session_time_left=%u\n",
          user.mac.c_str(), verifiedId.c_str(),
          formatLimitUptime(user.timeoutSeconds).c_str(),
          (unsigned)_lastActivateTrace.activeUptime,
          (unsigned)_lastActivateTrace.activeSessionTimeLeft);
      return true;
    }
  }

  _lastActivateTrace.activeVerifySuccess = false;
  Serial.printf("[activate] operation=active_login mac=%s result=ok "
                "(verify_skipped)\n",
                user.mac.c_str());
  return true;
}

bool MikroTikDriver::failHotspot(const String &reason, const char *logContext) {
  _lastHotspotError = reason;
  Serial.printf("[activate] FAILED step=%s reason=%s\n", logContext,
                reason.c_str());
  if (_logger) _logger->error("router", String(logContext) + ": " + reason);
  return false;
}

bool MikroTikDriver::createHotspotUser(const HotspotUser &user) {
  // Critical priority is set once per job by the caller — this only runs
  // from RouterProvisioningWorker::runOp() (OpType::ActivateHotspotUser),
  // which already wraps the whole job in RouterPriorityGuard(Critical). See
  // RouterProvisioningWorker.cpp / RouterApiTransportGate.
  //
  // Entitlement contract (Model B — cumulative RouterOS uptime compensation):
  //   ESP32 timeoutSeconds = seconds of Internet FROM NOW (Clock A).
  //   RouterOS user.uptime is cumulative lifetime consumed (Clock B).
  //   RouterOS user.limit-uptime is a LIFETIME CAP, not "minutes from now".
  //   Therefore:
  //     new_limit_uptime = existing_uptime + timeoutSeconds
  //   so remaining usable time = new_limit - uptime = timeoutSeconds.
  //
  // Command budget (1 API session):
  //   user/print + user/set|add + active/print + active/login|set
  //   (+ optional active verify print after login)
  logRouterStack("createHotspotUser entry");
  const uint32_t t0 = millis();
  uint8_t commandCount = 0;
  _lastHotspotError = "";
  _lastActivateTrace = ActivateAuthTrace{};
  _lastActivateTrace.grantedSeconds = user.timeoutSeconds;

  String host;
  String username;
  String password;
  String configuredProfile;
  Serial.println("[activate] router step 1 — load credentials");
  const uint32_t credStart = millis();
  if (!loadRouterCredentials(host, username, password, configuredProfile)) {
    return failHotspot("Router settings unavailable (/config/router.json)",
                       "load-credentials");
  }
  logRouterStack("after loadRouterCredentials");
  Serial.printf("[activate-latency] load_credentials=%u\n",
                (unsigned)(millis() - credStart));

  const String profile =
      user.profile.length() > 0 ? user.profile : configuredProfile;
  const String hotspotUser =
      user.username.length() > 0 ? user.username : macToHotspotUsername(user.mac);
  const String hotspotPassword = hotspotUser;
  const uint32_t requestedSeconds = user.timeoutSeconds;

  Serial.println("[activate] router step 2 — open RouterOS session");
  String errorOut;
  if (!openRouterSession(host, username, password, errorOut)) {
    return failHotspot(errorOut, "api-login");
  }
  logRouterStack("after openRouterSession (connect+login)");
  activationLatencyTrace().markT5();

  Serial.println("[activate] router step 3 — print existing hotspot user");
  activationLatencyTrace().markT6();
  RouterOsClient::CommandResult &printResult = RouterCommandScratchContext::acquire();
  const String printQuery                    = "?name=" + hotspotUser;
  if (!_routerOs.executeCommand("/ip/hotspot/user/print", printQuery, printResult)) {
    const String reason = _routerOs.lastError();
    closeRouterSession();
    return failHotspot(reason.isEmpty() ? String("Hotspot user lookup failed")
                                        : reason,
                       "hotspot-user-print");
  }
  commandCount++;
  logRouterStack("after hotspot user print");

  const String existingId = idFromResult(printResult);
  const uint32_t existingUptime =
      parseRouterOsDurationSeconds(attrFromResult(printResult, "uptime"));
  const uint32_t existingLimit =
      parseRouterOsDurationSeconds(attrFromResult(printResult, "limit-uptime"));

  // Lifetime cap must cover historical uptime + new entitlement from now.
  // Saturating add avoids wrap on pathological RouterOS values.
  //
  // Business contract: purchasedSeconds must equal usable remaining from now.
  //   new_limit = existing_uptime + requested_seconds
  //   usable    = new_limit - existing_uptime = requested_seconds
  // Do NOT add grace — ₱1 = exactly 300 seconds of new usable time.
  uint32_t newLimitSeconds = existingUptime;
  if (requestedSeconds > 0 &&
      newLimitSeconds > UINT32_MAX - requestedSeconds) {
    newLimitSeconds = UINT32_MAX;
  } else {
    newLimitSeconds += requestedSeconds;
  }
  // Never shrink a still-valid cap below what RouterOS already has if that
  // would leave less remaining than requested (defensive).
  if (existingLimit > newLimitSeconds &&
      existingLimit > existingUptime &&
      (existingLimit - existingUptime) >= requestedSeconds) {
    newLimitSeconds = existingLimit;
  }

  const String limitUptime = formatLimitUptime(newLimitSeconds);
  _lastActivateTrace.existingUserUptime = existingUptime;
  _lastActivateTrace.existingUserLimit = existingLimit;
  _lastActivateTrace.newUserLimit = newLimitSeconds;
  bool ok = false;
  const char *operation = "create";

  if (!existingId.isEmpty()) {
    operation = "reuse";
    Serial.printf(
        "[activate] operation=reuse mac=%s username=%s existing_id=%s "
        "existing_uptime=%u existing_limit=%u requested_seconds=%u "
        "new_limit=%u (%s)\n",
        user.mac.c_str(), hotspotUser.c_str(), existingId.c_str(),
        (unsigned)existingUptime, (unsigned)existingLimit,
        (unsigned)requestedSeconds, (unsigned)newLimitSeconds,
        limitUptime.c_str());
    Serial.println("[activate] router step 4 — update existing hotspot user");
    const String setAttrs[] = {
        "=.id=" + existingId,
        "=profile=" + profile,
        "=limit-uptime=" + limitUptime,
        "=disabled=no",
        "=comment=" + user.mac,
    };
    RouterOsClient::CommandResult &setResult = RouterCommandScratchContext::acquire();
    ok = _routerOs.executeCommand("/ip/hotspot/user/set", setAttrs,
                                  sizeof(setAttrs) / sizeof(setAttrs[0]),
                                  setResult);
    commandCount++;
    if (!ok || setResult.trapReceived) {
      const String reason = setResult.trapMessage.isEmpty()
                                ? _routerOs.lastError()
                                : setResult.trapMessage;
      closeRouterSession();
      return failHotspot(reason.isEmpty() ? String("Hotspot user update failed")
                                          : reason,
                         "hotspot-user-set");
    }
  } else {
    operation = "create";
    Serial.printf(
        "[activate] operation=create mac=%s username=%s "
        "requested_seconds=%u new_limit=%u (%s)\n",
        user.mac.c_str(), hotspotUser.c_str(), (unsigned)requestedSeconds,
        (unsigned)newLimitSeconds, limitUptime.c_str());
    Serial.println("[activate] router step 4 — add new hotspot user");
    const String addAttrs[] = {
        "=name=" + hotspotUser,
        "=password=" + hotspotPassword,
        "=profile=" + profile,
        "=limit-uptime=" + limitUptime,
        "=comment=" + user.mac,
    };
    RouterOsClient::CommandResult &addResult = RouterCommandScratchContext::acquire();
    ok = _routerOs.executeCommand("/ip/hotspot/user/add", addAttrs,
                                  sizeof(addAttrs) / sizeof(addAttrs[0]),
                                  addResult);
    commandCount++;
    if (!ok || addResult.trapReceived) {
      const String reason = addResult.trapMessage.isEmpty()
                                ? _routerOs.lastError()
                                : addResult.trapMessage;
      closeRouterSession();
      return failHotspot(reason.isEmpty() ? String("Hotspot user add failed")
                                          : reason,
                         "hotspot-user-add");
    }
  }
  logRouterStack("after hotspot user set/add");
  activationLatencyTrace().markT7();

  // Creating /ip/hotspot/user is NOT Internet access. Authorize the host in
  // the same API session via native Hotspot active/login (or in-place active
  // set for Add Time). No polling, no reconnect.
  Serial.println("[activate] router step 5 — authorize hotspot active");
  if (!loginHotspotActive(user, hotspotUser, hotspotPassword)) {
    const String reason = _routerOs.lastError();
    closeRouterSession();
    Serial.printf(
        "[activate] operation=%s mac=%s active_authorized=no reason=%s\n",
        operation, user.mac.c_str(),
        reason.isEmpty() ? "login_rejected" : reason.c_str());
    Serial.printf(
        "[router-budget] operation=activate commands=%u session=1 ok=no elapsed=%lu\n",
        (unsigned)(commandCount + 2), (unsigned long)(millis() - t0));
    return failHotspot(
        reason.isEmpty() ? String("Hotspot active login was rejected") : reason,
        "hotspot-active-login");
  }
  commandCount += 3;  // active/print + login|set + verify print (login path)
  activationLatencyTrace().markT8();

  Serial.println("[activate] router step 6 — close RouterOS session");
  closeRouterSession();
  logRouterStack("createHotspotUser exit");
  Serial.printf(
      "[activate] operation=%s mac=%s username=%s existing_uptime=%u "
      "requested_seconds=%u new_limit=%u active_authorized=yes\n",
      operation, user.mac.c_str(), hotspotUser.c_str(),
      (unsigned)existingUptime, (unsigned)requestedSeconds,
      (unsigned)newLimitSeconds);
  Serial.printf(
      "[router-budget] operation=activate commands=%u session=1 ok=yes elapsed=%lu\n",
      (unsigned)commandCount, (unsigned long)(millis() - t0));
  if (_logger) {
    _logger->info("router",
                  String("Hotspot user authorized: ") + hotspotUser +
                      " profile=" + profile + " limit=" + limitUptime +
                      " uptime_was=" + String(existingUptime) +
                      " entitlement=" + String(requestedSeconds));
  }
  return true;
}

bool MikroTikDriver::authorizeUser(const HotspotUser &user) {
  return createHotspotUser(user);
}

bool MikroTikDriver::pauseHotspotUser(const String &mac) {
  // Pause = revoke active traffic + clear cookie so auto-login cannot restore
  // Internet. Keep /ip/hotspot/user so resume can re-login without recreate.
  // Budget: 1 session, active/print+remove + cookie/print+remove(s).
  const uint32_t t0 = millis();
  _lastHotspotError = "";
  String host;
  String username;
  String password;
  String profile;
  if (!loadRouterCredentials(host, username, password, profile)) {
    return failHotspot("Router settings unavailable (/config/router.json)",
                       "pause:load-credentials");
  }

  String errorOut;
  if (!openRouterSession(host, username, password, errorOut)) {
    return failHotspot(errorOut, "pause:api-login");
  }

  bool ok = removeHotspotActiveByMac(mac);
  if (!removeHotspotCookiesByMac(mac)) ok = false;

  closeRouterSession();
  if (!ok) {
    const String reason = _routerOs.lastError();
    _lastHotspotError =
        reason.isEmpty() ? String("Hotspot pause was only partially applied")
                         : reason;
  }
  Serial.printf(
      "[router-budget] operation=pause commands~4 session=1 ok=%s elapsed=%lu\n",
      ok ? "yes" : "no", (unsigned long)(millis() - t0));
  if (_logger) {
    _logger->info("router", String("Hotspot pause (active+cookie): ") + mac +
                                (ok ? " ok" : " partial"));
  }
  return ok;
}

bool MikroTikDriver::queryHotspotActivePresent(const String &mac,
                                               bool &presentOut) {
  presentOut = false;
  if (mac.isEmpty()) return false;

  String host;
  String username;
  String password;
  String profile;
  if (!loadRouterCredentials(host, username, password, profile)) {
    return false;
  }

  String errorOut;
  if (!openRouterSession(host, username, password, errorOut)) {
    return false;
  }

  RouterOsClient::CommandResult &activeResult =
      RouterCommandScratchContext::acquire();
  const bool ok = _routerOs.executeCommand("/ip/hotspot/active/print",
                                           "?mac-address=" + mac, activeResult);
  if (ok) {
    presentOut = !idFromResult(activeResult).isEmpty();
  }
  closeRouterSession();
  Serial.printf("[activate] operation=verify_active mac=%s query_ok=%s "
                "present=%s\n",
                mac.c_str(), ok ? "yes" : "no", presentOut ? "yes" : "no");
  return ok;
}

bool MikroTikDriver::deauthorizeUser(const String &mac) {
  // Critical priority is set once per job by the caller — see
  // createHotspotUser() above (runs only from
  // RouterProvisioningWorker::runOp(), OpType::DeauthorizeHotspotUser).
  // Budget: 1 session — active remove + user remove + cookie remove.
  const uint32_t t0 = millis();
  _lastHotspotError = "";
  String host;
  String username;
  String password;
  String profile;
  if (!loadRouterCredentials(host, username, password, profile)) {
    return failHotspot("Router settings unavailable (/config/router.json)",
                       "deauthorize:load-credentials");
  }

  String errorOut;
  if (!openRouterSession(host, username, password, errorOut)) {
    return failHotspot(errorOut, "deauthorize:api-login");
  }

  bool ok = true;
  if (!removeHotspotActiveByMac(mac)) ok = false;

  const String hotspotUser = macToHotspotUsername(mac);
  RouterOsClient::CommandResult &printResult = RouterCommandScratchContext::acquire();
  if (_routerOs.executeCommand("/ip/hotspot/user/print", "?name=" + hotspotUser,
                               printResult)) {
    const String userId = idFromResult(printResult);
    if (!userId.isEmpty()) {
      const String removeAttrs[] = {"=.id=" + userId};
      RouterOsClient::CommandResult &removeResult = RouterCommandScratchContext::acquire();
      if (!_routerOs.executeCommand("/ip/hotspot/user/remove", removeAttrs, 1,
                                    removeResult) ||
          removeResult.trapReceived) {
        ok = false;
      }
    }
  } else {
    ok = false;
  }

  if (!removeHotspotCookiesByMac(mac)) ok = false;

  closeRouterSession();
  Serial.printf(
      "[router-budget] operation=terminate/expire commands~6 session=1 ok=%s "
      "elapsed=%lu\n",
      ok ? "yes" : "no", (unsigned long)(millis() - t0));
  if (!ok) {
    const String reason = _routerOs.lastError();
    _lastHotspotError =
        reason.isEmpty()
            ? String("Hotspot disconnect was only partially applied")
            : reason;
  }
  if (_logger) {
    _logger->info("router", String("Hotspot user disconnected: ") + mac);
  }
  return ok;
}

bool MikroTikDriver::assignProfile(const String &username, const String &profile) {
  HotspotUser user;
  user.username = username;
  user.profile  = profile;
  return createHotspotUser(user);
}

bool MikroTikDriver::fillStatistics(JsonDocument &out) {
  out["activeSessions"] = 0;
  out["error"]          = "";

  String host;
  String username;
  String password;
  String profile;
  if (!loadRouterCredentials(host, username, password, profile)) {
    out["error"] = "Router settings not available";
    return false;
  }

  String errorOut;
  if (!openRouterSession(host, username, password, errorOut)) {
    out["error"] = errorOut;
    return false;
  }

  RouterOsClient::CommandResult &activeResult = RouterCommandScratchContext::acquire();
  if (!_routerOs.executeCommand("/ip/hotspot/active/print", activeResult)) {
    out["error"] = _routerOs.lastError();
    closeRouterSession();
    return false;
  }

  out["activeSessions"] = activeResult.replyCount;
  closeRouterSession();
  return true;
}

void MikroTikDriver::detect(JsonObject out) const {
  out["driverId"] = driverId();

  String host;
  String username;
  String password;
  String profile;
  const bool configured = loadRouterCredentials(host, username, password, profile) &&
                          host.length() > 0;

  out["detected"]   = false;
  out["configured"] = configured;
  out["confidence"] = configured ? "configured" : "none";
  if (!configured) {
    out["reason"] = "RouterOS host not configured";
  } else {
    out["reason"] = "RouterOS API live probe deferred to setup wizard";
    out["host"]   = host;
  }

  JsonObject manifestObj = out["manifest"].to<JsonObject>();
  manifest().toJson(manifestObj);
}
