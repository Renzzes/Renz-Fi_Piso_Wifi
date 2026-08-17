#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "EventBus.h"
#include "Logger.h"
#include "StorageManager.h"

class AssetManager;

// Portal configuration only (title, theme, behaviour — Phase 3B).
// Media files and paths are owned by AssetManager — never StoragePaths here.
// See docs/PORTAL_CONFIG_ARCHITECTURE.md
class PortalConfigManager {
 public:
  void begin(StorageManager *storage, Logger *logger, EventBus *events,
             AssetManager *assets);

  bool loadMeta();

  bool hasCustomBanner() const;
  bool hasCustomMusic() const;
  uint32_t revision() const;

  bool fillBrandingJson(JsonObject out, const String &baseUrl) const;
  bool fillSettingsJson(JsonObject out, const String &baseUrl) const;

 private:
  StorageManager *_storage = nullptr;
  Logger         *_logger  = nullptr;
  EventBus       *_events  = nullptr;
  AssetManager   *_assets  = nullptr;

  bool     _hasBanner = false;
  bool     _hasMusic  = false;
  uint32_t _revision  = 0;

  void refreshMediaFlagsFromAssets();
};
