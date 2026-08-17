#include "FactoryResetWorker.h"
#include "FirmwareApp.h"

#include <SPIFFS.h>

#include "BootDiagnostics.h"
#include "BurnInDiagnostics.h"
#include "DmaMemoryMonitor.h"
#include "MemoryDiagnostics.h"
#include "GpioIsrService.h"
#include "InstallationState.h"
#include "NetworkDiagnostics.h"
#include "NetworkSettingsManager.h"
#include "RecoveryManager.h"
#include "RenzFiDebug.h"
#include "DeviceIdentity.h"
#include "SalesTime.h"
#include "SpiffsHost.h"
#include "SetupDnsPolicy.h"
#include "web/HttpPlaneGate.h"

FirmwareApp::FirmwareApp() : _events("/api/events") {}

void FirmwareApp::begin() {
  Serial.begin(115200);

  // ── Phase 0: Recovery button (GPIO2) — before serial delay / W5500 / auth ───
  RecoveryManager::runBootCheck();

  if (!RecoveryManager::isMonitoring()) {
    delay(3000);
  }

  Serial.println();
  Serial.println("========================================");
  Serial.println("RENZ-FI ESP32-S3 FIRMWARE STARTING");
  Serial.printf("Firmware version : %s\n", RenzFiConfig::FIRMWARE_VERSION);
  Serial.println("========================================");

  NetworkDiagnostics::install();
  DmaMemoryMonitor::install();

  // ── Phase 1: W5500 Ethernet (same sequence as w5500_minimal) ───────────────
  // Nothing runs before _eth.begin() — no GPIO ISR, no SD, no SPI3 pre-init.
  // NOTE: ETH.begin() failure (no cable, faulty/missing W5500, brand-new
  // unit, etc.) is NEVER treated as fatal. The Management AP and web server
  // MUST still come up so the unit stays reachable for setup/recovery — the
  // appliance must never hang or reboot-loop due to network issues.
  Serial.println("[boot] Phase 1: W5500 Ethernet initialization");

  // NVS-only read (no SD dependency yet) — decides DHCP vs. validated static
  // mode for this boot. Corrupt/never-provisioned data safely resolves to
  // DHCP inside loadNvsOnly().
  const NetworkSettings bootNetworkSettings = NetworkSettingsManager::loadNvsOnly();
  Serial.printf("[boot] Network settings: mode=%s provisioned=%s\n",
                ethernetAddressModeLabel(bootNetworkSettings.addressMode),
                bootNetworkSettings.provisioned ? "true" : "false");

  _ethBootOk = _eth.begin(bootNetworkSettings);
  NetworkDiagnostics::noteEthBeginFinished(_ethBootOk, millis());

  Serial.println("----------------------------------------");
  if (_ethBootOk) {
    Serial.printf("[boot] MAC  : %s\n", _eth.macAddress().c_str());
    Serial.printf("[boot] Link : %s\n", _eth.linkUp() ? "UP" : "DOWN");
    Serial.printf("[boot] IP   : %s (%s)\n", _eth.ip().c_str(), _eth.addressModeLabel());
  } else {
    Serial.println("[boot] W5500 driver did not start this boot — continuing without Ethernet.");
    Serial.println("[boot] Management AP (192.168.4.1) will remain the recovery/setup path.");
  }
  Serial.println("----------------------------------------");

  // A stable per-chip identifier that never depends on Ethernet being
  // present — used for deviceId/AP naming so a unit with no cable or a
  // faulty W5500 still gets a unique, consistent identity every boot.
  const String deviceMac =
      _ethBootOk ? _eth.macAddress() : DeviceIdentity::stableChipMacAddress();

  // GpioIsrService must be installed unconditionally: the recovery button
  // (GPIO2) and CoinManager pulse counting both depend on it, and must work
  // even when Ethernet never comes up. ETH.begin() installs the ISR service
  // as a side effect on arduino-esp32 3.x when it succeeds — record that so
  // ensureInstalled() never calls gpio_install_isr_service() a second time.
  if (_ethBootOk) {
    GpioIsrService::noteExternalInstall("ETH.begin");
  }
  GpioIsrService::ensureInstalled("boot");

  // ── Phase 2: SPIFFS ────────────────────────────────────────────────────────
  Serial.println("[boot] Phase 2: SPIFFS initialization");
  bool frontendReady = SPIFFS.begin(false);
  if (frontendReady) {
    Serial.println("[boot] SPIFFS mounted");
    logSpiffsInventory();
    BootDiagnostics::logPortalAssetValidation(true);
  } else {
    Serial.println("[ERROR] SPIFFS mount failed — admin dashboard unavailable");
    BootDiagnostics::logPortalAssetValidation(false);
  }

  // ── Phase 3: SD card (FSPI / SPI2 only — after ETH.begin success) ──────────
  Serial.println("[boot] Phase 3: SD card initialization");
  EthernetManager::logDiagnosticStage("before_sd_init");
  bool sdReady = false;
#if RENZFI_DISABLE_SD_BOOT
  Serial.println("[boot] SD card initialization skipped (RENZFI_DISABLE_SD_BOOT)");
#else
  sdReady = _storage.begin();
#endif
  _storage.setEventBus(&_events);
  Serial.printf("[boot] SD mount status: %s\n", sdReady ? "mounted" : "degraded");
  EthernetManager::logDiagnosticStage("after_fallback_sync");

  if (_storage.isSdMounted()) {
    String restoreRecoveryError;
    if (!BackupManager::recoverPendingRestore(&_storage,
                                              restoreRecoveryError)) {
      _storage.markDegraded(
          String("Restore recovery blocked boot: ") + restoreRecoveryError);
      _bootBlocked = true;
      Serial.println(
          "[FATAL] Pending restore could not be recovered; subsystem "
          "initialization is blocked to prevent mixed configuration state.");
      return;
    }
  }

  _buildMetadata.begin(&_storage);

  // ── Phase 4: Subsystems ────────────────────────────────────────────────────
  Serial.println("[boot] Phase 4: Subsystem initialization");

  _logger.begin(&_storage, &_events);
  _installation.begin(&_storage, &_logger, &_events);
  _installation.setDeviceId(deviceMac);
  Serial.printf("[boot] Installation state: %s (%u%%)\n",
                installationStateLabel(_installation.current()),
                static_cast<unsigned>(_installation.progressPercent()));
  _auth.begin(&_storage, &_logger);
  _setupRouterConnection.begin(&_storage, &_installation, &_eth);
  _routerProvisioning.begin(&_storage, &_installation, &_setupRouterConnection, &_eth);
  _networkSettings.begin(&_storage);
  _promos.begin(&_storage, &_logger, &_events);
  CoinManager *coinPtr = nullptr;
  if (RenzFiConfig::ENABLE_COIN_MANAGER) {
    _coin.begin(&_storage, &_logger, &_events, &_promos, &_portalSessions);
    coinPtr = &_coin;
  } else {
    Serial.println("[coin] CoinManager disabled");
  }
  _wizardConfig.begin(&_storage, &_networkSettings, coinPtr);
  _setupProvisioning.begin(&_storage, &_auth, &_installation, &_setupRouterConnection,
                           &_wizardConfig);
  _routerCache.begin(&_storage, &_logger);
  _router.begin(&_storage, &_logger, &_events);
  _router.attachCache(&_routerCache);
  _routerWorker.begin(&_eth, &_setupRouterConnection, &_routerProvisioning,
                      &_setupProvisioning, &_installation, &_finishEngine, &_router,
                      &_events);
  _setupProvisioning.synchronizeAtBoot();
  salesTimeBindInstallation(&_installation);
  SetupDnsPolicy::begin(&_installation, &_eth);
  _sessions.begin(&_storage, &_logger, &_events);
  _vouchers.begin(&_storage, &_logger, &_events);
  _portalSessions.begin(&_storage, &_logger, &_events, &_promos, &_router,
                        &_routerWorker, &_vouchers, &_sessions);
  MemoryDiagnostics::begin(&_routerWorker, &_events, &_portalSessions);
  _assetManager.begin(&_storage, &_logger, &_events);
  _portalConfig.begin(&_storage, &_logger, &_events, &_assetManager);
  _factoryReset.begin(&_storage, &_logger, &_auth, &_assetManager, &_portalConfig,
                      &_installation, &_setupProvisioning, &_setupRouterConnection,
                      &_wizardConfig, &_routerProvisioning);
  if (!coinPtr) {
    Serial.println("[coin] CoinManager disabled");
  }
  _rgb.begin(&_storage, &_events, &_eth, &_storage, coinPtr);
  _health.begin(&_eth, &_storage, coinPtr, &_rgb);
  _provisioning.begin(&_storage, &_logger, &_events, &_router, &_installation,
                      &_portalConfig, coinPtr);

  _mgmtAp.begin(&_installation, deviceMac);
  _mgmtApLifecycle.begin(&_mgmtAp, &_auth, &_installation, &_networkSettings);
  _mgmtApLifecycle.applyBootPolicy();
  _finishEngine.begin(&_storage, &_installation, &_setupProvisioning,
                      &_setupRouterConnection, &_routerProvisioning, &_wizardConfig,
                      &_eth, &_router, &_mgmtApLifecycle);

  {
    String credentialError;
    if (_finishEngine.ensureProductionRouterCredentials(credentialError)) {
      Serial.println("[boot] Production RouterOS credentials ready");
    } else {
      Serial.printf(
          "[boot] Production RouterOS credentials unavailable: %s\n",
          credentialError.c_str());
      _logger.warn("router",
                   "Production RouterOS credentials unavailable — hotspot "
                   "activation will fail until setup is completed: " +
                       credentialError);
    }
  }

  HttpPlaneGate::bindEthernet(&_eth);
  HttpPlaneGate::bindAuth(&_auth);

  CoinManager *coin = RenzFiConfig::ENABLE_COIN_MANAGER ? &_coin : nullptr;
  _accessPoints.begin(&_storage, &_eth, &_logger);

  _api.begin(&_storage, &_auth, &_sessions, &_promos, &_vouchers,
             coin, &_router, &_logger, &_events, &_eth,
             &_portalSessions, &_portalConfig, &_assetManager, &_rgb, &_health,
             &_buildMetadata, &_installation, &_mgmtAp, &_mgmtApLifecycle,
             &_networkSettings, &_routerWorker, &_factoryReset, &_accessPoints);

  if (_mgmtAp.isRunning()) {
    startSetupServices();
    SetupDnsPolicy::applySetupPhasePolicy();
  }

  if (_installation.isReady()) {
    salesTimeBegin();
  }

  updateNetworkLifecycle();

  EthernetManager::logDiagnosticStage("before_appliance_summary");
  BootDiagnostics::BootSummaryContext summary;
  summary.eth           = &_eth;
  summary.storage       = &_storage;
  summary.installation  = &_installation;
  summary.router        = &_router;
  summary.assets        = &_assetManager;
  summary.spiffsMounted = frontendReady;
  summary.coinEnabled   = RenzFiConfig::ENABLE_COIN_MANAGER;
  BootDiagnostics::logProductionSummary(summary);

  _logger.info("system", "Firmware boot complete v" + String(RenzFiConfig::FIRMWARE_VERSION));

  NetworkDiagnostics::printStartupReport(&_eth, &_mgmtAp, _ethBootOk);
  NetworkDiagnostics::printRegisteredInterfaces(&_eth, &_mgmtAp);

#if RENZFI_BURN_IN_DIAG
  BurnInDiagnostics::begin(&_routerWorker, &_eth, &_router);
#endif
}

