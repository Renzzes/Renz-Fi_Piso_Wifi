#include "NetworkDiagnostics.h"

#include <WiFi.h>

#include <esp_netif.h>
#include <lwip/ip_addr.h>
#include <ping/ping_sock.h>

#include "EthernetManager.h"
#include "ManagementApConfig.h"
#include "ManagementApManager.h"
#include "RenzFiDebug.h"
#include "W5500Config.h"
#include "web/WebRequestDiagnostics.h"

namespace NetworkDiagnostics {

namespace {

#if RENZFI_NETWORK_DIAG

bool g_installed = false;
bool g_ethBeginOk = false;
uint32_t g_ethInitCompletedMs = 0;
bool g_wifiEventsRegistered = false;

bool g_pingInFlight = false;
uint32_t g_lastPingMs = 0;
constexpr uint32_t kPingIntervalMs = 10000;
constexpr const char *kPingTarget = "10.10.10.1";

void logTsPrefix() {
  Serial.printf("[net-diag] t=%lu ms ", static_cast<unsigned long>(millis()));
}

const char *ethEventName(arduino_event_id_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:        return "ETH_START";
    case ARDUINO_EVENT_ETH_CONNECTED:    return "ETH_CONNECTED";
    case ARDUINO_EVENT_ETH_DISCONNECTED: return "ETH_DISCONNECTED";
    case ARDUINO_EVENT_ETH_GOT_IP:       return "ETH_GOT_IP";
    case ARDUINO_EVENT_ETH_LOST_IP:       return "ETH_LOST_IP";
    case ARDUINO_EVENT_ETH_STOP:         return "ETH_STOP";
    default:                             return "ETH_OTHER";
  }
}

const char *wifiEventName(arduino_event_id_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START:           return "WiFi STA Start";
    case ARDUINO_EVENT_WIFI_STA_STOP:            return "WiFi STA Stop";
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:       return "WiFi Connected";
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:    return "WiFi Disconnected";
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:          return "WiFi Got IP";
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:         return "WiFi Lost IP";
    case ARDUINO_EVENT_WIFI_AP_START:            return "SoftAP Start";
    case ARDUINO_EVENT_WIFI_AP_STOP:             return "SoftAP Stop";
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:     return "SoftAP Client Connected";
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:  return "SoftAP Client Disconnected";
    default:                                     return "WiFi Other";
  }
}

void onWiFiArduinoEvent(arduino_event_id_t event, arduino_event_info_t info) {
  (void)info;
  logTsPrefix();
  Serial.printf("EVENT %s\n", wifiEventName(event));

  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      logTsPrefix();
      Serial.printf("  SSID=%s  channel=%d\n",
                    WiFi.SSID().c_str(), WiFi.channel());
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      logTsPrefix();
      Serial.printf("  STA IP=%s  gw=%s  mask=%s\n",
                    WiFi.localIP().toString().c_str(),
                    WiFi.gatewayIP().toString().c_str(),
                    WiFi.subnetMask().toString().c_str());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      logTsPrefix();
      Serial.printf("  reason=%d\n", info.wifi_sta_disconnected.reason);
      break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      logTsPrefix();
      Serial.printf("  client MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
                    info.wifi_ap_staconnected.mac[0],
                    info.wifi_ap_staconnected.mac[1],
                    info.wifi_ap_staconnected.mac[2],
                    info.wifi_ap_staconnected.mac[3],
                    info.wifi_ap_staconnected.mac[4],
                    info.wifi_ap_staconnected.mac[5]);
      break;
    default:
      break;
  }
}

void ensureWiFiEventHandler() {
  if (g_wifiEventsRegistered) return;
  WiFi.onEvent(onWiFiArduinoEvent);
  g_wifiEventsRegistered = true;
}

void printNetifRow(const char *title, esp_netif_t *netif) {
  if (!netif) {
    Serial.printf("  %s: (not present)\n", title);
    return;
  }

  esp_netif_ip_info_t ipInfo{};
  const bool hasIp = esp_netif_get_ip_info(netif, &ipInfo) == ESP_OK;
  char implName[8] = {};
  esp_netif_get_netif_impl_name(netif, implName);

  Serial.printf("  %s\n", title);
  Serial.printf("    ifkey    : %s\n", esp_netif_get_ifkey(netif));
  Serial.printf("    desc     : %s\n", esp_netif_get_desc(netif));
  Serial.printf("    impl     : %s\n", implName);
  if (hasIp && ipInfo.ip.addr != 0) {
    Serial.printf("    IP       : %s\n", IPAddress(ipInfo.ip.addr).toString().c_str());
    Serial.printf("    Gateway  : %s\n", IPAddress(ipInfo.gw.addr).toString().c_str());
    Serial.printf("    Netmask  : %s\n", IPAddress(ipInfo.netmask.addr).toString().c_str());
  } else {
    Serial.println("    IP       : (none)");
  }
}

