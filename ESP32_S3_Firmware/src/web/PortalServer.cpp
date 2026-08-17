#include "PortalServer.h"

#include <SPIFFS.h>

#include "CacheManager.h"
#include "ErrorHandler.h"
#include "HttpPlaneGate.h"
#include "ManagementApConfig.h"
#include "MimeResolver.h"
#include "PortalSpiffsLayout.h"
#include "PortalTemplate.h"
#include "WebRequestDiagnostics.h"
#include "WebResponse.h"
#include "WebServerManager.h"

void PortalServer::begin(AssetManager *assets) {
  _assets = assets;
  (void)_assets;
}

CachePolicy PortalServer::cachePolicyForPath(const String &path) const {
  if (path.endsWith(".html")) return CachePolicy::NoCache;
  if (path.endsWith(".css") || path.endsWith(".js")) return CachePolicy::ShortCache;
  if (path.endsWith(".png") || path.endsWith(".jpg") || path.endsWith(".jpeg") ||
      path.endsWith(".webp") || path.endsWith(".mp3") || path.endsWith(".ico")) {
    return CachePolicy::ShortCache;
  }
  return CachePolicy::NoCache;
}

bool PortalServer::isPortalUrl(const String &path) const {
  // Customer captive portal is served by MikroTik Hotspot. ESP32 /portal is
  // a recovery/dev fallback only — never claim "/" (reserved for admin entry).
  if (path == "/portal" || path.startsWith("/portal/")) return true;
  for (size_t i = 0; i < PortalSpiffsLayout::kFlatAliasCount; ++i) {
    if (path == PortalSpiffsLayout::kFlatAliases[i].urlPath) return true;
  }
  return false;
}

bool PortalServer::mapToSpiffsPath(const String &urlPath,
                                   String &spiffsOut) const {
  String path = urlPath;
  const int query = path.indexOf('?');
  if (query >= 0) path = path.substring(0, query);

  if (path == "/portal" || path == "/portal/") {
    spiffsOut = PortalSpiffsLayout::kLoginHtml;
    return true;
  }

  for (size_t i = 0; i < PortalSpiffsLayout::kFlatAliasCount; ++i) {
    if (path == PortalSpiffsLayout::kFlatAliases[i].urlPath) {
      spiffsOut = PortalSpiffsLayout::kFlatAliases[i].spiffsPath;
      return true;
    }
  }

  if (path.startsWith("/portal/")) {
    spiffsOut = path;
    return true;
  }

  return false;
}

bool PortalServer::isManagementApRequest(AsyncWebServerRequest *req) const {
  if (!req || !req->client()) return false;
  // The Management AP is admin/installer-only and never carries customer
  // traffic — a request that arrived on the AP's own IP (192.168.4.1)
  // could only have come from a device joined to "Renz-Fi Setup", never
  // from a paying customer on the Ethernet/MikroTik side. Distinguishing by
  // the local (server-side) endpoint IP works even though both interfaces
  // share one AsyncWebServer instance.
  return req->client()->localIP() == ManagementApConfig::IP;
}

void PortalServer::servePortalEntry(AsyncWebServerRequest *req) {
  if (!req) return;
  if (HttpPlaneGate::isSetupPlane(req)) {
    WebResponse::serveErrorJson(
        req, 403,
        "This route is available only through the Ethernet dashboard",
        "SETUP_PLANE_RESTRICTED");
    return;
  }
  if (!HttpPlaneGate::ensureManagementClient(req)) return;

  if (!SPIFFS.exists(PortalSpiffsLayout::kLoginHtml)) {
    Serial.println("[portal] login.html missing from SPIFFS");
    ErrorHandler::serve(req, 404);
    return;
  }

  File file = SPIFFS.open(PortalSpiffsLayout::kLoginHtml, "r");
  if (!file) {
    ErrorHandler::serve(req, 500);
    return;
  }
  String html = file.readString();
  file.close();

  html = PortalTemplate::process(html, req);
  AsyncWebServerResponse *res =
      req->beginResponse(200, "text/html; charset=utf-8", html);
  WebResponse::addCorsHeaders(res);
  CacheManager::apply(res, CachePolicy::NoCache);
  req->send(res);
}

void PortalServer::servePortalFile(AsyncWebServerRequest *req,
                                   const String &spiffsPath,
                                   const String &mimePath) {
  if (!req) return;
  if (HttpPlaneGate::isSetupPlane(req)) {
    WebResponse::serveErrorJson(
        req, 403,
        "This route is available only through the Ethernet dashboard",
        "SETUP_PLANE_RESTRICTED");
    return;
  }
  if (!HttpPlaneGate::ensureManagementClient(req)) return;
  if (!SPIFFS.exists(spiffsPath)) {
    Serial.printf("[portal] 404 missing spiffs=%s\n", spiffsPath.c_str());
    WebResponse::serveNotFound(req, true);
    return;
  }
  WebResponse::serveFile(req, SPIFFS, spiffsPath, mimePath,
                         cachePolicyForPath(mimePath));
}

void PortalServer::registerRoutes(WebServerManager &web) {
  AsyncWebServer &server = web.routeServer();

  // GET / is owned by SetupServer on the Management AP. Production portal
  // entry is /portal only (dev/recovery fallback on Ethernet).
  server.on("/portal", HTTP_GET, [this](AsyncWebServerRequest *req) {
    WebRequestDiagnostics::RequestTimer timer(req, "PortalServer/portal");
    servePortalEntry(req);
  });

  for (size_t i = 0; i < PortalSpiffsLayout::kFlatAliasCount; ++i) {
    const char *url    = PortalSpiffsLayout::kFlatAliases[i].urlPath;
    const char *spiffs = PortalSpiffsLayout::kFlatAliases[i].spiffsPath;
    server.on(url, HTTP_GET, [this, url, spiffs](AsyncWebServerRequest *req) {
      WebRequestDiagnostics::RequestTimer timer(req, "PortalServer/file");
      servePortalFile(req, spiffs, url);
    });
  }

  Serial.println("[web] PortalServer routes registered (production plane / Ethernet only):");
  Serial.println("[web]   GET /portal -> /portal/login.html (dev/recovery fallback)");
  Serial.println("[web]   GET /renzfi-app.js, /renzfi-style.css, ... -> gated SPIFFS");
  Serial.println("[web]   Production customer portal is served by MikroTik Hotspot");
}

const char *PortalServer::providerName() const {
  return "PortalServer";
}

int PortalServer::notFoundPriority() const {
  return 35;
}

bool PortalServer::handleNotFound(AsyncWebServerRequest *req) {
  if (!req) return false;

  String path = req->url();
  const int query = path.indexOf('?');
  if (query >= 0) path = path.substring(0, query);

  // Claim only ESP32 portal recovery URLs. Do NOT run Management gating on
  // unrelated notFound traffic (Admin SPA /assets/* must reach StaticFileServer).
  if (!isPortalUrl(path)) return false;

  if (!HttpPlaneGate::ensureManagementClient(req)) return true;

  if (path == "/portal" || path == "/portal/") {
    servePortalEntry(req);
    return true;
  }

  String spiffsPath;
  if (mapToSpiffsPath(path, spiffsPath)) {
    servePortalFile(req, spiffsPath, path);
    return true;
  }

  WebResponse::serveNotFound(req, true);
  return true;
}
