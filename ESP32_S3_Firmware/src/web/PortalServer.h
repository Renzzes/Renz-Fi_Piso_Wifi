#pragma once

#include <ESPAsyncWebServer.h>

#include "AssetManager.h"
#include "CacheManager.h"
#include "IWebRouteProvider.h"

class WebServerManager;

// Captive portal HTTP host — sole owner of /, /portal, and portal static
// files on this appliance.
//
// Production note: the customer-facing captive portal is deployed to
// MikroTik Hotspot storage (see deployment/mikrotik-hotspot/), not served
// from here. These routes remain registered for local development, factory
// setup, management-AP access, and field recovery only — see
// docs/HTTP_ROUTE_CONTRACT.md.
class PortalServer : public IWebRouteProvider {
 public:
  void begin(AssetManager *assets);

  void registerRoutes(WebServerManager &web) override;
  const char *providerName() const override;
  int notFoundPriority() const override;
  bool handleNotFound(AsyncWebServerRequest *req) override;

 private:
  CachePolicy cachePolicyForPath(const String &path) const;
  bool isPortalUrl(const String &path) const;
  bool mapToSpiffsPath(const String &urlPath, String &spiffsOut) const;
  bool isManagementApRequest(AsyncWebServerRequest *req) const;

  void servePortalEntry(AsyncWebServerRequest *req);
  void servePortalFile(AsyncWebServerRequest *req, const String &spiffsPath,
                       const String &mimePath);

  AssetManager *_assets = nullptr;
};
