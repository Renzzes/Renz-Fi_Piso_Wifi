#include "CaptivePortalDetectionServer.h"

#include "ManagementApConfig.h"
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
};

}  // namespace

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

  WebResponse::serveRedirect(req, ManagementApConfig::SETUP_URL);
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
  Serial.println("[web]   GET /connecttest.txt, /ncsi.txt, /fwlink -> 302 http://192.168.4.1 (AP requests only)");
}

const char *CaptivePortalDetectionServer::providerName() const {
  return "CaptivePortalDetection";
}