void onPingSuccess(esp_ping_handle_t hdl, void *args) {
  (void)args;
  uint32_t elapsedMs = 0;
  esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsedMs, sizeof(elapsedMs));
  logTsPrefix();
  Serial.printf("Ping %s SUCCESS latency=%u ms\n", kPingTarget,
                static_cast<unsigned>(elapsedMs));
}

void onPingTimeout(esp_ping_handle_t hdl, void *args) {
  (void)hdl;
  (void)args;
  logTsPrefix();
  Serial.printf("Ping %s FAILED (timeout)\n", kPingTarget);
}

void onPingEnd(esp_ping_handle_t hdl, void *args) {
  (void)args;
  esp_ping_stop(hdl);
  esp_ping_delete_session(hdl);
  g_pingInFlight = false;
}

void runPingTest() {
  if (g_pingInFlight) return;

  esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
  ip_addr_t target{};
  if (!ipaddr_aton(kPingTarget, &target)) {
    logTsPrefix();
    Serial.printf("Ping %s FAILED (invalid target)\n", kPingTarget);
    return;
  }
  config.target_addr = target;
  config.count = 1;
  config.interval_ms = 0;
  config.timeout_ms = 3000;

  esp_ping_callbacks_t callbacks{};
  callbacks.on_ping_success = onPingSuccess;
  callbacks.on_ping_timeout = onPingTimeout;
  callbacks.on_ping_end = onPingEnd;

  esp_ping_handle_t ping = nullptr;
  const esp_err_t err = esp_ping_new_session(&config, &callbacks, &ping);
  if (err != ESP_OK || !ping) {
    logTsPrefix();
    Serial.printf("Ping %s FAILED (session create err=%d)\n", kPingTarget,
                  static_cast<int>(err));
    return;
  }

  g_pingInFlight = true;
  logTsPrefix();
  Serial.printf("Ping %s ...\n", kPingTarget);
  esp_ping_start(ping);
}

#endif  // RENZFI_NETWORK_DIAG

}  // namespace

void install() {
#if RENZFI_NETWORK_DIAG
  if (g_installed) return;
  g_installed = true;
  ensureWiFiEventHandler();
  Serial.println("[net-diag] Network diagnostics installed");
#endif
}

void noteEthBeginFinished(bool success, uint32_t finishedAtMs) {
#if RENZFI_NETWORK_DIAG
  g_ethBeginOk = success;
  g_ethInitCompletedMs = finishedAtMs;
#endif
}

