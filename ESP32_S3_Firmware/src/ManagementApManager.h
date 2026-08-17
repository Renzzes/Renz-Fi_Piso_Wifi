#pragma once



#include <Arduino.h>

#include <ArduinoJson.h>



#include "CaptiveApDnsServer.h"



class InstallationStateManager;



// Dedicated Management Access Point service — separate from PortalServer and

// customer-facing captive portal logic.

class ManagementApManager {

 public:

  void begin(InstallationStateManager *installation, const String &macAddress);

  void loop();



  bool start();

  bool stop();



  bool isEnabled() const { return _enabled; }

  bool isRunning() const { return _running; }



  String ssid() const { return _ssid; }

  String ip() const;

  const char *mode() const;

  uint8_t connectedClients() const;

  uint32_t uptimeSeconds() const;



  /** Refresh Wi-Fi-derived fields from the driver (loop / lifecycle only). */

  void refreshRuntimeState();



  void fillStatus(JsonObject out) const;



 private:

  InstallationStateManager *_installation = nullptr;

  String _macAddress;

  String _ssid;

  String _cachedIp;

  uint8_t _cachedClients = 0;

  bool   _enabled  = false;

  bool   _running  = false;

  uint32_t _startedAtMs = 0;



  // AP-local captive DNS — every hostname resolves to 192.168.4.1; never

  // forwarded to Ethernet/MikroTik DNS.

  CaptiveApDnsServer _dns;

  bool               _dnsRunning = false;



  String buildSsid() const;

  bool configureApDhcpDns() const;

};

