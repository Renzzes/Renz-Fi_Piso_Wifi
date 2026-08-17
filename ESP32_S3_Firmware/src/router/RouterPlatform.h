#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "IRouterDriver.h"
#include "RouterProfile.h"
#include "RouterDriverManifest.h"
#include "drivers/GenericAPDriver.h"
#include "drivers/MikroTikDriver.h"
#include "drivers/OpenWRTDriver.h"
#include "drivers/RuijieDriver.h"
#include "drivers/TPLinkDriver.h"

class EventBus;
class Logger;
class RouterCacheManager;
class StorageManager;

// Composition root for interchangeable router drivers.
// Only RouterPlatform knows driver types; upper layers use its facade API.
class RouterPlatform {
 public:
  static constexpr size_t kMaxDrivers = 8;

  void begin(StorageManager *storage, Logger *logger, EventBus *events);
  void attachCache(RouterCacheManager *cache) { _cache = cache; }

  void registerBuiltInDrivers();
  bool selectActiveDriver();
  bool registerDriver(IRouterDriver *driver);

  IRouterDriver *activeDriver();
  const IRouterDriver *activeDriver() const;

  // Setup-wizard / admin discovery API.
  size_t availableDrivers(JsonArray &out) const;
  RouterDriverManifest driverManifest() const;
  RouterDriverManifest driverManifest(const String &driverId) const;
  bool switchDriver(const String &driverId);
  bool detectDrivers(JsonDocument &out) const;
  bool isFirmwareSupported(const String &firmware, const String &version,
                           String &reasonOut) const;

  RouterProfile profile() const;
  RouterCapabilities capabilities() const;

  // Backward-compatible facade (same contract as former MikroTikManager).
  bool load(JsonDocument &doc);
  bool save(JsonObjectConst settings);
  bool fillPublicSettings(JsonDocument &doc) const;
  bool test(JsonObjectConst overrideSettings, JsonDocument &out);
  bool listProfiles(JsonDocument &out);

  /** Cache-miss / explicit refresh / rate-limit / managed-speed — worker only. */
  bool adminUserProfileOp(JsonObjectConst request, JsonDocument &out);

  // Cached RouterOS metadata — live API only on explicit refresh / config writes.
  bool fillWireless(JsonDocument &out);
  bool saveWireless(JsonObjectConst settings, JsonDocument &out);
  // Configuration Sync (Admin Synchronize Router / stale login).
  bool refreshRouterCache(bool markProvisioned = false);
  bool synchronizeRouterCache(bool markProvisioned = false);
  // Telemetry Refresh (Admin Refresh Router Information) — read-only ROS.
  bool refreshRouterTelemetry();
  /** Last collectCacheSnapshot error — never contains passwords. */
  const String &lastCollectError() const { return _lastCollectError; }
  bool fillRouterCache(JsonDocument &out) const;
  void fillRouterCacheStatus(JsonObject out) const;
  bool cachePopulated() const;
  bool provisionHotspotUser(const HotspotUser &user);
  bool lastActivateAuthTrace(ActivateAuthTrace &out) const;
  bool disconnectHotspotUser(const String &mac);
  bool pauseHotspotUser(const String &mac);
  bool queryHotspotActivePresent(const String &mac, bool &presentOut);
  /** Exact failure text from the last hotspot authorize/pause/deauthorize. */
  String lastHotspotError() const;
  bool assignProfile(const String &username, const String &profile);
  /** Health FSM readiness probe — not an idle poll. */
  bool probeApiReady();

  bool activateProductionNetwork(JsonDocument &result);
  bool activateProductionNetworkForFinish(JsonDocument &result,
                                          RouterOsClient &session, bool sessionReused,
                                          bool reconnected);
  bool productionNetworkActive(JsonDocument &result);
  bool saveProductionNetworkVerification(JsonObjectConst verifyResult);
  /** Finish pipeline: persist verification + provisioned flag without live RouterOS probe. */
  bool persistFinishRouterCache(JsonObjectConst verifyResult);

  /** Loop / lifecycle: refresh router configured flag (may read storage). */
  void refreshHealthCache();

  /** RAM snapshot of last refreshHealthCache() — HTTP-safe, no SD. */
  bool cachedRouterConfigured() const { return _healthConfigured; }
  const String &cachedRouterHost() const { return _healthHost; }

  /** Health snapshot: cached router product/status only — no I/O. */
  void fillHealthStatus(JsonObject routerObj) const;

 private:
  StorageManager *_storage = nullptr;
  Logger *_logger = nullptr;
  EventBus *_events = nullptr;
  RouterCacheManager *_cache = nullptr;

  MikroTikDriver _mikrotikDriver;
  GenericAPDriver _genericAPDriver;
  TPLinkDriver _tplinkDriver;
  RuijieDriver _ruijieDriver;
  OpenWRTDriver _openwrtDriver;

  IRouterDriver *_drivers[kMaxDrivers] = {};
  size_t _driverCount = 0;
  IRouterDriver *_active = nullptr;
  bool _healthConfigured = false;
  String _healthHost;
  String _lastCollectError;

  String readDriverType() const;
  IRouterDriver *findDriver(const String &driverType) const;
  void emitRouterEvent(const char *event, const String &json = "{}");
  void emitProfileUpdated();
  void emitCapabilitiesChanged();
};

// Optional helper for profile assignment through RouterPlatform.
class ProfileManager {
 public:
  explicit ProfileManager(RouterPlatform *platform = nullptr)
      : _platform(platform) {}
  void attach(RouterPlatform *platform) { _platform = platform; }
  bool assign(const String &username, const String &profile) {
    return _platform && _platform->assignProfile(username, profile);
  }

 private:
  RouterPlatform *_platform = nullptr;
};
