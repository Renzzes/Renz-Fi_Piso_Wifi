#include "ExistingNetworkScanner.h"

#include "Config.h"
#include "EthernetManager.h"
#include "InstallationStateManager.h"
#include "RenzFiRouterApiLog.h"
#include "RouterProvisioningPreconditions.h"
#include "RouterProvisioningTypes.h"
#include "RouterCommandScratch.h"

namespace {

void reportScanProgress(void (*progressFn)(void *, const char *, const char *),
                        void *ctx, const char *stageId, const char *label) {
  if (progressFn) progressFn(ctx, stageId, label);
}

const String kBridgeAttrs[] = {"=.proplist=name,comment"};
const String kAddressAttrs[] = {"=.proplist=address,interface,comment"};
const String kPoolAttrs[] = {"=.proplist=name,ranges,comment"};
const String kDhcpServerAttrs[] = {
    "=.proplist=name,interface,address-pool,disabled,comment"};
const String kDhcpNetworkAttrs[] = {"=.proplist=address,comment"};
const String kFilterAttrs[] = {
    "=.proplist=action,chain,protocol,dst-port,src-address,disabled,comment",
    "?chain=input",
    "?protocol=tcp",
    "?dst-port=8728",
};
const String kHotspotAttrs[] = {"=.proplist=name,interface,disabled"};

bool isLoginFailureCode(const String &code) {
  return code == "API_LOGIN_FAILED" || code == "ROUTEROS_LOGIN_FAILED" ||
         code == "ROUTEROS_API_AUTH_TRAP" || code == "ROUTEROS_API_AUTH_FATAL";
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

// Maps a completed executeCommand() call (ok flag + scratch result) to the
// same error taxonomy the previous implementation used, without keeping the
// scratch reply alive any longer than this call.
bool mapFetchFailure(const RouterOsClient &client,
                     const RouterOsClient::CommandResult &result, bool executedOk,
                     String &errorOut, String &errorCodeOut) {
  if (!executedOk) {
    errorOut     = client.lastError();
    errorCodeOut = client.lastErrorCode().isEmpty() ? "API_COMMAND_FAILED"
                                                    : client.lastErrorCode();
    return false;
  }
  if (result.fatalReceived) {
    errorOut = result.fatalMessage.isEmpty() ? "RouterOS fatal reply"
                                             : result.fatalMessage;
    errorCodeOut = "ROUTEROS_API_FATAL";
    return false;
  }
  if (result.trapReceived) {
    errorOut = result.trapMessage.isEmpty() ? "RouterOS API trap"
                                            : result.trapMessage;
    errorCodeOut = "API_TRAP";
    return false;
  }
  if (result.replyLimitReached) {
    errorOut     = "reply cap reached";
    errorCodeOut = "ROUTER_INSPECTION_LIMIT";
    return false;
  }
  return true;
}

bool isRenzfiComment(const String &comment) {
  return comment.startsWith(RouterProvisioning::COMMENT_PREFIX);
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

bool cidrOverlaps(const String &a, const String &b) {
  IPAddress netA, netB;
  uint8_t lenA = 0, lenB = 0;
  if (!parseIpv4Prefix(a, netA, lenA) || !parseIpv4Prefix(b, netB, lenB)) {
    return false;
  }
  const uint8_t common = lenA < lenB ? lenA : lenB;
  const uint32_t mask = common == 0 ? 0 : (0xFFFFFFFFu << (32 - common));
  return (ipv4ToHostOrder(netA) & mask) == (ipv4ToHostOrder(netB) & mask);
}

String networkFromGatewayCidr(const String &gatewayCidr) {
  IPAddress ip;
  uint8_t prefix = 0;
  if (!parseIpv4Prefix(gatewayCidr, ip, prefix)) return "";
  const uint32_t mask = prefix == 0 ? 0 : (0xFFFFFFFFu << (32 - prefix));
  const uint32_t netHost = ipv4ToHostOrder(ip) & mask;
  IPAddress network((netHost >> 24) & 0xFF, (netHost >> 16) & 0xFF,
                    (netHost >> 8) & 0xFF, netHost & 0xFF);
  return network.toString() + "/" + String(prefix);
}

bool isHostInCidr(const String &hostIp, const String &networkCidr) {
  IPAddress net, host;
  uint8_t prefix = 0;
  if (!parseIpv4Prefix(networkCidr, net, prefix) || !host.fromString(hostIp)) {
    return false;
  }
  const uint32_t mask = prefix == 0 ? 0 : (0xFFFFFFFFu << (32 - prefix));
  return (ipv4ToHostOrder(host) & mask) == (ipv4ToHostOrder(net) & mask);
}

bool isProblematicEspSubnetOverlap(const String &guestNetwork,
                                   const String &espSubnetCidr,
                                   const String &espIp) {
  if (espSubnetCidr.isEmpty() || guestNetwork.isEmpty()) return false;
  if (guestNetwork == espSubnetCidr) return false;
  if (!espIp.isEmpty() && isHostInCidr(espIp, guestNetwork)) return false;
  return cidrOverlaps(guestNetwork, espSubnetCidr);
}

void logCandidateDivider() {
  RENZFI_ROUTER_API_VERBOSE_LINE("--------------------------------------------------");
}

void logCandidateEvaluation(const ExistingNetworkScan::Candidate &candidate,
                            const String &espIp, bool bridgeFound,
                            bool gatewayFound, bool dhcpServerFound,
                            bool poolFound, bool dhcpNetworkFound,
                            bool hotspotDetected, const String &result,
                            const String &rejectReason = "") {
  RENZFI_ROUTER_API_VERBOSE_LINE("--------------------------------------------------");
  RENZFI_ROUTER_API_VERBOSE("[scan] evaluating bridge=%s\n", candidate.bridgeName.c_str());
  RENZFI_ROUTER_API_VERBOSE("bridgeFound=%s\n", bridgeFound ? "true" : "false");
  RENZFI_ROUTER_API_VERBOSE("gatewayFound=%s\n", gatewayFound ? "true" : "false");
  if (gatewayFound) {
    RENZFI_ROUTER_API_VERBOSE("gateway=%s\n", candidate.gatewayCidr.c_str());
  }
  RENZFI_ROUTER_API_VERBOSE("dhcpServerFound=%s\n", dhcpServerFound ? "true" : "false");
  if (dhcpServerFound) {
    RENZFI_ROUTER_API_VERBOSE("dhcpServer=%s\n", candidate.dhcpServerName.c_str());
  }
  RENZFI_ROUTER_API_VERBOSE("poolFound=%s\n", poolFound ? "true" : "false");
  if (poolFound) {
    RENZFI_ROUTER_API_VERBOSE("pool=%s\n", candidate.poolName.c_str());
    RENZFI_ROUTER_API_VERBOSE("poolRange=%s\n", candidate.poolRange.c_str());
  }
  RENZFI_ROUTER_API_VERBOSE("dhcpNetworkFound=%s\n", dhcpNetworkFound ? "true" : "false");
  if (dhcpNetworkFound) {
    RENZFI_ROUTER_API_VERBOSE("network=%s\n", candidate.dhcpNetwork.c_str());
  }
  RENZFI_ROUTER_API_VERBOSE("espIp=%s\n", espIp.c_str());
  RENZFI_ROUTER_API_VERBOSE("apiAccess=%s\n", candidate.apiAccessOk ? "true" : "false");
  RENZFI_ROUTER_API_VERBOSE("hotspotDetected=%s\n", hotspotDetected ? "true" : "false");
  RENZFI_ROUTER_API_VERBOSE("renzfiManaged=%s\n", candidate.renzfiManaged ? "true" : "false");
  if (!candidate.origin.isEmpty()) {
    RENZFI_ROUTER_API_VERBOSE("origin=%s\n", candidate.origin.c_str());
  }
  if (!candidate.confidence.isEmpty()) {
    RENZFI_ROUTER_API_VERBOSE("confidence=%s\n", candidate.confidence.c_str());
  }
  RENZFI_ROUTER_API_VERBOSE(
      "compatibility bridge=%s gateway=%s dhcp=%s pool=%s "
      "dhcpNetwork=%s firewall=%s hotspot=%s\n",
      candidate.compatibility.bridge.c_str(), candidate.compatibility.gateway.c_str(),
      candidate.compatibility.dhcp.c_str(), candidate.compatibility.pool.c_str(),
      candidate.compatibility.dhcpNetwork.c_str(),
      candidate.compatibility.firewall.c_str(), candidate.compatibility.hotspot.c_str());
  RENZFI_ROUTER_API_VERBOSE("adoptionScore=%u\n",
                            static_cast<unsigned>(candidate.adoptionScore));
  for (uint8_t ri = 0; ri < candidate.confidenceReasonCount; ++ri) {
    RENZFI_ROUTER_API_VERBOSE("confidenceReason=%s\n",
                              candidate.confidenceReasons[ri].c_str());
  }
  RENZFI_ROUTER_API_VERBOSE("genericCompatible=%s\n",
                            candidate.genericCompatible ? "true" : "false");
  if (!rejectReason.isEmpty()) {
    RENZFI_ROUTER_API_VERBOSE_LINE("candidateResult=rejected");
    RENZFI_ROUTER_API_VERBOSE("reason=%s\n", rejectReason.c_str());
  } else {
    RENZFI_ROUTER_API_VERBOSE("candidateResult=%s\n", result.c_str());
  }
  RENZFI_ROUTER_API_VERBOSE_LINE("--------------------------------------------------");
}

void addConfidenceReason(ExistingNetworkScan::Candidate &candidate,
                         const char *reason) {
  if (!reason || reason[0] == '\0') return;
  if (candidate.confidenceReasonCount >=
      ExistingNetworkScan::kMaxConfidenceReasons) {
    return;
  }
  candidate.confidenceReasons[candidate.confidenceReasonCount++] = String(reason);
}

bool compatPasses(const String &level) {
  return level == ExistingNetworkScan::CompatLevel::Pass;
}

bool compatHardSatisfied(const String &level) {
  return level == ExistingNetworkScan::CompatLevel::Pass;
}

ExistingNetworkScan::CandidateCompatibility buildCompatibility(
    bool bridgeFound, bool gatewayFound, bool dhcpFound, bool poolFound,
    bool dhcpNetworkFound, bool apiOk, bool firewallLimited,
    bool hotspotOnBridge, bool hotspotInspectionOk) {
  ExistingNetworkScan::CandidateCompatibility compat;
  compat.bridge      = bridgeFound ? ExistingNetworkScan::CompatLevel::Pass
                                   : ExistingNetworkScan::CompatLevel::Fail;
  compat.gateway     = gatewayFound ? ExistingNetworkScan::CompatLevel::Pass
                                    : ExistingNetworkScan::CompatLevel::Fail;
  compat.dhcp        = dhcpFound ? ExistingNetworkScan::CompatLevel::Pass
                                 : ExistingNetworkScan::CompatLevel::Fail;
  compat.pool        = poolFound ? ExistingNetworkScan::CompatLevel::Pass
                                 : ExistingNetworkScan::CompatLevel::Fail;
  compat.dhcpNetwork = dhcpNetworkFound ? ExistingNetworkScan::CompatLevel::Pass
                                       : ExistingNetworkScan::CompatLevel::Fail;
  if (apiOk) {
    compat.firewall = ExistingNetworkScan::CompatLevel::Pass;
  } else if (firewallLimited) {
    compat.firewall = ExistingNetworkScan::CompatLevel::Warning;
  } else {
    compat.firewall = ExistingNetworkScan::CompatLevel::Fail;
  }
  if (!hotspotInspectionOk) {
    compat.hotspot = ExistingNetworkScan::CompatLevel::Unknown;
  } else if (hotspotOnBridge) {
    compat.hotspot = ExistingNetworkScan::CompatLevel::Pass;
  } else {
    compat.hotspot = ExistingNetworkScan::CompatLevel::Fail;
  }
  return compat;
}

ExistingNetworkScan::CandidateInspection buildInspection(
    bool firewallPrintOk, bool hotspotInspectionAttempted,
    bool hotspotInspectionOk) {
  ExistingNetworkScan::CandidateInspection inspection;
  inspection.bridge      = ExistingNetworkScan::InspectionLevel::Complete;
  inspection.gateway     = ExistingNetworkScan::InspectionLevel::Complete;
  inspection.dhcp        = ExistingNetworkScan::InspectionLevel::Complete;
  inspection.pool        = ExistingNetworkScan::InspectionLevel::Complete;
  inspection.dhcpNetwork = ExistingNetworkScan::InspectionLevel::Complete;
  inspection.firewall =
      firewallPrintOk ? ExistingNetworkScan::InspectionLevel::Complete
                      : ExistingNetworkScan::InspectionLevel::Failed;
  if (!hotspotInspectionAttempted) {
    inspection.hotspot = ExistingNetworkScan::InspectionLevel::Skipped;
  } else if (hotspotInspectionOk) {
    inspection.hotspot = ExistingNetworkScan::InspectionLevel::Complete;
  } else {
    inspection.hotspot = ExistingNetworkScan::InspectionLevel::Failed;
  }
  inspection.nat = ExistingNetworkScan::InspectionLevel::NotSupported;
  return inspection;
}

String makeCandidateId(const String &bridgeName, const String &gatewayIp,
                       uint8_t sequence) {
  if (!bridgeName.isEmpty() && !gatewayIp.isEmpty()) {
    return bridgeName + "@" + gatewayIp;
  }
  return String("candidate-") + String(static_cast<unsigned>(sequence));
}

uint8_t adoptionScorePoints(const String &level, uint8_t weight) {
  uint8_t pct = 0;
  if (level == ExistingNetworkScan::CompatLevel::Pass) {
    pct = 100;
  } else if (level == ExistingNetworkScan::CompatLevel::Warning) {
    pct = 50;
  } else if (level == ExistingNetworkScan::CompatLevel::Unknown) {
    pct = 25;
  }
  return static_cast<uint8_t>((static_cast<uint16_t>(pct) * weight) / 100);
}

uint8_t computeAdoptionScore(const ExistingNetworkScan::CandidateCompatibility &c) {
  const uint16_t sum = adoptionScorePoints(c.bridge, 5) +
                      adoptionScorePoints(c.gateway, 15) +
                      adoptionScorePoints(c.dhcp, 15) +
                      adoptionScorePoints(c.pool, 15) +
                      adoptionScorePoints(c.dhcpNetwork, 15) +
                      adoptionScorePoints(c.firewall, 20) +
                      adoptionScorePoints(c.hotspot, 15);
  return static_cast<uint8_t>(sum > 100 ? 100 : sum);
}

void populateRequirements(ExistingNetworkScan::Candidate &candidate) {
  const auto &c = candidate.compatibility;
  candidate.requirements.hard.gateway     = compatHardSatisfied(c.gateway);
  candidate.requirements.hard.dhcp        = compatHardSatisfied(c.dhcp);
  candidate.requirements.hard.pool        = compatHardSatisfied(c.pool);
  candidate.requirements.hard.dhcpNetwork = compatHardSatisfied(c.dhcpNetwork);
  candidate.requirements.hard.firewall    = compatHardSatisfied(c.firewall);
  candidate.requirements.soft.hotspot       = compatPasses(c.hotspot);
  candidate.requirements.soft.renzfiMarkers = candidate.renzfiManaged;
}

void populateConfidenceReasons(ExistingNetworkScan::Candidate &candidate) {
  candidate.confidenceReasonCount = 0;
  const auto &c = candidate.compatibility;
  if (!compatHardSatisfied(c.gateway)) addConfidenceReason(candidate, "gateway_not_on_bridge");
  if (!compatHardSatisfied(c.dhcp)) addConfidenceReason(candidate, "missing_dhcp_server");
  if (!compatHardSatisfied(c.pool)) addConfidenceReason(candidate, "pool_not_linked");
  if (!compatHardSatisfied(c.dhcpNetwork)) addConfidenceReason(candidate, "missing_dhcp_network");
  if (c.firewall == ExistingNetworkScan::CompatLevel::Warning) {
    addConfidenceReason(candidate, "firewall_inspection_limited");
  } else if (!compatHardSatisfied(c.firewall)) {
    addConfidenceReason(candidate, "firewall_rule_not_detected");
  }
  if (c.hotspot == ExistingNetworkScan::CompatLevel::Unknown) {
    addConfidenceReason(candidate, "hotspot_not_inspected");
  } else if (!compatPasses(c.hotspot)) {
    addConfidenceReason(candidate, "hotspot_not_detected");
  }
}

void assignCandidateOutcome(ExistingNetworkScan::Candidate &candidate,
                            bool overlapRejected) {
  candidate.confidenceReasonCount = 0;
  populateRequirements(candidate);
  const auto &c = candidate.compatibility;

  if (overlapRejected) {
    candidate.status = "rejected_overlap";
    addConfidenceReason(candidate, "network_overlap");
    candidate.confidence = "low";
    candidate.adoptionScore = computeAdoptionScore(c);
    return;
  }

  const bool coreComplete = candidate.requirements.hard.gateway &&
                            candidate.requirements.hard.dhcp &&
                            candidate.requirements.hard.pool &&
                            candidate.requirements.hard.dhcpNetwork &&
                            candidate.requirements.hard.firewall;

  if (coreComplete) {
    candidate.status = "compatible_candidate";
    candidate.origin = candidate.renzfiManaged ? "renzfi" : "generic";
    candidate.genericCompatible = (candidate.origin == "generic");
    if (!candidate.requirements.soft.hotspot) {
      if (c.hotspot == ExistingNetworkScan::CompatLevel::Unknown) {
        addConfidenceReason(candidate, "hotspot_not_inspected");
      } else {
        addConfidenceReason(candidate, "hotspot_not_detected");
      }
    }
    if (c.firewall == ExistingNetworkScan::CompatLevel::Warning) {
      addConfidenceReason(candidate, "firewall_inspection_limited");
    }
    candidate.confidence = candidate.confidenceReasonCount == 0 ? "high" : "medium";
    candidate.adoptionScore = computeAdoptionScore(c);
    return;
  }

  candidate.status = "partial_candidate";
  populateConfidenceReasons(candidate);

  uint8_t missingCore = 0;
  if (!candidate.requirements.hard.dhcp) missingCore++;
  if (!candidate.requirements.hard.pool) missingCore++;
  if (!candidate.requirements.hard.dhcpNetwork) missingCore++;
  if (!candidate.requirements.hard.firewall) missingCore++;

  if (candidate.requirements.hard.gateway && missingCore >= 1 && missingCore <= 2) {
    candidate.confidence = "high";
  } else if (!candidate.requirements.hard.gateway || missingCore >= 3) {
    candidate.confidence = "low";
  } else {
    candidate.confidence = "medium";
  }
  candidate.adoptionScore = computeAdoptionScore(c);
}

}  // namespace

ExistingNetworkScanner::ExistingNetworkScanner(RouterOsClient &client)
    : _client(client) {}

ExistingNetworkScanner::~ExistingNetworkScanner() = default;

bool ExistingNetworkScanner::connectAndLogin(
    const SetupRouterConnectionManager::RouterInput &input, EthernetManager *eth,
    String &errorOut, String &errorCodeOut,
    void (*progressFn)(void *, const char *, const char *), void *progressCtx) {
  (void)eth;  // Ethernet readiness already verified by the shared precondition.
  errorOut.clear();
  errorCodeOut.clear();
  reportScanProgress(progressFn, progressCtx, "connecting", "Connecting...");
  _client.setTimeouts(RenzFiConfig::SETUP_ROUTER_CONNECT_TIMEOUT_MS,
                      RenzFiConfig::SETUP_ROUTER_IO_TIMEOUT_MS);
  _client.setCredentials(input.host, input.username, input.password, input.apiPort);
  _client.setCredentialSource("existing-network-scan");
  RENZFI_ROUTER_API_VERBOSE("[existing-scan] connect host=%s port=%u user=%s\n",
                            input.host.c_str(), static_cast<unsigned>(input.apiPort),
                            input.username.c_str());
  if (!_client.connect()) {
    errorOut     = _client.lastError();
    errorCodeOut = _client.lastErrorCode().isEmpty() ? "TCP_CONNECT_FAILED"
                                                     : _client.lastErrorCode();
    RENZFI_ROUTER_API_FAILURE("connect", errorCodeOut.c_str(), errorOut.c_str());
    return false;
  }
  RENZFI_ROUTER_API_VERBOSE_LINE("[existing-scan] connect ok");
  reportScanProgress(progressFn, progressCtx, "logging_in", "Logging in...");
  if (!_client.login()) {
    errorOut     = _client.lastError();
    errorCodeOut = _client.lastErrorCode().isEmpty() ? "API_LOGIN_FAILED"
                                                     : _client.lastErrorCode();
    RENZFI_ROUTER_API_FAILURE("login", errorCodeOut.c_str(), errorOut.c_str());
    _client.disconnect();
    return false;
  }
  RENZFI_ROUTER_API_LOGIN_SUCCESS();
  return true;
}

bool ExistingNetworkScanner::fetchBridges(String &errorOut, String &errorCodeOut,
                                          void (*progressFn)(void *, const char *,
                                                             const char *),
                                          void *progressCtx) {
  reportScanProgress(progressFn, progressCtx, "reading_bridges", "Reading Bridges...");
  _client.setCommandReplyLimits(ExistingNetworkScan::kReplyCap, true);
  const bool ok =
      _client.executeCommand("/interface/bridge/print", kBridgeAttrs, 1, RouterCommandScratchContext::get());
  _client.resetCommandReplyLimits();
  if (!mapFetchFailure(_client, RouterCommandScratchContext::get(), ok, errorOut, errorCodeOut)) return false;

  _bridgeCount = 0;
  for (uint8_t i = 0; i < RouterCommandScratchContext::get().replyCount && _bridgeCount < kMaxRows; ++i) {
    BridgeRow &row = _bridges[_bridgeCount++];
    replyAttr(RouterCommandScratchContext::get(), i, "name", row.name);
    replyAttr(RouterCommandScratchContext::get(), i, "comment", row.comment);
  }
  return true;
}

bool ExistingNetworkScanner::fetchAddresses(String &errorOut, String &errorCodeOut,
                                            void (*progressFn)(void *, const char *,
                                                               const char *),
                                            void *progressCtx) {
  reportScanProgress(progressFn, progressCtx, "reading_addresses", "Reading Addresses...");
  _client.setCommandReplyLimits(ExistingNetworkScan::kReplyCap, true);
  const bool ok =
      _client.executeCommand("/ip/address/print", kAddressAttrs, 1, RouterCommandScratchContext::get());
  _client.resetCommandReplyLimits();
  if (!mapFetchFailure(_client, RouterCommandScratchContext::get(), ok, errorOut, errorCodeOut)) return false;

  _addressCount = 0;
  _addressIndicesByInterface.clear();
  for (uint8_t i = 0; i < RouterCommandScratchContext::get().replyCount && _addressCount < kMaxRows; ++i) {
    String address;
    if (!replyAttr(RouterCommandScratchContext::get(), i, "address", address)) continue;
    AddressRow &row = _addresses[_addressCount];
    row.address = address;
    replyAttr(RouterCommandScratchContext::get(), i, "interface", row.interfaceName);
    replyAttr(RouterCommandScratchContext::get(), i, "comment", row.comment);
    _addressIndicesByInterface.emplace(row.interfaceName, _addressCount);
    _addressCount++;
  }
  return true;
}

bool ExistingNetworkScanner::fetchPools(String &errorOut, String &errorCodeOut,
                                        void (*progressFn)(void *, const char *,
                                                           const char *),
                                        void *progressCtx) {
  reportScanProgress(progressFn, progressCtx, "reading_pools", "Reading Pools...");
  _client.setCommandReplyLimits(ExistingNetworkScan::kReplyCap, true);
  const bool ok = _client.executeCommand("/ip/pool/print", kPoolAttrs, 1, RouterCommandScratchContext::get());
  _client.resetCommandReplyLimits();
  if (!mapFetchFailure(_client, RouterCommandScratchContext::get(), ok, errorOut, errorCodeOut)) return false;

  _poolCount = 0;
  _poolIndexByName.clear();
  for (uint8_t i = 0; i < RouterCommandScratchContext::get().replyCount && _poolCount < kMaxRows; ++i) {
    String name;
    if (!replyAttr(RouterCommandScratchContext::get(), i, "name", name)) continue;
    PoolRow &row = _pools[_poolCount];
    row.name = name;
    replyAttr(RouterCommandScratchContext::get(), i, "ranges", row.ranges);
    replyAttr(RouterCommandScratchContext::get(), i, "comment", row.comment);
    _poolIndexByName.emplace(name, _poolCount);
    _poolCount++;
  }
  return true;
}

bool ExistingNetworkScanner::fetchDhcpServers(String &errorOut, String &errorCodeOut,
                                              void (*progressFn)(void *, const char *,
                                                                 const char *),
                                              void *progressCtx) {
  reportScanProgress(progressFn, progressCtx, "reading_dhcp_servers",
                    "Reading DHCP Servers...");
  _client.setCommandReplyLimits(ExistingNetworkScan::kReplyCap, true);
  const bool ok = _client.executeCommand("/ip/dhcp-server/print", kDhcpServerAttrs, 1,
                                         RouterCommandScratchContext::get());
  _client.resetCommandReplyLimits();
  if (!mapFetchFailure(_client, RouterCommandScratchContext::get(), ok, errorOut, errorCodeOut)) return false;

  _dhcpServerCount = 0;
  _firstEnabledDhcpServerByBridge.clear();
  for (uint8_t i = 0; i < RouterCommandScratchContext::get().replyCount && _dhcpServerCount < kMaxRows; ++i) {
    String name;
    if (!replyAttr(RouterCommandScratchContext::get(), i, "name", name)) continue;
    DhcpServerRow &row = _dhcpServers[_dhcpServerCount];
    row.name = name;
    replyAttr(RouterCommandScratchContext::get(), i, "interface", row.interfaceName);
    String disabled;
    replyAttr(RouterCommandScratchContext::get(), i, "disabled", disabled);
    row.disabled = disabled == "true";
    if (!replyAttr(RouterCommandScratchContext::get(), i, "address-pool", row.poolRef) || row.poolRef.isEmpty()) {
      replyAttr(RouterCommandScratchContext::get(), i, "address-pools", row.poolRef);
    }
    replyAttr(RouterCommandScratchContext::get(), i, "comment", row.comment);

    if (!row.disabled && !row.interfaceName.isEmpty() &&
        _firstEnabledDhcpServerByBridge.find(row.interfaceName) ==
            _firstEnabledDhcpServerByBridge.end()) {
      _firstEnabledDhcpServerByBridge.emplace(row.interfaceName, _dhcpServerCount);
    }
    _dhcpServerCount++;
  }
  return true;
}

bool ExistingNetworkScanner::fetchDhcpNetworks(String &errorOut, String &errorCodeOut,
                                               void (*progressFn)(void *, const char *,
                                                                  const char *),
                                               void *progressCtx) {
  reportScanProgress(progressFn, progressCtx, "reading_dhcp_networks",
                    "Reading DHCP Networks...");
  _client.setCommandReplyLimits(ExistingNetworkScan::kReplyCap, true);
  const bool ok = _client.executeCommand("/ip/dhcp-server/network/print",
                                         kDhcpNetworkAttrs, 1, RouterCommandScratchContext::get());
  _client.resetCommandReplyLimits();
  if (!mapFetchFailure(_client, RouterCommandScratchContext::get(), ok, errorOut, errorCodeOut)) return false;

  _dhcpNetworkCount = 0;
  _dhcpNetworkIndexByAddress.clear();
  for (uint8_t i = 0; i < RouterCommandScratchContext::get().replyCount && _dhcpNetworkCount < kMaxRows; ++i) {
    String address;
    if (!replyAttr(RouterCommandScratchContext::get(), i, "address", address)) continue;
    DhcpNetworkRow &row = _dhcpNetworks[_dhcpNetworkCount];
    row.address = address;
    replyAttr(RouterCommandScratchContext::get(), i, "comment", row.comment);
    _dhcpNetworkIndexByAddress.emplace(address, _dhcpNetworkCount);
    _dhcpNetworkCount++;
  }
  return true;
}

bool ExistingNetworkScanner::fetchFirewall(const String &espIp, String &errorOut,
                                           String &errorCodeOut,
                                           void (*progressFn)(void *, const char *,
                                                              const char *),
                                           void *progressCtx) {
  reportScanProgress(progressFn, progressCtx, "reading_firewall", "Reading Firewall...");
  _client.setCommandReplyLimits(ExistingNetworkScan::kReplyCap, true);
  const bool ok = _client.executeCommand("/ip/firewall/filter/print", kFilterAttrs, 4,
                                         RouterCommandScratchContext::get());
  _client.resetCommandReplyLimits();
  if (!mapFetchFailure(_client, RouterCommandScratchContext::get(), ok, errorOut, errorCodeOut)) {
    errorCodeOut = errorCodeOut.isEmpty() ? "ROUTER_INSPECTION_FAILED" : errorCodeOut;
    return false;
  }

  _firewallLimited = RouterCommandScratchContext::get().replyLimitReached;

  // Distill immediately: only the two scalars below survive past this call —
  // the raw filter-rule reply is discarded once this function returns.
  _apiAccessOk = false;
  _apiAccessManaged = false;
  for (uint8_t i = 0; i < RouterCommandScratchContext::get().replyCount; ++i) {
    String chain, protocol, dstPort, srcAddress, action, disabled, comment;
    replyAttr(RouterCommandScratchContext::get(), i, "chain", chain);
    replyAttr(RouterCommandScratchContext::get(), i, "protocol", protocol);
    replyAttr(RouterCommandScratchContext::get(), i, "dst-port", dstPort);
    replyAttr(RouterCommandScratchContext::get(), i, "src-address", srcAddress);
    replyAttr(RouterCommandScratchContext::get(), i, "action", action);
    replyAttr(RouterCommandScratchContext::get(), i, "disabled", disabled);
    replyAttr(RouterCommandScratchContext::get(), i, "comment", comment);
    if (chain != "input" || protocol != "tcp" || dstPort != "8728") continue;
    if (disabled == "true") continue;
    if (action != "accept") continue;
    if (!srcAddress.isEmpty() && srcAddress != espIp) continue;
    if (isRenzfiComment(comment)) _apiAccessManaged = true;
    _apiAccessOk = true;
    break;
  }
  return true;
}

void ExistingNetworkScanner::fetchHotspot(
    uint32_t deadlineMs, void (*progressFn)(void *, const char *, const char *),
    void *progressCtx) {
  _hotspotInspectionAttempted = false;
  _hotspotInspectionOk = false;
  _hotspotDetectedGlobal = false;
  _hotspotEnabledByInterface.clear();

  if (millis() >= deadlineMs) return;
  reportScanProgress(progressFn, progressCtx, "reading_hotspot", "Reading Hotspot...");
  _hotspotInspectionAttempted = true;

  _client.setCommandReplyLimits(ExistingNetworkScan::kReplyCap, true);
  const bool ok =
      _client.executeCommand("/ip/hotspot/print", kHotspotAttrs, 1, RouterCommandScratchContext::get());
  _client.resetCommandReplyLimits();
  String ignoredError, ignoredCode;
  if (!mapFetchFailure(_client, RouterCommandScratchContext::get(), ok, ignoredError, ignoredCode)) return;

  _hotspotInspectionOk = true;
  for (uint8_t i = 0; i < RouterCommandScratchContext::get().replyCount; ++i) {
    String iface, disabled;
    replyAttr(RouterCommandScratchContext::get(), i, "interface", iface);
    replyAttr(RouterCommandScratchContext::get(), i, "disabled", disabled);
    const bool enabled = disabled != "true";
    if (enabled) {
      _hotspotDetectedGlobal = true;
      if (!iface.isEmpty()) _hotspotEnabledByInterface[iface] = true;
    }
  }
}

void ExistingNetworkScanner::correlateCandidates(
    const String &espIp, const String &espSubnetCidr,
    ExistingNetworkScan::ScanResult &out) {
  out.candidateCount = 0;

  for (uint8_t bi = 0; bi < _bridgeCount; ++bi) {
    if (out.candidateCount >= ExistingNetworkScan::kMaxCandidates) break;

    const String &bridgeName = _bridges[bi].name;
    const String &bridgeComment = _bridges[bi].comment;
    if (bridgeName.isEmpty()) {
      logCandidateDivider();
      RENZFI_ROUTER_API_VERBOSE_LINE("[scan] evaluating bridge=(unnamed)");
      RENZFI_ROUTER_API_VERBOSE_LINE("bridgeFound=false");
      RENZFI_ROUTER_API_VERBOSE_LINE("candidateResult=rejected");
      RENZFI_ROUTER_API_VERBOSE_LINE("reason=bridge_filtered");
      logCandidateDivider();
      continue;
    }

    bool bridgeHadGateway = false;
    const auto range = _addressIndicesByInterface.equal_range(bridgeName);
    for (auto it = range.first; it != range.second; ++it) {
      if (out.candidateCount >= ExistingNetworkScan::kMaxCandidates) break;
      const AddressRow &addrRow = _addresses[it->second];
      bridgeHadGateway = true;

      IPAddress net;
      uint8_t prefix = 0;
      if (!parseIpv4Prefix(addrRow.address, net, prefix)) {
        ExistingNetworkScan::Candidate candidate;
        candidate.bridgeName  = bridgeName;
        candidate.gatewayCidr = addrRow.address;
        logCandidateEvaluation(candidate, espIp, true, false, false, false, false,
                               _hotspotDetectedGlobal, "rejected",
                               "invalid_gateway_cidr");
        continue;
      }

      ExistingNetworkScan::Candidate candidate;
      candidate.id = makeCandidateId(bridgeName, net.toString(), out.candidateCount + 1);
      candidate.bridgeName   = bridgeName;
      candidate.gatewayCidr  = addrRow.address;
      candidate.guestNetwork = networkFromGatewayCidr(addrRow.address);
      candidate.gatewayIp    = net.toString();
      candidate.renzfiManaged =
          isRenzfiComment(bridgeComment) || isRenzfiComment(addrRow.comment);

      candidate.espSubnetOverlap =
          isProblematicEspSubnetOverlap(candidate.guestNetwork, espSubnetCidr, espIp);
      const bool hotspotOnBridge =
          _hotspotEnabledByInterface.find(bridgeName) !=
              _hotspotEnabledByInterface.end() ||
          _hotspotDetectedGlobal;
      candidate.compatibility =
          buildCompatibility(true, true, false, false, false, _apiAccessOk,
                             _firewallLimited, hotspotOnBridge, _hotspotInspectionOk);
      candidate.inspection = buildInspection(!_firewallLimited,
                                             _hotspotInspectionAttempted,
                                             _hotspotInspectionOk);

      if (candidate.espSubnetOverlap) {
        assignCandidateOutcome(candidate, true);
        logCandidateEvaluation(candidate, espIp, true, true, false, false, false,
                               hotspotOnBridge, "rejected", "network_overlap");
        out.candidates[out.candidateCount++] = candidate;
        continue;
      }

      bool dhcpFound = false;
      bool poolFound = false;
      bool dhcpNetworkFound = false;
      const auto dhcpIt = _firstEnabledDhcpServerByBridge.find(bridgeName);
      if (dhcpIt != _firstEnabledDhcpServerByBridge.end()) {
        const DhcpServerRow &dRow = _dhcpServers[dhcpIt->second];
        candidate.dhcpServerName = dRow.name;
        if (isRenzfiComment(dRow.comment)) candidate.renzfiManaged = true;

        const auto poolIt = _poolIndexByName.find(dRow.poolRef);
        if (poolIt != _poolIndexByName.end()) {
          poolFound = true;
          const PoolRow &pRow = _pools[poolIt->second];
          candidate.poolName  = pRow.name;
          candidate.poolRange = pRow.ranges;
          if (isRenzfiComment(pRow.comment)) candidate.renzfiManaged = true;
        }

        const auto netIt = _dhcpNetworkIndexByAddress.find(candidate.guestNetwork);
        if (netIt != _dhcpNetworkIndexByAddress.end()) {
          dhcpNetworkFound = true;
          const DhcpNetworkRow &nRow = _dhcpNetworks[netIt->second];
          candidate.dhcpNetwork = nRow.address;
          if (isRenzfiComment(nRow.comment)) candidate.renzfiManaged = true;
        }

        dhcpFound = true;
      }

      candidate.apiAccessOk = _apiAccessOk;
      candidate.apiAccessNeedsRepair = !_apiAccessOk;
      candidate.compatibility = buildCompatibility(
          true, true, dhcpFound, poolFound, dhcpNetworkFound, _apiAccessOk,
          _firewallLimited, hotspotOnBridge, _hotspotInspectionOk);
      candidate.inspection = buildInspection(!_firewallLimited,
                                             _hotspotInspectionAttempted,
                                             _hotspotInspectionOk);

      String rejectReason;
      if (!dhcpFound) {
        rejectReason = "missing_dhcp_server";
      } else if (!poolFound || candidate.poolName.isEmpty()) {
        rejectReason = "pool_not_linked";
      } else if (!dhcpNetworkFound || candidate.dhcpNetwork.isEmpty()) {
        rejectReason = "missing_dhcp_network";
      } else if (!_apiAccessOk) {
        rejectReason = "firewall_api_not_allowed";
      }

      if (!candidate.renzfiManaged && dhcpFound && poolFound && dhcpNetworkFound) {
        candidate.genericCompatible = true;
      }

      assignCandidateOutcome(candidate, false);
      logCandidateEvaluation(
          candidate, espIp, true, true, dhcpFound, poolFound, dhcpNetworkFound,
          hotspotOnBridge,
          candidate.status == "partial_candidate" ? "partial_candidate"
                                                  : candidate.status,
          rejectReason);

      out.candidates[out.candidateCount++] = candidate;
    }

    if (!bridgeHadGateway) {
      ExistingNetworkScan::Candidate candidate;
      candidate.bridgeName = bridgeName;
      logCandidateEvaluation(candidate, espIp, true, false, false, false, false,
                             _hotspotDetectedGlobal, "rejected",
                             "gateway_not_on_bridge");
    }
  }

  uint8_t compatibleCount = 0;
  uint8_t partialCount    = 0;
  bool ambiguousOverlap   = false;
  String firstCompatibleNetwork;
  for (uint8_t i = 0; i < out.candidateCount; ++i) {
    const auto &c = out.candidates[i];
    if (c.status == "rejected_overlap") continue;
    if (c.status == "compatible_candidate") {
      compatibleCount++;
      if (firstCompatibleNetwork.isEmpty()) {
        firstCompatibleNetwork = c.guestNetwork;
      } else if (firstCompatibleNetwork != c.guestNetwork) {
        ambiguousOverlap = true;
      }
    } else if (c.status == "partial_candidate") {
      partialCount++;
    }
  }

  if (compatibleCount == 0 && partialCount == 0) {
    out.scanStatus = "no_compatible_candidate";
  } else if (ambiguousOverlap) {
    out.scanStatus = "conflict_ambiguous";
  } else if (compatibleCount > 0) {
    out.scanStatus = "compatible_candidate";
  } else {
    out.scanStatus = "partial_only";
  }

  ExistingNetworkScan::finalizeScanDecision(out);
}

ExistingNetworkScanner::Outcome ExistingNetworkScanner::run(
    EthernetManager *eth, SetupRouterConnectionManager *routerConnection,
    InstallationStateManager *installation, JsonObject dataOut,
    void (*progressFn)(void *, const char *, const char *), void *progressCtx) {
  Outcome outcome;

  const auto pre =
      RouterProvisioningPreconditions::check(installation, routerConnection, eth);
  if (!pre.success) {
    outcome.httpStatus   = pre.httpStatus;
    outcome.errorCode    = pre.errorCode;
    outcome.errorMessage = pre.errorMessage;
    return outcome;
  }

  SetupRouterConnectionManager::ResolvedRouterCredentials credentials;
  SetupRouterConnectionManager::OperationResult credResult;
  if (!routerConnection->resolveRouterCredentials(
          SetupRouterConnectionManager::RouterCredentialSource::Persisted, nullptr,
          credentials, credResult)) {
    outcome.httpStatus = credResult.httpStatus ? credResult.httpStatus : 409;
    outcome.errorCode = credResult.errorCode.isEmpty() ? "ROUTER_CONNECTION_REQUIRED"
                                                       : credResult.errorCode;
    outcome.errorMessage = credResult.errorMessage.isEmpty()
                               ? "Unable to load saved router credentials"
                               : credResult.errorMessage;
    return outcome;
  }

  String stepError, stepErrorCode;
  if (!connectAndLogin(credentials.toRouterInput(), eth, stepError, stepErrorCode,
                       progressFn, progressCtx)) {
    outcome.httpStatus = isLoginFailureCode(stepErrorCode) ? 401 : 503;
    outcome.errorCode = stepErrorCode.isEmpty() ? "ROUTEROS_API_UNAVAILABLE"
                                                : stepErrorCode;
    outcome.errorMessage =
        stepError.isEmpty() ? "Unable to connect to router" : stepError;
    outcome.stage = "scan-login";
    RENZFI_ROUTER_API_FAILURE("scan-login", outcome.errorCode.c_str(),
                              outcome.errorMessage.c_str());
    return outcome;
  }

  // From here on the RouterOS session is open exactly once and must be
  // closed exactly once, on every exit path (success or failure) below.
  struct SessionCloseGuard {
    RouterOsClient &client;
    ~SessionCloseGuard() { client.disconnect("success"); }
  } sessionGuard{_client};

  const uint32_t scanStartMs = millis();
  const String espIp = eth->ip();
  String espSubnetCidr;
  if (eth->hasIp()) {
    IPAddress ip, mask;
    if (ip.fromString(eth->ip()) && mask.fromString(eth->subnet())) {
      const uint32_t maskHost = ipv4ToHostOrder(mask);
      const uint32_t netHost  = ipv4ToHostOrder(ip) & maskHost;
      IPAddress network((netHost >> 24) & 0xFF, (netHost >> 16) & 0xFF,
                        (netHost >> 8) & 0xFF, netHost & 0xFF);
      uint8_t prefix = 0;
      uint32_t walk  = maskHost;
      while (walk & 0x80000000u) {
        prefix++;
        walk <<= 1;
      }
      if (prefix >= 8) espSubnetCidr = network.toString() + "/" + String(prefix);
    }
  }

  const uint32_t deadline = millis() + RenzFiConfig::ROUTER_WORKER_JOB_TIMEOUT_MS - 500;

  if (!fetchBridges(stepError, stepErrorCode, progressFn, progressCtx) ||
      !fetchAddresses(stepError, stepErrorCode, progressFn, progressCtx) ||
      !fetchPools(stepError, stepErrorCode, progressFn, progressCtx) ||
      !fetchDhcpServers(stepError, stepErrorCode, progressFn, progressCtx) ||
      !fetchDhcpNetworks(stepError, stepErrorCode, progressFn, progressCtx) ||
      !fetchFirewall(espIp, stepError, stepErrorCode, progressFn, progressCtx)) {
    outcome.httpStatus = 503;
    outcome.errorCode =
        stepErrorCode.isEmpty() ? "EXISTING_NETWORK_SCAN_FAILED" : stepErrorCode;
    outcome.errorMessage =
        stepError.isEmpty() ? "Existing network scan failed" : stepError;
    outcome.stage = "existing-network-scan";
    RENZFI_ROUTER_API_FAILURE("existing-network-scan", outcome.errorCode.c_str(),
                              outcome.errorMessage.c_str());
    return outcome;
  }

  fetchHotspot(deadline, progressFn, progressCtx);  // best-effort

  reportScanProgress(progressFn, progressCtx, "analyzing", "Analyzing Configuration...");

  ExistingNetworkScan::ScanResult scan;
  scan.espIp           = espIp;
  scan.driver.name      = "mikrotik";
  scan.driver.version   = "routeros";
  scan.driver.api       = String(credentials.apiPort);
  scan.firewallLimited  = _firewallLimited;
  scan.hotspotDetected  = _hotspotDetectedGlobal;

  correlateCandidates(espIp, espSubnetCidr, scan);
  scan.scanDurationMs = millis() - scanStartMs;
  scan.routerHost     = credentials.host;

  ExistingNetworkScan::serializeScanJson(dataOut, scan);
  dataOut["currentEspIp"] = espIp;
  reportScanProgress(progressFn, progressCtx, "done", "Done.");
  outcome.success      = true;
  outcome.httpStatus    = 200;
  outcome.errorMessage  = "Existing network scan complete";
  RENZFI_ROUTER_API_SCAN_COMPLETE();
  return outcome;
}
