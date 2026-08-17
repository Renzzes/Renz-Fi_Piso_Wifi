#pragma once

#include "../IRouterDriver.h"

/**
 * RESERVED — GenericAPDriver (Phase 7D)
 *
 * Passive gateway driver for routers that only provide DHCP/NAT/captive redirect.
 * Session state lives on the ESP32; no programmable router API is required.
 *
 * @deprecated Not selected in the standard Renz-Fi v1 installer flow.
 *   Reserved for future product editions. GenericAPDriver remains compiled and
 *   registered in RouterPlatform but is hidden from the installer UI.
 *   driverId: "generic_ap"
 */
class GenericAPDriver : public IRouterDriver {
 public:
  void begin(StorageManager *storage, Logger *logger, EventBus *events) override;

  const char *driverId() const override { return "generic_ap"; }
  const char *vendorName() const override { return "Generic AP"; }

  RouterCapabilities capabilities() const override;
  RouterDriverManifest manifest() const override;
  RouterProfile profile() const override;

  bool loadSettings(JsonDocument &doc) override;
  bool saveSettings(JsonObjectConst settings) override;
  bool fillPublicSettings(JsonDocument &doc) const override;

  bool connect(String &errorOut) override;
  void disconnect() override;
  bool isConnected() const override { return _configured; }

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

 private:
  StorageManager *_storage = nullptr;
  Logger *_logger = nullptr;
  EventBus *_events = nullptr;
  bool _configured = false;

  bool refreshConfigured();
};
