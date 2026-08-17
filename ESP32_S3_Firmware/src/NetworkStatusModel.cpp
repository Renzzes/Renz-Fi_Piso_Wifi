#include "NetworkStatusModel.h"

#include "Config.h"
#include "EthernetManager.h"
#include "ManagementApManager.h"
#include "ManagementApLifecycle.h"
#include "W5500Config.h"

namespace NetworkStatusModel {

void fill(JsonObject out,
          EthernetManager *eth,
          ManagementApManager *mgmtAp,
          ManagementApLifecycle *lifecycle) {
  const bool ethLink = eth && eth->linkUp();
  const bool ethReady = eth && eth->isServiceReady();

  JsonObject interfaces = out["interfaces"].to<JsonObject>();
  JsonObject mgmtIface = interfaces["managementAp"].to<JsonObject>();
  if (mgmtAp) {
    mgmtAp->fillStatus(mgmtIface);
  } else {
    JsonObject ap = mgmtIface;
    ap["enabled"]          = false;
    ap["running"]          = false;
    ap["ssid"]             = nullptr;
    ap["ip"]               = nullptr;
    ap["mode"]             = "disabled";
    ap["clients"]          = 0;
    ap["connectedClients"] = 0;
    ap["uptimeSeconds"]    = nullptr;
    ap["timeoutSeconds"]   = nullptr;
    ap["portalUrl"]        = nullptr;
    ap["security"]         = nullptr;
  }

  if (lifecycle) {
    lifecycle->patchStatus(mgmtIface);
  }

  JsonObject ethObj = interfaces["ethernet"].to<JsonObject>();
  ethObj["driverReady"] = eth && eth->driverReady();
  ethObj["link"]    = ethLink;
  ethObj["linkUp"]  = ethLink;  // backward-compat with pre-7C.1 clients
  ethObj["hasIp"]   = eth && eth->hasIp();
  ethObj["mode"]    = eth ? eth->addressModeLabel() : "dhcp";
  ethObj["ip"]      = eth ? eth->ip() : W5500Config::IP.toString();
  ethObj["gateway"] = eth ? eth->gateway() : W5500Config::GATEWAY.toString();
  ethObj["subnet"]  = eth ? eth->subnet() : W5500Config::SUBNET.toString();
  ethObj["dns"]     = eth ? eth->dns() : W5500Config::DNS.toString();
  ethObj["mac"]     = eth ? eth->macAddress() : "";

  // Top-level aliases are preserved for existing Browser / Android consumers.
  out["managementAp"].set(mgmtIface);
  out["ethernet"].set(ethObj);
  if (lifecycle) {
    lifecycle->patchStatus(out["managementAp"].to<JsonObject>());
  }

  // Legacy fields — preserved for existing admin UI consumers.
  out["mode"]      = ethReady ? "ethernet" : "management-ap";
  out["modeLabel"] = ethLink ? "W5500 wired (link up)"
                             : (mgmtAp && mgmtAp->isRunning()
                                    ? "Management AP (Ethernet offline)"
                                    : "W5500 wired (no link)");

  String mdns = eth ? eth->mdnsHostname()
                    : String(RenzFiConfig::MDNS_NAME) + ".local";
  out["mdns"]["hostname"] = mdns;
  out["mdns"]["adminUrl"] = "http://" + mdns + "/admin";
}

}  // namespace NetworkStatusModel
