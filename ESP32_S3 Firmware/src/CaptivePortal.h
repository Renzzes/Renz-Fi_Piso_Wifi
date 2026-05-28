#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>

class CaptivePortal {
 public:
  void begin();
  void loop();
  bool isCaptiveRequest(AsyncWebServerRequest *request) const;
  void redirectToPortal(AsyncWebServerRequest *request) const;

 private:
  DNSServer _dns;
};
