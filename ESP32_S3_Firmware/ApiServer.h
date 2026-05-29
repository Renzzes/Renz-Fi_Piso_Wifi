#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

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

class AsyncWebServer;
class AsyncWebServerRequest;

class ApiServer {
 public:
  void begin(AsyncWebServer *server,
             StorageManager *storage,
             AuthManager *auth,
             SessionManager *sessions,
             PromoManager *promos,
             VoucherManager *vouchers,
             CoinManager *coin,
             MikroTikManager *mikrotik,
             Logger *logger,
             EventBus *events,
             CaptivePortal *captive);

 private:
  AsyncWebServer *_server = nullptr;
  StorageManager *_storage = nullptr;
  AuthManager *_auth = nullptr;
  SessionManager *_sessions = nullptr;
  PromoManager *_promos = nullptr;
  VoucherManager *_vouchers = nullptr;
  CoinManager *_coin = nullptr;
  MikroTikManager *_mikrotik = nullptr;
  Logger *_logger = nullptr;
  EventBus *_events = nullptr;
  CaptivePortal *_captive = nullptr;

  bool requireAuth(AsyncWebServerRequest *request);
  void sendOk(AsyncWebServerRequest *request, JsonDocument &data, const String &message = "OK");
  void sendOk(AsyncWebServerRequest *request, const String &message = "OK");
  void sendError(AsyncWebServerRequest *request, int status, const String &error, const String &code);
  void sendStaticOrIndex(AsyncWebServerRequest *request);
  void registerRoutes();
};