void printStartupReport(EthernetManager *eth, ManagementApManager *mgmtAp,
                        bool ethBeginOk) {
#if RENZFI_NETWORK_DIAG
  (void)ethBeginOk;
  Serial.println();
  Serial.println("================================================");
  Serial.println("NETWORK DIAGNOSTICS");
  Serial.println("================================================");

  Serial.println();
  Serial.println("Ethernet:");
  Serial.printf("  ETH.begin() success/failure : %s\n",
                g_ethBeginOk ? "SUCCESS" : "FAILURE");
  Serial.printf("  Link Up                     : %s\n",
                (eth && eth->linkUp()) ? "yes" : "no");
  Serial.printf("  PHY type                    : W5500 (ETH_PHY_W5500)\n");
  Serial.printf("  PHY address                 : 1\n");
  Serial.printf("  Clock mode                  : SPI3_HOST @ %u MHz, INT=%d%s\n",
                static_cast<unsigned>(W5500Config::SPI_FREQ_MHZ),
                W5500Config::PIN_INT,
                W5500Config::PIN_INT < 0 ? " (poll)" : " (irq)");
  if (eth) {
    Serial.printf("  MAC address                 : %s\n",
                  eth->macAddress().c_str());
    Serial.printf("  Local IP                    : %s\n", eth->ip().c_str());
    Serial.printf("  Gateway                     : %s\n", eth->gateway().c_str());
    Serial.printf("  Subnet                      : %s\n", eth->subnet().c_str());
    Serial.printf("  DNS                         : %s\n", eth->dns().c_str());
    Serial.printf("  DHCP or Static              : %s\n", eth->addressModeLabel());
  } else {
    Serial.println("  MAC address                 : (unavailable)");
    Serial.println("  Local IP                    : (unavailable)");
    Serial.println("  Gateway                     : (unavailable)");
    Serial.println("  Subnet                      : (unavailable)");
    Serial.println("  DNS                         : (unavailable)");
    Serial.println("  DHCP or Static              : (unavailable)");
  }

  esp_netif_t *ethNetif = esp_netif_get_handle_from_ifkey("ETH_DEF");
  if (ethNetif) {
    char implName[8] = {};
    esp_netif_get_netif_impl_name(ethNetif, implName);
    Serial.printf("  Interface name              : %s (%s)\n",
                  esp_netif_get_desc(ethNetif), implName);
  } else {
    Serial.println("  Interface name              : (ETH_DEF not found)");
  }

  if (ETH.linkUp()) {
    Serial.printf("  Link speed / duplex         : %u Mbps / %s\n",
                  static_cast<unsigned>(ETH.linkSpeed()),
                  ETH.fullDuplex() ? "full" : "half");
  } else {
    Serial.println("  Link speed / duplex         : (link down)");
  }

  Serial.printf("  Ethernet initialized at     : %lu ms\n",
                static_cast<unsigned long>(g_ethInitCompletedMs));

  Serial.println();
  Serial.println("WiFi Station:");
  const wifi_mode_t mode = WiFi.getMode();
  const bool staEnabled =
      mode == WIFI_STA || mode == WIFI_AP_STA;
  Serial.printf("  Enabled?                    : %s\n",
                staEnabled ? "yes" : "no");
  if (staEnabled) {
    Serial.printf("  Connected?                  : %s\n",
                  WiFi.isConnected() ? "yes" : "no");
    Serial.printf("  SSID                        : %s\n", WiFi.SSID().c_str());
    Serial.printf("  RSSI                        : %d dBm\n", WiFi.RSSI());
    Serial.printf("  MAC                         : %s\n",
                  WiFi.macAddress().c_str());
    Serial.printf("  Local IP                    : %s\n",
                  WiFi.localIP().toString().c_str());
    Serial.printf("  Gateway                     : %s\n",
                  WiFi.gatewayIP().toString().c_str());
  } else {
    Serial.println("  Connected?                  : no");
    Serial.println("  SSID                        : (disabled)");
    Serial.println("  RSSI                        : n/a");
    Serial.println("  MAC                         : n/a");
    Serial.println("  Local IP                    : n/a");
    Serial.println("  Gateway                     : n/a");
  }

  Serial.println();
  Serial.println("SoftAP:");
  const bool apRunning = mgmtAp && mgmtAp->isRunning();
  Serial.printf("  Running?                    : %s\n",
                apRunning ? "yes" : "no");
  if (mgmtAp) {
    Serial.printf("  SSID                        : %s\n",
                  mgmtAp->ssid().c_str());
  } else {
    Serial.printf("  SSID                        : %s\n",
                  ManagementApConfig::SSID);
  }
  if (apRunning) {
    Serial.printf("  MAC                         : %s\n",
                  WiFi.softAPmacAddress().c_str());
    Serial.printf("  IP                          : %s\n",
                  WiFi.softAPIP().toString().c_str());
  } else {
    Serial.println("  MAC                         : n/a");
    Serial.printf("  IP                          : %s (configured, not running)\n",
                  ManagementApConfig::IP.toString().c_str());
  }

  Serial.println("================================================");
  Serial.println();
#endif
}

