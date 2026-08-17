#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "RouterOsClient.h"

namespace ExistingNetworkScan {

static constexpr uint8_t kScanSchemaVersion = 1;
static constexpr uint8_t kMaxCandidates = 8;
static constexpr uint8_t kReplyCap = 24;
static constexpr uint8_t kMaxConfidenceReasons = 8;

namespace CompatLevel {
static constexpr const char *Pass    = "pass";
static constexpr const char *Fail    = "fail";
static constexpr const char *Unknown = "unknown";
static constexpr const char *Warning = "warning";
}  // namespace CompatLevel

namespace InspectionLevel {
static constexpr const char *Complete     = "complete";
static constexpr const char *Skipped      = "skipped";
static constexpr const char *NotSupported = "not_supported";
static constexpr const char *Failed       = "failed";
}  // namespace InspectionLevel

struct CandidateCompatibility {
  String bridge;
  String gateway;
  String dhcp;
  String pool;
  String dhcpNetwork;
  String firewall;
  String hotspot;
};

struct CandidateInspection {
  String bridge;
  String gateway;
  String dhcp;
  String pool;
  String dhcpNetwork;
  String firewall;
  String hotspot;
  String nat;
};

struct CandidateRequirements {
  struct HardReq {
    bool gateway = false;
    bool dhcp = false;
    bool pool = false;
    bool dhcpNetwork = false;
    bool firewall = false;
  } hard;
  struct SoftReq {
    bool hotspot = false;
    bool renzfiMarkers = false;
  } soft;
};

struct ScanDriverInfo {
  String name;     // e.g. "mikrotik", "tp-link", "openwrt"
  String version;  // e.g. "routeros"
  String api;      // e.g. "8728"
};

struct Candidate {
  String id;
  String bridgeName;
  String gatewayCidr;
  String guestNetwork;
  String gatewayIp;
  String poolName;
  String poolRange;
  String dhcpServerName;
  String dhcpNetwork;
  bool   renzfiManaged = false;
  bool   genericCompatible = false;
  bool   apiAccessOk = false;
  bool   apiAccessNeedsRepair = false;
  bool   espSubnetOverlap = false;
  String origin;
  String confidence;
  uint8_t adoptionScore = 0;
  CandidateCompatibility compatibility;
  CandidateInspection inspection;
  CandidateRequirements requirements;
  String confidenceReasons[kMaxConfidenceReasons];
  uint8_t confidenceReasonCount = 0;
  String status;
};

struct ScanResult {
  String espIp;
  String routerHost;
  bool   hotspotDetected = false;
  bool   firewallLimited = false;
  String scanStatus;
  bool   confirmAllowed = false;
  String confirmCandidateId;
  uint8_t confirmCompatibility = 0;
  String confirmConfidence;
  uint32_t scanDurationMs = 0;
  ScanDriverInfo driver;
  Candidate candidates[kMaxCandidates];
  uint8_t candidateCount = 0;
};

void finalizeScanDecision(ScanResult &scan);
const char *failureStatusFromErrorCode(const String &errorCode);
void serializeScanJson(JsonObject dataOut, const ScanResult &scan);

bool parseAdoptionCandidate(JsonObjectConst body, Candidate &out, String &errorOut);

}  // namespace ExistingNetworkScan
