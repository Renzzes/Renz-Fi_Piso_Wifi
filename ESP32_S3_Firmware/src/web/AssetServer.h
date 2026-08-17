#pragma once

#include <ESPAsyncWebServer.h>

#include "AssetManager.h"
#include "AssetResolver.h"
#include "IWebRouteProvider.h"
#include "PortalConfigManager.h"
#include "StorageManager.h"

class WebServerManager;

class AssetServer : public IWebRouteProvider {
 public:
  void begin(AssetManager *assets, StorageManager *storage,
             PortalConfigManager *portalConfig);

  void registerRoutes(WebServerManager &web) override;
  const char *providerName() const override;

  bool serveResolvedAsset(AsyncWebServerRequest *req,
                          const ResolvedAsset &asset) const;

 private:
  AssetManager        *_assets       = nullptr;
  StorageManager      *_storage      = nullptr;
  PortalConfigManager *_portalConfig = nullptr;
};
