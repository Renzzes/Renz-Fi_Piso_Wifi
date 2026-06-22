#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "ApiServer.h"
#include "AuthManager.h"
#include "CoinManager.h"
#include "Config.h"
#include "EthernetManager.h"
#include "EventBus.h"
#include "Logger.h"
#include "MikroTikManager.h"
#include "PortalSessionManager.h"
#include "PortalConfigManager.h"
#include "PromoManager.h"
#include "SessionManager.h"
#include "StorageManager.h"
#include "VoucherManager.h"

class FirmwareApp {
 public:
  FirmwareApp();
  void begin();
  void loop();

 private:
  // Heap-allocated in startNetworkServices() AFTER ETH.begin() + confirmed link-up.
  // Deferring construction prevents any lwIP tcpip_thread mailbox access during
  // global/static init, which would trigger "Invalid mbox" asserts on ESP32-S3.
  AsyncWebServer*      _server    = nullptr;
  EthernetManager      _eth;      // W5500 wired backend (replaces WiFiBootstrap)
  StorageManager       _storage;
  EventBus             _events;
  Logger               _logger;
  AuthManager          _auth;
  SessionManager       _sessions;
  PromoManager         _promos;
  VoucherManager       _vouchers;
  MikroTikManager      _mikrotik;
  PortalSessionManager _portalSessions;
  PortalConfigManager  _portalConfig;
  CoinManager          _coin;
  ApiServer            _api;

  uint32_t _lastCleanup  = 0;
  uint32_t _waitLogTimer = 0;
  bool     _networkReady = false;
  bool     _ethBootOk    = false;

  void startNetworkServices();
};
