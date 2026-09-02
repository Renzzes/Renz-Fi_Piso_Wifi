#include "WebResponse.h"

#include <FS.h>
#include <cstring>
#include <esp_heap_caps.h>
#include <memory>

#include "CacheManager.h"
#include "DmaMemoryMonitor.h"
#include "JsonHeap.h"
#include "MemoryDiagnostics.h"
#include "MimeResolver.h"

namespace {

constexpr size_t kLargeAssetBytes = 32U * 1024U;

int s_largeAssetInFlight = 0;

// Setup status is polled every 250 ms during Step 4 Finish. serializeJson →
// Arduino String copies that envelope into INTERNAL/DMA SRAM (N16R8
// remaining-issues). W5500 setup_dma_priv_buffer needs a contiguous ~1394 B
// block from the same pool.
struct PsramJsonBody {
  char *buf = nullptr;
  size_t len = 0;
  bool holdsSlot = false;
  ~PsramJsonBody() {
    if (holdsSlot) DmaMemoryMonitor::releaseHttpSlot();
    if (buf) heap_caps_free(buf);
  }
};

std::shared_ptr<PsramJsonBody> makePsramJsonBody(JsonDocument &doc) {
  auto body = std::make_shared<PsramJsonBody>();
  const size_t n = measureJson(doc);
  body->buf = static_cast<char *>(
      heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!body->buf) {
    body->buf = static_cast<char *>(heap_caps_malloc(n + 1, MALLOC_CAP_8BIT));
  }
  if (!body->buf) return nullptr;
  body->len = serializeJson(doc, body->buf, n + 1);
  return body;
}

void dropClient(AsyncWebServerRequest *req, const char *reason) {
  // Proven StoreProhibited (EXCVADDR≈0xb4): closing/aborting the socket while
  // DMA is already exhausted still drives W5500 TX + concurrent AsyncWebServer
  // _send paths (ArduinoJson→String). Do not touch the client — peer times out;
  // in-flight paced bodies already return RESPONSE_TRY_AGAIN.
  (void)req;
  Serial.printf("[http] drop reason=%s (ETH DMA critical, no socket I/O)\n",
                reason ? reason : "dma");
  DmaMemoryMonitor::logSnapshot("http-drop-dma-critical");
}

void sendEthDmaLow(AsyncWebServerRequest *req, const char *reason) {
  // When DMA is already below the W5500 RX floor, sending a 503 still needs a
  // SPI TX bounce buffer and can race RX into LoadProhibited. Close instead.
  if (DmaMemoryMonitor::isEthDmaCritical()) {
    dropClient(req, reason);
    return;
  }
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

bool admitPacedHttp(AsyncWebServerRequest *req, const char *reason) {
  if (DmaMemoryMonitor::isEthDmaCritical()) {
    dropClient(req, reason);
    return false;
  }
  if (!DmaMemoryMonitor::hasHttpServeHeadroom()) {
    sendEthDmaLow(req, reason);
    return false;
  }
  if (!DmaMemoryMonitor::tryAcquireHttpSlot()) {
    sendEthDmaLow(req, "concurrency");
    return false;
  }
  return true;
}

void sendPsramJson(AsyncWebServerRequest *req, int status, JsonDocument &doc,
                   CachePolicy cache) {
  if (!admitPacedHttp(req, "json-admit")) return;

  auto body = makePsramJsonBody(doc);
  if (!body) {
    DmaMemoryMonitor::releaseHttpSlot();
    req->send(500, "application/json",
              "{\"success\":false,\"error\":\"JSON alloc failed\","
              "\"code\":\"JSON_ALLOC_FAILED\"}");
    return;
  }
  body->holdsSlot = true;

  AsyncWebServerResponse *res = req->beginResponse(
      "application/json", body->len,
      [body](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
        if (!buffer || !body->buf || index >= body->len) return 0;
        if (DmaMemoryMonitor::isEthDmaCritical() ||
            !DmaMemoryMonitor::hasEthTransmitHeadroom()) {
          return RESPONSE_TRY_AGAIN;
        }
        const size_t remain = body->len - index;
        const size_t n = remain < maxLen ? remain : maxLen;
        memcpy(buffer, reinterpret_cast<const uint8_t *>(body->buf) + index, n);
        return n;
      });
  if (!res) {
    // beginResponse failed — release slot via PsramJsonBody dtor.
    return;
  }
  res->setCode(status);
  WebResponse::addCorsHeaders(res);
  CacheManager::apply(res, cache);
  req->send(res);
}

struct LargeAssetStream {
  File file;
  bool holdsSlot = false;
  LargeAssetStream() {
    s_largeAssetInFlight++;
    holdsSlot = DmaMemoryMonitor::tryAcquireHttpSlot();
  }
  ~LargeAssetStream() {
    if (file) file.close();
    if (s_largeAssetInFlight > 0) s_largeAssetInFlight--;
    if (holdsSlot) DmaMemoryMonitor::releaseHttpSlot();
  }
};

struct SmallAssetStream {
  File file;
  bool holdsSlot = false;
  explicit SmallAssetStream(bool acquireSlot) : holdsSlot(acquireSlot) {}
  ~SmallAssetStream() {
    if (file) file.close();
    if (holdsSlot) DmaMemoryMonitor::releaseHttpSlot();
  }
};

}  // namespace

bool WebResponse::ensureEthTransmitHeadroom(AsyncWebServerRequest *req,
                                            const char *reason) {
  if (DmaMemoryMonitor::isEthDmaCritical()) {
    dropClient(req, reason);
    return false;
  }
  if (DmaMemoryMonitor::hasHttpServeHeadroom()) return true;
  sendEthDmaLow(req, reason);
  return false;
}

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

