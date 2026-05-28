#include "WiFiBootstrap.h"

#include <ESPmDNS.h>
#include <WiFi.h>

#include "config.h"

bool WiFiBootstrap::begin() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(RenzFiConfig::AP_IP, RenzFiConfig::AP_GATEWAY, RenzFiConfig::AP_SUBNET);
  bool ok = WiFi.softAP(RenzFiConfig::AP_SSID, RenzFiConfig::AP_PASSWORD);
  if (ok) {
    MDNS.begin(RenzFiConfig::MDNS_NAME);
    MDNS.addService("http", "tcp", RenzFiConfig::HTTP_PORT);
  }
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  return ok;
}
