#include "WebResponse.h"

#include <FS.h>
#include <memory>

#include "CacheManager.h"
#include "DmaMemoryMonitor.h"
#include "MimeResolver.h"

namespace {

constexpr size_t kLargeAssetBytes = 32U * 1024U;

int s_largeAssetInFlight = 0;

void sendEthDmaLow(AsyncWebServerRequest *req, const char *reason) {
  Serial.printf("[http] 503 reason=ETH_DMA_LOW detail=%s\n",
                reason ? reason : "");
  DmaMemoryMonitor::logSnapshot("spa-serve-dma-low");
  AsyncWebServerResponse *res = req->beginResponse(
      503, "application/json",
      "{\"success\":false,\"error\":\"Ethernet DMA temporarily "
      "exhausted\",\"code\":\"ETH_DMA_LOW\"}");
  WebResponse::addCorsHeaders(res);
  res->addHeader("Retry-After", "2");
  CacheManager::apply(res, CachePolicy::NoCache);
  req->send(res);
}

struct LargeAssetStream {
  File file;
  LargeAssetStream() { s_largeAssetInFlight++; }
  ~LargeAssetStream() {
    if (file) file.close();
    if (s_largeAssetInFlight > 0) s_largeAssetInFlight--;
  }
};

}  // namespace

void WebResponse::addCorsHeaders(AsyncWebServerResponse *res) {
  if (!res) return;
  addSecurityHeaders(res);
  // Access-Control-Allow-Origin is owned solely by DefaultHeaders (registered
  // once in WebServerManager::initialize). Adding it here again duplicated ACAO
  // when DefaultHeaders already contributed one (Chrome: "*, *").
  res->addHeader("Access-Control-Allow-Methods",
                 "GET, POST, PUT, DELETE, OPTIONS");
  res->addHeader("Access-Control-Allow-Headers",
                 "Content-Type, Authorization");
}

void WebResponse::addSecurityHeaders(AsyncWebServerResponse *res) {
  if (!res) return;
  res->addHeader("X-Content-Type-Options", "nosniff");
  res->addHeader("X-Frame-Options", "SAMEORIGIN");
  res->addHeader("Referrer-Policy", "same-origin");
}

void WebResponse::serveFile(AsyncWebServerRequest *req, fs::FS &fs,
                            const String &fsPath, const String &mimePath,
                            CachePolicy cache, bool gzip) {
  if (!req) return;

  File probe = fs.open(fsPath, "r");
  const size_t fileBytes = probe ? probe.size() : 0;
  if (probe) probe.close();

  const bool largeAsset = fileBytes >= kLargeAssetBytes;
  if (largeAsset) {
    // Gate must run before beginResponse allocates the 2872-byte send buffer
    // and before lwIP/W5500 fill the TCP window. Healthy DMA at accept is
    // not enough: a second concurrent JS/CSS/PNG stream is what collapses
    // dma_largest below the 1490-byte SPI priv TX buffer.
    if (!DmaMemoryMonitor::hasEthTransmitHeadroom()) {
      sendEthDmaLow(req, "headroom");
      return;
    }
    if (s_largeAssetInFlight != 0) {
      sendEthDmaLow(req, "in-flight");
      return;
    }
  }

  const String mime = MimeResolver::fromPath(mimePath);
  AsyncWebServerResponse *res = nullptr;

  if (largeAsset) {
    auto ctx = std::make_shared<LargeAssetStream>();
    ctx->file = fs.open(fsPath, "r");
    if (!ctx->file) {
      serveNotFound(req, true);
      return;
    }
    Serial.printf(
        "[spa-stream] path=%s bytes=%u inFlight=%d dma_largest=%u\n",
        fsPath.c_str(), static_cast<unsigned>(fileBytes),
        s_largeAssetInFlight,
        static_cast<unsigned>(
            heap_caps_get_largest_free_block(MALLOC_CAP_DMA)));
    res = req->beginResponse(
        mime.c_str(), fileBytes,
        [ctx](uint8_t *buf, size_t maxLen, size_t /*index*/) -> size_t {
          if (!buf || !ctx->file) return 0;
          if (!DmaMemoryMonitor::hasEthTransmitHeadroom()) {
            return RESPONSE_TRY_AGAIN;
          }
          return ctx->file.read(buf, maxLen);
        });
  } else {
    res = req->beginResponse(fs, fsPath, mime.c_str());
  }

  if (!res) {
    serveNotFound(req, true);
    return;
  }
  if (gzip) res->addHeader("Content-Encoding", "gzip");
  addCorsHeaders(res);
  CacheManager::apply(res, cache);
  req->send(res);
}

void WebResponse::serveJson(AsyncWebServerRequest *req, int status,
                            const String &body, CachePolicy cache) {
  if (!req) return;
  AsyncWebServerResponse *res =
      req->beginResponse(status, "application/json", body);
  addCorsHeaders(res);
  CacheManager::apply(res, cache);
  req->send(res);
}

void WebResponse::serveJsonEnvelope(AsyncWebServerRequest *req, int status,
                                    JsonDocument &doc, CachePolicy cache) {
  String body;
  serializeJson(doc, body);
  serveJson(req, status, body, cache);
}

void WebResponse::serveErrorJson(AsyncWebServerRequest *req, int status,
                                 const String &error, const String &code) {
  DynamicJsonDocument doc(256);
  doc["success"] = false;
  doc["error"] = error;
  doc["code"] = code;
  String body;
  serializeJson(doc, body);
  serveJson(req, status, body, CachePolicy::NoCache);
}

void WebResponse::serveNotFound(AsyncWebServerRequest *req, bool plainText) {
  if (!req) return;
  if (plainText) {
    AsyncWebServerResponse *res =
        req->beginResponse(404, "text/plain", "Not Found");
    addCorsHeaders(res);
    CacheManager::apply(res, CachePolicy::NoCache);
    req->send(res);
    return;
  }
  req->send(404, "text/plain", "Not Found");
}

void WebResponse::serveRedirect(AsyncWebServerRequest *req,
                                const String &location, int statusCode) {
  if (!req) return;
  AsyncWebServerResponse *res = req->beginResponse(statusCode);
  res->addHeader("Location", location);
  addCorsHeaders(res);
  CacheManager::apply(res, CachePolicy::NoCache);
  req->send(res);
}

void WebResponse::serveDownload(AsyncWebServerRequest *req, fs::FS &fs,
                                const String &fsPath, const String &filename,
                                const char *mime) {
  if (!req) return;
  AsyncWebServerResponse *res = req->beginResponse(fs, fsPath, mime);
  res->addHeader("Content-Disposition",
                 String("attachment; filename=\"") + filename + "\"");
  addCorsHeaders(res);
  CacheManager::apply(res, CachePolicy::NoCache);
  req->send(res);
}

void WebResponse::serveOptions(AsyncWebServerRequest *req) {
  if (!req) return;
  AsyncWebServerResponse *res = req->beginResponse(204, "text/plain", "");
  addCorsHeaders(res);
  req->send(res);
}