  // Gate must run before beginResponse allocates the 2872-byte send buffer
  // and before lwIP/W5500 fill the TCP window. This applies to every file
  // serve, not only large SPA assets: favicon.svg / icon-192.png / small
  // manifest files go through req->beginResponse(fs, path, mime) too, which
  // has no per-chunk pacing hook. A concurrent browser fan-out (favicon +
  // icons + several JSON polls, all on async_tcp) can still request a small
  // (~200-450 byte) W5500 SPI DMA bounce buffer while the pool is already
  // fragmented from a prior large asset/JSON burst — proven by
  // [dma-alloc-fail] size=208/437/438 caps=0x00000808 task=async_tcp during
  // /admin + /dashboard concurrent small-file + JSON load.
  if (DmaMemoryMonitor::isEthDmaCritical()) {
    dropClient(req, "headroom");
    return;
  }
  if (!DmaMemoryMonitor::hasHttpServeHeadroom()) {
    sendEthDmaLow(req, "headroom");
    return;
  }

  const bool isStylesheet =
      mimePath.endsWith(".css") || mimePath.endsWith(".css.gz");
  // Stylesheets must not lose to the SPA JS single-flight lock: a dropped CSS
  // response paints the unstyled login form until reload (when CSS is cached).
  const bool largeAsset =
      fileBytes >= kLargeAssetBytes && !isStylesheet;
  if (largeAsset) {
    // Large assets additionally serialize against each other; small files
    // complete quickly enough that single-flight is not required.
    if (s_largeAssetInFlight != 0) {
      sendEthDmaLow(req, "in-flight");
      return;
    }
    if (MemoryDiagnostics::hasOperationalPortalLoad() &&
        !DmaMemoryMonitor::hasDmaHeadroom(
            DmaMemoryMonitor::kMinLargestDmaBlockForLargeAssetWithPortal)) {
      Serial.printf(
          "[spa-stream] defer path=%s bytes=%u reason=portal-load "
          "dma_largest=%u\n",
          fsPath.c_str(), static_cast<unsigned>(fileBytes),
          static_cast<unsigned>(
              heap_caps_get_largest_free_block(MALLOC_CAP_DMA)));
      sendEthDmaLow(req, "portal-load");
      return;
    }
    if (DmaMemoryMonitor::pacedHttpInFlight() >=
        DmaMemoryMonitor::kMaxPacedHttpInFlight) {
      sendEthDmaLow(req, "concurrency");
      return;
    }
  } else if (!isStylesheet &&
             DmaMemoryMonitor::pacedHttpInFlight() >=
                 DmaMemoryMonitor::kMaxPacedHttpInFlight) {
    sendEthDmaLow(req, "concurrency");
    return;
  }
  // Stylesheets skip the large-asset lock and the paced concurrency reject so
  // first paint is not left unstyled while SPA JS is streaming.

  const String mime = MimeResolver::fromPath(mimePath);
  AsyncWebServerResponse *res = nullptr;

