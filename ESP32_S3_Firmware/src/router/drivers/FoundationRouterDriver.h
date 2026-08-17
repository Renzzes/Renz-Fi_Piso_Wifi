#pragma once

#include "../IRouterDriver.h"
#include "../RouterDriverManifest.h"

class EventBus;
class Logger;
class StorageManager;

// Shared stub behavior for drivers whose protocols are not yet implemented.
class FoundationRouterDriver : public IRouterDriver {
 public:
  explicit FoundationRouterDriver(const RouterDriverManifest &manifest);

  void begin(StorageManager *storage, Logger *logger, EventBus *events) override;

  const char *driverId() const override { return _manifest.driverId; }
  const char *vendorName() const override { return _manifest.vendor; }

  RouterCapabilities capabilities() const override { return _manifest.capabilities; }
  RouterDriverManifest manifest() const override { return _manifest; }
  RouterProfile profile() const override;

  bool loadSettings(JsonDocument &doc) override;
  bool saveSettings(JsonObjectConst settings) override;
  bool fillPublicSettings(JsonDocument &doc) const override;

  bool connect(String &errorOut) override;
  void disconnect() override;
  bool isConnected() const override { return false; }

  bool healthCheck(JsonDocument &out) override;
  bool testSettings(JsonObjectConst overrideSettings, JsonDocument &out) override;
  bool listProfiles(JsonDocument &out) override;

  bool authorizeUser(const HotspotUser &user) override;
  bool deauthorizeUser(const String &mac) override;
  bool assignProfile(const String &username, const String &profile) override;

  bool activateProductionNetwork(JsonDocument &result) override;
  bool productionNetworkActive(JsonDocument &result) override;

  bool fillStatistics(JsonDocument &out) override;

  void detect(JsonObject out) const override;

 protected:
  StorageManager *_storage = nullptr;
  Logger *_logger = nullptr;
  EventBus *_events = nullptr;

  RouterDriverManifest _manifest;

  void setNotImplementedError(JsonDocument &out) const;
};
