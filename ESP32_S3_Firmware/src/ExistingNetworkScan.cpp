#include "ExistingNetworkScan.h"

namespace {

void writeInspectionJson(JsonObject obj,
                         const ExistingNetworkScan::CandidateInspection &in) {
  obj["bridge"]       = in.bridge;
  obj["gateway"]      = in.gateway;
  obj["dhcp"]         = in.dhcp;
  obj["pool"]         = in.pool;
  obj["dhcpNetwork"]  = in.dhcpNetwork;
  obj["firewall"]     = in.firewall;
  obj["hotspot"]      = in.hotspot;
  obj["nat"]          = in.nat;
}

static constexpr uint8_t kConfirmAdoptionScoreThreshold = 80;

bool confidenceAcceptable(const String &confidence) {
  String normalized = confidence;
  normalized.trim();
  normalized.toLowerCase();
  return normalized == "high" || normalized == "medium";
}

bool candidateEligibleForConfirm(const ExistingNetworkScan::Candidate &candidate) {
  if (candidate.status == "rejected_overlap") return false;
  if (!candidate.apiAccessOk) return false;
  if (candidate.adoptionScore < kConfirmAdoptionScoreThreshold) return false;
  if (!confidenceAcceptable(candidate.confidence)) return false;
  return candidate.status == "compatible_candidate" ||
         candidate.status == "partial_candidate";
}

// Router Scan (Step 3) confirm criteria after the wizard simplification:
// bridge + gateway + hotspot on a successful scan are enough to proceed to
// Wi-Fi Configuration. DHCP/pool/firewall gaps are handled during configure,
// not at scan time — the old adoptionScore>=80 gate left confirmAllowed false
// while the UI correctly showed Bridge/Hotspot as detected.
bool routerScanStepConfirmEligible(
    const ExistingNetworkScan::Candidate &candidate) {
  if (candidate.status == "rejected_overlap") return false;
  if (candidate.espSubnetOverlap) return false;
  const auto &c = candidate.compatibility;
  return c.bridge == ExistingNetworkScan::CompatLevel::Pass &&
         c.gateway == ExistingNetworkScan::CompatLevel::Pass &&
         c.hotspot == ExistingNetworkScan::CompatLevel::Pass;
}

const ExistingNetworkScan::Candidate *bestRouterScanStepCandidate(
    const ExistingNetworkScan::ScanResult &scan) {
  const ExistingNetworkScan::Candidate *best = nullptr;
  for (uint8_t i = 0; i < scan.candidateCount; ++i) {
    const ExistingNetworkScan::Candidate &candidate = scan.candidates[i];
    if (!routerScanStepConfirmEligible(candidate)) continue;
    if (!best || candidate.adoptionScore > best->adoptionScore) {
      best = &candidate;
    }
  }
  return best;
}

const ExistingNetworkScan::Candidate *bestConfirmCandidate(
    const ExistingNetworkScan::ScanResult &scan) {
  const ExistingNetworkScan::Candidate *best = nullptr;
  for (uint8_t i = 0; i < scan.candidateCount; ++i) {
    const ExistingNetworkScan::Candidate &candidate = scan.candidates[i];
    if (!candidateEligibleForConfirm(candidate)) continue;
    if (!best || candidate.adoptionScore > best->adoptionScore) {
      best = &candidate;
    }
  }
  return best;
}

uint8_t countVisibleCandidates(const ExistingNetworkScan::ScanResult &scan) {
  uint8_t count = 0;
  for (uint8_t i = 0; i < scan.candidateCount; ++i) {
    if (scan.candidates[i].status != "rejected_overlap") count++;
  }
  return count;
}

}  // namespace