void FirmwareApp::loop() {
  RecoveryManager::loop();
  if (_bootBlocked) {
    delay(250);
    return;
  }
  MemoryDiagnostics::periodicLog();
  _eth.loop();
  NetworkDiagnostics::loop();
#if RENZFI_BURN_IN_DIAG
  BurnInDiagnostics::loop();
#endif
  _mgmtAp.loop();
  _mgmtApLifecycle.loop();

  if (SetupDnsPolicy::isSetupLifecycleActive()) {
    SetupDnsPolicy::applySetupPhasePolicy();
  }

  updateNetworkLifecycle();

  if (RenzFiConfig::ENABLE_COIN_MANAGER) {
    _coin.loop();
  }
  _rgb.loop();
  _portalSessions.loop();
  _setupProvisioning.loop();
  _factoryReset.loop();
  _vouchers.loop();
  _accessPoints.loop();
  if (!_factoryReset.busy()) {
    // Every FirmwareApp::loop() iteration while reset is idle: complete the
    // TWDT-safe deferred commit (QUEUED → PERSISTING → persist() → PERSISTED).
    // Suppressed only while FactoryResetWorker owns the provisioning object.
    _routerProvisioning.loop();
    // Health poll uses try-lock; also skip while voucher persist holds storage.
    if (!_vouchers.generateBusy()) {
      _storage.pollStorageHealth();
    }
  }
  if (_setupServerStarted) {
    _web.pollSetupWorkflows();
  }
  _events.heartbeat();

  if (millis() - _waitLogTimer >= 10000) {
    _waitLogTimer = millis();
    Serial.printf(
        "[net] heartbeat lifecycle=%s eth_link=%s eth_ip=%s mgmt_ap=%s "
        "setup=%s production=%s install=%s\n",
        networkLifecycleStateLabel(_lifecycleState),
        _eth.linkUp() ? "up" : "down",
        _eth.hasIp() ? _eth.ip().c_str() : "none",
        _mgmtAp.isRunning() ? "running" : "off",
        _setupServerStarted ? "ready" : "pending",
        _productionRegistered ? "ready" : "pending",
        installationStateLabel(_installation.current()));
    SetupDnsPolicy::logDiagnostics();
  }

  if (millis() - _lastCleanup > RenzFiConfig::CLEANUP_INTERVAL_MS) {
    _lastCleanup = millis();
    _auth.cleanupExpired();
    _sessions.cleanupExpired();
    _portalSessions.cleanupExpired();
  }

  if (_productionRegistered && !_factoryReset.busy()) {
    refreshHealthSnapshots();
  }
}

