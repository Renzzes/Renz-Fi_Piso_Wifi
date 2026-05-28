#include "FirmwareApp.h"

#include <esp_heap_caps.h>

FirmwareApp::FirmwareApp() : _server(RenzFiConfig::HTTP_PORT), _events("/api/events") {}

void FirmwareApp::begin() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("Renz-Fi ESP32-S3 firmware starting");

  if (psramFound()) {
    Serial.printf("PSRAM detected: %u bytes\n", ESP.getPsramSize());
  } else {
    Serial.println("PSRAM not detected; continuing with internal RAM");
  }

  _wifi.begin();
  _captive.begin();
  _storage.begin();
  _events.begin(_server);
  _logger.begin(&_storage, &_events);
  _auth.begin(&_storage, &_logger);
  _sessions.begin(&_storage, &_logger, &_events);
  _promos.begin(&_storage, &_logger, &_events);
  _vouchers.begin(&_storage, &_logger, &_events);
  _mikrotik.begin(&_storage, &_logger);
  _coin.begin(&_storage, &_logger, &_events, &_promos, &_sessions);
  _api.begin(&_server, &_storage, &_auth, &_sessions, &_promos, &_vouchers, &_coin, &_mikrotik, &_logger, &_events, &_captive);

  const char *headers[] = {"Cookie"};
  _server.collectHeaders(headers, 1);
  DefaultHeaders::Instance().addHeader("X-Content-Type-Options", "nosniff");
  DefaultHeaders::Instance().addHeader("X-Frame-Options", "SAMEORIGIN");
  DefaultHeaders::Instance().addHeader("Referrer-Policy", "same-origin");

  _server.begin();
  _logger.info("system", "Firmware boot complete");
}

void FirmwareApp::loop() {
  _captive.loop();
  _coin.loop();
  _events.heartbeat();

  if (millis() - _lastCleanup > RenzFiConfig::CLEANUP_INTERVAL_MS) {
    _lastCleanup = millis();
    _auth.cleanupExpired();
    _sessions.cleanupExpired();
  }
}
