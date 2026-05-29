#include "WiFiBootstrap.h"

#include <ESPmDNS.h>
#include <WiFi.h>

#include "Config.h"

bool WiFiBootstrap::begin() {
  Serial.println("[boot] WiFi AP startup starting");
  WiFi.mode(WIFI_AP);
  Serial.printf("[boot] WiFi mode: %d\n", WiFi.getMode());
  WiFi.softAPConfig(RenzFiConfig::AP_IP, RenzFiConfig::AP_GATEWAY, RenzFiConfig::AP_SUBNET);
  bool ok = WiFi.softAP(RenzFiConfig::AP_SSID, RenzFiConfig::AP_PASSWORD);
  if (ok) {
    Serial.printf("[boot] AP started: ssid=%s ip=%s\n", RenzFiConfig::AP_SSID, WiFi.softAPIP().toString().c_str());
    MDNS.begin(RenzFiConfig::MDNS_NAME);
    MDNS.addService("http", "tcp", RenzFiConfig::HTTP_PORT);
    Serial.printf("[boot] mDNS started: %s.local\n", RenzFiConfig::MDNS_NAME);
  } else {
    Serial.println("[ERROR] AP startup failed");
  }
  return ok;
}
