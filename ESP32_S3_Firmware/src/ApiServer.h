#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "AuthManager.h"
#include "BackupManager.h"
#include "CoinManager.h"
#include "EthernetManager.h"
#include "EventBus.h"
#include "Logger.h"
#include "MikroTikManager.h"
#include "PortalConfigManager.h"
#include "PortalSessionManager.h"
#include "PromoManager.h"
#include "SessionManager.h"
#include "StorageManager.h"
#include "VoucherManager.h"

class ApiServer {
 public:
  void begin(AsyncWebServer      *server,
             StorageManager       *storage,
             AuthManager          *auth,
             SessionManager       *sessions,
             PromoManager         *promos,
             VoucherManager       *vouchers,
             CoinManager          *coin,
             MikroTikManager      *mikrotik,
             Logger               *logger,
             EventBus             *events,
             EthernetManager      *eth,
             PortalSessionManager *portalSessions,
             PortalConfigManager  *portalConfig);

 private:
  AsyncWebServer       *_server         = nullptr;
  StorageManager       *_storage        = nullptr;
  AuthManager          *_auth           = nullptr;
  SessionManager       *_sessions       = nullptr;
  PromoManager         *_promos         = nullptr;
  VoucherManager       *_vouchers       = nullptr;
  CoinManager          *_coin           = nullptr;
  MikroTikManager      *_mikrotik       = nullptr;
  Logger               *_logger         = nullptr;
  EventBus             *_events         = nullptr;
  EthernetManager      *_eth            = nullptr;
  PortalSessionManager *_portalSessions = nullptr;
  PortalConfigManager  *_portalConfig   = nullptr;
  BackupManager        _backup;

  // Returns false and sends 401 if the request is not authenticated.
  bool requireAuth(AsyncWebServerRequest *req);

  // Adds CORS + security headers to a response object.
  static void addCorsHeaders(AsyncWebServerResponse *res);

  // Send JSON success envelope.
  void sendOk(AsyncWebServerRequest *req, JsonDocument &data,
               const String &message = "OK");
  void sendOk(AsyncWebServerRequest *req, const String &message = "OK");

  // Send JSON error envelope.
  void sendError(AsyncWebServerRequest *req, int status,
                 const String &error, const String &code);

  // Serve a SPIFFS file or SPA index fallback for the request URI.
  void sendStaticOrIndex(AsyncWebServerRequest *req);

  // Stream a file from SD card as an attachment download.
  void sendSdFile(AsyncWebServerRequest *req, const char *sdPath,
                  const char *filename);

  // Returns the raw POST/PUT body (empty string if none).
  static String getBody(AsyncWebServerRequest *req);

  // Log the current request to Serial with handler tag.
  static void logRequest(AsyncWebServerRequest *req, const char *handler);

  void registerRoutes();
};
