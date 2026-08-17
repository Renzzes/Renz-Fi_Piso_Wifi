#pragma once

#include <Arduino.h>

class AssetManager;
class AuthManager;
class InstallationStateManager;
class Logger;
class PortalConfigManager;
class RouterProvisioningManager;
class SetupProvisioningManager;
class SetupRouterConnectionManager;
class SetupWizardConfigManager;
class StorageManager;

// Stepped factory-reset job. HTTP only enqueues/polls; work runs on loopTask.
class FactoryResetWorker {
 public:
  struct Snapshot {
    uint32_t    jobId     = 0;
    const char *status    = "idle";
    const char *phase     = "";
    uint8_t     progress  = 0;
    bool        rebooting = false;
    const char *error     = "";
  };

  void begin(StorageManager *storage, Logger *logger, AuthManager *auth,
             AssetManager *assets, PortalConfigManager *portalConfig,
             InstallationStateManager *installation,
             SetupProvisioningManager *setupProvisioning,
             SetupRouterConnectionManager *routerConnection,
             SetupWizardConfigManager *wizardConfig,
             RouterProvisioningManager *routerProvisioning = nullptr);

  // Returns jobId, or 0 if a reset is already in progress.
  uint32_t enqueue();
  bool     busy() const;
  bool     poll(uint32_t jobId, Snapshot &out) const;
  void     fillSnapshot(Snapshot &out) const;

  // Bounded work. Call from FirmwareApp::loop() (not async_tcp).
  void loop();

 private:
  enum class State : uint8_t { Idle, Queued, Running, Completed, Failed };
  enum class Step : uint8_t {
    Quiesce = 0,
    DeleteBanner,
    DeleteMusic,
    ClearFallback,
    DeleteFiles,
    History,
    EnsureLayout,
    ClearRouterRam,
    ClearWizardRam,
    ReloadPortal,
    ResetInstallation,
    Validate,
    Complete,
  };

  StorageManager                 *_storage          = nullptr;
  Logger                         *_logger           = nullptr;
  AuthManager                    *_auth             = nullptr;
  AssetManager                   *_assets           = nullptr;
  PortalConfigManager            *_portalConfig     = nullptr;
  InstallationStateManager       *_installation     = nullptr;
  SetupProvisioningManager       *_setupProvisioning = nullptr;
  SetupRouterConnectionManager   *_routerConnection = nullptr;
  SetupWizardConfigManager       *_wizardConfig     = nullptr;
  RouterProvisioningManager      *_routerProvisioning = nullptr;

  State    _state      = State::Idle;
  Step     _step       = Step::Quiesce;
  uint32_t _jobId      = 0;
  uint32_t _nextJobId  = 1;
  uint32_t _rebootAtMs = 0;
  size_t   _fileIndex  = 0;
  String   _error;

  void        fail(const char *message);
  void        advance();
  void        runStep();
  const char *statusLabel() const;
  const char *phaseLabel() const;
  uint8_t     progressPercent() const;
};
