#pragma once

#include <ESPAsyncWebServer.h>

#include "IWebRouteProvider.h"

class WebServerManager;

// Answers the small set of well-known "is there a captive portal?" probe
// requests that Android, iOS/macOS, Windows, and ChromeOS fire automatically
// after joining a Wi-Fi network. Redirecting them (instead of letting them
// succeed normally) is what makes phones/laptops pop up their native
// "Sign in to network" prompt when joining the "Renz-Fi Setup" AP.
//
// AP-scoped only: these routes intentionally do nothing (fall through to the
// normal 404/route chain) when the request did not arrive via the
// Management AP's own IP, so they never interfere with LAN-side clients,
// the admin app, or the MikroTik-hosted customer captive portal.
class CaptivePortalDetectionServer : public IWebRouteProvider {
 public:
  void registerRoutes(WebServerManager &web) override;
  const char *providerName() const override;

 private:
  void handleProbe(AsyncWebServerRequest *req);
  bool isManagementApRequest(AsyncWebServerRequest *req) const;
};
