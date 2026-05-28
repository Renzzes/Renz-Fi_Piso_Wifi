#include "CaptivePortal.h"

#include <WiFi.h>

#include "config.h"

void CaptivePortal::begin() {
  _dns.start(RenzFiConfig::DNS_PORT, "*", RenzFiConfig::AP_IP);
}

void CaptivePortal::loop() {
  _dns.processNextRequest();
}

bool CaptivePortal::isCaptiveRequest(AsyncWebServerRequest *request) const {
  String host = request->host();
  if (host.isEmpty()) return false;
  if (host == RenzFiConfig::AP_IP.toString()) return false;
  if (host == String(RenzFiConfig::MDNS_NAME) + ".local") return false;
  return !request->url().startsWith("/api/");
}

void CaptivePortal::redirectToPortal(AsyncWebServerRequest *request) const {
  AsyncWebServerResponse *response = request->beginResponse(302);
  response->addHeader("Location", String("http://") + RenzFiConfig::AP_IP.toString() + "/");
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}
