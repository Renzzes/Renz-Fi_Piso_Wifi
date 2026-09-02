#include "CaptivePortalDetectionServer.h"

#include "DmaMemoryMonitor.h"
#include "ManagementApConfig.h"
#include "RouterProvisioningWorker.h"
#include "WebRequestDiagnostics.h"
#include "WebResponse.h"
#include "WebServerManager.h"

namespace {

// Every URL a mainstream OS/browser fires to decide "is this network behind
// a captive portal?" — kept to the well-documented, stable set (Android,
// ChromeOS, iOS/macOS, Windows). Redirecting any of these to the portal
// root is what triggers the OS's built-in sign-in prompt/browser.
const char *const kDetectionPaths[] = {
    "/generate_204",              // Android / ChromeOS
    "/gen_204",                   // Android (alternate)
    "/hotspot-detect.html",       // iOS / macOS
    "/library/test/success.html", // iOS / macOS (older)
    "/connecttest.txt",           // Windows NCSI
    "/ncsi.txt",                  // Windows NCSI
    "/fwlink",                    // Windows (legacy captive portal probe)
    "/redirect",                  // Windows captive "open browser" path
};

}  // namespace

void CaptivePortalDetectionServer::begin(RouterProvisioningWorker *routerWorker) {
  _routerWorker = routerWorker;
}

bool CaptivePortalDetectionServer::isManagementApRequest(
    AsyncWebServerRequest *req) const {
  if (!req || !req->client()) return false;
  return req->client()->localIP() == ManagementApConfig::IP;
}

void CaptivePortalDetectionServer::handleProbe(AsyncWebServerRequest *req) {
  if (!req) return;
  WebRequestDiagnostics::RequestTimer timer(req, "CaptivePortalDetection");

  if (!isManagementApRequest(req)) {
    // Not the Management AP — these probe URLs are otherwise unused by this
    // firmware, so behave exactly as if no route were registered at all.
    req->send(404, "text/plain", "Not Found");
    return;
  }

  // Cheap no-body reply while RouterOS setup work or DMA is tight. Avoids
  // SoftAP HTML redirect storms that compete with W5500 SPI DMA (Guru).
  // Installer already on /admin/setup is unaffected; first join still gets
  // 302 when the worker is idle and DMA has headroom.
  if ((_routerWorker && _routerWorker->isBusy()) ||
      !DmaMemoryMonitor::hasEthTransmitHeadroom()) {
    req->send(204);
    return;
  }

  WebResponse::serveRedirect(req, ManagementApConfig::SETUP_PATH);
}

void CaptivePortalDetectionServer::registerRoutes(WebServerManager &web) {
  AsyncWebServer &server = web.routeServer();

  for (const char *path : kDetectionPaths) {
    server.on(path, HTTP_GET, [this](AsyncWebServerRequest *req) {
      handleProbe(req);
    });
  }

  Serial.println("[web] CaptivePortalDetectionServer routes registered (Management AP only):");
  Serial.println("[web]   GET /generate_204, /gen_204, /hotspot-detect.html, /library/test/success.html,");
  Serial.println("[web]   GET /connecttest.txt, /ncsi.txt, /fwlink, /redirect -> 302 /admin/setup (or 204 when busy/DMA-low)");
}

const char *CaptivePortalDetectionServer::providerName() const {
  return "CaptivePortalDetection";
}
