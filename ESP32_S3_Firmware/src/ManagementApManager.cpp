#include "ManagementApManager.h"



#include <WiFi.h>

#include <esp_netif.h>



#include "DeviceIdentity.h"

#include "EthernetManager.h"

#include "InstallationStateManager.h"

#include "ManagementApConfig.h"

#include "SetupDnsPolicy.h"



namespace {



bool offerApDhcpDns(const IPAddress &dnsIp) {

  esp_netif_t *apNetif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");

  if (!apNetif) return false;



  esp_netif_dns_info_t dnsInfo{};

  dnsInfo.ip.type = ESP_IPADDR_TYPE_V4;

  dnsInfo.ip.u_addr.ip4.addr = static_cast<uint32_t>(dnsIp);



  esp_netif_dhcps_stop(apNetif);

  esp_netif_set_dns_info(apNetif, ESP_NETIF_DNS_MAIN, &dnsInfo);



  uint32_t dnsAddr = dnsInfo.ip.u_addr.ip4.addr;
  esp_err_t err = esp_netif_dhcps_option(
      apNetif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &dnsAddr,
      sizeof(dnsAddr));

  esp_netif_dhcps_start(apNetif);

  return err == ESP_OK;

}



}  // namespace



void ManagementApManager::begin(InstallationStateManager *installation,

                                const String &macAddress) {

  _installation = installation;

  _macAddress     = macAddress;

  _ssid           = buildSsid();

  _cachedIp       = ManagementApConfig::IP.toString();

  _cachedClients  = 0;

}



void ManagementApManager::loop() {

  refreshRuntimeState();

}



void ManagementApManager::refreshRuntimeState() {

  if (!_running) {

    _cachedIp = ManagementApConfig::IP.toString();

    _cachedClients = 0;

    return;

  }

  _cachedIp = WiFi.softAPIP().toString();

  _cachedClients = WiFi.softAPgetStationNum();

}



String ManagementApManager::buildSsid() const {

  return String(ManagementApConfig::SSID);

}



bool ManagementApManager::configureApDhcpDns() const {

  return offerApDhcpDns(ManagementApConfig::IP);

}



bool ManagementApManager::start() {

  if (_running) return true;



  _ssid = buildSsid();



  WiFi.mode(WIFI_AP);

  if (!WiFi.softAPConfig(ManagementApConfig::IP,

                         ManagementApConfig::GATEWAY,

                         ManagementApConfig::SUBNET)) {

    Serial.println("[mgmt-ap] softAPConfig failed");

    return false;

  }



  const bool ok = WiFi.softAP(_ssid.c_str(),

                              nullptr,

                              ManagementApConfig::CHANNEL,

                              0,

                              ManagementApConfig::MAX_CLIENTS);

  if (!ok) {

    Serial.println("[mgmt-ap] softAP start failed");

    return false;

  }



  if (!configureApDhcpDns()) {

    Serial.println("[mgmt-ap] WARNING: AP DHCP DNS option failed — clients may not use AP-local DNS");

  }



  _running = true;

  _enabled = true;

  _startedAtMs = millis();

  refreshRuntimeState();



  SetupDnsPolicy::applySetupPhasePolicy();



  _dnsRunning = _dns.start(ManagementApConfig::IP, ManagementApConfig::IP);

  if (!_dnsRunning) {

    Serial.println("[mgmt-ap-dns] WARNING: captive DNS failed to start (setup portal still reachable at http://192.168.4.1)");

  } else {

    Serial.println("[mgmt-ap-dns] captive DNS started (AP-local only, wildcard -> 192.168.4.1)");

  }



  Serial.println("[mgmt-ap] Management AP started");

  Serial.printf("[mgmt-ap] SSID : %s\n", _ssid.c_str());

  Serial.printf("[mgmt-ap] IP   : %s\n", _cachedIp.c_str());

  Serial.printf("[mgmt-ap] URL  : %s\n", ManagementApConfig::PORTAL_URL);

  Serial.printf("[mgmt-ap] DNS  : %s\n",

                _dnsRunning ? "captive AP-local (192.168.4.1)" : "unavailable");

  EthernetManager::noteManagementApState(true, _cachedIp);

  return true;

}



bool ManagementApManager::stop() {

  if (!_running) return true;



  if (_dnsRunning) {

    _dns.stop();

    _dnsRunning = false;

    Serial.println("[mgmt-ap-dns] captive DNS stopped");

  }



  WiFi.softAPdisconnect(true);

  _running = false;

  _enabled = false;

  _startedAtMs = 0;

  _cachedClients = 0;

  _cachedIp = ManagementApConfig::IP.toString();

  EthernetManager::noteManagementApState(false, _cachedIp);

  Serial.println("[mgmt-ap] Management AP stopped");

  return true;

}



String ManagementApManager::ip() const {

  return _cachedIp;

}



uint8_t ManagementApManager::connectedClients() const {

  return _cachedClients;

}



const char *ManagementApManager::mode() const {

  if (!_enabled || !_running) return "disabled";

  if (_installation && _installation->needsSetup()) return "factory";

  return "maintenance";

}



uint32_t ManagementApManager::uptimeSeconds() const {

  if (!_running || _startedAtMs == 0) return 0;

  return (millis() - _startedAtMs) / 1000U;

}



void ManagementApManager::fillStatus(JsonObject out) const {

  out["ssid"]             = _ssid;

  out["ip"]               = _cachedIp;

  out["running"]          = _running;

  out["clients"]          = _cachedClients;

  out["connectedClients"] = _cachedClients;

  out["enabled"]          = _enabled;

  out["mode"]             = mode();

  if (_running) {

    out["uptimeSeconds"] = uptimeSeconds();

  } else {

    out["uptimeSeconds"] = nullptr;

  }

  out["portalUrl"]        = ManagementApConfig::PORTAL_URL;

  out["security"]         = "open";

}

