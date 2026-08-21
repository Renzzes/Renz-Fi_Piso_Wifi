// ─────────────────────────────────────────────────────────────────────────────
//  EthernetManager.cpp  —  W5500 backend, DHCP-first with optional static IP
// ─────────────────────────────────────────────────────────────────────────────

#include "EthernetManager.h"

#include "DmaMemoryMonitor.h"
#include "NetworkDiagnostics.h"
#include "SetupDnsPolicy.h"
#include "W5500Config.h"

namespace {

bool g_ethEventsRegistered = false;

// One-shot ETH event logs (reset on link/IP loss).
bool g_loggedEthStart        = false;
bool g_loggedEthConnected    = false;
bool g_loggedEthGotIp        = false;
bool g_loggedEthLostIp       = false;
bool g_loggedEthDisconnected = false;
bool g_loggedEthStop         = false;

// Regression guard state: set true on ARDUINO_EVENT_ETH_STOP, cleared on
// ARDUINO_EVENT_ETH_START. Lets isDriverActive() report the *current* state
// of the esp_eth driver, independent of whatever the initial ETH.begin()
// call at boot returned.
bool g_driverStopped = false;

// Last MAC address observed while ETH.macAddress() returned a real,
// non-zero value. Used by EthernetManager::macAddress() as a fallback so a
// later transient/failed SPI read (e.g. driver stopped) never surfaces as
// 00:00:00:00:00:00 in the device identity.
String g_lastKnownGoodMac;

bool isMacValid(const String &mac) {
  if (mac.length() == 0) return false;
  return mac != "00:00:00:00:00:00";
}

// Diagnostics-only mirror of subsystem readiness.
bool   g_webServerStarted = false;
bool   g_mgmtApRunning    = false;
String g_mgmtApIp;

void resetEthEventLogFlags() {
  g_loggedEthStart        = false;
  g_loggedEthConnected    = false;
  g_loggedEthGotIp        = false;
  g_loggedEthLostIp       = false;
  g_loggedEthDisconnected = false;
  g_loggedEthStop         = false;
}

void logInterfaceSummary() {
  Serial.printf("[ETH] API/web server: %s\n",
                g_webServerStarted ? "ready" : "not started yet");
  Serial.printf("[ETH] Management AP : %s%s%s\n",
                g_mgmtApRunning ? "running (" : "not running",
                g_mgmtApRunning ? g_mgmtApIp.c_str() : "",
                g_mgmtApRunning ? ")" : "");
}

bool isValidStaticConfig(const NetworkSettings &settings) {
  IPAddress probe;
  if (!probe.fromString(settings.staticIp)) return false;
  if (!probe.fromString(settings.staticGateway)) return false;
  if (!probe.fromString(settings.staticSubnetMask)) return false;
  if (!probe.fromString(settings.staticDnsPrimary)) return false;
  return true;
}

void onEthArduinoEvent(arduino_event_id_t event, arduino_event_info_t info) {
  (void)info;
  NetworkDiagnostics::onEthEvent(event);
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      g_driverStopped = false;
      if (!g_loggedEthStart) {
        g_loggedEthStart = true;
        Serial.println("[ETH] event: START");
      }
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      if (!g_loggedEthConnected) {
        g_loggedEthConnected = true;
        Serial.println("[ETH] event: CONNECTED");
      }
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      if (!g_loggedEthGotIp) {
        g_loggedEthGotIp = true;
        Serial.printf("[ETH] event: GOT_IP  ip=%s  gw=%s  mask=%s  dns=%s\n",
                      ETH.localIP().toString().c_str(),
                      ETH.gatewayIP().toString().c_str(),
                      ETH.subnetMask().toString().c_str(),
                      ETH.dnsIP().toString().c_str());
        logInterfaceSummary();
      }
      break;
    case ARDUINO_EVENT_ETH_LOST_IP:
      if (!g_loggedEthLostIp) {
        g_loggedEthLostIp = true;
        Serial.println("[ETH] event: LOST_IP");
      }
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      if (!g_loggedEthDisconnected) {
        g_loggedEthDisconnected = true;
        Serial.println("[ETH] event: DISCONNECTED");
      }
      resetEthEventLogFlags();
      break;
    case ARDUINO_EVENT_ETH_STOP:
      g_driverStopped = true;
      if (!g_loggedEthStop) {
        g_loggedEthStop = true;
        Serial.println("[ETH] event: STOP");
        Serial.printf(
            "[ETH] WARNING: esp_eth driver stopped after boot — check for "
            "code that re-touches W5500 SPI3_HOST pins (CS%d/RST%d/SCK%d/"
            "MISO%d/MOSI%d) via pinMode()/digitalWrite()/SPI.end() after "
            "ETH.begin(). See SdSpi.cpp comment for prior root cause.\n",
            W5500Config::PIN_CS, W5500Config::PIN_RST, W5500Config::PIN_SCK,
            W5500Config::PIN_MISO, W5500Config::PIN_MOSI);
      }
      resetEthEventLogFlags();
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

void EthernetManager::noteWebServerState(bool started) {
  g_webServerStarted = started;
}

void EthernetManager::noteManagementApState(bool running, const String &ip) {
  g_mgmtApRunning = running;
  g_mgmtApIp      = ip;
}

String EthernetManager::macAddress() const {
  const String live = ETH.macAddress();
  if (isMacValid(live)) {
    g_lastKnownGoodMac = live;
    return live;
  }
  if (isMacValid(g_lastKnownGoodMac)) return g_lastKnownGoodMac;
  return live;
}

bool EthernetManager::isDriverActive() { return !g_driverStopped; }

void EthernetManager::logDiagnosticStage(const char *stage) {
  Serial.printf(
      "[ETH-DIAG] stage=%-20s driver=%-7s link=%-4s ip=%-15s mac=%s\n",
      stage, isDriverActive() ? "STARTED" : "STOPPED",
      (ETH.linkUp() || ETH.connected()) ? "UP" : "DOWN",
      ETH.localIP().toString().c_str(), ETH.macAddress().c_str());
}

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
  } else {
    Serial.println("[ETH] Link Down");
    _hasIp = false;
    resetEthEventLogFlags();
  }
}

