#pragma once

#include "../IRouterDriver.h"
#include "RouterOsClient.h"

class MikroTikDriver : public IRouterDriver {
 public:
  void begin(StorageManager *storage, Logger *logger, EventBus *events) override;

  const char *driverId() const override { return "mikrotik"; }
  const char *vendorName() const override { return "MikroTik"; }

  RouterCapabilities capabilities() const override;
  RouterDriverManifest manifest() const override;
  RouterProfile profile() const override;

  bool loadSettings(JsonDocument &doc) override;
  bool saveSettings(JsonObjectConst settings) override;
  bool fillPublicSettings(JsonDocument &doc) const override;

  bool connect(String &errorOut) override;
  void disconnect() override;
  bool isConnected() const override { return _routerOs.isConnected(); }

  bool healthCheck(JsonDocument &out) override;
  bool testSettings(JsonObjectConst overrideSettings, JsonDocument &out) override;
  bool listProfiles(JsonDocument &out) override;
  bool probeApiReady() override;

  bool fillWireless(JsonDocument &out) override;
  bool saveWireless(JsonObjectConst settings, JsonDocument &out) override;
  bool collectCacheSnapshot(
      JsonDocument &out,
      RouterCacheCollectMode mode = RouterCacheCollectMode::Configuration) override;

  // Admin hotspot USER profile mutations (not /ip/hotspot/profile).
  bool setUserProfileRateLimit(const String &name, const String &rateLimit,
                               JsonDocument &out);
  bool ensureManagedSpeedProfile(uint16_t downloadMbps, uint16_t uploadMbps,
                                 JsonDocument &out);
  static String managedSpeedProfileName(uint16_t downloadMbps,
                                        uint16_t uploadMbps);
  static String formatRateLimitMbps(uint16_t downloadMbps, uint16_t uploadMbps);

  bool authorizeUser(const HotspotUser &user) override;
  bool lastActivateAuthTrace(ActivateAuthTrace &out) const override;
  bool deauthorizeUser(const String &mac) override;
  bool pauseHotspotUser(const String &mac) override;
  bool queryHotspotActivePresent(const String &mac, bool &presentOut) override;
  bool assignProfile(const String &username, const String &profile) override;
  String lastHotspotError() const override { return _lastHotspotError; }

  bool activateProductionNetwork(JsonDocument &result) override;
  bool activateProductionNetworkForFinish(JsonDocument &result,
                                          RouterOsClient &session, bool sessionReused,
                                          bool reconnected);
  bool productionNetworkActive(JsonDocument &result) override;

  bool fillStatistics(JsonDocument &out) override;

  void detect(JsonObject out) const override;

 private:
  StorageManager *_storage = nullptr;
  Logger *_logger = nullptr;
  EventBus *_events = nullptr;

  RouterOsClient _routerOs;

  mutable String _cachedIdentity;
  mutable String _cachedHost;
  mutable String _cachedUsername;
  // Exact reason the last hotspot authorize/pause/deauthorize failed.
  String _lastHotspotError;
  ActivateAuthTrace _lastActivateTrace{};

  bool failHotspot(const String &reason, const char *logContext);

  void mergeSettings(JsonObjectConst overrideSettings, JsonDocument &settings) const;
  bool loadRouterCredentials(String &host, String &username, String &password,
                             String &profile) const;
  bool openRouterSession(const String &host, const String &username,
                         const String &password, String &errorOut);
  void closeRouterSession();
  bool createHotspotUser(const HotspotUser &user);
  bool loginHotspotActive(const HotspotUser &user, const String &hotspotUser,
                          const String &hotspotPassword);
  bool removeHotspotActiveByMac(const String &mac);
  bool removeHotspotCookiesByMac(const String &mac);
  // Sync/Test only — bounded WAN observe/repair inside an open RouterOS session.
  void observeAndRepairWan(JsonObject observationOut);
  bool resolveProductionWirelessInterface(String &ifaceOut,
                                          String &errorOut) const;
  bool resolveExpectedProductionSsid(String &ssidOut) const;
  struct WirelessInterfaceState {
    String id;
    String disabled;
    String running;  // raw print attr if present ("true"/"false"/empty)
    String ssid;
    String mode;
    String frequency;
    String channel;
    // Tri-state runtime: never treat a missing print attr as "not running".
    enum class Runtime : uint8_t { Unknown = 0, Running = 1, NotRunning = 2 };
    Runtime runtime = Runtime::Unknown;
    bool runningAttrPresent = false;
    bool disabledAttrPresent = false;
    bool attrLimitReached = false;
    uint8_t attrsStored = 0;
    String runtimeSource;   // print | monitor | unknown
    String runtimeStatus;   // e.g. running-ap from monitor
  };
  bool queryWirelessInterfaceState(RouterOsClient &client, const String &ifaceName,
                                 WirelessInterfaceState &out, String &errorOut);
  bool queryWirelessInterfaceState(const String &ifaceName,
                                   WirelessInterfaceState &out, String &errorOut);
  bool queryWirelessRuntimeOnce(RouterOsClient &client, const String &ifaceId,
                                const String &ifaceName,
                                WirelessInterfaceState &inout,
                                String &errorOut);
  bool fillProductionNetworkDiagnostics(JsonDocument &result,
                                        const String &ifaceName,
                                        const WirelessInterfaceState &state,
                                        const String &expectedSsid) const;

  static String identityFromResult(const RouterOsClient::CommandResult &result);
  static void profileNamesFromResult(const RouterOsClient::CommandResult &result,
                                     JsonArray &out);
  static void profileDetailsFromResult(const RouterOsClient::CommandResult &result,
                                       JsonArray &detailsOut, JsonArray &namesOut);
  static bool profileExistsInResult(const RouterOsClient::CommandResult &result,
                                    const String &profile);
  static String idFromResult(const RouterOsClient::CommandResult &result);
  static String macToHotspotUsername(const String &mac);
  static String formatLimitUptime(uint32_t seconds);
  // RouterOS HotSpot user/active duration strings ("5m", "1h2m3s", "00:05:00").
  static uint32_t parseRouterOsDurationSeconds(const String &raw);
  static String attrFromResult(const RouterOsClient::CommandResult &result,
                               const char *attrName);
};