  if (largeAsset) {
    auto ctx = std::make_shared<LargeAssetStream>();
    if (!ctx->holdsSlot) {
      sendEthDmaLow(req, "concurrency");
      return;
    }
    ctx->file = fs.open(fsPath, "r");
    if (!ctx->file) {
      serveNotFound(req, true);
      return;
    }
    Serial.printf(
        "[spa-stream] path=%s bytes=%u inFlight=%d paced=%d dma_largest=%u\n",
        fsPath.c_str(), static_cast<unsigned>(fileBytes),
        s_largeAssetInFlight, DmaMemoryMonitor::pacedHttpInFlight(),
        static_cast<unsigned>(
            heap_caps_get_largest_free_block(MALLOC_CAP_DMA)));
    res = req->beginResponse(
        mime.c_str(), fileBytes,
        [ctx](uint8_t *buf, size_t maxLen, size_t /*index*/) -> size_t {
          if (!buf || !ctx->file) return 0;
          if (DmaMemoryMonitor::isEthDmaCritical() ||
              !DmaMemoryMonitor::hasEthTransmitHeadroom()) {
            return RESPONSE_TRY_AGAIN;
          }
          return ctx->file.read(buf, maxLen);
        });
  } else {
    const bool slotOk = DmaMemoryMonitor::tryAcquireHttpSlot();
    if (!slotOk) {
      sendEthDmaLow(req, "concurrency");
      return;
    }
    auto ctx = std::make_shared<SmallAssetStream>(true);
    ctx->file = fs.open(fsPath, "r");
    if (!ctx->file) {
      DmaMemoryMonitor::releaseHttpSlot();
      serveNotFound(req, true);
      return;
    }
    res = req->beginResponse(
        mime.c_str(), fileBytes,
        [ctx](uint8_t *buf, size_t maxLen, size_t /*index*/) -> size_t {
          if (!buf || !ctx->file) return 0;
          if (DmaMemoryMonitor::isEthDmaCritical() ||
              !DmaMemoryMonitor::hasEthTransmitHeadroom()) {
            return RESPONSE_TRY_AGAIN;
          }
          return ctx->file.read(buf, maxLen);
        });
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
  if (!ensureEthTransmitHeadroom(req, "json-string")) return;
  AsyncWebServerResponse *res =
      req->beginResponse(status, "application/json", body);
  addCorsHeaders(res);
  CacheManager::apply(res, cache);
  req->send(res);
}

void WebResponse::serveJsonEnvelope(AsyncWebServerRequest *req, int status,
                                    JsonDocument &doc, CachePolicy cache) {
  sendPsramJson(req, status, doc, cache);
}

void WebResponse::serveErrorJson(AsyncWebServerRequest *req, int status,
                                 const String &error, const String &code) {
  PsramJsonDocument envHeap;
  JsonDocument &doc = envHeap.doc();
  doc["success"] = false;
  doc["error"] = error;
  doc["code"] = code;
  sendPsramJson(req, status, doc, CachePolicy::NoCache);
}

void WebResponse::serveNotFound(AsyncWebServerRequest *req, bool plainText) {
  if (!req) return;
  if (DmaMemoryMonitor::isEthDmaCritical()) {
    dropClient(req, "not-found");
    return;
  }
  if (!ensureEthTransmitHeadroom(req, "not-found")) return;
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
  if (DmaMemoryMonitor::isEthDmaCritical()) {
    dropClient(req, "redirect");
    return;
  }
  if (!ensureEthTransmitHeadroom(req, "redirect")) return;
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
  if (!ensureEthTransmitHeadroom(req, "download")) return;
  AsyncWebServerResponse *res = req->beginResponse(fs, fsPath, mime);
  res->addHeader("Content-Disposition",
                 String("attachment; filename=\"") + filename + "\"");
  addCorsHeaders(res);
  CacheManager::apply(res, CachePolicy::NoCache);
  req->send(res);
}

void WebResponse::serveOptions(AsyncWebServerRequest *req) {
  if (!req) return;
  if (DmaMemoryMonitor::isEthDmaCritical()) {
    dropClient(req, "options");
    return;
  }
  if (!ensureEthTransmitHeadroom(req, "options")) return;
  AsyncWebServerResponse *res = req->beginResponse(204, "text/plain", "");
  addCorsHeaders(res);
  req->send(res);
}