void EthernetManager::applyAddressMode(const NetworkSettings &settings) {
  const bool wantStatic = settings.provisioned &&
                          settings.addressMode == EthernetAddressMode::Static &&
                          isValidStaticConfig(settings);

  if (!wantStatic) {
    _staticModeApplied = false;
    _staticDns = "";
    Serial.println("[ETH] Address mode: DHCP (default) — requesting lease in background");
    return;
  }

  Serial.printf("[ETH] Address mode: STATIC ip=%s gw=%s mask=%s dns=%s\n",
                 settings.staticIp.c_str(), settings.staticGateway.c_str(),
                 settings.staticSubnetMask.c_str(), settings.staticDnsPrimary.c_str());

  IPAddress ip, gw, mask, dns1;
  ip.fromString(settings.staticIp);
  gw.fromString(settings.staticGateway);
  mask.fromString(settings.staticSubnetMask);
  dns1.fromString(settings.staticDnsPrimary);
  const bool configOk = ETH.config(ip, gw, mask, dns1);
  Serial.printf("[ETH] ETH.config() = %s\n", configOk ? "true" : "false");

  if (configOk) {
    _staticModeApplied = true;
    _staticDns = settings.staticDnsPrimary;
  } else {
    Serial.println("[ETH] Static config failed — falling back to DHCP");
    _staticModeApplied = false;
    _staticDns = "";
  }
}

bool EthernetManager::begin(const NetworkSettings &settings) {
  DmaMemoryMonitor::ScopedProbe dmaProbe("w5500-begin");
  Serial.println("[ETH] Initializing W5500...");

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
  Serial.printf("[ETH] MAC: %s\n", macAddress().c_str());

  if (!ok) {
    _driverReady = false;
    _linkUp      = false;
    _hasIp       = false;
    g_driverStopped = true;
    Serial.println("[ETH] begin() returning false — driver did not start (non-fatal, Management AP will remain available)");
    return false;
  }

  _driverReady = true;
  g_driverStopped = false;

  uint32_t start = millis();
  while (!ETH.connected() && !ETH.linkUp()) {
    if (millis() - start > W5500Config::LINK_TIMEOUT_MS) {
      Serial.println("[ETH] Link timeout at boot — will retry in loop() (non-blocking)");
      break;
    }
    delay(100);
  }
  refreshLinkState();

  applyAddressMode(settings);

  _hasIp = ETH.hasIP();
  if (_hasIp) {
    Serial.printf("[ETH] IP already available at boot: %s\n", ip().c_str());
  }

  Serial.printf("[ETH] Driver : %s\n", _driverReady ? "UP" : "DOWN");
  Serial.printf("[ETH] Link   : %s\n", _linkUp ? "UP" : "DOWN");
  Serial.printf("[ETH] Mode   : %s\n", addressModeLabel());
  Serial.println("[ETH] begin() returning true (DHCP lease, if any, continues in background)");
  return true;
}

void EthernetManager::loop() {
  if (!_driverReady) return;

  if (millis() - _lastPoll >= W5500Config::LINK_POLL_INTERVAL_MS) {
    _lastPoll = millis();
    refreshLinkState();

    const bool hadIp = _hasIp;
    _hasIp = ETH.hasIP();
    if (_hasIp && !hadIp) {
      Serial.printf("[ETH] IP acquired (%s): %s  gw=%s  mask=%s  dns=%s\n",
                    addressModeLabel(), ip().c_str(), gateway().c_str(),
                    subnet().c_str(), dns().c_str());
      SetupDnsPolicy::onEthernetIpChanged();
    } else if (!_hasIp && hadIp) {
      Serial.println("[ETH] IP lost");
    }
  }

  if (!_linkUp && millis() - _lastWaitLog >= 5000) {
    _lastWaitLog = millis();
    Serial.println("[ETH] Waiting for Ethernet cable...");
  }
}
