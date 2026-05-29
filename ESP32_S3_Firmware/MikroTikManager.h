#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "Logger.h"
#include "Models.h"
#include "StorageManager.h"

class MikroTikManager {
 public:
  void begin(StorageManager *storage, Logger *logger);
  bool load(JsonDocument &doc);
  bool save(JsonObjectConst settings);
  bool test(JsonObjectConst overrideSettings);
  bool createHotspotUser(const HotspotUser &user);
  bool disconnectHotspotUser(const String &mac);
  bool assignProfile(const String &username, const String &profile);

 private:
  StorageManager *_storage = nullptr;
  Logger *_logger = nullptr;
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
