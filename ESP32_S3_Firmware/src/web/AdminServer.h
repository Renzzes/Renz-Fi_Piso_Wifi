#pragma once

#include <ESPAsyncWebServer.h>

#include "IWebRouteProvider.h"

class StaticFileServer;
class WebServerManager;

// Foundation for admin React SPA shell routes (Phase 4B migration target).
class AdminServer : public IWebRouteProvider {
 public:
  void begin(StaticFileServer *staticFiles);

  void registerRoutes(WebServerManager &web) override;
  const char *providerName() const override;
  int notFoundPriority() const override;

  bool handleNotFound(AsyncWebServerRequest *req) override;

 private:
  StaticFileServer *_staticFiles = nullptr;
};
