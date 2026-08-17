#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

class EthernetManager;
class InstallationStateManager;
class ManagementApLifecycle;
class RouterPlatform;
class RouterProvisioningManager;
class SetupProvisioningManager;
class SetupRouterConnectionManager;
class SetupWizardConfigManager;
class StorageManager;

// Final setup step — idempotent RouterOS hotspot + portal provisioning after
// the 4-step Management AP wizard completes (Finish Setup).
class RouterProvisioningEngine {
 public:
  struct OperationResult {
    bool   success = false;
    int    httpStatus = 400;
    String errorCode;
    String errorMessage;
    String reason;
    String stage;
    bool   rebootScheduled = false;
    // Captive portal (MikroTik) verification outcome — informational for
    // MANUAL_EXTERNAL / SKIPPED; may be blocking only for MANAGED.
    String portalDeploymentMode;
    String portalStatus;
    bool   portalBlocking = false;
  };

  enum class PortalDeploymentMode : uint8_t {
    ManualExternal = 0,
    Managed = 1,
    Skipped = 2,
  };

  using ProgressFn = void (*)(void *ctx, const char *stageId, const char *label);

  void begin(StorageManager *storage, InstallationStateManager *installation,
             SetupProvisioningManager *setupProvisioning,
             SetupRouterConnectionManager *routerConnection,
             RouterProvisioningManager *routerProvisioning,
             SetupWizardConfigManager *wizardConfig, EthernetManager *eth,
             RouterPlatform *router = nullptr,
             ManagementApLifecycle *mgmtApLifecycle = nullptr);

  bool isRunning() const { return _running; }
  bool finishCompleted() const { return _finishCompleted; }

  void setPortalDeploymentMode(PortalDeploymentMode mode) {
    _portalDeploymentMode = mode;
  }
  void setPortalDeploymentModeFromLabel(const char *label);
  PortalDeploymentMode portalDeploymentMode() const {
    return _portalDeploymentMode;
  }
  static PortalDeploymentMode parsePortalDeploymentModeLabel(const char *raw);
  static const char *portalDeploymentModeLabel(PortalDeploymentMode mode);

  OperationResult runFinishPipeline(ProgressFn progressFn = nullptr,
                                    void *progressCtx = nullptr);

  bool servePortalAsset(const String &filename, String &contentOut,
                        String &contentTypeOut, String &errorOut) const;
  bool acceptsPortalFetchToken(const String &token) const;

  bool persistWirelessSelection(const String &interfaceName,
                                const String &ssidPolicy,
                                const String &targetSsid);

  bool syncProductionRouterCredentials(String &errorOut);

  // Reconciles /config/router.json with the setup-verified credentials when the
  // production file is still seeded/empty. Storage-only — issues zero RouterOS
  // commands. Always re-reads disk (the in-memory ready flag is not trusted
  // across SD remount / fallback). Returns false only when production
  // credentials are still unusable afterwards.
  bool ensureProductionRouterCredentials(String &errorOut);
  bool productionRouterCredentialsReady() const {
    return _productionCredentialsOk;
  }
  StorageManager *storage() const { return _storage; }

  // Stops Management AP after finish success is published to the client.
  void completeSetupAfterFinishSuccess();

 private:
  StorageManager               *_storage = nullptr;
  InstallationStateManager     *_installation = nullptr;
  SetupProvisioningManager     *_setupProvisioning = nullptr;
  SetupRouterConnectionManager *_routerConnection = nullptr;
  RouterProvisioningManager    *_routerProvisioning = nullptr;
  SetupWizardConfigManager     *_wizardConfig = nullptr;
  EthernetManager              *_eth = nullptr;
  RouterPlatform               *_router = nullptr;
  ManagementApLifecycle        *_mgmtApLifecycle = nullptr;

  bool     _running = false;
  bool     _finishCompleted = false;
  bool     _productionCredentialsOk = false;
  PortalDeploymentMode _portalDeploymentMode = PortalDeploymentMode::ManualExternal;
  String   _selectedWirelessInterface;
  String   _ssidPolicy;
  String   _targetSsid;
  String   _portalFetchToken;

  OperationResult ensurePreconditions() const;
  OperationResult persistLocalState();
  OperationResult resolveWirelessSelection(String &interfaceOut,
                                           String &ssidPolicyOut,
                                           String &targetSsidOut);
  bool loadWirelessSelection();
  String applianceBaseUrl() const;
  String portalFetchUrl(const String &filename) const;
  void reportProgress(ProgressFn fn, void *ctx, const char *stageId,
                      const char *label) const;
};
