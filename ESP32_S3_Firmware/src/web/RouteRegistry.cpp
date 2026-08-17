#include "RouteRegistry.h"

#include "WebServerManager.h"

void RouteRegistry::clear() {
  _count = 0;
}

bool RouteRegistry::registerProvider(IWebRouteProvider *provider) {
  if (!provider || _count >= kMaxProviders) return false;
  _providers[_count++] = provider;
  return true;
}

void RouteRegistry::registerAll(WebServerManager &web) {
  for (size_t i = 0; i < _count; ++i) {
    if (!_providers[i]) continue;
    Serial.printf("[web] Registering routes: %s\n",
                  _providers[i]->providerName());
    _providers[i]->registerRoutes(web);
  }
}

bool RouteRegistry::dispatchNotFound(AsyncWebServerRequest *req) {
  IWebRouteProvider *ordered[kMaxProviders];
  int priorities[kMaxProviders];
  size_t n = 0;

  for (size_t i = 0; i < _count; ++i) {
    if (!_providers[i]) continue;
    const int priority = _providers[i]->notFoundPriority();
    if (priority < 0) continue;
    ordered[n] = _providers[i];
    priorities[n] = priority;
    ++n;
  }

  for (size_t i = 0; i + 1 < n; ++i) {
    for (size_t j = i + 1; j < n; ++j) {
      if (priorities[j] < priorities[i]) {
        int tmpP = priorities[i];
        priorities[i] = priorities[j];
        priorities[j] = tmpP;
        IWebRouteProvider *tmp = ordered[i];
        ordered[i] = ordered[j];
        ordered[j] = tmp;
      }
    }
  }

  for (size_t i = 0; i < n; ++i) {
    if (ordered[i]->handleNotFound(req)) return true;
  }
  return false;
}

size_t RouteRegistry::count() const {
  return _count;
}
