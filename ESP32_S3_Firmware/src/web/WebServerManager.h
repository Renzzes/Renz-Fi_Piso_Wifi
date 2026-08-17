#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "RouteRegistry.h"

class ApiServer;
class AssetManager;
class AuthManager;
class EventBus;
class EthernetManager;
class InstallationStateManager;
class SetupProvisioningManager;
class SetupRouterConnectionManager;
class RouterProvisioningEngine;
class RouterProvisioningManager;
class RouterProvisioningWorker;
class SetupWizardConfigManager;
class NetworkSettingsManager;
class PortalConfigManager;
class ProvisioningEngine;
class StorageManager;

struct WebServerDependencies {
  EventBus                 *events       = nullptr;
  ApiServer                *api          = nullptr;
  AuthManager              *auth         = nullptr;
  ProvisioningEngine       *provisioning = nullptr;
  StorageManager           *storage      = nullptr;
  AssetManager             *assets       = nullptr;
  PortalConfigManager      *portalConfig = nullptr;
  EthernetManager          *eth          = nullptr;
  InstallationStateManager *installation = nullptr;
  SetupProvisioningManager *setupProvisioning = nullptr;
  SetupRouterConnectionManager *setupRouterConnection = nullptr;
  RouterProvisioningManager *routerProvisioning = nullptr;
  RouterProvisioningWorker *routerWorker = nullptr;
  RouterProvisioningEngine *finishEngine = nullptr;
  SetupWizardConfigManager *setupWizardConfig = nullptr;
  NetworkSettingsManager *networkSettings = nullptr;
};

class WebServerManager {
 public:
  WebServerManager();
  ~WebServerManager();

  WebServerManager(const WebServerManager &) = delete;
  WebServerManager &operator=(const WebServerManager &) = delete;

  void initialize(uint16_t port = 80);
  void configure(const WebServerDependencies &deps);

  // Phase 1 — setup plane (Management AP). Idempotent; starts AsyncWebServer once.
  void startSetupPlane(const WebServerDependencies &deps);

  // Phase 2 — production plane (Ethernet with valid IP). Idempotent; never restarts server.
  void registerProductionPlane(const WebServerDependencies &deps);

  bool registerSetupProvider(IWebRouteProvider *provider);
  bool registerProductionProvider(IWebRouteProvider *provider);

  bool isSetupStarted() const { return _setupStarted; }
  bool isProductionRegistered() const { return _productionRegistered; }

  void fillHealth(JsonObject obj) const;

  // Setup-plane background work (adoption job state machine).
  void pollSetupWorkflows();

  AsyncWebServer &routeServer();

 private:
  struct Subsystems;
  void ensureSubsystems();
  void wireSetupProviders(const WebServerDependencies &deps);
  void wireProductionProviders(const WebServerDependencies &deps);
  void registerAdminEntryRoute();
  void registerNotFoundHandler();

  AsyncWebServer *_server   = nullptr;
  uint16_t        _port     = 80;
  bool            _setupStarted          = false;
  bool            _setupRoutesRegistered = false;
  bool            _productionRegistered  = false;

  RouteRegistry   _setupRegistry;
  RouteRegistry   _productionRegistry;
  WebServerDependencies _deps{};
  Subsystems     *_subsystems = nullptr;
};