void FirmwareApp::warmHealthSnapshots() {
  _mgmtAp.refreshRuntimeState();
  _storage.refreshRuntimeSnapshot();
  _router.refreshHealthCache();
  _sessions.refreshSalesSummarySnapshot();
  _sessions.refreshMergedActiveUserSnapshot(&_portalSessions);
  _promos.ensureCacheLoaded();
  const bool coinEnabled = RenzFiConfig::ENABLE_COIN_MANAGER;
  DeviceIdentity::refreshRuntimeProfile(&_eth, &_storage, &_router, coinEnabled);
  _lastHealthSnapshotMs = millis();
}

void FirmwareApp::refreshHealthSnapshots() {
  const uint32_t now = millis();
  if (now - _lastHealthSnapshotMs < 2000U) return;
  _lastHealthSnapshotMs = now;

  _storage.refreshRuntimeSnapshot();
  _router.refreshHealthCache();
  _sessions.refreshSalesSummarySnapshot();
  _sessions.refreshMergedActiveUserSnapshot(&_portalSessions);
  _promos.ensureCacheLoaded();
  const bool coinEnabled = RenzFiConfig::ENABLE_COIN_MANAGER;
  DeviceIdentity::refreshRuntimeProfile(&_eth, &_storage, &_router, coinEnabled);
}

