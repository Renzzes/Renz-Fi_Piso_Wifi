#pragma once

#include <ArduinoJson.h>

#include "Models.h"
#include "RouterCapabilities.h"
#include "RouterDriverManifest.h"
#include "RouterProfile.h"

class EventBus;
class Logger;
class StorageManager;

/** Admin cache collect scope — Sync vs Refresh share one session path. */
enum class RouterCacheCollectMode : uint8_t {
  Configuration = 0,  // required Admin config fields only
  Telemetry     = 1,  // lightweight read-only CPU/mem/uptime (+ light HS)
};

// Vendor-specific router communication contract.
// New routers implement this interface and register with RouterPlatform.
class IRouterDriver {
 public:
  virtual ~IRouterDriver() = default;

  virtual const char *driverId() const = 0;
  virtual const char *vendorName() const = 0;

  virtual void begin(StorageManager *storage, Logger *logger, EventBus *events) = 0;

  virtual RouterCapabilities capabilities() const = 0;
  virtual RouterDriverManifest manifest() const = 0;
  virtual RouterProfile profile() const = 0;

  virtual bool loadSettings(JsonDocument &doc) = 0;
  virtual bool saveSettings(JsonObjectConst settings) = 0;
  virtual bool fillPublicSettings(JsonDocument &doc) const = 0;

  virtual bool connect(String &errorOut) = 0;
  virtual void disconnect() = 0;
  virtual bool isConnected() const = 0;

  virtual bool healthCheck(JsonDocument &out) = 0;
  virtual bool testSettings(JsonObjectConst overrideSettings, JsonDocument &out) = 0;
  virtual bool listProfiles(JsonDocument &out) = 0;

  /** Lightweight API readiness: connect+login+identity/print+disconnect.
   *  Default false — non-MikroTik drivers skip health probes. */
  virtual bool probeApiReady() { return false; }

  // One-shot RouterOS metadata probe for Admin cache Sync/Refresh.
  // Default mode = Configuration for any legacy callers.
  virtual bool collectCacheSnapshot(
      JsonDocument &out,
      RouterCacheCollectMode mode = RouterCacheCollectMode::Configuration);

  // Live wireless interface (SSID/security) read/write. Default: unsupported
  // — most drivers defer wireless configuration to a later phase. Only
  // drivers that implement this should report success.
  virtual bool fillWireless(JsonDocument &out);
  virtual bool saveWireless(JsonObjectConst settings, JsonDocument &out);

  virtual bool authorizeUser(const HotspotUser &user) = 0;
  virtual bool deauthorizeUser(const String &mac) = 0;
  // Snapshot from the last authorizeUser. Default: no trace.
  virtual bool lastActivateAuthTrace(ActivateAuthTrace &out) const {
    (void)out;
    return false;
  }
  // Pause: revoke active Hotspot traffic only — keep user record for resume.
  virtual bool pauseHotspotUser(const String &mac);
  // Query-only: true if the RouterOS call succeeded. presentOut set on success.
  virtual bool queryHotspotActivePresent(const String &mac, bool &presentOut);
  virtual bool assignProfile(const String &username, const String &profile) = 0;

  // Exact reason the last authorize/pause/deauthorize attempt failed. Empty
  // when the driver has nothing more specific than the generic job failure.
  virtual String lastHotspotError() const { return String(); }

  virtual bool activateProductionNetwork(JsonDocument &result) = 0;
  virtual bool productionNetworkActive(JsonDocument &result) = 0;

  virtual bool fillStatistics(JsonDocument &out) = 0;

  // Setup-wizard hook: populate detection metadata (default: not detected).
  virtual void detect(JsonObject out) const;
};
