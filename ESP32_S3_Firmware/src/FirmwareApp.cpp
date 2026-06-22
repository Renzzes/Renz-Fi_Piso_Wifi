#include "FirmwareApp.h"

#include <esp_heap_caps.h>
#include <SPIFFS.h>

#include "GpioIsrService.h"
#include "SalesTime.h"
#include "SpiffsHost.h"
#include "W5500Config.h"

FirmwareApp::FirmwareApp() : _events("/api/events") {}

void FirmwareApp::begin() {
  Serial.begin(115200);
  delay(3000);
  Serial.println();
  Serial.println("========================================");
  Serial.println("RENZ-FI ESP32-S3 FIRMWARE STARTING");
  Serial.printf("Firmware version : %s\n", RenzFiConfig::FIRMWARE_VERSION);
  Serial.println("========================================");

  // ── Phase 1: W5500 Ethernet (same sequence as w5500_minimal) ───────────────
  // Nothing runs before _eth.begin() — no GPIO ISR, no SD, no SPI3 pre-init.
  Serial.println("[boot] Phase 1: W5500 Ethernet initialization");
  _ethBootOk = _eth.begin();
  if (!_ethBootOk) {
    Serial.println("[boot] HALTED — ETH.begin() failed");
    Serial.println("========================================");
    return;
  }

  Serial.println("----------------------------------------");
  Serial.printf("[boot] MAC  : %s\n", _eth.macAddress().c_str());
  Serial.printf("[boot] Link : %s\n", _eth.linkUp() ? "UP" : "DOWN");
  Serial.printf("[boot] IP   : %s\n", _eth.ip().c_str());
  Serial.println("----------------------------------------");

  // ETH.begin() installs the GPIO ISR service on arduino-esp32 3.x — record it
  // so ensureInstalled() never calls gpio_install_isr_service() a second time.
  GpioIsrService::noteExternalInstall("ETH.begin");
  GpioIsrService::ensureInstalled("boot");

  // ── Phase 2: SPIFFS ────────────────────────────────────────────────────────
  Serial.println("[boot] Phase 2: SPIFFS initialization");
  bool frontendReady = SPIFFS.begin(true);
  if (frontendReady) {
    Serial.println("[boot] SPIFFS mounted");
    logSpiffsInventory();
  } else {
    Serial.println("[ERROR] SPIFFS mount failed — admin dashboard unavailable");
  }

  // ── Phase 3: SD card (FSPI / SPI2 only — after ETH.begin success) ──────────
  Serial.println("[boot] Phase 3: SD card initialization");
  bool sdReady = _storage.begin();
  Serial.printf("[boot] SD mount status: %s\n", sdReady ? "mounted" : "degraded");

  // ── Phase 4: Subsystems ────────────────────────────────────────────────────
  Serial.println("[boot] Phase 4: Subsystem initialization");

  _logger.begin(&_storage, &_events);
  _auth.begin(&_storage, &_logger);
  _sessions.begin(&_storage, &_logger, &_events);
  _promos.begin(&_storage, &_logger, &_events);
  _vouchers.begin(&_storage, &_logger, &_events);
  _mikrotik.begin(&_storage, &_logger);
  _portalSessions.begin(&_storage, &_logger, &_events, &_promos, &_mikrotik);
  _portalConfig.begin(&_storage, &_logger, &_events);
  if (RenzFiConfig::ENABLE_COIN_MANAGER) {
    _coin.begin(&_storage, &_logger, &_events, &_promos, &_portalSessions);
  } else {
    Serial.println("[coin] CoinManager disabled");
  }

  if (_eth.isServiceReady()) {
    startNetworkServices();
  }

  Serial.println("----------------------------------------");
  Serial.printf("[net] Driver : %s\n", _eth.driverReady() ? "UP" : "DOWN");
  Serial.printf("[net] Link   : %s\n", _eth.linkUp() ? "UP" : "DOWN");
  Serial.printf("[net] IP     : %s\n", _eth.ip().c_str());
  Serial.printf("[boot] Admin dashboard : http://%s/admin\n", W5500Config::IP.toString().c_str());
  Serial.println("RENZ-FI BOOT COMPLETE");
  Serial.println("========================================");

  _logger.info("system", "Firmware boot complete v" + String(RenzFiConfig::FIRMWARE_VERSION));
}

void FirmwareApp::loop() {
  _eth.loop();
  if (!_ethBootOk) return;

  if (!_networkReady && _eth.isServiceReady()) {
    startNetworkServices();
  }

  if (RenzFiConfig::ENABLE_COIN_MANAGER) {
    _coin.loop();
  }
  _portalSessions.loop();
  _storage.pollStorageHealth();
  _events.heartbeat();

  if (millis() - _lastCleanup > RenzFiConfig::CLEANUP_INTERVAL_MS) {
    _lastCleanup = millis();
    _auth.cleanupExpired();
    _sessions.cleanupExpired();
    _portalSessions.cleanupExpired();
  }
}

void FirmwareApp::startNetworkServices() {
  if (_networkReady) return;

  salesTimeBegin();

  if (!_server) {
    _server = new AsyncWebServer(RenzFiConfig::HTTP_PORT);
  }

  _events.begin(*_server);
  CoinManager *coin = RenzFiConfig::ENABLE_COIN_MANAGER ? &_coin : nullptr;
  _api.begin(_server, &_storage, &_auth, &_sessions, &_promos, &_vouchers,
             coin, &_mikrotik, &_logger, &_events, &_eth,
             &_portalSessions, &_portalConfig);

  _server->begin();
  _networkReady = true;
  Serial.println("[API] Server started");
}
