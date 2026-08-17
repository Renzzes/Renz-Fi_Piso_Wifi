#include "EventBusRouteProvider.h"

#include "RenzFiDebug.h"
#include "WebServerManager.h"

void EventBusRouteProvider::bind(EventBus *events) {
  _events = events;
}

void EventBusRouteProvider::registerRoutes(WebServerManager &web) {
#if RENZFI_DISABLE_EVENTBUS_SSE
  Serial.println("[events] SSE route registration skipped (RENZFI_DISABLE_EVENTBUS_SSE)");
  return;
#endif
  if (_events) _events->begin(web.routeServer());
}

const char *EventBusRouteProvider::providerName() const {
  return "EventBus";
}
