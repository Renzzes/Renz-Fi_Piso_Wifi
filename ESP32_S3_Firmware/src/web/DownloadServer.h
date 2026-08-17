#pragma once

#include <ESPAsyncWebServer.h>

#include "IWebRouteProvider.h"

class WebServerManager;

// Reserved foundation for future download endpoints (backups, exports).
class DownloadServer : public IWebRouteProvider {
 public:
  void registerRoutes(WebServerManager &web) override;
  const char *providerName() const override;
  int notFoundPriority() const override;

  bool handleNotFound(AsyncWebServerRequest *req) override;
};
