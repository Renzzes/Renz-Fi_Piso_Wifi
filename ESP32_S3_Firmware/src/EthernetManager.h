#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  EthernetManager.h  —  Minimal W5500 backend (arduino-esp32 3.x)
//
//  Restored pre-SD / pre-probe sequence:
//    1. hardwareReset() — pulse W5500 RST
//    2. Network.onEvent  — register before ETH.begin()
//    3. ETH.begin()      — SPI3_HOST, poll mode when PIN_INT = -1
//    4. ETH.config()     — static VLAN40 IP
// ─────────────────────────────────────────────────────────────────────────────

#include <Arduino.h>
#include <ETH.h>

#include "Config.h"

class EthernetManager {
 public:
  bool begin();
  void loop();

  bool driverReady()    const { return _driverReady; }
  bool linkUp()         const { return _linkUp; }
  bool hasIp()          const { return _hasIp; }
  bool isServiceReady() const { return _driverReady && _hasIp; }

  String ip()           const { return ETH.localIP().toString(); }
  String gateway()      const { return ETH.gatewayIP().toString(); }
  String subnet()       const { return ETH.subnetMask().toString(); }
  String macAddress()   const { return ETH.macAddress(); }
  String mdnsHostname() const {
    return String(RenzFiConfig::MDNS_NAME) + ".local";
  }

 private:
  bool     _driverReady = false;
  bool     _linkUp      = false;
  bool     _hasIp       = false;
  uint32_t _lastPoll    = 0;
  uint32_t _lastWaitLog = 0;

  static void hardwareReset();
  void refreshLinkState();
};
