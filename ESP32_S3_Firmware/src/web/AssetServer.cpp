#include "AssetServer.h"

#include <SD.h>
#include <SPIFFS.h>

#include "HttpPlaneGate.h"
#include "WebRequestDiagnostics.h"
#include "WebResponse.h"
#include "WebServerManager.h"

void AssetServer::begin(AssetManager *assets, StorageManager *storage,
                        PortalConfigManager *portalConfig) {
  _assets = assets;
  _storage = storage;
  _portalConfig = portalConfig;
}

bool AssetServer::serveResolvedAsset(AsyncWebServerRequest *req,
                                     const ResolvedAsset &asset) const {
  if (!req || !asset.found || asset.path.length() == 0) return false;

  const char *mime = asset.mimeType.length() > 0
                         ? asset.mimeType.c_str()
                         : "application/octet-stream";

  if (asset.storageLocation == AssetStorageLocation::Sd && _storage &&
      _storage->healthy() && _storage->sdIoAllowed() &&
      SD.exists(asset.path.c_str())) {
    req->send(SD, asset.path.c_str(), mime);
    return true;
  }

  if (SPIFFS.exists(asset.path.c_str())) {
    req->send(SPIFFS, asset.path.c_str(), mime);
    return true;
  }

  return false;
}

void AssetServer::registerRoutes(WebServerManager &web) {
  AsyncWebServer &server = web.routeServer();
  server.on("/api/portal/assets/banner", HTTP_GET,
            [this](AsyncWebServerRequest *req) {
              WebRequestDiagnostics::RequestTimer timer(req, "AssetServer/banner");
              if (!HttpPlaneGate::ensureProductionPlane(req)) return;
              if (!_assets) {
                WebResponse::serveErrorJson(req, 500, "Asset manager unavailable",
                                          "NOT_READY");
                return;
              }
              const ResolvedAsset asset = _assets->resolveBanner();
              if (serveResolvedAsset(req, asset)) return;
              Serial.printf(
                  "[portal] GET assets/banner NOT_FOUND hasCustom=%s tier=%s\n",
                  (_portalConfig && _portalConfig->hasCustomBanner()) ? "yes"
                                                                      : "no",
                  assetResolveTierLabel(asset.tier));
              WebResponse::serveErrorJson(req, 404, "Banner not found",
                                          "NOT_FOUND");
            });

  server.on("/api/portal/assets/music", HTTP_GET,
            [this](AsyncWebServerRequest *req) {
              WebRequestDiagnostics::RequestTimer timer(req, "AssetServer/music");
              if (!HttpPlaneGate::ensureProductionPlane(req)) return;
              if (!_assets) {
                WebResponse::serveErrorJson(req, 500, "Asset manager unavailable",
                                          "NOT_READY");
                return;
              }
              const ResolvedAsset asset = _assets->resolveMusic();
              if (serveResolvedAsset(req, asset)) return;
              Serial.printf(
                  "[portal] GET assets/music NOT_FOUND hasCustom=%s tier=%s\n",
                  (_portalConfig && _portalConfig->hasCustomMusic()) ? "yes"
                                                                     : "no",
                  assetResolveTierLabel(asset.tier));
              WebResponse::serveErrorJson(req, 404, "Music not found",
                                          "NOT_FOUND");
            });

  Serial.println("[web] AssetServer routes registered:");
  Serial.println("[web]   GET /api/portal/assets/banner");
  Serial.println("[web]   GET /api/portal/assets/music");
}

const char *AssetServer::providerName() const {
  return "AssetServer";
}
