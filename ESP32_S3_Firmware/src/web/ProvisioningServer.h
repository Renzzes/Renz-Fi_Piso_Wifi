#pragma once

#include <ESPAsyncWebServer.h>

#include "IWebRouteProvider.h"

class AuthManager;
class ProvisioningEngine;

class WebServerManager;

// Admin setup wizard REST facade — delegates exclusively to ProvisioningEngine.
class ProvisioningServer : public IWebRouteProvider {
 public:
  void begin(AuthManager *auth, ProvisioningEngine *engine);

  void registerRoutes(WebServerManager &web) override;
  const char *providerName() const override;

 private:
  AuthManager *_auth   = nullptr;
  ProvisioningEngine *_engine = nullptr;
  AsyncWebServer *_server = nullptr;

  bool requireAuth(AsyncWebServerRequest *req) const;
  static String getBody(AsyncWebServerRequest *req);
  static void bodyCollect(AsyncWebServerRequest *req, uint8_t *data, size_t len,
                          size_t index, size_t total);

  void sendOk(AsyncWebServerRequest *req, JsonDocument &data,
              const char *message = "OK") const;
  void sendError(AsyncWebServerRequest *req, int status, const char *error,
                 const char *code) const;
};
