#include "EventBus.h"

#include "Config.h"

EventBus::EventBus(const char *url) : _url(url) {}

EventBus::~EventBus() {
  delete _source;
}

void EventBus::begin(AsyncWebServer &server) {
  if (!_source) {
    _source = new AsyncEventSource(_url);
  }
  if (!_source) {
    Serial.println("[events] EventBus begin failed — source allocation failed");
    return;
  }

  Serial.println("[events] EventBus SSE endpoint registered");

  _source->onConnect([](AsyncEventSourceClient *client) {
    if (!client) return;
    client->send("{\"ok\":true}", "system.status", millis(), 1000);
    client->send("connected", "ping", millis(), 1000);
  });

  server.addHandler(_source);
}

void EventBus::emit(const char *event, const String &json) {
  if (!event) return;
  if (_internalListener) {
    _internalListener(event, json, _internalListenerCtx);
  }
  if (!_source) return;
  if (_source->count() == 0) return;
  const char *payload = json.length() > 0 ? json.c_str() : "{}";
  _source->send(payload, event, millis());
}

void EventBus::setInternalListener(InternalListener listener, void *ctx) {
  _internalListener = listener;
  _internalListenerCtx = ctx;
}

void EventBus::heartbeat() {
  if (!_source) return;
  if (_source->count() == 0) return;
  if (millis() - _lastHeartbeat >= 30000) {
    _lastHeartbeat = millis();
    _source->send("{}", "ping", millis());
  }
}

size_t EventBus::clientCount() const {
  return _source ? _source->count() : 0;
}
