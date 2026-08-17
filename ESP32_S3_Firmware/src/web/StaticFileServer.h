#pragma once

#include <ESPAsyncWebServer.h>

#include "IWebRouteProvider.h"

class WebServerManager;

class StaticFileServer : public IWebRouteProvider {
 public:
  void registerRoutes(WebServerManager &web) override;
  const char *providerName() const override;
  int notFoundPriority() const override;

  bool handleNotFound(AsyncWebServerRequest *req) override;

  void serveStaticOrIndex(AsyncWebServerRequest *req);

 private:
  static void logStaticRequest(AsyncWebServerRequest *req);
};
