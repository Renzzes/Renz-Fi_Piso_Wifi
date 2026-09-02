#include "EventBus.h"

#include "Config.h"
#include "DmaMemoryMonitor.h"
#include "web/HttpPlaneGate.h"

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

  // Pre-construction reject while factory reset is busy OR DMA is critical.
  // authorizeConnect installs AsyncAuthorizationMiddleware that runs before
  // AsyncEventSource::handleRequest — so AsyncEventSourceClient is never built
  // and onConnect never runs. Do NOT reject via client->close() in onConnect:
  // that re-enters disconnect and nulls _client before setNoDelay (see
  // docs/FACTORY_RESET_SSE_ONCONNECT_CLOSE_FORENSIC.md).
  _source->authorizeConnect([](AsyncWebServerRequest *request) -> bool {
    (void)request;
    if (HttpPlaneGate::isFactoryResetBusy()) {
      Serial.println(
          "[http-quiesce] SSE rejected before client construction "
          "reason=factory_reset_busy");
      return false;
    }
    if (DmaMemoryMonitor::isEthDmaCritical() ||
        !DmaMemoryMonitor::hasHttpServeHeadroom()) {
      Serial.println(
          "[http-quiesce] SSE rejected before client construction "
          "reason=ETH_DMA_LOW");
      DmaMemoryMonitor::logSnapshot("sse-admit-dma-low");
      return false;
    }
    return true;
  });

  _source->onConnect([](AsyncEventSourceClient *client) {
    if (!client) return;
    // Fully constructed client only. Never close()/delete/mutate collections.
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
  if (HttpPlaneGate::isFactoryResetBusy()) return;
  // SSE frames also need W5500 SPI DMA. Skip observational Admin traffic when
  // DMA is critical so portal/coin Core paths keep Ethernet alive.
  if (DmaMemoryMonitor::isEthDmaCritical()) return;
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
  if (HttpPlaneGate::isFactoryResetBusy()) return;
  if (DmaMemoryMonitor::isEthDmaCritical()) return;
  if (_source->count() == 0) return;
  if (millis() - _lastHeartbeat >= 30000) {
    _lastHeartbeat = millis();
    _source->send("{}", "ping", millis());
  }
}

void EventBus::closeAllClients() {
  if (!_source) return;
  if (_source->count() == 0) return;
  Serial.printf("[http-quiesce] closing SSE clients count=%u\n",
                static_cast<unsigned>(_source->count()));
  _source->close();
}

size_t EventBus::clientCount() const {
  return _source ? _source->count() : 0;
}
