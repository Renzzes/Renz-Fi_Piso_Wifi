#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "RouterProvisioningTypes.h"
#include "ExistingNetworkScan.h"
#include "RouterWirelessAdapter.h"

class EthernetManager;
class InstallationStateManager;
class SetupRouterConnectionManager;
class StorageManager;

class RouterOsClient;
class RouterProvisioningEngine;

// Phase 3 — read-only router inspection, provisioning plan, and safe apply.
class RouterProvisioningManager {
 public:
  static constexpr uint16_t SCHEMA_VERSION = 3;

  struct OperationResult {
    bool   success = false;
    int    httpStatus = 400;
    String errorCode;
    String errorMessage;
    String stage;
  };

  void begin(StorageManager *storage, InstallationStateManager *installation,
             SetupRouterConnectionManager *routerConnection,
             EthernetManager *eth);

  // Completes deferred router-provisioning.json commits off async_tcp.
  // Call from FirmwareApp::loop() (same pattern as SetupProvisioningManager).
  void loop();

  bool load();
  bool persist();
  // Durable commit phases for wifi/selection (and related deferred persists).
  // Reported exactly as QUEUED | PERSISTING | PERSISTED | FAILED.
  bool wifiSelectionDurablePending() const {
    return _durableCommitPhase == DurableCommitPhase::Queued ||
           _durableCommitPhase == DurableCommitPhase::Persisting;
  }
  bool durableCommitFailed() const {
    return _durableCommitPhase == DurableCommitPhase::Failed;
  }
  String durableCommitError() const { return _durableCommitError; }
  const char *durableCommitStatus() const;

  bool applied() const { return _foundationApplied; }
  bool hotspotActivated() const { return _hotspotActivated; }
  String guestBridgeName() const { return _guestBridgeName; }
  String networkMode() const { return _networkMode; }
  bool isExistingNetworkAdopted() const {
    return _networkMode == RouterProvisioning::NETWORK_MODE_EXISTING &&
           _foundationApplied && _adoptedAt > 0;
  }
  bool wifiSelectionConfigured() const { return _wifiSelectionConfigured; }
  // Existing SSID: configured + wireless interface id.
  // Create New SSID: configured + target SSID (interface is created at apply).
  bool wifiSetupComplete() const;
  // RAM-only. Cancels a queued durable persist so factory reset cannot
  // rewrite router-provisioning.json after the file was deleted.
  void clearForFactoryReset();
  void fillWirelessStatus(JsonObject dataOut) const;

  OperationResult buildLocalPlan(JsonObject dataOut,
                                 JsonObjectConst settingsBody = JsonObject());
  OperationResult saveWifiSelection(JsonObjectConst body, JsonObject dataOut);
  OperationResult configureExistingNetwork(JsonObjectConst body, JsonObject dataOut,
                                           RouterOsClient *routerClient = nullptr,
                                           class RouterProvisioningEngine *engine =
                                               nullptr);
  OperationResult applyConfiguration(JsonObjectConst settingsBody,
                                     JsonObjectConst confirmationBody,
                                     JsonObject dataOut,
                                     JsonObject errorDataOut);
  OperationResult setNetworkModePreference(JsonObjectConst body);
  void fillNetworkModeStatus(JsonObject dataOut);

  void fillDefaults(JsonObject defaults) const;

 private:
  StorageManager               *_storage = nullptr;
  InstallationStateManager     *_installation = nullptr;
  SetupRouterConnectionManager *_routerConnection = nullptr;
  EthernetManager              *_eth = nullptr;

  bool     _foundationApplied = false;
  bool     _hotspotActivated = false;
  bool     _clientInterfaceAttached = false;
  uint32_t _appliedAt = 0;
  String   _routerIdentity;
  String   _routerVersion;
  String   _guestBridgeName;
  String   _guestNetwork;
  String   _guestGateway;
  String   _dhcpPool;
  String   _dhcpServerName;
  String   _poolName;
  String   _networkMode;
  String   _networkModePreference;
  bool     _hotspotDetected = false;
  uint32_t _adoptedAt = 0;
  uint32_t _updatedAt = 0;
  uint16_t _schemaVersion = SCHEMA_VERSION;
  bool     _wifiSelectionConfigured = false;
  String   _wifiMode;
  String   _wifiInterfaceId;
  String   _wifiSsid;
  String   _wifiPassword;

  // Owner-style deferred durable commit (loopTask). Never writeJson from
  // async_tcp for these callers. Single-slot — duplicates coalesce.
  enum class DurableCommitPhase : uint8_t {
    Idle = 0,       // last known durable success (or never queued)
    Queued,         // accepted; waiting for loopTask
    Persisting,     // loopTask currently inside persist()/writeJson
    Failed,         // durable write failed; RAM retained for retry
  };
  DurableCommitPhase _durableCommitPhase = DurableCommitPhase::Idle;
  String             _durableCommitError;
  // Set if RAM changed while Persisting — one follow-up Queued commit after.
  bool               _durableNeedsReschedule = false;
  // Set by factory reset. Blocks loop()/persist()/scheduleDeferredPersist so
  // a queued or worker commit cannot recreate router-provisioning.json.
  bool               _factoryResetQuiesced = false;

  void applyDefaults();
  void applyDocument(JsonObjectConst doc);
  void buildDocument(JsonDocument &doc) const;
  bool migrateDocument(JsonDocument &doc);
  void scheduleDeferredPersist();
  bool wifiSelectionMatches(const RouterWireless::WifiSelection &selection,
                            const String &selectedSsidHint) const;
  void fillDurableCommitFields(JsonObject dataOut) const;

  OperationResult ensurePreconditions() const;
  OperationResult ensureLocalPreviewPreconditions() const;
  bool parseSettings(JsonObjectConst body, RouterProvisioning::Settings &out,
                     String &errorOut) const;
};