WebServerDependencies FirmwareApp::buildWebDeps() {
  WebServerDependencies webDeps;
  webDeps.events       = &_events;
  webDeps.api          = &_api;
  webDeps.auth         = &_auth;
  webDeps.provisioning = &_provisioning;
  webDeps.storage      = &_storage;
  webDeps.assets       = &_assetManager;
  webDeps.portalConfig = &_portalConfig;
  webDeps.eth          = &_eth;
  webDeps.installation = &_installation;
  webDeps.setupProvisioning = &_setupProvisioning;
  webDeps.setupWizardConfig = &_wizardConfig;
  webDeps.networkSettings = &_networkSettings;
  webDeps.setupRouterConnection = &_setupRouterConnection;
  webDeps.routerProvisioning = &_routerProvisioning;
  webDeps.routerWorker = &_routerWorker;
  webDeps.finishEngine = &_finishEngine;
  return webDeps;
}

void FirmwareApp::updateNetworkLifecycle() {
  // Setup plane starts with the Management AP during factory/setup. On
  // provisioned production boots the AP stays off, so start the single shared
  // AsyncWebServer once Ethernet has a valid IP instead.
  if (!_setupServerStarted &&
      (_mgmtAp.isRunning() ||
       (_installation.isReady() && _eth.isServiceReady()))) {
    startSetupServices();
  }

  if (_setupServerStarted && _eth.isServiceReady() && !_productionRegistered) {
    registerProductionServices();
  }

  if (_installation.needsSetup() && _mgmtAp.isRunning()) {
    _lifecycleState =
        (_installation.current() == InstallationState::Factory)
            ? NetworkLifecycleState::FactoryProvisioning
            : NetworkLifecycleState::SetupApReady;
    return;
  }

  if (_productionRegistered && _eth.isServiceReady()) {
    _lifecycleState = NetworkLifecycleState::ProductionReady;
    return;
  }

  if (_setupServerStarted && _eth.driverReady() && !_eth.hasIp()) {
    _lifecycleState = NetworkLifecycleState::EthernetWaiting;
    return;
  }

  if (_setupServerStarted && _productionRegistered && !_eth.hasIp()) {
    _lifecycleState = NetworkLifecycleState::DegradedEthernetUnavailable;
    return;
  }

  if (_setupServerStarted && _mgmtAp.isRunning()) {
    _lifecycleState = NetworkLifecycleState::SetupApReady;
    return;
  }

  if (_eth.isServiceReady() && !_productionRegistered) {
    _lifecycleState = NetworkLifecycleState::EthernetReady;
  }
}

void FirmwareApp::startSetupServices() {
  if (_setupServerStarted) return;

  _web.initialize(RenzFiConfig::HTTP_PORT);
  _web.startSetupPlane(buildWebDeps());

  _setupServerStarted = true;
  EthernetManager::noteWebServerState(true);
  Serial.println("[net] Setup HTTP plane started (Management AP only routes)");
}

void FirmwareApp::registerProductionServices() {
  if (!_setupServerStarted || _productionRegistered) return;

  warmHealthSnapshots();
  _web.registerProductionPlane(buildWebDeps());

  _productionRegistered = true;
  Serial.println(
      "[net] Production HTTP plane registered (Ethernet routes, server not restarted)");
  NetworkDiagnostics::printRegisteredInterfaces(&_eth, &_mgmtAp);
}
