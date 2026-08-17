#include "AdminServer.h"

#include <SPIFFS.h>

#include "HttpPlaneGate.h"
#include "StaticFileServer.h"
#include "WebRequestDiagnostics.h"
#include "WebResponse.h"
#include "WebServerManager.h"

namespace {

bool serveAdminSpaFallback(AsyncWebServerRequest *req, fs::FS &fs) {
  if (!req->url().startsWith("/admin/")) return false;

  if (fs.exists("/index.html")) {
    req->send(fs, "/index.html", "text/html");
  } else {
    req->send(404, "text/plain", "Admin not found");
  }
  return true;
}

}  // namespace

void AdminServer::begin(StaticFileServer *staticFiles) {
  _staticFiles = staticFiles;
}

void AdminServer::registerRoutes(WebServerManager &web) {
  AsyncWebServer &server = web.routeServer();
  if (!_staticFiles) return;

  // GET /, /login, /dashboard, /admin are registered once by
  // WebServerManager::registerAdminEntryRoute (plane-aware) — not duplicated.

  server.on("/manifest.webmanifest", HTTP_GET, [this](AsyncWebServerRequest *req) {
    WebRequestDiagnostics::RequestTimer timer(req, "AdminServer/manifest");
    if (!HttpPlaneGate::ensureAdminSpaClient(req)) return;
    _staticFiles->serveStaticOrIndex(req);
  });
  server.on("/sw.js", HTTP_GET, [this](AsyncWebServerRequest *req) {
    WebRequestDiagnostics::RequestTimer timer(req, "AdminServer/sw");
    if (!HttpPlaneGate::ensureAdminSpaClient(req)) return;
    _staticFiles->serveStaticOrIndex(req);
  });
  server.on("/favicon.svg", HTTP_GET, [this](AsyncWebServerRequest *req) {
    WebRequestDiagnostics::RequestTimer timer(req, "AdminServer/favicon.svg");
    if (!HttpPlaneGate::ensureAdminSpaClient(req)) return;
    _staticFiles->serveStaticOrIndex(req);
  });
  server.on("/favicon.ico", HTTP_GET, [this](AsyncWebServerRequest *req) {
    WebRequestDiagnostics::RequestTimer timer(req, "AdminServer/favicon.ico");
    if (HttpPlaneGate::isSetupPlane(req)) {
      req->send(204);
      return;
    }
    if (!HttpPlaneGate::ensureAdminSpaClient(req)) return;
    _staticFiles->serveStaticOrIndex(req);
  });

  Serial.println("[web] AdminServer routes registered (Admin SPA surface):");
  Serial.println("[web]   GET /manifest.webmanifest, /sw.js, /favicon.*");
  Serial.println("[web]   GET /, /login, /dashboard, /admin -> WebServerManager (plane-aware)");
}

const char *AdminServer::providerName() const {
  return "AdminServer";
}

int AdminServer::notFoundPriority() const {
  return 10;
}

bool AdminServer::handleNotFound(AsyncWebServerRequest *req) {
  if (!req) return false;
  // Only claim Admin deep-link SPA fallback. Never intercept /assets/* —
  // StaticFileServer owns those with ensureAdminSpaClient.
  if (!req->url().startsWith("/admin/")) return false;
  if (!HttpPlaneGate::ensureAdminSpaClient(req)) return true;
  return serveAdminSpaFallback(req, SPIFFS);
}