namespace ExistingNetworkScan {

void finalizeScanDecision(ScanResult &scan) {
  scan.confirmAllowed       = false;
  scan.confirmCandidateId.clear();
  scan.confirmCompatibility = 0;
  scan.confirmConfidence.clear();

  if (scan.scanStatus == "no_compatible_candidate" ||
      scan.scanStatus == "conflict_ambiguous") {
    return;
  }

  const Candidate *best = bestConfirmCandidate(scan);
  if (!best) {
    best = bestRouterScanStepCandidate(scan);
  }
  if (!best) return;

  scan.confirmAllowed       = true;
  scan.confirmCandidateId   = best->id;
  scan.confirmCompatibility = best->adoptionScore;
  scan.confirmConfidence    = best->confidence.isEmpty() ? "medium" : best->confidence;

  if (scan.scanStatus == "partial_only") {
    scan.scanStatus = "compatible_candidate";
  }
}

const char *failureStatusFromErrorCode(const String &errorCode) {
  if (errorCode == "API_LOGIN_FAILED") return "router_login_failed";
  if (errorCode == "TCP_CONNECT_FAILED" || errorCode == "ROUTEROS_API_UNAVAILABLE") {
    return "router_unreachable";
  }
  if (errorCode == "ROUTER_CONNECTION_REQUIRED" ||
      errorCode == "ROUTER_CONFIGURE_REQUIRED") {
    return "invalid_configuration";
  }
  if (errorCode == "ETHERNET_NOT_READY") return "api_failure";
  if (errorCode == "EXISTING_NETWORK_SCAN_FAILED" ||
      errorCode == "ROUTER_INSPECTION_FAILED" ||
      errorCode == "API_TRAP" ||
      errorCode == "ROUTEROS_API_FATAL") {
    return "fatal_error";
  }
  return "api_failure";
}

void serializeScanJson(JsonObject dataOut, const ScanResult &scan) {
  dataOut["schemaVersion"]   = ExistingNetworkScan::kScanSchemaVersion;
  dataOut["previewMode"]     = "existing_scan";
  dataOut["espIp"]           = scan.espIp;
  dataOut["hotspotDetected"] = scan.hotspotDetected;
  dataOut["scanStatus"]      = scan.scanStatus;
  dataOut["status"]          = scan.scanStatus;
  dataOut["confirmAllowed"]  = scan.confirmAllowed;
  dataOut["firewallLimited"] = scan.firewallLimited;
  dataOut["scanDurationMs"]  = scan.scanDurationMs;
  dataOut["candidateCount"]  = countVisibleCandidates(scan);
  if (!scan.routerHost.isEmpty()) {
    dataOut["router"] = scan.routerHost;
  }
  if (scan.confirmAllowed) {
    if (!scan.confirmCandidateId.isEmpty()) {
      dataOut["confirmCandidateId"] = scan.confirmCandidateId;
    }
    if (scan.confirmCompatibility > 0) {
      dataOut["compatibility"] = scan.confirmCompatibility;
    }
    if (!scan.confirmConfidence.isEmpty()) {
      dataOut["confidence"] = scan.confirmConfidence;
    }
  }

  JsonObject driver = dataOut.createNestedObject("driver");
  driver["name"]    = scan.driver.name;
  driver["version"] = scan.driver.version;
  driver["api"]     = scan.driver.api;

  JsonArray candidates = dataOut.createNestedArray("candidates");
  for (uint8_t i = 0; i < scan.candidateCount; ++i) {
    const Candidate &c = scan.candidates[i];
    JsonObject row        = candidates.createNestedObject();
    row["id"]             = c.id;
    row["bridgeName"]     = c.bridgeName;
    row["bridge"]         = c.bridgeName;
    row["gatewayCidr"]    = c.gatewayCidr;
    row["gatewayIp"]      = c.gatewayIp;
    row["guestNetwork"]   = c.guestNetwork;
    row["poolName"]       = c.poolName;
    row["poolRange"]      = c.poolRange;
    row["dhcpServerName"] = c.dhcpServerName;
    row["dhcpNetwork"]    = c.dhcpNetwork;
    row["renzfiManaged"]  = c.renzfiManaged;
    if (!c.origin.isEmpty()) row["origin"] = c.origin;
    if (!c.confidence.isEmpty()) row["confidence"] = c.confidence;
    row["adoptionScore"] = c.adoptionScore;
    if (c.confidenceReasonCount > 0) {
      JsonArray reasons = row.createNestedArray("confidenceReasons");
      for (uint8_t ri = 0; ri < c.confidenceReasonCount; ++ri) {
        reasons.add(c.confidenceReasons[ri]);
      }
    }
    JsonObject compat = row.createNestedObject("compatibility");
    compat["bridge"]       = c.compatibility.bridge;
    compat["gateway"]      = c.compatibility.gateway;
    compat["dhcp"]         = c.compatibility.dhcp;
    compat["pool"]         = c.compatibility.pool;
    compat["dhcpNetwork"]  = c.compatibility.dhcpNetwork;
    compat["firewall"]     = c.compatibility.firewall;
    compat["hotspot"]      = c.compatibility.hotspot;
    JsonObject inspection = row.createNestedObject("inspection");
    writeInspectionJson(inspection, c.inspection);
    JsonObject req = row.createNestedObject("requirements");
    JsonObject hard = req.createNestedObject("hard");
    hard["gateway"]     = c.requirements.hard.gateway;
    hard["dhcp"]        = c.requirements.hard.dhcp;
    hard["pool"]        = c.requirements.hard.pool;
    hard["dhcpNetwork"] = c.requirements.hard.dhcpNetwork;
    hard["firewall"]    = c.requirements.hard.firewall;
    JsonObject soft = req.createNestedObject("soft");
    soft["hotspot"]       = c.requirements.soft.hotspot;
    soft["renzfiMarkers"] = c.requirements.soft.renzfiMarkers;
    row["apiAccessOk"]    = c.apiAccessOk;
    row["apiAccessNeedsRepair"] = c.apiAccessNeedsRepair;
    row["espSubnetOverlap"] = c.espSubnetOverlap;
    row["status"]         = c.status;
  }

  JsonObject summary = dataOut.createNestedObject("existingNetworkSummary");
  summary["hotspotDetected"] = scan.hotspotDetected;
  summary["confidence"]      = scan.scanStatus;
  summary["confirmAllowed"]  = scan.confirmAllowed;
  summary["candidateCount"]  = countVisibleCandidates(scan);
  if (scan.confirmAllowed && scan.confirmCompatibility > 0) {
    summary["compatibility"] = scan.confirmCompatibility;
  }
  if (scan.confirmAllowed && !scan.confirmConfidence.isEmpty()) {
    summary["confidenceLevel"] = scan.confirmConfidence;
  }
}

bool parseAdoptionCandidate(JsonObjectConst body, Candidate &out, String &errorOut) {
  out = Candidate{};
  out.id             = body["candidateId"] | "";
  out.bridgeName     = body["bridgeName"] | "";
  out.gatewayCidr    = body["gatewayCidr"] | "";
  out.guestNetwork   = body["guestNetwork"] | "";
  out.gatewayIp      = body["gatewayIp"] | "";
  out.poolName       = body["poolName"] | "";
  out.poolRange      = body["poolRange"] | "";
  out.dhcpServerName = body["dhcpServerName"] | "";
  out.dhcpNetwork    = body["dhcpNetwork"] | "";
  out.origin         = body["origin"] | "";
  out.confidence     = body["confidence"] | "";
  out.renzfiManaged  = (out.origin == "renzfi") || (body["renzfiManaged"] | false);

  if (out.bridgeName.isEmpty() || out.gatewayCidr.isEmpty() ||
      out.guestNetwork.isEmpty() || out.dhcpServerName.isEmpty() ||
      out.poolName.isEmpty() || out.dhcpNetwork.isEmpty()) {
    errorOut = "Adoption payload missing required network fields";
    return false;
  }
  if (out.id.isEmpty()) out.id = out.bridgeName;
  return true;
}

}  // namespace ExistingNetworkScan
