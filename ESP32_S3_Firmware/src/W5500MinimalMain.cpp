// Minimal W5500-only firmware — no SD, portal, coin, or MikroTik.
// Built when env:w5500_minimal is selected in platformio.ini.

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "EthernetManager.h"
#include "W5500Config.h"

static EthernetManager g_eth;
static AsyncWebServer  g_server(80);
static bool            g_serverStarted = false;

void setup() {
  Serial.begin(115200);
  delay(3000);  // allow serial monitor to attach before ETH boot logs

  Serial.println();
  Serial.println("========================================");
  Serial.println("RENZ-FI W5500 MINIMAL FIRMWARE");
  Serial.println("========================================");

  if (!g_eth.begin()) {
    Serial.println("[boot] HALTED — ETH.begin() failed");
    return;
  }

  Serial.println("----------------------------------------");
  Serial.printf("[boot] MAC  : %s\n", g_eth.macAddress().c_str());
  Serial.printf("[boot] Link : %s\n", g_eth.linkUp() ? "UP" : "DOWN");
  Serial.printf("[boot] IP   : %s\n", g_eth.ip().c_str());
  Serial.println("----------------------------------------");

  g_server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    String body = "Renz-Fi W5500 minimal OK\n";
    body += "MAC:  " + g_eth.macAddress() + "\n";
    body += "Link: " + String(g_eth.linkUp() ? "UP" : "DOWN") + "\n";
    body += "IP:   " + g_eth.ip() + "\n";
    req->send(200, "text/plain", body);
  });

  g_server.begin();
  g_serverStarted = true;
  Serial.println("[boot] Web server listening on http://" + g_eth.ip());
  Serial.println("========================================");
}

void loop() {
  g_eth.loop();

  if (g_serverStarted && g_eth.linkUp() && !g_eth.hasIp()) {
    static uint32_t lastWarn = 0;
    if (millis() - lastWarn >= 10000) {
      lastWarn = millis();
      Serial.println("[ETH] Link up but no IP yet");
    }
  }
}
