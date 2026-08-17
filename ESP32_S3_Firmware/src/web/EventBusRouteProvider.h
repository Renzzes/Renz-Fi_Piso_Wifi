#pragma once

#include "EventBus.h"
#include "IWebRouteProvider.h"

class WebServerManager;

class EventBusRouteProvider : public IWebRouteProvider {
 public:
  void bind(EventBus *events);

  void registerRoutes(WebServerManager &web) override;
  const char *providerName() const override;

 private:
  EventBus *_events = nullptr;
};
