#include "DownloadServer.h"

#include "HttpPlaneGate.h"
#include "WebRequestDiagnostics.h"
#include "WebResponse.h"
#include "WebServerManager.h"

void DownloadServer::registerRoutes(WebServerManager &web) {
  (void)web;
  Serial.println("[web] DownloadServer foundation registered (reserved /downloads/*)");
}

const char *DownloadServer::providerName() const {
  return "DownloadServer";
}

int DownloadServer::notFoundPriority() const {
  return 30;
}

bool DownloadServer::handleNotFound(AsyncWebServerRequest *req) {
  if (!req) return false;
  if (!req->url().startsWith("/downloads/")) return false;
  if (!HttpPlaneGate::ensureManagementClient(req)) return true;
  WebResponse::serveNotFound(req, true);
  return true;
}
