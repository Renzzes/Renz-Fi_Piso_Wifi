#include "FirmwareApp.h"

#include <esp_heap_caps.h>
#include <SPIFFS.h>
#include <WiFi.h>

FirmwareApp::FirmwareApp() : _server(RenzFiConfig::HTTP_PORT), _events("/api/events") {}

void FirmwareApp::begin() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("========================================");
  Serial.println("RENZ-FI ESP32-S3 FIRMWARE STARTING");
  Serial.printf("Firmware version: %s\n", RenzFiConfig::FIRMWARE_VERSION);
  Serial.printf("Uptime: %lu ms\n", millis());
  Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
  Serial.printf("Flash size: %u bytes\n", ESP.getFlashChipSize());

  if (psramFound()) {
    Serial.printf("PSRAM detected: %u bytes\n", ESP.getPsramSize());
  } else {
    Serial.println("PSRAM not detected; continuing with internal RAM");
  }

  Serial.println("[boot] AP-first startup sequence starting");
  _wifi.begin();
  _captive.begin();

  Serial.println("[boot] SPIFFS initialization starting");
  bool frontendReady = SPIFFS.begin(true);
  if (frontendReady) {
    Serial.println("[boot] SPIFFS mounted");
    Serial.println(SPIFFS.exists("/index.html") ? "[boot] Frontend assets detected" : "[boot] Frontend index.html not found; upload data/ SPIFFS image");
  } else {
    Serial.println("[ERROR] SPIFFS mount failed");
  }

  Serial.println("[boot] Event bus initialization starting");
  _events.begin(_server);
  Serial.println("[boot] Event bus initialized");

  Serial.println("[boot] Runtime SD storage initialization starting");
  bool sdReady = _storage.begin();
  Serial.printf("[boot] SD mount status: %s\n", sdReady ? "mounted" : "degraded");

  Serial.println("[boot] Logger initialization starting");
  _logger.begin(&_storage, &_events);
  Serial.println("[boot] Logger initialized");

  Serial.println("[boot] Auth manager initialization starting");
  _auth.begin(&_storage, &_logger);
  Serial.println("[boot] Auth manager initialized");

  Serial.println("[boot] Session manager initialization starting");
  _sessions.begin(&_storage, &_logger, &_events);
  Serial.println("[boot] Session manager initialized");

  Serial.println("[boot] Promo manager initialization starting");
  _promos.begin(&_storage, &_logger, &_events);
  Serial.println("[boot] Promo manager initialized");

  Serial.println("[boot] Voucher manager initialization starting");
  _vouchers.begin(&_storage, &_logger, &_events);
  Serial.println("[boot] Voucher manager initialized");

  Serial.println("[boot] MikroTik manager initialization starting");
  _mikrotik.begin(&_storage, &_logger);
  Serial.println("[boot] MikroTik manager initialized in offline boundary mode");

  Serial.println("[boot] Coin manager initialization starting");
  _coin.begin(&_storage, &_logger, &_events, &_promos, &_sessions);
  Serial.println("[boot] Coin manager initialized");

  Serial.println("[boot] API server route registration starting");
  _api.begin(&_server, &_storage, &_auth, &_sessions, &_promos, &_vouchers, &_coin, &_mikrotik, &_logger, &_events, &_captive);
  Serial.println("[boot] API server routes registered");
  Serial.println("[boot] Frontend static hosting routes registered");

  DefaultHeaders::Instance().addHeader("X-Content-Type-Options", "nosniff");
  DefaultHeaders::Instance().addHeader("X-Frame-Options", "SAMEORIGIN");
  DefaultHeaders::Instance().addHeader("Referrer-Policy", "same-origin");

  Serial.println("[boot] AsyncWebServer startup starting");
  _server.begin();
  Serial.println("[boot] AsyncWebServer started");
  Serial.printf("[boot] AP SSID: %s\n", RenzFiConfig::AP_SSID);
  Serial.printf("[boot] AP IP address: %s\n", WiFi.softAPIP().toString().c_str());
  Serial.printf("[boot] WiFi mode: %d\n", WiFi.getMode());
  Serial.printf("[boot] Free heap after boot: %u bytes\n", ESP.getFreeHeap());
  Serial.println("RENZ-FI BOOT COMPLETE");
  Serial.println("========================================");
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
