#include "StaticFileServer.h"

#include <SPIFFS.h>

#include "CacheManager.h"
#include "MimeResolver.h"
#include "HttpPlaneGate.h"
#include "SpiffsHost.h"
#include "WebRequestDiagnostics.h"
#include "WebResponse.h"
#include "WebServerManager.h"

namespace {

const char SPIFFS_FALLBACK_PAGE[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Renz-Fi Admin Dashboard</title>
  <style>
    body{font-family:Arial,sans-serif;margin:0;background:#0f172a;color:#e2e8f0;display:grid;min-height:100vh;place-items:center}
    main{max-width:640px;padding:32px}
    a{color:#38bdf8}
    code{background:#1e293b;padding:2px 6px;border-radius:4px}
  </style>
</head>
<body>
  <main>
    <h1>Renz-Fi Admin Dashboard</h1>
    <p>The ESP32-S3 hotspot and backend are running in degraded mode, but the SPIFFS frontend image is not available.</p>
    <p>Build the React app, copy <code>dist/*</code> into <code>ESP32_S3_Firmware/data/</code>, upload the SPIFFS image, then reopen the admin dashboard at <code>/admin</code> on the ESP32 STA address or fallback setup AP.</p>
    <p>Backend health: <a href="/api/health">/api/health</a></p>
  </main>
</body>
</html>)rawliteral";

const char *methodStr(WebRequestMethodComposite method) {
  return WebRequestDiagnostics::methodStr(method);
}

void sendFallbackPage(AsyncWebServerRequest *req) {
  AsyncWebServerResponse *res = req->beginResponse(
      200, "text/html; charset=utf-8", String(FPSTR(SPIFFS_FALLBACK_PAGE)));
  WebResponse::addCorsHeaders(res);
  CacheManager::apply(res, CachePolicy::NoCache);
  req->send(res);
}

}  // namespace

void StaticFileServer::logStaticRequest(AsyncWebServerRequest *req) {
  WebRequestDiagnostics::logRequest(req, "static");
}

void StaticFileServer::registerRoutes(WebServerManager &web) {
  // Production static trees are served through serveStaticOrIndex / handleNotFound
  // with HttpPlaneGate checks — do not register ungated AsyncStaticWebHandler
  // routes here (they would accept Management AP clients and read SPIFFS on
  // async_tcp, reproducing the watchdog under ETH+AP coexistence).
  (void)web;
  Serial.println("[web] StaticFileServer production file routes (plane-gated via handlers):");
  Serial.println("[web]   GET /assets/*, /static/* -> gated serveStaticOrIndex");
}

const char *StaticFileServer::providerName() const {
  return "StaticFileServer";
}

int StaticFileServer::notFoundPriority() const {
  return 40;
}

void StaticFileServer::serveStaticOrIndex(AsyncWebServerRequest *req) {
  WebRequestDiagnostics::RequestTimer timer(req, "StaticFileServer");
  if (!HttpPlaneGate::ensureProductionPlane(req)) return;

  String path = req->url();
  const int query = path.indexOf('?');
  if (query >= 0) path = path.substring(0, query);
  logStaticRequest(req);

  bool gzip = false;
  const String spiffsPath = resolveSpiffsServePath(path, &gzip);

  if (spiffsPath.isEmpty()) {
    if (path.startsWith("/assets/") ||
        path == "/manifest.webmanifest" || path == "/sw.js" ||
        path == "/favicon.svg" || path == "/favicon.ico") {
      Serial.printf("[http] 404 path=%s reason=missing-static-asset\n",
                    path.c_str());
      WebResponse::serveNotFound(req, true);
      return;
    }
    Serial.printf("[http] 200 path=%s reason=spa-fallback-page\n", path.c_str());
    sendFallbackPage(req);
    return;
  }

  String typePath = path;
  if (spiffsPath.endsWith("index.html") ||
      spiffsPath.endsWith("index.html.gz")) {
    typePath = "/index.html";
  } else if (spiffsPath.endsWith(".gz")) {
    typePath = spiffsPath.substring(0, spiffsPath.length() - 3);
  }

  if (!SPIFFS.exists(spiffsPath)) {
    Serial.printf("[http] 404 path=%s spiffs=%s reason=open-failed\n",
                  path.c_str(), spiffsPath.c_str());
    sendFallbackPage(req);
    return;
  }

  const CachePolicy cache = path.startsWith("/assets/")
                                ? CachePolicy::Immutable
                                : CachePolicy::NoCache;

  Serial.printf("[http] 200 path=%s spiffs=%s gzip=%s contentType=%s\n",
                path.c_str(), spiffsPath.c_str(), gzip ? "yes" : "no",
                MimeResolver::fromPath(typePath).c_str());

  if (spiffsPath.endsWith("index.html") ||
      spiffsPath.endsWith("index.html.gz")) {
    File probe = SPIFFS.open(spiffsPath, "r");
    const size_t bytes = probe ? probe.size() : 0;
    if (probe) probe.close();
    Serial.printf(
        "[http-forensic] path=%s spiffs=%s gzip=%s file_bytes=%u\n",
        path.c_str(), spiffsPath.c_str(), gzip ? "yes" : "no",
        static_cast<unsigned>(bytes));
  }

  WebResponse::serveFile(req, SPIFFS, spiffsPath, typePath, cache, gzip);
}

bool StaticFileServer::handleNotFound(AsyncWebServerRequest *req) {
  if (!req) return false;
  const String path = req->url();

  if (path.startsWith("/login/") || path.startsWith("/dashboard/")) {
    serveStaticOrIndex(req);
    return true;
  }

  if (path.startsWith("/portal/") || path == "/portal") {
    return false;
  }

  if (path.startsWith("/assets/")) {
    if (!HttpPlaneGate::ensureProductionPlane(req)) return true;
    serveStaticOrIndex(req);
    return true;
  }

  if (path.startsWith("/static/")) {
    if (!HttpPlaneGate::ensureProductionPlane(req)) return true;
    serveStaticOrIndex(req);
    return true;
  }

  if (path.startsWith("/downloads/")) {
    return false;
  }

  if (path.startsWith("/api/") || path.startsWith("/admin/")) {
    return false;
  }

  WebRequestDiagnostics::logRequest(req, "static-spa");
  serveStaticOrIndex(req);
  return true;
}
