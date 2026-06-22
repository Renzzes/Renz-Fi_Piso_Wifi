#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "Logger.h"
#include "Models.h"
#include "RouterOsClient.h"
#include "StorageManager.h"

class MikroTikManager {
 public:
  void begin(StorageManager *storage, Logger *logger);
  bool load(JsonDocument &doc);
  bool save(JsonObjectConst settings);
  bool fillPublicSettings(JsonDocument &doc) const;
  bool test(JsonObjectConst overrideSettings, JsonDocument &out);
  bool listProfiles(JsonDocument &out);
  bool createHotspotUser(const HotspotUser &user);
  bool provisionHotspotUser(const HotspotUser &user);
  bool disconnectHotspotUser(const String &mac);
  bool assignProfile(const String &username, const String &profile);

 private:
  StorageManager *_storage = nullptr;
  Logger *_logger = nullptr;
  RouterOsClient _routerOs;

  void mergeSettings(JsonObjectConst overrideSettings, JsonDocument &settings) const;
  bool openRouterSession(const String &host, const String &username,
                         const String &password, String &errorOut);
  void closeRouterSession();
  static String identityFromResult(const RouterOsClient::CommandResult &result);
  static void profileNamesFromResult(const RouterOsClient::CommandResult &result,
                                     JsonArray &out);
  static bool profileExistsInResult(const RouterOsClient::CommandResult &result,
                                    const String &profile);
  static String idFromResult(const RouterOsClient::CommandResult &result);
  static String macToHotspotUsername(const String &mac);
  static String formatLimitUptime(uint32_t seconds);
  bool loadRouterCredentials(String &host, String &username, String &password,
                             String &profile) const;
};

using MikroTikService = MikroTikManager;

class ProfileManager {
 public:
  explicit ProfileManager(MikroTikService *service = nullptr) : _service(service) {}
  void attach(MikroTikService *service) { _service = service; }
  bool assign(const String &username, const String &profile) {
    return _service && _service->assignProfile(username, profile);
  }

 private:
  MikroTikService *_service = nullptr;
};