void printRegisteredInterfaces(EthernetManager *eth, ManagementApManager *mgmtAp) {
#if RENZFI_NETWORK_DIAG
  Serial.println("================================================");
  Serial.println("REGISTERED INTERFACES");
  Serial.println("================================================");

  int index = 1;

  esp_netif_t *ethNetif = esp_netif_get_handle_from_ifkey("ETH_DEF");
  Serial.printf("Interface #%d\n", index++);
  Serial.println("Name : Ethernet");
  if (eth && eth->hasIp()) {
    Serial.printf("IP   : %s\n", eth->ip().c_str());
  } else if (ethNetif) {
    esp_netif_ip_info_t ipInfo{};
    if (esp_netif_get_ip_info(ethNetif, &ipInfo) == ESP_OK &&
        ipInfo.ip.addr != 0) {
      Serial.printf("IP   : %s\n", IPAddress(ipInfo.ip.addr).toString().c_str());
    } else {
      Serial.println("IP   : (none yet)");
    }
  } else {
    Serial.println("IP   : (driver down)");
  }

  Serial.printf("Interface #%d\n", index++);
  Serial.println("Name : WiFi AP");
  if (mgmtAp && mgmtAp->isRunning()) {
    Serial.printf("IP   : %s\n", WiFi.softAPIP().toString().c_str());
  } else {
    Serial.printf("IP   : %s (not running)\n",
                  ManagementApConfig::IP.toString().c_str());
  }

  Serial.printf("Interface #%d\n", index++);
  Serial.println("Name : WiFi STA");
  const wifi_mode_t mode = WiFi.getMode();
  if (mode == WIFI_STA || mode == WIFI_AP_STA) {
    Serial.printf("IP   : %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("Disabled");
  }

  esp_netif_t *defNetif = esp_netif_get_default_netif();
  Serial.println("------------------------------------------------");
  Serial.println("esp_netif detail:");
  printNetifRow("ETH_DEF", ethNetif);
  printNetifRow("WIFI_AP_DEF", esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"));
  printNetifRow("WIFI_STA_DEF", esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"));
  if (defNetif) {
    Serial.printf("Default route netif: %s (%s)\n",
                  esp_netif_get_desc(defNetif),
                  esp_netif_get_ifkey(defNetif));
  } else {
    Serial.println("Default route netif: (none)");
  }
  Serial.println("================================================");
  Serial.println();
#else
  (void)eth;
  (void)mgmtAp;
#endif
}

void loop() {
#if RENZFI_NETWORK_DIAG
  const uint32_t now = millis();
  if (now - g_lastPingMs < kPingIntervalMs) return;
  g_lastPingMs = now;
  runPingTest();
#endif
}

void onEthEvent(arduino_event_id_t event) {
#if RENZFI_NETWORK_DIAG
  // Network.onEvent delivers ALL network events (including Wi-Fi). Only label
  // real Ethernet IDs here — otherwise SoftAP/STA events were mislabeled
  // ETH_OTHER beside the correct WiFi.onEvent lines.
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
    case ARDUINO_EVENT_ETH_STOP:
    case ARDUINO_EVENT_ETH_CONNECTED:
    case ARDUINO_EVENT_ETH_DISCONNECTED:
    case ARDUINO_EVENT_ETH_GOT_IP:
    case ARDUINO_EVENT_ETH_LOST_IP:
      break;
    default:
      return;
  }

  logTsPrefix();
  Serial.printf("EVENT %s\n", ethEventName(event));

  switch (event) {
    case ARDUINO_EVENT_ETH_CONNECTED:
      logTsPrefix();
      Serial.printf("  link speed=%u Mbps duplex=%s\n",
                    static_cast<unsigned>(ETH.linkSpeed()),
                    ETH.fullDuplex() ? "full" : "half");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      logTsPrefix();
      Serial.printf("  ip=%s gw=%s mask=%s dns=%s\n",
                    ETH.localIP().toString().c_str(),
                    ETH.gatewayIP().toString().c_str(),
                    ETH.subnetMask().toString().c_str(),
                    ETH.dnsIP().toString().c_str());
      break;
    default:
      break;
  }
#else
  (void)event;
#endif
}

const char *interfaceLabel(AsyncWebServerRequest *req) {
  if (!req) return "Unknown";
  if (WebRequestDiagnostics::isManagementApRequest(req)) return "WiFi AP";
  const IPAddress local = WebRequestDiagnostics::requestLocalIp(req);
  if (local[0] != 0) return "Ethernet";
  return "Unknown";
}

void logHttpIncoming(AsyncWebServerRequest *req) {
#if RENZFI_NETWORK_DIAG
  if (!req) return;
  Serial.println("[HTTP]");
  Serial.println("Incoming Request");
  Serial.printf("Interface : %s\n", interfaceLabel(req));
  Serial.printf("Client IP : %s\n",
                WebRequestDiagnostics::requestRemoteIp(req).toString().c_str());
  Serial.printf("URL       : %s\n", req->url().c_str());
#else
  (void)req;
#endif
}

void logPortalApiDebug(AsyncWebServerRequest *req, const char *apiPath) {
#if RENZFI_NETWORK_DIAG
  if (!req) return;
  const IPAddress local = WebRequestDiagnostics::requestLocalIp(req);
  AsyncClient *client = req->client();
  Serial.println("[portal-api]");
  Serial.printf("API path              : %s\n", apiPath ? apiPath : req->url().c_str());
  Serial.printf("Destination interface : %s\n", interfaceLabel(req));
  Serial.printf("Server IP (local)     : %s\n", local.toString().c_str());
  if (client) {
    Serial.printf("Socket remote         : %s:%u\n",
                  client->remoteIP().toString().c_str(),
                  static_cast<unsigned>(client->remotePort()));
    Serial.printf("Socket local          : %s:%u\n",
                  client->localIP().toString().c_str(),
                  static_cast<unsigned>(client->localPort()));
  } else {
    Serial.println("Socket                : (no client)");
  }
#else
  (void)req;
  (void)apiPath;
#endif
}

}  // namespace NetworkDiagnostics
