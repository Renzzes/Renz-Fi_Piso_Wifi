#pragma once

#include <Arduino.h>

#include "web/IWebRouteProvider.h"

class EthernetManager;
class InstallationStateManager;
class SetupProvisioningManager;
class StorageManager;
class SetupRouterConnectionManager;
class RouterProvisioningEngine;
class RouterProvisioningManager;
class RouterProvisioningWorker;
class AuthManager;
class NetworkSettingsManager;
class SetupWizardConfigManager;

// Setup-plane HTTP routes — Management AP (192.168.4.1) only.
class SetupServer : public IWebRouteProvider {
 public:
  void begin(EthernetManager *eth, InstallationStateManager *installation,
             SetupProvisioningManager *provisioning, StorageManager *storage,
             AuthManager *auth, SetupRouterConnectionManager *routerConnection,
             RouterProvisioningManager *routerProvisioning,
             RouterProvisioningWorker *routerWorker,
             SetupWizardConfigManager *wizardConfig = nullptr,
             NetworkSettingsManager *networkSettings = nullptr,
             RouterProvisioningEngine *finishEngine = nullptr);

  void registerRoutes(WebServerManager &web) override;
  const char *providerName() const override;

 private:
  EthernetManager           *_eth          = nullptr;
  InstallationStateManager  *_installation = nullptr;
  SetupProvisioningManager  *_provisioning = nullptr;
  StorageManager            *_storage      = nullptr;
  AuthManager               *_auth         = nullptr;
  SetupRouterConnectionManager *_routerConnection = nullptr;
  RouterProvisioningManager     *_routerProvisioning = nullptr;
  RouterProvisioningWorker      *_routerWorker = nullptr;
  SetupWizardConfigManager      *_wizardConfig = nullptr;
  NetworkSettingsManager        *_networkSettings = nullptr;
  RouterProvisioningEngine      *_finishEngine = nullptr;
};
