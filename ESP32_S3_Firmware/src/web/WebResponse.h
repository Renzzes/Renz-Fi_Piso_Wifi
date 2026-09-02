#pragma once

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>

#include "CacheManager.h"

class WebResponse {
 public:
  static void addCorsHeaders(AsyncWebServerResponse *res);
  static void addSecurityHeaders(AsyncWebServerResponse *res);

  // Shared DMA safety gate for every response that will transmit over
  // Ethernet (W5500 SPI). Sends 503 ETH_DMA_LOW + Retry-After (or closes the
  // client when DMA is already below the W5500 RX survival floor) and returns
  // false when dma_largest is below the HTTP-admit threshold. Callers must
  // return immediately when this returns false.
  static bool ensureEthTransmitHeadroom(AsyncWebServerRequest *req,
                                        const char *reason);

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
