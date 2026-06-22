#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// EventBus provides an SSE /api/events endpoint via AsyncEventSource.
// The _source pointer is nullptr until begin() is called; begin() must only be
// invoked AFTER Ethernet link-up so that no lwIP socket is opened during
// global/static construction (avoids "Invalid mbox" asserts on ESP32-S3).
class EventBus {
 public:
  explicit EventBus(const char *url = "/api/events");
  ~EventBus();

  // Allocates AsyncEventSource and registers it on the server.
  // Call only after ETH.begin() + confirmed link-up.
  void begin(AsyncWebServer &server);

  // Broadcast an SSE event to all connected clients.
  void emit(const char *event, const String &json = "{}");

  // Send a periodic keepalive ping; call from loop().
  void heartbeat();

 private:
  const char       *_url;
  AsyncEventSource *_source        = nullptr;
  uint32_t          _lastHeartbeat = 0;
};
