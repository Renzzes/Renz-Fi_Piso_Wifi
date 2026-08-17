#pragma once

#include <stddef.h>

#include "IWebRouteProvider.h"

class WebServerManager;

class RouteRegistry {
 public:
  static const size_t kMaxProviders = 16;

  void clear();
  bool registerProvider(IWebRouteProvider *provider);
  void registerAll(WebServerManager &web);
  bool dispatchNotFound(AsyncWebServerRequest *req);
  size_t count() const;

 private:
  IWebRouteProvider *_providers[kMaxProviders] = {};
  size_t             _count                    = 0;
};
