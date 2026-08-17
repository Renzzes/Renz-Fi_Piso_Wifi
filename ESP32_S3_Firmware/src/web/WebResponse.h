#pragma once

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>

#include "CacheManager.h"

class WebResponse {
 public:
  static void addCorsHeaders(AsyncWebServerResponse *res);
  static void addSecurityHeaders(AsyncWebServerResponse *res);

  static void serveFile(AsyncWebServerRequest *req, fs::FS &fs,
                        const String &fsPath, const String &mimePath,
                        CachePolicy cache = CachePolicy::NoCache,
                        bool gzip = false);

  static void serveJson(AsyncWebServerRequest *req, int status,
                        const String &body,
                        CachePolicy cache = CachePolicy::NoCache);

  static void serveJsonEnvelope(AsyncWebServerRequest *req, int status,
                                JsonDocument &doc,
                                CachePolicy cache = CachePolicy::NoCache);

  static void serveErrorJson(AsyncWebServerRequest *req, int status,
                             const String &error, const String &code);

  static void serveNotFound(AsyncWebServerRequest *req, bool plainText = true);

  static void serveRedirect(AsyncWebServerRequest *req, const String &location,
                            int statusCode = 302);

  static void serveDownload(AsyncWebServerRequest *req, fs::FS &fs,
                            const String &fsPath, const String &filename,
                            const char *mime);

  static void serveOptions(AsyncWebServerRequest *req);
};
