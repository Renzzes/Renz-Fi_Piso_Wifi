#pragma once

#include <ESPAsyncWebServer.h>

// Minimal HTML error pages for non-API HTTP responses.
class ErrorHandler {
 public:
  static void serve(AsyncWebServerRequest *req, int status);
  static const char *titleForStatus(int status);
};
