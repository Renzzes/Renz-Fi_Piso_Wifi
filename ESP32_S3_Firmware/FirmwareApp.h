#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "ApiServer.h"
#include "AuthManager.h"
#include "CaptivePortal.h"
#include "CoinManager.h"
#include "EventBus.h"
#include "Logger.h"
#include "MikroTikManager.h"
#include "PromoManager.h"
#include "SessionManager.h"
#include "StorageManager.h"
#include "VoucherManager.h"
#include "WiFiBootstrap.h"
#include "Config.h"

class FirmwareApp {
 public:
  FirmwareApp();
  void begin();
  void loop();

 private:
  AsyncWebServer _server;
  WiFiBootstrap _wifi;
  CaptivePortal _captive;
  StorageManager _storage;
  EventBus _events;
  Logger _logger;
  AuthManager _auth;
  SessionManager _sessions;
  PromoManager _promos;
  VoucherManager _vouchers;
  MikroTikManager _mikrotik;
  CoinManager _coin;
  ApiServer _api;

  uint32_t _lastCleanup = 0;
};
