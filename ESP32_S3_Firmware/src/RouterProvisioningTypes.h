#pragma once

#include <Arduino.h>

namespace RouterProvisioning {

static constexpr const char *COMMENT_PREFIX = "RENZFI:";
static constexpr const char *APPLY_CONFIRMATION =
    "APPLY RENZ-FI CONFIGURATION";
static constexpr const char *ADOPT_CONFIRMATION =
    "ADOPT EXISTING RENZ-FI NETWORK";
static constexpr const char *CHANGE_NETWORK_MODE_CONFIRMATION =
    "CHANGE NETWORK MODE";

static constexpr const char *NETWORK_MODE_CREATE_NEW = "create_new";
static constexpr const char *NETWORK_MODE_EXISTING   = "existing";

static constexpr const char *COMMENT_GUEST_BRIDGE =
    "RENZFI: guest bridge";
static constexpr const char *COMMENT_GUEST_LAN =
    "RENZFI: guest LAN address";
static constexpr const char *COMMENT_GUEST_POOL =
    "RENZFI: guest DHCP pool";
static constexpr const char *COMMENT_GUEST_DHCP_SERVER =
    "RENZFI: guest DHCP server";
static constexpr const char *COMMENT_GUEST_DHCP_NETWORK =
    "RENZFI: guest DHCP network";
static constexpr const char *COMMENT_GUEST_HS_PROFILE =
    "RENZFI: guest hotspot profile";
static constexpr const char *COMMENT_GUEST_HS_SERVER =
    "RENZFI: guest hotspot server";
static constexpr const char *COMMENT_ESP32_API =
    "RENZFI: ESP32 appliance API access";
static constexpr const char *COMMENT_WALLED_GARDEN =
    "RENZFI: captive portal walled garden";

static constexpr const char *DEFAULT_GUEST_NETWORK     = "10.20.20.0/24";
static constexpr const char *DEFAULT_GUEST_GATEWAY     = "10.20.20.1";
static constexpr const char *DEFAULT_GUEST_GATEWAY_CIDR = "10.20.20.1/24";
static constexpr const char *DEFAULT_DHCP_POOL_RANGE   = "10.20.20.10-10.20.20.254";

struct Settings {
  String guestBridgeName   = "bridge-renzfi";
  String guestNetwork      = DEFAULT_GUEST_NETWORK;
  String guestGateway      = DEFAULT_GUEST_GATEWAY;
  String guestGatewayCidr  = DEFAULT_GUEST_GATEWAY_CIDR;
  String dhcpPoolRange     = DEFAULT_DHCP_POOL_RANGE;
  String dhcpLeaseTime     = "1h";
  String poolName          = "pool-renzfi";
  String dhcpServerName    = "dhcp-renzfi";
  String hotspotServerName = "hs-renzfi";
  String hotspotProfileName = "hsprof-renzfi";
  String dnsName           = "wifi.renzfi.local";
  String guestSsid         = "Renz-Fi WiFi";
};

struct ProposedAction {
  String id;
  String category;
  String action;
  String target;
  String details;
  bool safe           = true;
  bool willCreate     = false;
  bool alreadyManaged = false;
  bool conflict       = false;
  bool deferred       = false;
  String conflictReason;
};

Settings defaultSettings();

}  // namespace RouterProvisioning
