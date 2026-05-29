#include "EventBus.h"

#include "Config.h"

EventBus::EventBus(const char *url) : _events(url) {}

AsyncEventSource &EventBus::source() {
  return _events;
}

void EventBus::begin(AsyncWebServer &server) {
  _events.onConnect([](AsyncEventSourceClient *client) {
    client->send("{\"ok\":true}", "system.status", millis(), 3000);
    client->send("connected", "ping", millis(), 3000);
  });
  server.addHandler(&_events);
}

void EventBus::emit(const char *event, const String &json) {
  _events.send(json.c_str(), event, millis(), 3000);
}

void EventBus::heartbeat() {
  if (millis() - _lastHeartbeat < RenzFiConfig::SSE_HEARTBEAT_MS) return;
  _lastHeartbeat = millis();
  _events.send("{}", "ping", millis(), 3000);
}
