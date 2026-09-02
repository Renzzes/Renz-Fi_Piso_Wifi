#include "RouterProvisioningManager.h"

#include <memory>

#include "Config.h"
#include "ExistingNetworkScan.h"
#include "EthernetManager.h"
#include "InstallationState.h"
#include "InstallationStateManager.h"
#include "RouterOsClient.h"
#include "RouterProvisioningEngine.h"
#include "RouterWirelessAdapter.h"
#include "RouterProvisioningPreconditions.h"
#include "RenzFiRouterApiLog.h"
#include "SetupRouterConnectionManager.h"
#include "StorageManager.h"
#include "StoragePaths.h"

#include "JsonHeap.h"
#include "MemoryDiagnostics.h"
#include "DmaMemoryMonitor.h"
#include "RenzFiDebug.h"

#if RENZFI_DEBUG_ROUTER
#define RP_LOG(...) Serial.printf(__VA_ARGS__)
#define RP_LN(msg) Serial.println(msg)
#else
#define RP_LOG(...) ((void)0)
#define RP_LN(msg) ((void)0)
#endif

namespace {

constexpr size_t kPersistDocCapacity = RenzFiConfig::JSON_DOC_SMALL;
constexpr const char *kConfirmationRequired = "CONFIRMATION_REQUIRED";
constexpr uint8_t kFirewallApiRuleReplyCap = 8;
constexpr const char *kFirewallInspectionLimit = "FIREWALL_INSPECTION_LIMIT";

bool isRenzfiManagedComment(const String &comment) {
  return comment.startsWith(RouterProvisioning::COMMENT_PREFIX);
}

// True for any RouterOS API code that represents a rejected login (wrong
// credentials, trap/fatal during auth, or an incomplete login exchange)
// rather than a transport/connectivity failure.
bool isLoginFailureCode(const String &code) {
  return code == "API_LOGIN_FAILED" || code == "ROUTEROS_LOGIN_FAILED" ||
         code == "ROUTEROS_API_AUTH_TRAP" || code == "ROUTEROS_API_AUTH_FATAL";
}

void markRouterPlanFailed(RouterProvisioningManager::OperationResult &result) {
  result.success = false;
}

bool isValidObjectName(const String &name) {
  if (name.isEmpty() || name.length() > 32) return false;
  for (size_t i = 0; i < name.length(); ++i) {
    const char c = name.charAt(i);
    const bool ok =
        (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) return false;
  }
  return true;
}

bool isValidDnsName(const String &name) {
  if (name.isEmpty() || name.length() > 64) return false;
  if (name.indexOf('.') < 0) return false;
  for (size_t i = 0; i < name.length(); ++i) {
    const char c = name.charAt(i);
    const bool ok =
        (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '.';
    if (!ok) return false;
  }
  return true;
}

bool parseIpv4Prefix(const String &cidr, IPAddress &network, uint8_t &prefixLen) {
  const int slash = cidr.indexOf('/');
  if (slash <= 0) return false;
  if (!network.fromString(cidr.substring(0, slash))) return false;
  prefixLen = static_cast<uint8_t>(cidr.substring(slash + 1).toInt());
  return prefixLen >= 8 && prefixLen <= 30;
}

uint32_t ipv4ToHostOrder(const IPAddress &ip) {
  return (static_cast<uint32_t>(ip[0]) << 24) |
         (static_cast<uint32_t>(ip[1]) << 16) |
         (static_cast<uint32_t>(ip[2]) << 8) |
         static_cast<uint32_t>(ip[3]);
}

bool maskToPrefixLen(const String &maskStr, uint8_t &prefixOut) {
  IPAddress mask;
  if (!mask.fromString(maskStr)) return false;
  const uint32_t bits = ipv4ToHostOrder(mask);
  if (bits == 0) return false;
  const uint32_t inverted = ~bits;
  if ((inverted & (inverted + 1)) != 0) return false;
  prefixOut = 0;
  uint32_t walk = bits;
  while (walk & 0x80000000u) {
    prefixOut++;
    walk <<= 1;
  }
  return prefixOut >= 8 && prefixOut <= 30;
}

String networkCidrFromIpAndMask(const String &ipStr, const String &maskStr) {
  IPAddress ip;
  uint8_t prefix = 0;
  if (!ip.fromString(ipStr) || !maskToPrefixLen(maskStr, prefix)) return "";
  const uint32_t maskHost =
      prefix == 0 ? 0 : (0xFFFFFFFFu << (32 - prefix));
  const uint32_t netHost = ipv4ToHostOrder(ip) & maskHost;
  IPAddress network((netHost >> 24) & 0xFF, (netHost >> 16) & 0xFF,
                    (netHost >> 8) & 0xFF, netHost & 0xFF);
  return network.toString() + "/" + String(prefix);
}

bool cidrOverlaps(const String &a, const String &b) {
  IPAddress netA, netB;
  uint8_t lenA = 0, lenB = 0;
  if (!parseIpv4Prefix(a, netA, lenA) || !parseIpv4Prefix(b, netB, lenB)) {
    return false;
  }
  const uint8_t common = lenA < lenB ? lenA : lenB;
  const uint32_t mask =
      common == 0 ? 0 : (0xFFFFFFFFu << (32 - common));
  return (ipv4ToHostOrder(netA) & mask) == (ipv4ToHostOrder(netB) & mask);
}

bool subnetsOverlapOrEqual(const String &a, const String &b) {
  if (a == b) return true;
  return cidrOverlaps(a, b);
}

void appendLocalSafetySummary(JsonArray summary) {
  summary.add("Apply creates the guest-network foundation only.");
  summary.add("It does not attach a physical port to bridge-renzfi.");
  summary.add("It does not enable MikroTik Hotspot.");
  summary.add("It does not configure MikroTik wireless.");
  summary.add("It does not configure external APs.");
  summary.add(
      "It does not change NAT, existing bridges, existing DHCP, or existing "
      "customer Wi-Fi.");
}

bool validateGuestSubnetLocal(const RouterProvisioning::Settings &settings,
                              EthernetManager *eth, String *errorOut,
                              String *routerSubnetWarningOut) {
  bool managementKnown = false;

  if (eth && eth->hasIp()) {
    const String espSubnet =
        networkCidrFromIpAndMask(eth->ip(), eth->subnet());
    if (!espSubnet.isEmpty()) {
      managementKnown = true;
      if (subnetsOverlapOrEqual(settings.guestNetwork, espSubnet)) {
        if (errorOut) {
          *errorOut = "Guest subnet overlaps the ESP32 Ethernet subnet";
        }
        return false;
      }
    }

    const String gateway = eth->gateway();
    if (!gateway.isEmpty()) {
      const String routerSubnet =
          networkCidrFromIpAndMask(gateway, eth->subnet());
      if (!routerSubnet.isEmpty()) {
        managementKnown = true;
        if (subnetsOverlapOrEqual(settings.guestNetwork, routerSubnet)) {
          if (errorOut) {
            *errorOut =
                "Guest subnet overlaps the MikroTik management/router subnet";
          }
          return false;
        }
      }
    }
  }

  if (!managementKnown && routerSubnetWarningOut) {
    *routerSubnetWarningOut =
        "Apply will stop safely if this guest subnet conflicts with an "
        "existing MikroTik address.";
  }
  return true;
}

bool ipInCidr(const IPAddress &ip, const IPAddress &network, uint8_t prefix) {
  const uint32_t mask =
      prefix == 0 ? 0 : (0xFFFFFFFFu << (32 - prefix));
  return (ipv4ToHostOrder(ip) & mask) == (ipv4ToHostOrder(network) & mask);
}

bool dhcpRangeInNetwork(const String &range, const String &networkCidr) {
  IPAddress net;
  uint8_t prefix = 0;
  if (!parseIpv4Prefix(networkCidr, net, prefix)) return false;
  const int dash = range.indexOf('-');
  if (dash <= 0) return false;
  String startPart = range.substring(0, dash);
  String endPart = range.substring(dash + 1);
  startPart.trim();
  endPart.trim();
  IPAddress start, end;
  if (!start.fromString(startPart) || !end.fromString(endPart)) {
    return false;
  }
  return ipInCidr(start, net, prefix) && ipInCidr(end, net, prefix) &&
         ipv4ToHostOrder(start) <= ipv4ToHostOrder(end);
}

bool replyAttr(const RouterOsClient::CommandResult &result, uint8_t replyIdx,
               const char *key, String &valueOut) {
  if (replyIdx >= result.replyCount) return false;
  const auto &record = result.replyAt(replyIdx);
  for (uint8_t i = 0; i < record.attrCount; ++i) {
    String keyPart, valPart;
    if (!RouterOsClient::parseAttr(record.attr(i), keyPart, valPart)) continue;
    if (keyPart == key) {
      valueOut = valPart;
      return true;
    }
  }
  return false;
}

int findReplyByName(const RouterOsClient::CommandResult &result,
                    const String &name) {
  for (uint8_t i = 0; i < result.replyCount; ++i) {
    String value;
    if (replyAttr(result, i, "name", value) && value == name) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

class RouterSession {
 public:
  explicit RouterSession(EthernetManager *eth) : _eth(eth) {
    _client.setTimeouts(RenzFiConfig::SETUP_ROUTER_CONNECT_TIMEOUT_MS,
                        RenzFiConfig::SETUP_ROUTER_IO_TIMEOUT_MS);
  }

  bool open(const SetupRouterConnectionManager::RouterInput &input,
            String &errorOut, String &errorCodeOut) {
    errorOut.clear();
    errorCodeOut.clear();
    if (!_eth || !_eth->linkUp() || !_eth->hasIp()) {
      errorOut     = "Ethernet link and DHCP IP are required";
      errorCodeOut = "ETHERNET_NOT_READY";
      return false;
    }
    _client.setCredentials(input.host, input.username, input.password,
                          input.apiPort);
    _client.setCredentialSource("setup-provisioning");
    RP_LOG("[router-plan] session open host=%s port=%u user=%s\n",
                  input.host.c_str(), static_cast<unsigned>(input.apiPort),
                  input.username.c_str());
    if (!_client.connect()) {
      errorOut     = _client.lastError();
      errorCodeOut = _client.lastErrorCode().isEmpty() ? "TCP_CONNECT_FAILED"
                                                       : _client.lastErrorCode();
      RP_LOG("[router-plan] session connect failed code=%s msg=%s\n",
                    errorCodeOut.c_str(), errorOut.c_str());
      return false;
    }
    RP_LN(F("[router-plan] session connect ok"));
    if (!_client.login()) {
      errorOut     = _client.lastError();
      errorCodeOut = _client.lastErrorCode().isEmpty() ? "API_LOGIN_FAILED"
                                                       : _client.lastErrorCode();
      RP_LOG("[router-plan] session login failed code=%s msg=%s\n",
                    errorCodeOut.c_str(), errorOut.c_str());
      _client.disconnect();
      return false;
    }
    RP_LN(F("[router-plan] session login ok"));
    _open = true;
    return true;
  }

  void close() {
    if (_open) _client.disconnect("success");
    _open = false;
  }

  RouterOsClient &client() { return _client; }
  ~RouterSession() { close(); }

 private:
  EthernetManager *_eth;
  RouterOsClient _client;
  bool _open = false;
};

RouterSession *allocRouterSession(EthernetManager *eth) {
  return new (std::nothrow) RouterSession(eth);
}

void freeRouterSession(RouterSession *session) { delete session; }

struct SessionGuard {
  RouterSession *session = nullptr;
  explicit SessionGuard(RouterSession *s) : session(s) {}
  ~SessionGuard() { freeRouterSession(session); }
  SessionGuard(const SessionGuard &) = delete;
  SessionGuard &operator=(const SessionGuard &) = delete;
};

struct InspectionData {
  String identity;
  String version;
  bool firewallInspectionLimited = false;

  std::unique_ptr<RouterOsClient::CommandResult> bridges;
  std::unique_ptr<RouterOsClient::CommandResult> addresses;
  std::unique_ptr<RouterOsClient::CommandResult> pools;
  std::unique_ptr<RouterOsClient::CommandResult> dhcpServers;
  std::unique_ptr<RouterOsClient::CommandResult> dhcpNetworks;
  std::unique_ptr<RouterOsClient::CommandResult> filterRules;

  RouterOsClient::CommandResult &ensureBridges() {
    if (!bridges) bridges.reset(new (std::nothrow) RouterOsClient::CommandResult());
    return *bridges;
  }
  RouterOsClient::CommandResult &ensureAddresses() {
    if (!addresses) addresses.reset(new (std::nothrow) RouterOsClient::CommandResult());
    return *addresses;
  }
  RouterOsClient::CommandResult &ensurePools() {
    if (!pools) pools.reset(new (std::nothrow) RouterOsClient::CommandResult());
    return *pools;
  }
  RouterOsClient::CommandResult &ensureDhcpServers() {
    if (!dhcpServers) {
      dhcpServers.reset(new (std::nothrow) RouterOsClient::CommandResult());
    }
    return *dhcpServers;
  }
  RouterOsClient::CommandResult &ensureDhcpNetworks() {
    if (!dhcpNetworks) {
      dhcpNetworks.reset(new (std::nothrow) RouterOsClient::CommandResult());
    }
    return *dhcpNetworks;
  }
  RouterOsClient::CommandResult &ensureFilterRules() {
    if (!filterRules) {
      filterRules.reset(new (std::nothrow) RouterOsClient::CommandResult());
    }
    return *filterRules;
  }

  const RouterOsClient::CommandResult &bridgesRef() const {
    static const RouterOsClient::CommandResult kEmpty;
    return bridges ? *bridges : kEmpty;
  }
  const RouterOsClient::CommandResult &addressesRef() const {
    static const RouterOsClient::CommandResult kEmpty;
    return addresses ? *addresses : kEmpty;
  }
  const RouterOsClient::CommandResult &poolsRef() const {
    static const RouterOsClient::CommandResult kEmpty;
    return pools ? *pools : kEmpty;
  }
  const RouterOsClient::CommandResult &dhcpServersRef() const {
    static const RouterOsClient::CommandResult kEmpty;
    return dhcpServers ? *dhcpServers : kEmpty;
  }
  const RouterOsClient::CommandResult &dhcpNetworksRef() const {
    static const RouterOsClient::CommandResult kEmpty;
    return dhcpNetworks ? *dhcpNetworks : kEmpty;
  }
  const RouterOsClient::CommandResult &filterRulesRef() const {
    static const RouterOsClient::CommandResult kEmpty;
    return filterRules ? *filterRules : kEmpty;
  }
};

// Heap-only: InspectionData holds multiple CommandResult catalogs. Never
// allocate on the async_tcp stack (16 KB) or router_worker stack.
using InspectionDataPtr = InspectionData *;

InspectionDataPtr allocInspectionData() {
  MemoryDiagnostics::setInspectionActive(true);
  return new (std::nothrow) InspectionData();
}

void freeInspectionData(InspectionDataPtr inspection) {
  delete inspection;
  MemoryDiagnostics::setInspectionActive(false);
}

struct InspectionGuard {
  InspectionDataPtr inspection = nullptr;
  explicit InspectionGuard(InspectionDataPtr ptr) : inspection(ptr) {}
  ~InspectionGuard() { freeInspectionData(inspection); }
  InspectionGuard(const InspectionGuard &) = delete;
  InspectionGuard &operator=(const InspectionGuard &) = delete;
};

String mapInspectFailureCode(const RouterOsClient &ros,
                             const RouterOsClient::CommandResult &result) {
  if (result.fatalReceived) return "ROUTEROS_API_FATAL";
  if (result.trapReceived) return "API_TRAP";
  if (!ros.lastErrorCode().isEmpty()) return ros.lastErrorCode();
  return "API_COMMAND_FAILED";
}

bool runPrint(RouterOsClient &ros, const char *commandPath, const String &query,
              RouterOsClient::CommandResult &out, String &errorOut,
              String &errorCodeOut, uint32_t *elapsedMsOut = nullptr) {
  errorOut.clear();
  errorCodeOut.clear();
  const uint32_t cmdStart = millis();
  RP_LOG("[router-plan] inspect cmd=%s query=%s\n", commandPath,
                query.isEmpty() ? "(none)" : query.c_str());
  bool ok = false;
  if (query.isEmpty()) {
    ok = ros.executeCommand(commandPath, out);
  } else {
    ok = ros.executeCommand(commandPath, query, out);
  }
  const uint32_t elapsedMs = millis() - cmdStart;
  if (elapsedMsOut) *elapsedMsOut = elapsedMs;

  auto formatInspectError = [&](const char *suffix) {
    return String(commandPath) + ": " + suffix + " replyCount=" +
           String(static_cast<unsigned>(out.replyCount)) + " elapsedMs=" +
           String(elapsedMs);
  };

  if (!ok) {
    errorOut     = formatInspectError(ros.lastError().c_str());
    errorCodeOut = mapInspectFailureCode(ros, out);
    RP_LOG("[router-plan] inspect cmd=%s failed code=%s msg=%s elapsedMs=%u\n",
                  commandPath, errorCodeOut.c_str(), errorOut.c_str(), elapsedMs);
    return false;
  }
  if (out.fatalReceived) {
    errorOut     = formatInspectError(out.fatalMessage.isEmpty() ? "RouterOS fatal reply"
                                                                 : out.fatalMessage.c_str());
    errorCodeOut = "ROUTEROS_API_FATAL";
    RP_LOG("[router-plan] inspect cmd=%s failed code=%s msg=%s elapsedMs=%u\n",
                  commandPath, errorCodeOut.c_str(), errorOut.c_str(), elapsedMs);
    return false;
  }
  if (out.trapReceived) {
    errorOut     = formatInspectError(out.trapMessage.isEmpty() ? "RouterOS API trap"
                                                                : out.trapMessage.c_str());
    errorCodeOut = "API_TRAP";
    RP_LOG("[router-plan] inspect cmd=%s failed code=%s msg=%s elapsedMs=%u\n",
                  commandPath, errorCodeOut.c_str(), errorOut.c_str(), elapsedMs);
    return false;
  }
  if (out.replyLimitReached) {
    errorOut     = formatInspectError("RouterOS reply exceeded supported record limit");
    errorCodeOut = "ROUTER_INSPECTION_LIMIT";
    RP_LOG("[router-plan] inspect cmd=%s failed code=%s msg=%s elapsedMs=%u\n",
                  commandPath, errorCodeOut.c_str(), errorOut.c_str(), elapsedMs);
    return false;
  }
  RP_LOG("[router-plan] inspect cmd=%s ok replyCount=%u elapsedMs=%u\n",
                commandPath, static_cast<unsigned>(out.replyCount), elapsedMs);
  return true;
}

bool runPrintWithAttributes(RouterOsClient &ros, const char *commandPath,
                            const String *attributes, size_t attributeCount,
                            RouterOsClient::CommandResult &out, String &errorOut,
                            String &errorCodeOut, uint32_t *elapsedMsOut = nullptr) {
  errorOut.clear();
  errorCodeOut.clear();
  const uint32_t cmdStart = millis();
  RP_LOG("[router-plan] inspect cmd=%s attrs=%u\n", commandPath,
                static_cast<unsigned>(attributeCount));
  const bool ok =
      ros.executeCommand(commandPath, attributes, attributeCount, out);
  const uint32_t elapsedMs = millis() - cmdStart;
  if (elapsedMsOut) *elapsedMsOut = elapsedMs;

  auto formatInspectError = [&](const char *suffix) {
    return String(commandPath) + ": " + suffix + " replyCount=" +
           String(static_cast<unsigned>(out.replyCount)) + " elapsedMs=" +
           String(elapsedMs);
  };

  if (!ok) {
    errorOut     = formatInspectError(ros.lastError().c_str());
    errorCodeOut = mapInspectFailureCode(ros, out);
    RP_LOG("[router-plan] inspect cmd=%s failed code=%s msg=%s elapsedMs=%u\n",
                  commandPath, errorCodeOut.c_str(), errorOut.c_str(), elapsedMs);
    return false;
  }
  if (out.fatalReceived) {
    errorOut     = formatInspectError(out.fatalMessage.isEmpty() ? "RouterOS fatal reply"
                                                                 : out.fatalMessage.c_str());
    errorCodeOut = "ROUTEROS_API_FATAL";
    RP_LOG("[router-plan] inspect cmd=%s failed code=%s msg=%s elapsedMs=%u\n",
                  commandPath, errorCodeOut.c_str(), errorOut.c_str(), elapsedMs);
    return false;
  }
  if (out.trapReceived) {
    errorOut     = formatInspectError(out.trapMessage.isEmpty() ? "RouterOS API trap"
                                                                : out.trapMessage.c_str());
    errorCodeOut = "API_TRAP";
    RP_LOG("[router-plan] inspect cmd=%s failed code=%s msg=%s elapsedMs=%u\n",
                  commandPath, errorCodeOut.c_str(), errorOut.c_str(), elapsedMs);
    return false;
  }
  RP_LOG("[router-plan] inspect cmd=%s ok replyCount=%u elapsedMs=%u\n",
                commandPath, static_cast<unsigned>(out.replyCount), elapsedMs);
  return true;
}

bool inspectFirewallApiRules(RouterOsClient &ros,
                             RouterOsClient::CommandResult &filterRulesOut,
                             bool &limitedOut, String &errorOut,
                             String &errorCodeOut) {
  limitedOut = false;
  filterRulesOut = RouterOsClient::CommandResult{};

  RP_LN(F("[router-plan] inspect firewall api-rule query scoped"));
  ros.setCommandReplyLimits(kFirewallApiRuleReplyCap, true);

  const String attrs[] = {
      "=.proplist=.id,action,chain,protocol,dst-port,src-address,disabled,comment",
      "?chain=input",
      "?protocol=tcp",
      "?dst-port=8728",
  };

  uint32_t elapsedMs = 0;
  const bool ok = runPrintWithAttributes(
      ros, "/ip/firewall/filter/print", attrs,
      sizeof(attrs) / sizeof(attrs[0]), filterRulesOut, errorOut, errorCodeOut,
      &elapsedMs);

  ros.resetCommandReplyLimits();

  RP_LOG("[router-plan] inspect firewall api-rule replyCount=%u elapsedMs=%u\n",
                static_cast<unsigned>(filterRulesOut.replyCount), elapsedMs);

  if (!ok) {
    return false;
  }
  if (filterRulesOut.replyLimitReached) {
    limitedOut = true;
    RP_LOG("[router-plan] inspect firewall api-rule limit reached replyCount=%u\n",
                  static_cast<unsigned>(filterRulesOut.replyCount));
    return true;
  }
  return true;
}

bool runPrint(RouterOsClient &ros, const String &path, const String &query,
              RouterOsClient::CommandResult &out, String &errorOut) {
  String ignoredCode;
  return runPrint(ros, path.c_str(), query, out, errorOut, ignoredCode);
}

void summarizeExisting(JsonObject summary, const InspectionData &inspection) {
  JsonArray bridges = summary.createNestedArray("bridges");
  for (uint8_t i = 0; i < inspection.bridgesRef().replyCount; ++i) {
    String name;
    if (replyAttr(inspection.bridgesRef(), i, "name", name)) bridges.add(name);
  }
  JsonArray addresses = summary.createNestedArray("ipv4Addresses");
  for (uint8_t i = 0; i < inspection.addressesRef().replyCount; ++i) {
    String addr, comment;
    replyAttr(inspection.addressesRef(), i, "address", addr);
    replyAttr(inspection.addressesRef(), i, "comment", comment);
    JsonObject row = addresses.createNestedObject();
    row["address"] = addr;
    if (isRenzfiManagedComment(comment)) row["renzfi"] = true;
  }
  summary["deferredInspections"] =
      "Hotspot, walled garden, wireless, NAT, and bridge ports are deferred until "
      "a later connection/AP integration phase.";
}

enum class ObjectState { Missing, Managed, Conflict };

ObjectState classifyNamedObject(const RouterOsClient::CommandResult &result,
                                const String &name, String &conflictReason) {
  const int idx = findReplyByName(result, name);
  if (idx < 0) return ObjectState::Missing;
  String comment;
  replyAttr(result, static_cast<uint8_t>(idx), "comment", comment);
  if (isRenzfiManagedComment(comment)) return ObjectState::Managed;
  conflictReason = "Object '" + name + "' exists without a RENZFI: comment";
  return ObjectState::Conflict;
}

bool apiAccessRuleSatisfied(const RouterOsClient::CommandResult &filterRules,
                            const String &espIp, bool &managedOut) {
  managedOut = false;
  for (uint8_t i = 0; i < filterRules.replyCount; ++i) {
    String chain, protocol, dstPort, srcAddress, action, disabled, comment;
    replyAttr(filterRules, i, "chain", chain);
    replyAttr(filterRules, i, "protocol", protocol);
    replyAttr(filterRules, i, "dst-port", dstPort);
    replyAttr(filterRules, i, "src-address", srcAddress);
    replyAttr(filterRules, i, "action", action);
    replyAttr(filterRules, i, "disabled", disabled);
    replyAttr(filterRules, i, "comment", comment);

    if (chain != "input" || protocol != "tcp" || dstPort != "8728") continue;
    if (disabled == "true") continue;
    if (action != "accept") continue;
    if (!srcAddress.isEmpty() && srcAddress != espIp) continue;

    if (isRenzfiManagedComment(comment)) {
      managedOut = true;
      return true;
    }
    if (srcAddress == espIp) {
      return true;
    }
  }
  return false;
}

bool apiAccessRuleExists(const RouterOsClient::CommandResult &filterRules,
                         const String &espIp) {
  bool managed = false;
  return apiAccessRuleSatisfied(filterRules, espIp, managed);
}

void addAction(JsonArray actions, const RouterProvisioning::ProposedAction &action) {
  JsonObject row = actions.createNestedObject();
  row["id"]             = action.id;
  row["category"]       = action.category;
  row["action"]         = action.action;
  row["target"]         = action.target;
  row["details"]        = action.details;
  row["safe"]           = action.safe;
  row["willCreate"]     = action.willCreate;
  row["alreadyManaged"] = action.alreadyManaged;
  row["conflict"]       = action.conflict;
  row["deferred"]       = action.deferred;
  if (!action.conflictReason.isEmpty()) row["conflictReason"] = action.conflictReason;
}

void addDeferredAction(JsonArray actions, const char *id, const char *category,
                       const char *details) {
  RouterProvisioning::ProposedAction action;
  action.id       = id;
  action.category = category;
  action.action   = "deferred";
  action.details  = details;
  action.safe     = true;
  action.deferred = true;
  addAction(actions, action);
}

void buildStaticLocalPlan(const RouterProvisioning::Settings &settings,
                          const String &espIp, JsonArray proposedActions,
                          JsonArray warnings) {
  auto addCreate = [&](const char *id, const char *category, const String &target,
                       const String &details) {
    RouterProvisioning::ProposedAction action;
    action.id         = id;
    action.category   = category;
    action.action     = "create";
    action.target     = target;
    action.details    = details;
    action.willCreate = true;
    action.safe       = true;
    addAction(proposedActions, action);
  };

  addCreate("bridge", "bridge", settings.guestBridgeName,
            "Guest bridge (no ports added in this phase)");
  addCreate("guest-address", "address", settings.guestGatewayCidr,
            "Gateway on " + settings.guestBridgeName);
  addCreate("pool", "dhcp", settings.poolName, "DHCP pool " + settings.dhcpPoolRange);
  addCreate("dhcp-server", "dhcp", settings.dhcpServerName,
            "DHCP server on " + settings.guestBridgeName);
  addCreate("dhcp-network", "dhcp", settings.guestNetwork,
            "DHCP network gateway/DNS " + settings.guestGateway);
  addCreate("api-access", "firewall", espIp + ":8728",
            RouterProvisioning::COMMENT_ESP32_API);

  RouterProvisioning::ProposedAction safety;
  safety.id       = "apply-preflight";
  safety.category = "safety";
  safety.action   = "verify";
  safety.target   = "Exact Renz-Fi names, comments, and addresses";
  safety.details  =
      "Apply checks only exact Renz-Fi names/comments and the configured guest "
      "subnet/address before creation; CONFLICT_DETECTED stops on non-RENZFI "
      "use of the guest subnet without writes.";
  safety.safe     = true;
  addAction(proposedActions, safety);

  addDeferredAction(proposedActions, "deferred-hs-server", "hotspot",
                    "Deferred until a later connection/AP integration phase.");
  addDeferredAction(proposedActions, "deferred-bridge-port", "bridge",
                    "Deferred until a later connection/AP integration phase.");
  addDeferredAction(proposedActions, "deferred-captive-portal", "hotspot",
                    "Deferred until a later connection/AP integration phase.");
  addDeferredAction(proposedActions, "deferred-wireless", "wireless",
                    "Deferred until a later connection/AP integration phase.");
  addDeferredAction(proposedActions, "deferred-vouchers", "hotspot",
                    "Deferred until a later connection/AP integration phase.");
  addDeferredAction(proposedActions, "deferred-bandwidth", "hotspot",
                    "Deferred until a later connection/AP integration phase.");
  addDeferredAction(proposedActions, "deferred-external-ap", "wireless",
                    "External AP configuration is manual in this version.");
  addDeferredAction(proposedActions, "deferred-nat", "nat",
                    "Deferred until a later integration phase.");
  addDeferredAction(proposedActions, "deferred-captive-redirect", "hotspot",
                    "Deferred until captive portal integration phase.");

  warnings.add(
      "Preview is local and does not inspect existing MikroTik objects. Apply "
      "will refuse to overwrite objects that conflict with Renz-Fi names or "
      "addresses.");
}

bool validateApplyTargetCatalog(const RouterOsClient::CommandResult &result,
                                const String &objectLabel, String &errorOut,
                                String &errorCodeOut) {
  if (result.replyLimitReached || result.replyCount > 1) {
    errorOut     = objectLabel + ": ambiguous or excessive matches";
    errorCodeOut = "CONFLICT_DETECTED";
    return false;
  }
  if (result.replyCount == 1) {
    String comment;
    replyAttr(result, 0, "comment", comment);
    if (!comment.isEmpty() && !isRenzfiManagedComment(comment)) {
      errorOut     = objectLabel + " exists without a RENZFI: comment";
      errorCodeOut = "CONFLICT_DETECTED";
      return false;
    }
  }
  return true;
}

bool validateApplyAddressTarget(const RouterProvisioning::Settings &settings,
                                const RouterOsClient::CommandResult &result,
                                String &errorOut, String &errorCodeOut) {
  if (!validateApplyTargetCatalog(result, "Guest address " + settings.guestGatewayCidr,
                                  errorOut, errorCodeOut)) {
    return false;
  }
  if (result.replyCount == 1) {
    String iface;
    replyAttr(result, 0, "interface", iface);
    if (!iface.isEmpty() && iface != settings.guestBridgeName) {
      errorOut     = "Guest address exists on interface " + iface;
      errorCodeOut = "CONFLICT_DETECTED";
      return false;
    }
  }
  return true;
}

bool inspectApplyTargets(RouterSession &session,
                         const RouterProvisioning::Settings &settings,
                         InspectionData &out, String &errorOut,
                         String &errorCodeOut) {
  RouterOsClient &ros = session.client();

  const String bridgeAttrs[] = {
      "=.proplist=.id,name,comment",
      "?name=" + settings.guestBridgeName,
  };
  if (!runPrintWithAttributes(ros, "/interface/bridge/print", bridgeAttrs, 2,
                              out.ensureBridges(), errorOut, errorCodeOut)) {
    return false;
  }
  if (!validateApplyTargetCatalog(out.ensureBridges(), "Bridge " + settings.guestBridgeName,
                                  errorOut, errorCodeOut)) {
    return false;
  }

  const String addressAttrs[] = {
      "=.proplist=.id,address,interface,comment",
      "?address=" + settings.guestGatewayCidr,
  };
  if (!runPrintWithAttributes(ros, "/ip/address/print", addressAttrs, 2,
                              out.ensureAddresses(), errorOut, errorCodeOut)) {
    return false;
  }
  if (!validateApplyAddressTarget(settings, out.ensureAddresses(), errorOut, errorCodeOut)) {
    return false;
  }

  const String poolAttrs[] = {
      "=.proplist=.id,name,ranges,comment",
      "?name=" + settings.poolName,
  };
  if (!runPrintWithAttributes(ros, "/ip/pool/print", poolAttrs, 2, out.ensurePools(), errorOut,
                              errorCodeOut)) {
    return false;
  }
  if (!validateApplyTargetCatalog(out.ensurePools(), "Pool " + settings.poolName, errorOut,
                                  errorCodeOut)) {
    return false;
  }

  const String dhcpAttrs[] = {
      "=.proplist=.id,name,interface,address-pool,lease-time,comment",
      "?name=" + settings.dhcpServerName,
  };
  if (!runPrintWithAttributes(ros, "/ip/dhcp-server/print", dhcpAttrs, 2,
                              out.ensureDhcpServers(), errorOut, errorCodeOut)) {
    return false;
  }
  if (!validateApplyTargetCatalog(out.ensureDhcpServers(),
                                  "DHCP server " + settings.dhcpServerName, errorOut,
                                  errorCodeOut)) {
    return false;
  }

  const String netAttrs[] = {
      "=.proplist=.id,address,gateway,dns-server,comment",
      "?address=" + settings.guestNetwork,
  };
  if (!runPrintWithAttributes(ros, "/ip/dhcp-server/network/print", netAttrs, 2,
                              out.ensureDhcpNetworks(), errorOut, errorCodeOut)) {
    return false;
  }
  if (!validateApplyTargetCatalog(out.ensureDhcpNetworks(),
                                  "DHCP network " + settings.guestNetwork, errorOut,
                                  errorCodeOut)) {
    return false;
  }

  if (!inspectFirewallApiRules(ros, out.ensureFilterRules(), out.firewallInspectionLimited,
                               errorOut, errorCodeOut)) {
    return false;
  }
  if (out.firewallInspectionLimited) {
    errorOut     = "Firewall API rule inspection hit reply cap";
    errorCodeOut = "CONFLICT_DETECTED";
    return false;
  }

  return true;
}

void buildActionsFromInspection(const RouterProvisioning::Settings &settings,
                                const InspectionData &inspection,
                                const String &espIp, JsonArray proposedActions,
                                JsonArray conflicts, JsonArray warnings,
                                bool &canApply) {
  canApply = true;

  auto handleNamed = [&](const char *id, const char *category,
                         const RouterOsClient::CommandResult &catalog,
                         const String &name, const String &details) {
    RouterProvisioning::ProposedAction action;
    action.id = id;
    action.category = category;
    action.target = name;
    action.details = details;
    action.safe = true;
    String conflictReason;
    const ObjectState state = classifyNamedObject(catalog, name, conflictReason);
    if (state == ObjectState::Missing) {
      action.action = "create";
      action.willCreate = true;
      addAction(proposedActions, action);
      return;
    }
    if (state == ObjectState::Managed) {
      action.action = "verify";
      action.alreadyManaged = true;
      addAction(proposedActions, action);
      return;
    }
    action.action = "conflict";
    action.conflict = true;
    action.conflictReason = conflictReason;
    canApply = false;
    addAction(proposedActions, action);
    conflicts.add(conflictReason);
  };

  handleNamed("bridge", "bridge", inspection.bridgesRef(), settings.guestBridgeName,
              "Guest bridge (no ports added in this phase)");
  handleNamed("pool", "dhcp", inspection.poolsRef(), settings.poolName,
              "DHCP pool " + settings.dhcpPoolRange);
  handleNamed("dhcp-server", "dhcp", inspection.dhcpServersRef(), settings.dhcpServerName,
              "DHCP server on " + settings.guestBridgeName);

  RouterProvisioning::ProposedAction addressAction;
  addressAction.id = "guest-address";
  addressAction.category = "address";
  addressAction.target = settings.guestGatewayCidr;
  addressAction.details = "Gateway on " + settings.guestBridgeName;
  addressAction.safe = true;
  bool addressFound = false;
  for (uint8_t i = 0; i < inspection.addressesRef().replyCount; ++i) {
    String addr, comment, iface;
    replyAttr(inspection.addressesRef(), i, "address", addr);
    replyAttr(inspection.addressesRef(), i, "interface", iface);
    replyAttr(inspection.addressesRef(), i, "comment", comment);
    if (addr == settings.guestGatewayCidr && iface == settings.guestBridgeName) {
      addressFound = true;
      if (isRenzfiManagedComment(comment) || comment.isEmpty()) {
        addressAction.action = "verify";
        addressAction.alreadyManaged = true;
        if (!isRenzfiManagedComment(comment) && !comment.isEmpty()) {
          warnings.add("Guest LAN address exists with non-Renz-Fi comment — left unchanged");
        }
      } else {
        addressAction.action = "conflict";
        addressAction.conflict = true;
        addressAction.conflictReason = "Address exists with non-Renz-Fi comment";
        canApply = false;
        conflicts.add(addressAction.conflictReason);
      }
      break;
    }
  }
  if (!addressFound) {
    addressAction.action = "create";
    addressAction.willCreate = true;
  }
  addAction(proposedActions, addressAction);

  RouterProvisioning::ProposedAction dhcpNet;
  dhcpNet.id = "dhcp-network";
  dhcpNet.category = "dhcp";
  dhcpNet.target = settings.guestNetwork;
  dhcpNet.details = "DHCP network gateway/DNS " + settings.guestGateway;
  bool netFound = false;
  for (uint8_t i = 0; i < inspection.dhcpNetworksRef().replyCount; ++i) {
    String addr, comment;
    replyAttr(inspection.dhcpNetworksRef(), i, "address", addr);
    replyAttr(inspection.dhcpNetworksRef(), i, "comment", comment);
    if (addr == settings.guestNetwork) {
      netFound = true;
      if (isRenzfiManagedComment(comment)) {
        dhcpNet.action = "verify";
        dhcpNet.alreadyManaged = true;
      } else {
        dhcpNet.action = "conflict";
        dhcpNet.conflict = true;
        dhcpNet.conflictReason = "DHCP network exists without RENZFI comment";
        canApply = false;
        conflicts.add(dhcpNet.conflictReason);
      }
      break;
    }
  }
  if (!netFound) {
    dhcpNet.action = "create";
    dhcpNet.willCreate = true;
  }
  addAction(proposedActions, dhcpNet);

  RouterProvisioning::ProposedAction apiRule;
  apiRule.id = "api-access";
  apiRule.category = "firewall";
  apiRule.target = espIp + ":8728";
  bool apiRuleManaged = false;
  const bool apiRuleSatisfied =
      apiAccessRuleSatisfied(inspection.filterRulesRef(), espIp, apiRuleManaged);
  RP_LOG("[router-plan] firewall api rule satisfied=%s\n",
                apiRuleSatisfied ? "true" : "false");

  if (inspection.firewallInspectionLimited) {
    warnings.add(kFirewallInspectionLimit);
    if (!apiRuleSatisfied) {
      canApply = false;
      apiRule.action = "verify";
      apiRule.details =
          "Firewall API rule inspection hit the reply cap before a safe rule "
          "could be confirmed";
    } else if (apiRuleManaged) {
      apiRule.action = "verify";
      apiRule.alreadyManaged = true;
      apiRule.details = "Existing RENZFI input accept rule for ESP32 API access";
    } else {
      apiRule.action = "verify";
      apiRule.details = "Existing input accept rule for ESP32 API access";
    }
  } else if (apiRuleSatisfied) {
    apiRule.action = "verify";
    apiRule.alreadyManaged = apiRuleManaged;
    apiRule.details = apiRuleManaged
                          ? "Existing RENZFI input accept rule for ESP32 API access"
                          : "Existing input accept rule for ESP32 API access";
  } else {
    apiRule.action = "create";
    apiRule.willCreate = true;
    apiRule.details = RouterProvisioning::COMMENT_ESP32_API;
  }
  addAction(proposedActions, apiRule);

  addDeferredAction(
      proposedActions, "deferred-hs-server", "hotspot",
      "Deferred until a later connection/AP integration phase.");
  addDeferredAction(
      proposedActions, "deferred-bridge-port", "bridge",
      "Deferred until a later connection/AP integration phase.");
  addDeferredAction(
      proposedActions, "deferred-captive-portal", "hotspot",
      "Deferred until a later connection/AP integration phase.");
  addDeferredAction(
      proposedActions, "deferred-wireless", "wireless",
      "Deferred until a later connection/AP integration phase.");

  for (uint8_t i = 0; i < inspection.addressesRef().replyCount; ++i) {
    String addr;
    replyAttr(inspection.addressesRef(), i, "address", addr);
    if (addr == settings.guestNetwork) continue;
    if (cidrOverlaps(addr, settings.guestNetwork)) {
      canApply = false;
      conflicts.add("Guest network overlaps existing address " + addr);
    }
  }
  for (uint8_t i = 0; i < inspection.dhcpNetworksRef().replyCount; ++i) {
    String addr;
    replyAttr(inspection.dhcpNetworksRef(), i, "address", addr);
    if (addr == settings.guestNetwork) continue;
    if (cidrOverlaps(addr, settings.guestNetwork)) {
      canApply = false;
      conflicts.add("Guest network overlaps existing DHCP network " + addr);
    }
  }

  warnings.add(
      "Phase 3 creates guest bridge + DHCP foundation only. Hotspot, walled "
      "garden, wireless, and bridge ports are deferred until client-facing "
      "interface/AP integration.");
}

bool planCanApply(const RouterProvisioning::Settings &settings,
                  const InspectionData &inspection, const String &espIp,
                  JsonArray *conflictsOut) {
  HeapJsonDocument heapDoc(RenzFiConfig::JSON_DOC_MEDIUM);
  DynamicJsonDocument &doc = heapDoc.doc();
  JsonArray proposed = doc.createNestedArray("proposedActions");
  JsonArray conflicts = doc.createNestedArray("conflicts");
  JsonArray warnings = doc.createNestedArray("warnings");
  bool canApply = true;
  buildActionsFromInspection(settings, inspection, espIp, proposed, conflicts,
                             warnings, canApply);
  if (conflictsOut) {
    for (JsonVariant v : conflicts) conflictsOut->add(v.as<const char *>());
  }
  return canApply;
}

}  // namespace

void RouterProvisioningManager::begin(StorageManager *storage,
                                      InstallationStateManager *installation,
                                      SetupRouterConnectionManager *routerConnection,
                                      EthernetManager *eth) {
  _storage = storage;
  _installation = installation;
  _routerConnection = routerConnection;
  _eth = eth;
  load();
}

bool RouterProvisioningManager::wifiSetupComplete() const {
  if (!_wifiSelectionConfigured) return false;
  if (_externalApOnly || _wifiMode == RouterWireless::kModeExternalAp) {
    return true;
  }
  if (_wifiMode == RouterWireless::kModeNew) {
    return !_wifiSsid.isEmpty();
  }
  return !_wifiInterfaceId.isEmpty();
}

void RouterProvisioningManager::clearForFactoryReset() {
  applyDefaults();
  _factoryResetQuiesced = true;
  RP_LN(
      "[router-provisioning] factory reset: RAM cleared, durable commit cancelled");
}

void RouterProvisioningManager::scheduleDeferredPersist() {
  if (_factoryResetQuiesced) return;
  // Single-slot coalesce: never stack unbounded jobs.
  if (_durableCommitPhase == DurableCommitPhase::Persisting) {
    // RAM already updated by caller; request one follow-up after current write.
    _durableNeedsReschedule = true;
    return;
  }
  if (_durableCommitPhase == DurableCommitPhase::Queued) {
    return;
  }
  _durableCommitPhase = DurableCommitPhase::Queued;
  _durableCommitError = "";
}

bool RouterProvisioningManager::wifiSelectionMatches(
    const RouterWireless::WifiSelection &selection,
    const String &selectedSsidHint) const {
  if (!_wifiSelectionConfigured) return false;
  if (_wifiMode != selection.mode) return false;
  if (selection.mode == RouterWireless::kModeExternalAp) {
    return _externalApOnly;
  }
  if (selection.mode == RouterWireless::kModeExisting) {
    if (_wifiInterfaceId != selection.interfaceId) return false;
    const String wantSsid =
        selectedSsidHint.length() > 0 ? selectedSsidHint : selection.ssid;
    return _wifiSsid == wantSsid;
  }
  return _wifiSsid == selection.ssid;
}

void RouterProvisioningManager::fillDurableCommitFields(JsonObject dataOut) const {
  dataOut["durableCommitStatus"] = durableCommitStatus();
  dataOut["wifiSelectionDurablePending"] = wifiSelectionDurablePending();
  if (_durableCommitPhase == DurableCommitPhase::Failed &&
      !_durableCommitError.isEmpty()) {
    dataOut["durableCommitError"] = _durableCommitError;
  }
}

void RouterProvisioningManager::loop() {
  // Factory reset owns this object: never write after Quiesce.
  if (_factoryResetQuiesced) return;
  // Only Queued work is started here. Failed waits for an explicit retry
  // (new saveWifiSelection / scheduleDeferredPersist) so the UI can show FAILED.
  if (_durableCommitPhase != DurableCommitPhase::Queued) return;

  _durableCommitPhase = DurableCommitPhase::Persisting;
  RP_LN("[router-provisioning] durable commit PERSISTING");
  DmaMemoryMonitor::logTrace("durable-persist-before");
  if (!persist()) {
    _durableCommitPhase = DurableCommitPhase::Failed;
    _durableNeedsReschedule = false;
    if (_durableCommitError.isEmpty()) {
      _durableCommitError = "Unable to persist router provisioning record";
    }
    Serial.printf("[router-provisioning] durable commit FAILED: %s\n",
                  _durableCommitError.c_str());
    DmaMemoryMonitor::logTrace("durable-persist-failed");
    return;
  }
  DmaMemoryMonitor::logTrace("durable-persist-after");
  // persist() settled Idle. If RAM changed mid-write, queue exactly one more.
  if (_durableNeedsReschedule) {
    _durableNeedsReschedule = false;
    _durableCommitPhase = DurableCommitPhase::Queued;
    RP_LN(
        "[router-provisioning] durable commit rescheduled after mid-write change");
    return;
  }
  RP_LN("[router-provisioning] durable commit PERSISTED");
}

const char *RouterProvisioningManager::durableCommitStatus() const {
  switch (_durableCommitPhase) {
    case DurableCommitPhase::Queued:
      return "QUEUED";
    case DurableCommitPhase::Persisting:
      return "PERSISTING";
    case DurableCommitPhase::Failed:
      return "FAILED";
    case DurableCommitPhase::Idle:
    default:
      return "PERSISTED";
  }
}

void RouterProvisioningManager::applyDefaults() {
  const auto defaults = RouterProvisioning::defaultSettings();
  _foundationApplied        = false;
  _hotspotActivated         = false;
  _clientInterfaceAttached  = false;
  _appliedAt                = 0;
  _routerIdentity           = "";
  _routerVersion            = "";
  _guestBridgeName          = defaults.guestBridgeName;
  _guestNetwork             = defaults.guestNetwork;
  _guestGateway             = defaults.guestGateway;
  _dhcpPool                 = defaults.dhcpPoolRange;
  _dhcpServerName           = defaults.dhcpServerName;
  _poolName                 = defaults.poolName;
  _networkMode              = "";
  _networkModePreference    = RouterProvisioning::NETWORK_MODE_EXISTING;
  _hotspotDetected          = false;
  _adoptedAt                = 0;
  _updatedAt                = 0;
  _schemaVersion            = SCHEMA_VERSION;
  _wifiSelectionConfigured  = false;
  _externalApOnly           = false;
  _wifiMode                 = "";
  _wifiInterfaceId          = "";
  _wifiSsid                 = "";
  _wifiPassword             = "";
  _durableCommitPhase       = DurableCommitPhase::Idle;
  _durableCommitError       = "";
  _durableNeedsReschedule   = false;
}

bool RouterProvisioningManager::migrateDocument(JsonDocument &doc) {
  bool changed = false;
  const uint16_t version = doc["schemaVersion"] | 0U;
  if (version < 2) {
    if (doc["applied"].is<bool>() && !doc["foundationApplied"].is<bool>()) {
      doc["foundationApplied"] = doc["applied"].as<bool>();
      doc.remove("applied");
    }
    doc["hotspotActivated"]        = doc["hotspotActivated"] | false;
    doc["clientInterfaceAttached"] = doc["clientInterfaceAttached"] | false;
    if (doc.containsKey("hotspotServerName")) doc.remove("hotspotServerName");
    if (doc.containsKey("hotspotProfileName")) doc.remove("hotspotProfileName");
    if (doc.containsKey("dnsName")) doc.remove("dnsName");
    doc["schemaVersion"] = SCHEMA_VERSION;
    changed              = true;
  }
  if (version < 3) {
    doc["networkMode"]           = doc["networkMode"] | "";
    doc["networkModePreference"] = doc["networkModePreference"] |
                                   RouterProvisioning::NETWORK_MODE_EXISTING;
    doc["poolName"]              = doc["poolName"] | "pool-renzfi";
    doc["hotspotDetected"]       = doc["hotspotDetected"] | false;
    doc["adoptedAt"]             = doc["adoptedAt"] | 0U;
    doc["schemaVersion"]         = SCHEMA_VERSION;
    changed                      = true;
  }
  if (version < 4) {
    doc["externalApOnly"] = doc["externalApOnly"] | false;
    doc["schemaVersion"]  = SCHEMA_VERSION;
    changed               = true;
  }
  return changed;
}

void RouterProvisioningManager::applyDocument(JsonObjectConst doc) {
  _schemaVersion           = doc["schemaVersion"] | SCHEMA_VERSION;
  _foundationApplied       = doc["foundationApplied"] | false;
  _hotspotActivated        = doc["hotspotActivated"] | false;
  _clientInterfaceAttached = doc["clientInterfaceAttached"] | false;
  _appliedAt               = doc["appliedAt"] | 0U;
  _routerIdentity          = doc["routerIdentity"] | "";
  _routerVersion           = doc["routerVersion"] | "";
  _guestBridgeName         = doc["guestBridgeName"] | "bridge-renzfi";
  _guestNetwork            = doc["guestNetwork"] | RouterProvisioning::DEFAULT_GUEST_NETWORK;
  _guestGateway            = doc["guestGateway"] | RouterProvisioning::DEFAULT_GUEST_GATEWAY;
  _dhcpPool                = doc["dhcpPool"] | RouterProvisioning::DEFAULT_DHCP_POOL_RANGE;
  _dhcpServerName          = doc["dhcpServerName"] | "dhcp-renzfi";
  _poolName                = doc["poolName"] | "pool-renzfi";
  _networkMode             = doc["networkMode"] | "";
  _networkModePreference   = doc["networkModePreference"] |
                           RouterProvisioning::NETWORK_MODE_EXISTING;
  _hotspotDetected         = doc["hotspotDetected"] | false;
  _adoptedAt               = doc["adoptedAt"] | 0U;
  _updatedAt               = doc["updatedAt"] | 0U;
  _wifiSelectionConfigured = doc["wifiSelectionConfigured"] | false;
  _externalApOnly          = doc["externalApOnly"] | false;
  _wifiMode                = doc["wifiMode"] | "";
  _wifiInterfaceId         = doc["wifiInterfaceId"] | "";
  _wifiSsid                = doc["wifiSsid"] | "";
  _wifiPassword            = doc["wifiPassword"] | "";
}

void RouterProvisioningManager::buildDocument(JsonDocument &doc) const {
  doc.clear();
  doc["schemaVersion"]           = _schemaVersion;
  doc["foundationApplied"]       = _foundationApplied;
  doc["hotspotActivated"]        = _hotspotActivated;
  doc["clientInterfaceAttached"] = _clientInterfaceAttached;
  doc["appliedAt"]               = _appliedAt;
  doc["routerIdentity"]          = _routerIdentity;
  doc["routerVersion"]           = _routerVersion;
  doc["guestBridgeName"]         = _guestBridgeName;
  doc["guestNetwork"]            = _guestNetwork;
  doc["guestGateway"]            = _guestGateway;
  doc["dhcpPool"]                = _dhcpPool;
  doc["dhcpServerName"]          = _dhcpServerName;
  doc["poolName"]                = _poolName;
  doc["networkMode"]             = _networkMode;
  doc["networkModePreference"]   = _networkModePreference;
  doc["hotspotDetected"]         = _hotspotDetected;
  doc["adoptedAt"]               = _adoptedAt;
  doc["updatedAt"]               = _updatedAt;
  doc["wifiSelectionConfigured"] = _wifiSelectionConfigured;
  doc["externalApOnly"]          = _externalApOnly;
  doc["wifiMode"]                = _wifiMode;
  doc["wifiInterfaceId"]         = _wifiInterfaceId;
  doc["wifiSsid"]                = _wifiSsid;
  doc["wifiPassword"]            = _wifiPassword;
  if (!_wifiInterfaceId.isEmpty()) {
    doc["selectedWirelessInterface"] = _wifiInterfaceId;
  }
  doc.createNestedArray("createdObjectIds");
}

bool RouterProvisioningManager::load() {
  applyDefaults();
  if (!_storage) return false;
  if (!_storage->exists(StoragePaths::RouterProvisioningFile)) {
    RP_LN(F("[router-provisioning] first-run defaults (no persistence file)"));
    return true;
  }
  DynamicJsonDocument doc(kPersistDocCapacity);
  if (!_storage->readJson(StoragePaths::RouterProvisioningFile, doc)) {
    RP_LN(F("[router-provisioning] persistence unreadable, using defaults"));
    return true;
  }
  if (migrateDocument(doc)) {
    applyDocument(doc.as<JsonObjectConst>());
    if (!_externalApOnly && _foundationApplied && _wifiInterfaceId.isEmpty() &&
        isExistingNetworkAdopted() && _installation && _installation->isReady()) {
      _externalApOnly = true;
      if (_wifiMode.isEmpty()) {
        _wifiMode = RouterWireless::kModeExternalAp;
      }
      if (!_wifiSelectionConfigured) {
        _wifiSelectionConfigured = true;
      }
    }
    persist();
    return true;
  }
  applyDocument(doc.as<JsonObjectConst>());
  if (!_externalApOnly && _foundationApplied && _wifiInterfaceId.isEmpty() &&
      isExistingNetworkAdopted() && _installation && _installation->isReady()) {
    _externalApOnly = true;
    if (_wifiMode.isEmpty()) {
      _wifiMode = RouterWireless::kModeExternalAp;
    }
    if (!_wifiSelectionConfigured) {
      _wifiSelectionConfigured = true;
    }
  }
  return true;
}

bool RouterProvisioningManager::persist() {
  if (_factoryResetQuiesced) {
    RP_LN("[router-provisioning] persist suppressed (factory reset)");
    return false;
  }
  if (!_storage) return false;
  // CPU JSON — PSRAM. Must not occupy INTERNAL/DMA while SoftAP requests 1624.
  PsramJsonDocument heap;
  JsonDocument &doc = heap.doc();
  buildDocument(doc);
  _updatedAt = millis();
  doc["updatedAt"] = _updatedAt;
  if (!_storage->writeJson(StoragePaths::RouterProvisioningFile, doc)) {
    if (_durableCommitError.isEmpty()) {
      _durableCommitError = "Unable to persist router provisioning record";
    }
    return false;
  }
  // Any successful durable write (worker or loop) settles the deferred phase.
  _durableCommitPhase = DurableCommitPhase::Idle;
  _durableCommitError = "";
  return true;
}

void RouterProvisioningManager::fillDefaults(JsonObject defaults) const {
  const auto d = RouterProvisioning::defaultSettings();
  defaults["guestBridgeName"] = d.guestBridgeName;
  defaults["guestNetwork"] = d.guestNetwork;
  defaults["guestGateway"] = d.guestGateway;
  defaults["guestGatewayCidr"] = d.guestGatewayCidr;
  defaults["dhcpPoolRange"] = d.dhcpPoolRange;
  defaults["dhcpLeaseTime"] = d.dhcpLeaseTime;
  defaults["poolName"] = d.poolName;
  defaults["dhcpServerName"] = d.dhcpServerName;
  JsonObject deferred = defaults.createNestedObject("deferredHotspotDefaults");
  deferred["hotspotServerName"] = d.hotspotServerName;
  deferred["hotspotProfileName"] = d.hotspotProfileName;
  deferred["dnsName"] = d.dnsName;
  deferred["guestSsid"] = d.guestSsid;
}

RouterProvisioningManager::OperationResult
RouterProvisioningManager::ensurePreconditions() const {
  const auto pre = RouterProvisioningPreconditions::check(_installation,
                                                          _routerConnection,
                                                          _eth);
  OperationResult result;
  result.success      = pre.success;
  result.httpStatus    = pre.httpStatus;
  result.errorCode     = pre.errorCode;
  result.errorMessage  = pre.errorMessage;
  return result;
}

bool RouterProvisioningManager::parseSettings(JsonObjectConst body,
                                              RouterProvisioning::Settings &out,
                                              String &errorOut) const {
  out = RouterProvisioning::defaultSettings();
  if (!body.isNull()) {
    if (body["guestBridgeName"].is<const char *>())
      out.guestBridgeName = body["guestBridgeName"].as<const char *>();
    if (body["guestNetwork"].is<const char *>())
      out.guestNetwork = body["guestNetwork"].as<const char *>();
    if (body["guestGateway"].is<const char *>())
      out.guestGateway = body["guestGateway"].as<const char *>();
    if (body["guestGatewayCidr"].is<const char *>())
      out.guestGatewayCidr = body["guestGatewayCidr"].as<const char *>();
    if (body["dhcpPoolRange"].is<const char *>())
      out.dhcpPoolRange = body["dhcpPoolRange"].as<const char *>();
    if (body["dhcpLeaseTime"].is<const char *>())
      out.dhcpLeaseTime = body["dhcpLeaseTime"].as<const char *>();
    if (body["poolName"].is<const char *>())
      out.poolName = body["poolName"].as<const char *>();
    if (body["dhcpServerName"].is<const char *>())
      out.dhcpServerName = body["dhcpServerName"].as<const char *>();
  }
  out.guestBridgeName.trim();
  out.guestNetwork.trim();
  out.guestGateway.trim();
  if (out.guestGatewayCidr.isEmpty()) out.guestGatewayCidr = out.guestGateway + "/24";
  if (!isValidObjectName(out.guestBridgeName) || !isValidObjectName(out.poolName) ||
      !isValidObjectName(out.dhcpServerName)) {
    errorOut = "Object names may contain only letters, numbers, dash, and underscore";
    return false;
  }
  IPAddress net;
  uint8_t prefix = 0;
  if (!parseIpv4Prefix(out.guestNetwork, net, prefix)) {
    errorOut = "Guest network must be a valid IPv4 CIDR";
    return false;
  }
  if (!dhcpRangeInNetwork(out.dhcpPoolRange, out.guestNetwork)) {
    errorOut = "DHCP pool must belong to the guest subnet";
    return false;
  }
  String subnetError;
  if (!validateGuestSubnetLocal(out, _eth, &subnetError, nullptr)) {
    errorOut = subnetError;
    return false;
  }
  return true;
}

RouterProvisioningManager::OperationResult
RouterProvisioningManager::ensureLocalPreviewPreconditions() const {
  OperationResult result;
  if (!_installation ||
      _installation->current() != InstallationState::RouterConfigured) {
    result.httpStatus = 403;
    result.errorCode = "ROUTER_CONFIGURE_REQUIRED";
    result.errorMessage =
        "Installation must be router_configured before router provisioning";
    return result;
  }
  if (!_routerConnection || !_routerConnection->hasVerifiedConnection()) {
    result.httpStatus = 409;
    result.errorCode = "ROUTER_CONNECTION_REQUIRED";
    result.errorMessage = "Saved MikroTik connection is unavailable";
    return result;
  }
  result.success = true;
  result.httpStatus = 200;
  return result;
}

RouterProvisioningManager::OperationResult RouterProvisioningManager::buildLocalPlan(
    JsonObject dataOut, JsonObjectConst settingsBody) {
  RP_LN(F("[router-plan] local preview start"));

  OperationResult result = ensureLocalPreviewPreconditions();
  if (!result.success) {
    RP_LOG("[router-plan] local preview rejected code=%s\n",
                  result.errorCode.c_str());
    return result;
  }

  RouterProvisioning::Settings settings;
  String settingsError;
  if (!parseSettings(settingsBody, settings, settingsError)) {
    markRouterPlanFailed(result);
    result.errorCode = "INVALID_SETTINGS";
    result.errorMessage = settingsError;
    return result;
  }

  const String espIp =
      (_eth && _eth->hasIp()) ? _eth->ip() : String("Unavailable");

  dataOut["previewMode"]     = "local";
  dataOut["routerIdentity"]  = "Not checked in local preview";
  dataOut["routerVersion"]   = "Not checked in local preview";
  dataOut["currentEspIp"]    = espIp;
  dataOut["foundationApplied"] = _foundationApplied;
  dataOut["hotspotActivated"] = _hotspotActivated;
  dataOut["clientInterfaceAttached"] = _clientInterfaceAttached;

  String subnetError;
  String routerSubnetWarning;
  const bool guestSubnetOk =
      validateGuestSubnetLocal(settings, _eth, &subnetError, &routerSubnetWarning);
  dataOut["canApply"] = guestSubnetOk;

  JsonObject connection = dataOut.createNestedObject("routerConnection");
  connection["host"]     = _routerConnection->host();
  connection["apiPort"]  = _routerConnection->apiPort();
  connection["username"] = _routerConnection->username();

  fillDefaults(dataOut.createNestedObject("defaults"));

  JsonArray proposed = dataOut.createNestedArray("proposedActions");
  JsonArray warnings = dataOut.createNestedArray("warnings");
  JsonArray safetySummary = dataOut.createNestedArray("safetySummary");
  appendLocalSafetySummary(safetySummary);
  buildStaticLocalPlan(settings, espIp, proposed, warnings);

  if (!guestSubnetOk && !subnetError.isEmpty()) {
    warnings.add(subnetError);
  }
  if (!routerSubnetWarning.isEmpty()) {
    warnings.add(routerSubnetWarning);
  }

  result.success      = true;
  result.httpStatus   = 200;
  result.errorMessage = "Local router provisioning plan";
  RP_LN(F("[router-plan] local preview complete"));
  return result;
}

RouterProvisioningManager::OperationResult
RouterProvisioningManager::applyConfiguration(JsonObjectConst settingsBody,
                                              JsonObjectConst confirmationBody,
                                              JsonObject dataOut,
                                              JsonObject errorDataOut) {
  OperationResult result = ensurePreconditions();
  if (!result.success) return result;

  const char *confirmation = confirmationBody["confirmation"] | "";
  if (strcmp(confirmation, RouterProvisioning::APPLY_CONFIRMATION) != 0) {
    markRouterPlanFailed(result);
    result.httpStatus = 400;
    result.errorCode = kConfirmationRequired;
    result.errorMessage = "Confirmation text must match exactly";
    return result;
  }

  RouterProvisioning::Settings settings;
  String settingsError;
  JsonObjectConst settingsObj =
      settingsBody["settings"].is<JsonObjectConst>()
          ? settingsBody["settings"].as<JsonObjectConst>()
          : settingsBody;
  if (!parseSettings(settingsObj, settings, settingsError)) {
    markRouterPlanFailed(result);
    result.errorCode = "INVALID_SETTINGS";
    result.errorMessage = settingsError;
    return result;
  }

  SetupRouterConnectionManager::ResolvedRouterCredentials credentials;
  SetupRouterConnectionManager::OperationResult credResult;
  if (!_routerConnection->resolveRouterCredentials(
          SetupRouterConnectionManager::RouterCredentialSource::Persisted, nullptr,
          credentials, credResult)) {
    markRouterPlanFailed(result);
    result.httpStatus = credResult.httpStatus ? credResult.httpStatus : 409;
    result.errorCode = credResult.errorCode.isEmpty() ? "ROUTER_CONNECTION_REQUIRED"
                                                      : credResult.errorCode;
    result.errorMessage = credResult.errorMessage.isEmpty()
                              ? "Unable to load saved router credentials"
                              : credResult.errorMessage;
    return result;
  }
  const SetupRouterConnectionManager::RouterInput sessionInput =
      credentials.toRouterInput();

  RouterSession *session = allocRouterSession(_eth);
  if (!session) {
    markRouterPlanFailed(result);
    result.httpStatus = 503;
    result.errorCode = "ROUTER_PLAN_UNAVAILABLE";
    result.errorMessage = "Router provisioning temporarily unavailable";
    return result;
  }

  String sessionError;
  String sessionErrorCode;
  if (!session->open(sessionInput, sessionError, sessionErrorCode)) {
    freeRouterSession(session);
    markRouterPlanFailed(result);
    result.httpStatus = isLoginFailureCode(sessionErrorCode) ? 401 : 503;
    result.errorCode = sessionErrorCode == "TCP_CONNECT_FAILED"
                           ? "ROUTER_UNREACHABLE"
                           : (sessionErrorCode.isEmpty() ? "ROUTEROS_API_UNAVAILABLE"
                                                         : sessionErrorCode);
    result.errorMessage = sessionError.isEmpty() ? "Unable to connect to router"
                                                 : sessionError;
    return result;
  }

  InspectionDataPtr inspection = allocInspectionData();
  if (!inspection) {
    freeRouterSession(session);
    result.httpStatus = 503;
    result.errorCode = "ROUTER_PLAN_UNAVAILABLE";
    result.errorMessage = "Router provisioning temporarily unavailable";
    return result;
  }

  String inspectErrorCode;
  if (!inspectApplyTargets(*session, settings, *inspection, sessionError,
                           inspectErrorCode)) {
    freeInspectionData(inspection);
    freeRouterSession(session);
    result.httpStatus = inspectErrorCode == "CONFLICT_DETECTED" ? 409 : 503;
    result.errorCode = inspectErrorCode.isEmpty() ? "ROUTER_INSPECTION_FAILED"
                                                  : inspectErrorCode;
    result.errorMessage = sessionError.isEmpty() ? "Router apply preflight failed"
                                                 : sessionError;
    result.stage = "apply-preflight";
    return result;
  }

  JsonArray completed = dataOut.createNestedArray("completedActions");
  // Heap-allocated CommandResult keeps the worker stack frame small during apply.
  std::unique_ptr<RouterOsClient::CommandResult> cmdResultPtr(
      new (std::nothrow) RouterOsClient::CommandResult());
  if (!cmdResultPtr) {
    result.httpStatus = 503;
    result.errorCode = "ROUTER_PLAN_UNAVAILABLE";
    result.errorMessage = "Router provisioning temporarily unavailable";
    freeInspectionData(inspection);
    freeRouterSession(session);
    return result;
  }
  RouterOsClient::CommandResult &cmdResult = *cmdResultPtr;

  if (inspection->bridgesRef().replyCount == 0) {
    const String bridgeAttrs[] = {
        "=name=" + settings.guestBridgeName,
        "=comment=" + String(RouterProvisioning::COMMENT_GUEST_BRIDGE),
    };
    if (!session->client().executeCommand("/interface/bridge/add", bridgeAttrs, 2,
                                          cmdResult) ||
        cmdResult.trapReceived || cmdResult.fatalReceived) {
      errorDataOut["failedAction"] = "bridge";
      result.httpStatus            = 500;
      result.errorCode             = "APPLY_FAILED";
      result.errorMessage          = cmdResult.trapMessage;
      freeInspectionData(inspection);
      freeRouterSession(session);
      return result;
    }
  } else {
    String bridgeId;
    String bridgeComment;
    replyAttr(inspection->bridgesRef(), 0, ".id", bridgeId);
    replyAttr(inspection->bridgesRef(), 0, "comment", bridgeComment);
    if (bridgeComment != RouterProvisioning::COMMENT_GUEST_BRIDGE) {
      const String setAttrs[] = {
          "=.id=" + bridgeId,
          "=comment=" + String(RouterProvisioning::COMMENT_GUEST_BRIDGE),
      };
      if (!session->client().executeCommand("/interface/bridge/set", setAttrs, 2,
                                            cmdResult) ||
          cmdResult.trapReceived || cmdResult.fatalReceived) {
        errorDataOut["failedAction"] = "bridge";
        result.httpStatus            = 500;
        result.errorCode             = "APPLY_FAILED";
        result.errorMessage          = cmdResult.trapMessage;
        freeInspectionData(inspection);
        freeRouterSession(session);
        return result;
      }
    }
  }
  completed.add("bridge");

  if (inspection->addressesRef().replyCount == 0) {
    const String attrs[] = {
        "=address=" + settings.guestGatewayCidr,
        "=interface=" + settings.guestBridgeName,
        "=comment=" + String(RouterProvisioning::COMMENT_GUEST_LAN),
    };
    if (!session->client().executeCommand("/ip/address/add", attrs, 3, cmdResult) ||
        cmdResult.trapReceived) {
      errorDataOut["failedAction"] = "guest-address";
      result.httpStatus = 500;
      result.errorCode = "APPLY_FAILED";
      result.errorMessage = cmdResult.trapMessage;
      freeInspectionData(inspection);
      freeRouterSession(session);
      return result;
    }
  } else {
    String addrId, iface, comment;
    replyAttr(inspection->addressesRef(), 0, ".id", addrId);
    replyAttr(inspection->addressesRef(), 0, "interface", iface);
    replyAttr(inspection->addressesRef(), 0, "comment", comment);
    if (iface != settings.guestBridgeName ||
        comment != RouterProvisioning::COMMENT_GUEST_LAN) {
      const String setAttrs[] = {
          "=.id=" + addrId,
          "=interface=" + settings.guestBridgeName,
          "=comment=" + String(RouterProvisioning::COMMENT_GUEST_LAN),
      };
      if (!session->client().executeCommand("/ip/address/set", setAttrs, 3, cmdResult) ||
          cmdResult.trapReceived || cmdResult.fatalReceived) {
        errorDataOut["failedAction"] = "guest-address";
        result.httpStatus            = 500;
        result.errorCode             = "APPLY_FAILED";
        result.errorMessage          = cmdResult.trapMessage;
        freeInspectionData(inspection);
        freeRouterSession(session);
        return result;
      }
    }
  }
  completed.add("guest-address");

  if (inspection->poolsRef().replyCount == 0) {
    const String poolAttrs[] = {
        "=name=" + settings.poolName,
        "=ranges=" + settings.dhcpPoolRange,
        "=comment=" + String(RouterProvisioning::COMMENT_GUEST_POOL),
    };
    if (!session->client().executeCommand("/ip/pool/add", poolAttrs, 3, cmdResult) ||
        cmdResult.trapReceived || cmdResult.fatalReceived) {
      errorDataOut["failedAction"] = "pool";
      result.httpStatus            = 500;
      result.errorCode             = "APPLY_FAILED";
      result.errorMessage          = cmdResult.trapMessage;
      freeInspectionData(inspection);
      freeRouterSession(session);
      return result;
    }
  } else {
    String poolId, ranges, comment;
    replyAttr(inspection->poolsRef(), 0, ".id", poolId);
    replyAttr(inspection->poolsRef(), 0, "ranges", ranges);
    replyAttr(inspection->poolsRef(), 0, "comment", comment);
    if (ranges != settings.dhcpPoolRange ||
        comment != RouterProvisioning::COMMENT_GUEST_POOL) {
      const String setAttrs[] = {
          "=.id=" + poolId,
          "=ranges=" + settings.dhcpPoolRange,
          "=comment=" + String(RouterProvisioning::COMMENT_GUEST_POOL),
      };
      if (!session->client().executeCommand("/ip/pool/set", setAttrs, 3, cmdResult) ||
          cmdResult.trapReceived || cmdResult.fatalReceived) {
        errorDataOut["failedAction"] = "pool";
        result.httpStatus            = 500;
        result.errorCode             = "APPLY_FAILED";
        result.errorMessage          = cmdResult.trapMessage;
        freeInspectionData(inspection);
        freeRouterSession(session);
        return result;
      }
    }
  }
  completed.add("pool");

  if (inspection->dhcpServersRef().replyCount == 0) {
    const String dhcpAttrs[] = {
        "=name=" + settings.dhcpServerName,
        "=interface=" + settings.guestBridgeName,
        "=address-pool=" + settings.poolName,
        "=lease-time=" + settings.dhcpLeaseTime,
        "=comment=" + String(RouterProvisioning::COMMENT_GUEST_DHCP_SERVER),
    };
    if (!session->client().executeCommand("/ip/dhcp-server/add", dhcpAttrs, 5, cmdResult) ||
        cmdResult.trapReceived || cmdResult.fatalReceived) {
      errorDataOut["failedAction"] = "dhcp-server";
      result.httpStatus            = 500;
      result.errorCode             = "APPLY_FAILED";
      result.errorMessage          = cmdResult.trapMessage;
      freeInspectionData(inspection);
      freeRouterSession(session);
      return result;
    }
  } else {
    String dhcpId, iface, addrPool, leaseTime, comment;
    replyAttr(inspection->dhcpServersRef(), 0, ".id", dhcpId);
    replyAttr(inspection->dhcpServersRef(), 0, "interface", iface);
    replyAttr(inspection->dhcpServersRef(), 0, "address-pool", addrPool);
    replyAttr(inspection->dhcpServersRef(), 0, "lease-time", leaseTime);
    replyAttr(inspection->dhcpServersRef(), 0, "comment", comment);
    if (iface != settings.guestBridgeName || addrPool != settings.poolName ||
        leaseTime != settings.dhcpLeaseTime ||
        comment != RouterProvisioning::COMMENT_GUEST_DHCP_SERVER) {
      const String setAttrs[] = {
          "=.id=" + dhcpId,
          "=interface=" + settings.guestBridgeName,
          "=address-pool=" + settings.poolName,
          "=lease-time=" + settings.dhcpLeaseTime,
          "=comment=" + String(RouterProvisioning::COMMENT_GUEST_DHCP_SERVER),
      };
      if (!session->client().executeCommand("/ip/dhcp-server/set", setAttrs, 5,
                                            cmdResult) ||
          cmdResult.trapReceived || cmdResult.fatalReceived) {
        errorDataOut["failedAction"] = "dhcp-server";
        result.httpStatus            = 500;
        result.errorCode             = "APPLY_FAILED";
        result.errorMessage          = cmdResult.trapMessage;
        freeInspectionData(inspection);
        freeRouterSession(session);
        return result;
      }
    }
  }
  completed.add("dhcp-server");

  if (inspection->dhcpNetworksRef().replyCount == 0) {
    const String attrs[] = {
        "=address=" + settings.guestNetwork,
        "=gateway=" + settings.guestGateway,
        "=dns-server=" + settings.guestGateway,
        "=comment=" + String(RouterProvisioning::COMMENT_GUEST_DHCP_NETWORK),
    };
    if (!session->client().executeCommand("/ip/dhcp-server/network/add", attrs, 4,
                                         cmdResult) ||
        cmdResult.trapReceived) {
      errorDataOut["failedAction"] = "dhcp-network";
      result.httpStatus = 500;
      result.errorCode = "APPLY_FAILED";
      result.errorMessage = cmdResult.trapMessage;
      freeInspectionData(inspection);
      freeRouterSession(session);
      return result;
    }
  } else {
    String netId, gateway, dns, comment;
    replyAttr(inspection->dhcpNetworksRef(), 0, ".id", netId);
    replyAttr(inspection->dhcpNetworksRef(), 0, "gateway", gateway);
    replyAttr(inspection->dhcpNetworksRef(), 0, "dns-server", dns);
    replyAttr(inspection->dhcpNetworksRef(), 0, "comment", comment);
    if (gateway != settings.guestGateway || dns != settings.guestGateway ||
        comment != RouterProvisioning::COMMENT_GUEST_DHCP_NETWORK) {
      const String setAttrs[] = {
          "=.id=" + netId,
          "=gateway=" + settings.guestGateway,
          "=dns-server=" + settings.guestGateway,
          "=comment=" + String(RouterProvisioning::COMMENT_GUEST_DHCP_NETWORK),
      };
      if (!session->client().executeCommand("/ip/dhcp-server/network/set", setAttrs,
                                            4, cmdResult) ||
          cmdResult.trapReceived || cmdResult.fatalReceived) {
        errorDataOut["failedAction"] = "dhcp-network";
        result.httpStatus            = 500;
        result.errorCode             = "APPLY_FAILED";
        result.errorMessage          = cmdResult.trapMessage;
        freeInspectionData(inspection);
        freeRouterSession(session);
        return result;
      }
    }
  }
  completed.add("dhcp-network");

  if (!apiAccessRuleExists(inspection->filterRulesRef(), _eth->ip())) {
    const String attrs[] = {
        "=chain=input",
        "=action=accept",
        "=protocol=tcp",
        "=dst-port=8728",
        "=src-address=" + _eth->ip(),
        "=comment=" + String(RouterProvisioning::COMMENT_ESP32_API),
    };
    if (!session->client().executeCommand("/ip/firewall/filter/add", attrs, 6,
                                         cmdResult) ||
        cmdResult.trapReceived) {
      errorDataOut["failedAction"] = "api-access";
      result.httpStatus = 500;
      result.errorCode = "APPLY_FAILED";
      result.errorMessage = cmdResult.trapMessage;
      freeInspectionData(inspection);
      freeRouterSession(session);
      return result;
    }
  } else {
    for (uint8_t i = 0; i < inspection->filterRulesRef().replyCount; ++i) {
      String chain, protocol, dstPort, action, disabled, srcAddress, id;
      replyAttr(inspection->filterRulesRef(), i, ".id", id);
      replyAttr(inspection->filterRulesRef(), i, "chain", chain);
      replyAttr(inspection->filterRulesRef(), i, "protocol", protocol);
      replyAttr(inspection->filterRulesRef(), i, "dst-port", dstPort);
      replyAttr(inspection->filterRulesRef(), i, "action", action);
      replyAttr(inspection->filterRulesRef(), i, "disabled", disabled);
      replyAttr(inspection->filterRulesRef(), i, "src-address", srcAddress);
      if (chain == "input" && protocol == "tcp" && dstPort == "8728" &&
          action == "accept" && disabled != "true" &&
          (srcAddress.isEmpty() || srcAddress == _eth->ip())) {
        if (srcAddress != _eth->ip()) {
          const String setAttrs[] = {
              "=.id=" + id,
              "=src-address=" + _eth->ip(),
              "=comment=" + String(RouterProvisioning::COMMENT_ESP32_API),
          };
          if (!session->client().executeCommand("/ip/firewall/filter/set", setAttrs,
                                                3, cmdResult) ||
              cmdResult.trapReceived || cmdResult.fatalReceived) {
            errorDataOut["failedAction"] = "api-access";
            result.httpStatus            = 500;
            result.errorCode             = "APPLY_FAILED";
            result.errorMessage          = cmdResult.trapMessage;
            freeInspectionData(inspection);
            freeRouterSession(session);
            return result;
          }
        }
        break;
      }
    }
  }
  completed.add("api-access");

  _foundationApplied       = true;
  _hotspotActivated        = false;
  _clientInterfaceAttached = false;
  _appliedAt               = millis();
  _networkMode             = RouterProvisioning::NETWORK_MODE_CREATE_NEW;
  _adoptedAt               = 0;
  _guestBridgeName         = settings.guestBridgeName;
  _guestNetwork            = settings.guestNetwork;
  _guestGateway            = settings.guestGateway;
  _dhcpPool                = settings.dhcpPoolRange;
  _dhcpServerName          = settings.dhcpServerName;
  _poolName                = settings.poolName;
  persist();

  freeInspectionData(inspection);
  freeRouterSession(session);

  if (_installation &&
      _installation->current() == InstallationState::RouterConfigured) {
    if (!_installation->advanceTo(InstallationState::Provisioned)) {
      result.httpStatus = 500;
      result.errorCode = "STATE_SYNC_FAILED";
      result.errorMessage = "Router configured but installation state update failed";
      return result;
    }
    RP_LOG("[setup] installation state synchronized: %s\n",
                  installationStateLabel(_installation->current()));
  }

  dataOut["routerIdentity"] = _routerIdentity;
  dataOut["routerVersion"] = _routerVersion;
  dataOut["installationState"] =
      installationStateLabel(_installation->current());
  dataOut["foundationApplied"]       = true;
  dataOut["hotspotActivated"]        = false;
  dataOut["clientInterfaceAttached"] = false;
  result.success = true;
  result.httpStatus = 200;
  result.errorMessage = "MikroTik guest-network foundation saved";
  RP_LN("[setup] router foundation apply complete (Hotspot deferred)");
  return result;
}

RouterProvisioningManager::OperationResult
RouterProvisioningManager::saveWifiSelection(JsonObjectConst body,
                                             JsonObject dataOut) {
  OperationResult result = ensurePreconditions();
  if (!result.success) return result;

  RouterWireless::WifiSelection selection;
  String parseError;
  if (!RouterWireless::parseWifiSelection(body, selection, parseError)) {
    markRouterPlanFailed(result);
    result.httpStatus   = 400;
    result.errorCode    = "INVALID_WIFI_SELECTION";
    result.errorMessage = parseError;
    return result;
  }

  const char *selectedSsidRaw = body["selectedSsid"] | "";
  const String selectedSsidHint = selectedSsidRaw;

  // Duplicate coalesce: identical selection already durable → do not re-queue.
  if (wifiSelectionMatches(selection, selectedSsidHint) &&
      _durableCommitPhase == DurableCommitPhase::Idle) {
    dataOut["wifiSelectionConfigured"] = true;
    dataOut["wifiMode"]                = _wifiMode;
    fillDurableCommitFields(dataOut);
    if (_wifiMode == RouterWireless::kModeExisting) {
      dataOut["interfaceId"] = _wifiInterfaceId;
    } else {
      dataOut["ssid"] = _wifiSsid;
    }
    result.success      = true;
    result.httpStatus   = 200;
    result.errorMessage = "Wi-Fi selection already persisted";
    return result;
  }

  // Identical selection already QUEUED/PERSISTING → coalesce; no second job.
  if (wifiSelectionMatches(selection, selectedSsidHint) &&
      wifiSelectionDurablePending()) {
    dataOut["wifiSelectionConfigured"] = true;
    dataOut["wifiMode"]                = _wifiMode;
    fillDurableCommitFields(dataOut);
    if (_wifiMode == RouterWireless::kModeExisting) {
      dataOut["interfaceId"] = _wifiInterfaceId;
    } else {
      dataOut["ssid"] = _wifiSsid;
    }
    result.success      = true;
    result.httpStatus   = 202;
    result.errorMessage = "Wi-Fi selection persistence already in progress";
    return result;
  }

  _wifiSelectionConfigured = true;
  _wifiMode                = selection.mode;
  _wifiInterfaceId         = selection.interfaceId;
  _wifiSsid                = selection.ssid;
  if (selection.mode == RouterWireless::kModeExternalAp) {
    _externalApOnly    = true;
    _wifiInterfaceId   = "";
    _wifiSsid          = "";
    _wifiPassword      = "";
  } else if (_wifiMode == RouterWireless::kModeExisting) {
    if (selectedSsidHint.length() > 0) _wifiSsid = selectedSsidHint;
  }
  _wifiPassword = selection.password;

  // Execution boundary: no StorageManager::writeJson on async_tcp.
  // loopTask durable commit writes the provisioning record.
  scheduleDeferredPersist();

  dataOut["wifiSelectionConfigured"] = true;
  dataOut["wifiMode"]                = _wifiMode;
  fillDurableCommitFields(dataOut);
  if (_wifiMode == RouterWireless::kModeExisting) {
    dataOut["interfaceId"] = _wifiInterfaceId;
  } else {
    dataOut["ssid"] = _wifiSsid;
  }

  result.success      = true;
  result.httpStatus   = 202;
  result.errorMessage = "Wi-Fi selection QUEUED for durable persistence";
  return result;
}

RouterProvisioningManager::OperationResult
RouterProvisioningManager::configureExistingNetwork(JsonObjectConst body,
                                                    JsonObject dataOut,
                                                    RouterOsClient *routerClient,
                                                    RouterProvisioningEngine *engine) {
  OperationResult result = ensurePreconditions();
  if (!result.success) return result;

  const char *confirmation = body["confirmation"] | "";
  if (strcmp(confirmation, RouterProvisioning::ADOPT_CONFIRMATION) != 0) {
    markRouterPlanFailed(result);
    result.httpStatus = 400;
    result.errorCode = kConfirmationRequired;
    result.errorMessage = "Confirmation text must match exactly";
    return result;
  }

  ExistingNetworkScan::Candidate candidate;
  String verifyError;
  if (!ExistingNetworkScan::parseAdoptionCandidate(body, candidate, verifyError)) {
    markRouterPlanFailed(result);
    result.httpStatus   = 400;
    result.errorCode    = "INVALID_ADOPTION_PAYLOAD";
    result.errorMessage = verifyError.isEmpty()
                              ? "Adoption payload missing required network fields"
                              : verifyError;
    return result;
  }

  if (!(body["confirmAllowed"] | false)) {
    markRouterPlanFailed(result);
    result.httpStatus   = 409;
    result.errorCode    = "CONFIGURE_NOT_ALLOWED";
    result.errorMessage = "Selected network is not eligible for configuration";
    return result;
  }

  RouterWireless::WifiSelection wifiSelection;
  String wifiError;
  const bool externalApOnly =
      (body["externalApOnly"] | false) ||
      strcmp(body["wifiMode"] | "", RouterWireless::kModeExternalAp) == 0 ||
      _externalApOnly;

  if (externalApOnly) {
    wifiSelection.mode = RouterWireless::kModeExternalAp;
    _externalApOnly    = true;
  } else if (!RouterWireless::parseWifiSelection(body, wifiSelection, wifiError)) {
    markRouterPlanFailed(result);
    result.httpStatus   = 400;
    result.errorCode    = "INVALID_WIFI_SELECTION";
    result.errorMessage = wifiError;
    return result;
  }

  String wirelessIface = wifiSelection.interfaceId;
  String ssidPolicy    = "keep";
  String targetSsid    = wifiSelection.ssid;
  if (!externalApOnly && wifiSelection.mode == RouterWireless::kModeNew) {
    wirelessIface = "renzfi-wifi";
    ssidPolicy    = "rename";
  } else if (!externalApOnly && !wirelessIface.isEmpty()) {
    // SoftAP + wireless/print needs contiguous INTERNAL DMA; wait briefly.
    (void)DmaMemoryMonitor::waitForRouterOsConnectHeadroom(1500);
    std::unique_ptr<RouterOsClient::CommandResult> wirelessPtr(
        new (std::nothrow) RouterOsClient::CommandResult());
    RouterOsClient::CommandResult *wireless = wirelessPtr.get();
    if (wireless && routerClient &&
        routerClient->executeCommand("/interface/wireless/print", *wireless) &&
        !wireless->trapReceived) {
      for (uint8_t i = 0; i < wireless->replyCount; ++i) {
        String name, id, ssid;
        replyAttr(*wireless, i, "name", name);
        replyAttr(*wireless, i, ".id", id);
        replyAttr(*wireless, i, "ssid", ssid);
        if (name == wirelessIface || id == wirelessIface) {
          wirelessIface = name;
          if (!ssid.isEmpty()) targetSsid = ssid;
          break;
        }
      }
    }
  }

  if (routerClient && !externalApOnly) {
    (void)DmaMemoryMonitor::waitForRouterOsConnectHeadroom(1500);
    String applyError, applyStage;
    if (!RouterWireless::applyWifiSelection(*routerClient, wifiSelection,
                                            candidate.bridgeName, applyError,
                                            applyStage)) {
      markRouterPlanFailed(result);
      result.httpStatus   = 502;
      result.errorCode    = "WIFI_CONFIGURE_FAILED";
      result.errorMessage = applyError;
      result.stage        = applyStage;
      return result;
    }
    if (wifiSelection.mode == RouterWireless::kModeNew) {
      wirelessIface = "renzfi-wifi";
    }
  }

  if (engine && !wirelessIface.isEmpty()) {
    engine->persistWirelessSelection(wirelessIface, ssidPolicy, targetSsid);
  }

  _wifiSelectionConfigured = true;
  _wifiMode                = wifiSelection.mode;
  _wifiInterfaceId         = externalApOnly ? String("") : wirelessIface;
  if (externalApOnly) {
    _wifiSsid     = "";
    _wifiPassword = "";
  } else if (!targetSsid.isEmpty()) {
    _wifiSsid = targetSsid;
  } else if (!wifiSelection.ssid.isEmpty()) {
    _wifiSsid = wifiSelection.ssid;
  } else {
    const char *selectedSsid = body["selectedSsid"] | "";
    if (selectedSsid[0] != '\0') _wifiSsid = selectedSsid;
  }
  if (!wifiSelection.password.isEmpty()) {
    _wifiPassword = wifiSelection.password;
  }

  _foundationApplied       = true;
  _hotspotActivated        = body["hotspotDetected"] | false;
  _clientInterfaceAttached = false;
  _appliedAt               = millis();
  _adoptedAt               = millis();
  _networkMode             = RouterProvisioning::NETWORK_MODE_EXISTING;
  _networkModePreference   = RouterProvisioning::NETWORK_MODE_EXISTING;
  _guestBridgeName         = candidate.bridgeName;
  _guestNetwork            = candidate.guestNetwork;
  _guestGateway            = candidate.gatewayIp;
  _dhcpPool                = candidate.poolRange;
  _dhcpServerName          = candidate.dhcpServerName;
  _poolName                = candidate.poolName;
  _hotspotDetected         = _hotspotActivated;
  persist();

  dataOut["networkMode"]           = _networkMode;
  dataOut["foundationApplied"]     = true;
  dataOut["guestBridgeName"]       = _guestBridgeName;
  dataOut["guestNetwork"]          = _guestNetwork;
  dataOut["guestGateway"]          = _guestGateway;
  dataOut["dhcpPool"]              = _dhcpPool;
  dataOut["dhcpServerName"]        = _dhcpServerName;
  dataOut["poolName"]              = _poolName;
  dataOut["hotspotDetected"]       = _hotspotDetected;
  dataOut["adoptedAt"]             = _adoptedAt;
  dataOut["wifiSelectionConfigured"] = _wifiSelectionConfigured;
  dataOut["wifiMode"]                 = _wifiMode;
  dataOut["externalApOnly"]           = _externalApOnly;
  dataOut["interfaceId"]              = _wifiInterfaceId;
  if (!_wifiSsid.isEmpty()) dataOut["ssid"] = _wifiSsid;
  dataOut["installationState"] =
      installationStateLabel(_installation->current());

  result.success      = true;
  result.httpStatus   = 200;
  result.errorMessage = "Existing Renz-Fi network configured";
  RENZFI_ROUTER_API_ADOPTION_COMPLETE();
  return result;
}

void RouterProvisioningManager::fillWirelessStatus(JsonObject dataOut) const {
  dataOut["wifiMode"]      = _wifiMode;
  dataOut["interfaceId"]   = _wifiInterfaceId;
  dataOut["ssid"]          = _wifiSsid;
  dataOut["configured"]    = wifiSetupComplete();
  dataOut["externalApOnly"] = _externalApOnly;
  dataOut["security"]      = "wpa2-psk";
  if (!_wifiPassword.isEmpty()) dataOut["password"] = _wifiPassword;
}

RouterProvisioningManager::OperationResult
RouterProvisioningManager::setNetworkModePreference(JsonObjectConst body) {
  OperationResult result;
  const char *mode = body["mode"] | "";
  if (strcmp(mode, RouterProvisioning::NETWORK_MODE_CREATE_NEW) != 0 &&
      strcmp(mode, RouterProvisioning::NETWORK_MODE_EXISTING) != 0) {
    markRouterPlanFailed(result);
    result.errorCode = "INVALID_NETWORK_MODE";
    result.errorMessage = "Network mode must be existing or create_new";
    return result;
  }

  if (isExistingNetworkAdopted() ||
      (_foundationApplied &&
       _networkMode == RouterProvisioning::NETWORK_MODE_CREATE_NEW)) {
    const char *confirmation = body["confirmation"] | "";
    if (strcmp(confirmation, RouterProvisioning::CHANGE_NETWORK_MODE_CONFIRMATION) !=
        0) {
      markRouterPlanFailed(result);
      result.httpStatus = 400;
      result.errorCode = kConfirmationRequired;
      result.errorMessage =
          "Changing network mode after provisioning requires confirmation";
      return result;
    }
    _foundationApplied       = false;
    _networkMode             = "";
    _adoptedAt               = 0;
    _hotspotActivated        = false;
    _clientInterfaceAttached = false;
    _appliedAt               = 0;
    if (_installation &&
        _installation->current() == InstallationState::Provisioned) {
      _installation->setState(InstallationState::RouterConfigured);
    }
    scheduleDeferredPersist();
  }

  _networkModePreference = mode;
  scheduleDeferredPersist();

  result.success      = true;
  result.httpStatus   = 202;
  result.errorMessage = "Network mode preference accepted — persisting";
  return result;
}

void RouterProvisioningManager::fillNetworkModeStatus(JsonObject dataOut) {
  JsonObject network = dataOut["networkProvisioning"].to<JsonObject>();
  network["networkMode"]              = _networkMode;
  network["networkModePreference"]    = _networkModePreference.isEmpty()
                                            ? RouterProvisioning::NETWORK_MODE_EXISTING
                                            : _networkModePreference;
  network["foundationApplied"]        = _foundationApplied;
  network["existingNetworkAdopted"]   = isExistingNetworkAdopted();
  network["hotspotDetected"]          = _hotspotDetected;
  network["wifiSelectionConfigured"]  = _wifiSelectionConfigured;
  network["wifiSetupComplete"]        = wifiSetupComplete();
  network["externalApOnly"]           = _externalApOnly;
  network["durableCommitStatus"]      = durableCommitStatus();
  network["wifiSelectionDurablePending"] = wifiSelectionDurablePending();
  if (_durableCommitPhase == DurableCommitPhase::Failed &&
      !_durableCommitError.isEmpty()) {
    network["durableCommitError"] = _durableCommitError;
  }
  if (_wifiSelectionConfigured) {
    network["wifiMode"] = _wifiMode;
    if (!_wifiInterfaceId.isEmpty()) network["interfaceId"] = _wifiInterfaceId;
    if (!_wifiSsid.isEmpty()) network["selectedSsidHint"] = _wifiSsid;
  }
  network["adoptedAt"]                = _adoptedAt;
  network["guestBridgeName"]            = _guestBridgeName;
  network["guestNetwork"]               = _guestNetwork;
  network["guestGateway"]               = _guestGateway;
  network["dhcpPool"]                   = _dhcpPool;
  network["dhcpServerName"]             = _dhcpServerName;
  network["poolName"]                   = _poolName;
}

void routerProvisioningFillNetworkModeStatus(RouterProvisioningManager *mgr,
                                             JsonObject dataOut) {
  if (mgr) mgr->fillNetworkModeStatus(dataOut);
}

void routerProvisioningFillWirelessStatus(RouterProvisioningManager *mgr,
                                          JsonObject dataOut) {
  if (mgr) mgr->fillWirelessStatus(dataOut);
}
