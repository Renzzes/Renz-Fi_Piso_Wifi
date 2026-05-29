#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

class EventBus {
 public:
  explicit EventBus(const char *url = "/api/events");

  AsyncEventSource &source();
  void begin(AsyncWebServer &server);
  void emit(const char *event, const String &json = "{}");
  void heartbeat();

 private:
  AsyncEventSource _events;
  uint32_t _lastHeartbeat = 0;
};
