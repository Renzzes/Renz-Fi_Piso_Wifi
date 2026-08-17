#pragma once

#include <ESPAsyncWebServer.h>

class WebServerManager;

// Route providers register HTTP handlers through WebServerManager during
// registerRoutes(). Only providers that need the underlying AsyncWebServer
// should call WebServerManager::routeServer().
class IWebRouteProvider {
 public:
  virtual ~IWebRouteProvider() = default;

  virtual void registerRoutes(WebServerManager &web) = 0;
  virtual const char *providerName() const = 0;

  // Optional hook for the global onNotFound chain (first match wins).
  // Return -1 to skip; lower values run first (e.g. Admin=10, Api=20).
  virtual int notFoundPriority() const { return -1; }

  virtual bool handleNotFound(AsyncWebServerRequest *req) {
    (void)req;
    return false;
  }
};
