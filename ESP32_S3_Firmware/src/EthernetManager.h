#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  EthernetManager.h  —  Minimal W5500 backend (arduino-esp32 3.x)
//
//  Restored pre-SD / pre-probe sequence:
//    1. hardwareReset() — pulse W5500 RST
//    2. Network.onEvent  — register before ETH.begin()
//    3. ETH.begin()      — SPI3_HOST; poll when PIN_INT < 0, IRQ when >= 0
//    4. ETH.config()     — static VLAN40 IP (Static mode only)
// ─────────────────────────────────────────────────────────────────────────────

#include <Arduino.h>
#include <ETH.h>

#include "Config.h"
#include "Models.h"
#include "W5500Config.h"

class EthernetManager {
 public:
  // `settings` selects DHCP (default) vs validated static configuration.
  // Driver init (hardware reset, ETH.begin(), pin map) is unconditional and
  // always attempted regardless of `settings` — only the post-begin() IP
  // assignment step depends on it. Returns false only when the W5500 driver
  // itself fails to start (ETH.begin() == false); this must never be
  // treated as fatal by the caller.
  bool begin(const NetworkSettings &settings = NetworkSettings());
  void loop();

  bool driverReady()    const { return _driverReady; }
  bool linkUp()         const { return _linkUp; }
  bool hasIp()          const { return _hasIp; }
  bool isServiceReady() const { return _driverReady && _hasIp; }
  bool isStaticMode()   const { return _staticModeApplied; }
  const char *addressModeLabel() const {
    return ethernetAddressModeLabel(_staticModeApplied
                                        ? EthernetAddressMode::Static
                                        : EthernetAddressMode::Dhcp);
  }

  String ip()           const { return ETH.localIP().toString(); }
  String gateway()      const { return ETH.gatewayIP().toString(); }
  String subnet()       const { return ETH.subnetMask().toString(); }
  String dns()          const {
    return _staticModeApplied ? _staticDns : ETH.dnsIP().toString();
  }
  // Prefers a live read of ETH.macAddress(); if the driver has stopped and
  // the live read comes back invalid (all-zero), falls back to the last
  // known-good MAC captured while the driver was actually up. This never
  // hides the underlying failure — driverReady()/isDriverActive() and the
  // [ETH-DIAG] stage logs still report the driver as down — it only stops a
  // transient/late SPI read failure from corrupting the device identity
  // (deviceId/serial number) shown in the boot summary and API responses.
  String macAddress() const;
  String mdnsHostname() const {
    return String(RenzFiConfig::MDNS_NAME) + ".local";
  }

  static void noteWebServerState(bool started);
  static void noteManagementApState(bool running, const String &ip);

  // True unless the underlying esp_eth driver has posted an ETH_STOP event
  // since the last successful ETH_START/ETH.begin(). Distinct from
  // driverReady(), which only records whether the initial ETH.begin() call
  // at boot succeeded and is never cleared afterward.
  static bool isDriverActive();

  // Logs a single-line snapshot of Ethernet driver/link/IP/MAC state,
  // tagged with `stage`. Intended to be called at fixed boot checkpoints
  // (before SD init, after SD SPI begin, after SD.begin, after fallback
  // sync, before the appliance summary) so a regression that knocks the
  // W5500 offline is visible immediately at the point it happens, not just
  // in the final boot summary.
  static void logDiagnosticStage(const char *stage);

 private:
  bool     _driverReady        = false;
  bool     _linkUp             = false;
  bool     _hasIp              = false;
  bool     _staticModeApplied  = false;
  String   _staticDns;
  uint32_t _lastPoll    = 0;
  uint32_t _lastWaitLog = 0;

  static void hardwareReset();
  void refreshLinkState();
  void applyAddressMode(const NetworkSettings &settings);
};
