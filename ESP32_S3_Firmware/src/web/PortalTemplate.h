#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

class PortalTemplate {
 public:
  static String process(const String &html, AsyncWebServerRequest *req);
};
