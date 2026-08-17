#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "BuildMetadata.h"
#include "Config.h"
#include "ApiServer.h"
#include "AssetManager.h"
#include "AuthManager.h"
#include "CoinManager.h"
#include "EthernetManager.h"
#include "ExternalAccessPointManager.h"
#include "EventBus.h"
#include "FactoryResetWorker.h"
#include "Logger.h"
#include "InstallationStateManager.h"
#include "ManagementApManager.h"
#include "ManagementApLifecycle.h"
#include "NetworkLifecycleState.h"
#include "NetworkSettingsManager.h"
#include "provisioning/ProvisioningEngine.h"
#include "router/RouterPlatform.h"
#include "PortalSessionManager.h"
#include "PortalConfigManager.h"
#include "PromoManager.h"
#include "RgbController.h"
#include "SessionManager.h"
#include "StorageManager.h"
#include "SystemHealthService.h"
#include "VoucherManager.h"
#include "SetupRouterConnectionManager.h"
#include "RouterProvisioningManager.h"
#include "SetupProvisioningManager.h"
#include "SetupWizardConfigManager.h"
#include "RouterProvisioningEngine.h"
#include "RouterProvisioningWorker.h"
#include "RouterCacheManager.h"
#include "web/WebServerManager.h"

class FirmwareApp {
 public:
  FirmwareApp();
  void begin();
  void loop();

 private:
  // HTTP stack owned by WebServerManager (heap AsyncWebServer after ETH link-up).
  WebServerManager       _web;
  EthernetManager      _eth;      // W5500 wired backend (replaces WiFiBootstrap)
  ExternalAccessPointManager _accessPoints;  // Optional LAN coverage AP registry + Stage C check worker
  ManagementApManager  _mgmtAp;   // Installer/owner Wi-Fi AP @ 192.168.4.1
  ManagementApLifecycle _mgmtApLifecycle;
  StorageManager       _storage;
  EventBus             _events;
  Logger               _logger;
  AuthManager          _auth;
  NetworkSettingsManager _networkSettings;
  SessionManager       _sessions;
  PromoManager         _promos;
  VoucherManager       _vouchers;
  RouterPlatform       _router;
  RouterCacheManager   _routerCache;
  InstallationStateManager _installation;
  SetupProvisioningManager _setupProvisioning;
  SetupWizardConfigManager _wizardConfig;
  SetupRouterConnectionManager _setupRouterConnection;
  RouterProvisioningManager _routerProvisioning;
  RouterProvisioningWorker _routerWorker;
  FactoryResetWorker       _factoryReset;
  RouterProvisioningEngine _finishEngine;
  ProvisioningEngine   _provisioning;
  PortalSessionManager _portalSessions;
  PortalConfigManager  _portalConfig;
  AssetManager         _assetManager;
  CoinManager          _coin;
  RgbController        _rgb;
  SystemHealthService  _health;
  BuildMetadata        _buildMetadata;
  ApiServer            _api;

  uint32_t _lastCleanup  = 0;
  uint32_t _waitLogTimer = 0;
  uint32_t _lastHealthSnapshotMs = 0;
  bool     _ethBootOk           = false;
  bool     _bootBlocked         = false;
  bool     _setupServerStarted  = false;
  bool     _productionRegistered = false;
  NetworkLifecycleState _lifecycleState = NetworkLifecycleState::Booting;

  WebServerDependencies buildWebDeps();
  void updateNetworkLifecycle();
  void startSetupServices();
  void registerProductionServices();
  void warmHealthSnapshots();
  void refreshHealthSnapshots();
};
