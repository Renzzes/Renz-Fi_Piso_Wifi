// ─────────────────────────────────────────────────────────────────────────────
//  EthernetManager.cpp  —  Minimal W5500 backend (restored pre-SD stack)
// ─────────────────────────────────────────────────────────────────────────────

#include "EthernetManager.h"

#include "W5500Config.h"

namespace {

bool g_ethEventsRegistered = false;

void onEthArduinoEvent(arduino_event_id_t event, arduino_event_info_t info) {
  (void)info;
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("[ETH] event: START");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("[ETH] event: CONNECTED");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.println("[ETH] event: GOT_IP");
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("[ETH] event: DISCONNECTED");
      break;
    default:
      break;
  }
}

void ensureEthEventHandler() {
  if (g_ethEventsRegistered) return;
  Network.onEvent(onEthArduinoEvent);
  g_ethEventsRegistered = true;
}

}  // namespace

void EthernetManager::hardwareReset() {
  pinMode(W5500Config::PIN_RST, OUTPUT);
  digitalWrite(W5500Config::PIN_RST, LOW);
  delay(50);
  digitalWrite(W5500Config::PIN_RST, HIGH);
  delay(200);
  Serial.println("[ETH] hardware reset complete");
}

void EthernetManager::refreshLinkState() {
  const bool nowUp = ETH.connected() || ETH.linkUp();
  if (nowUp == _linkUp) return;

  _linkUp = nowUp;
  if (_linkUp) {
    Serial.println("[ETH] Link Up");
    Serial.printf("[ETH] IP: %s\n", ip().c_str());
  } else {
    Serial.println("[ETH] Link Down");
  }
}

bool EthernetManager::begin() {
  Serial.println("[ETH] Initializing W5500 (minimal stack)...");

  Serial.println("[ETH] Pin map:");
  Serial.printf("  MOSI=%d MISO=%d SCK=%d CS=%d RST=%d INT=%d\n",
                W5500Config::PIN_MOSI, W5500Config::PIN_MISO,
                W5500Config::PIN_SCK, W5500Config::PIN_CS,
                W5500Config::PIN_RST, W5500Config::PIN_INT);

  hardwareReset();
  ensureEthEventHandler();

  Serial.println("[ETH] Calling ETH.begin()...");
  Serial.printf("[ETH]   ETH_PHY_W5500 phy=1 cs=%d irq=%d rst=%d host=SPI3_HOST "
                "sck=%d miso=%d mosi=%d freq=%u MHz\n",
                W5500Config::PIN_CS, W5500Config::PIN_INT, W5500Config::PIN_RST,
                W5500Config::PIN_SCK, W5500Config::PIN_MISO,
                W5500Config::PIN_MOSI, W5500Config::SPI_FREQ_MHZ);

  const bool ok = ETH.begin(ETH_PHY_W5500,
                            1,
                            W5500Config::PIN_CS,
                            W5500Config::PIN_INT,
                            W5500Config::PIN_RST,
                            SPI3_HOST,
                            W5500Config::PIN_SCK,
                            W5500Config::PIN_MISO,
                            W5500Config::PIN_MOSI,
                            W5500Config::SPI_FREQ_MHZ);

  Serial.printf("[ETH] ETH.begin() = %s\n", ok ? "true" : "false");
  Serial.printf("[ETH] MAC: %s\n", ETH.macAddress().c_str());

  if (!ok) {
    _driverReady = false;
    _linkUp      = false;
    _hasIp       = false;
    Serial.println("[ETH] begin() returning false — driver did not start");
    return false;
  }

  _driverReady = true;

  uint32_t start = millis();
  while (!ETH.connected() && !ETH.linkUp()) {
    if (millis() - start > W5500Config::LINK_TIMEOUT_MS) {
      Serial.println("[ETH] Link timeout at boot — will retry in loop()");
      break;
    }
    delay(100);
  }
  refreshLinkState();

  Serial.println("[ETH] Applying static IP...");
  const bool configOk = ETH.config(W5500Config::IP,
                                   W5500Config::GATEWAY,
                                   W5500Config::SUBNET,
                                   W5500Config::DNS);
  Serial.printf("[ETH] ETH.config() = %s\n", configOk ? "true" : "false");
  _hasIp = configOk && ETH.hasIP();

  Serial.printf("[ETH] Driver : %s\n", _driverReady ? "UP" : "DOWN");
  Serial.printf("[ETH] Link   : %s\n", _linkUp ? "UP" : "DOWN");
  Serial.printf("[ETH] IP     : %s\n", ip().c_str());
  Serial.println("[ETH] begin() returning true");
  return true;
}

void EthernetManager::loop() {
  if (!_driverReady) return;

  if (millis() - _lastPoll >= W5500Config::LINK_POLL_INTERVAL_MS) {
    _lastPoll = millis();
    refreshLinkState();
    _hasIp = ETH.hasIP();
  }

  if (!_linkUp && millis() - _lastWaitLog >= 5000) {
    _lastWaitLog = millis();
    Serial.println("[ETH] Waiting for Ethernet cable...");
  }
}
