#pragma once

#include <Arduino.h>
#include <stdint.h>

// External LAN coverage AP types and live-LAN IP validation.
// Distinct from ManagementApManager (ESP32 SoftAP at 192.168.4.1).
namespace ExternalAccessPoint {

static constexpr uint8_t kSchemaVersion = 1;
static constexpr uint8_t kMaxAccessPoints = 8;
static constexpr uint8_t kNameMinLen = 1;
static constexpr uint8_t kNameMaxLen = 32;

static constexpr uint32_t kManagementApNetwork = 0xC0A80400u;  // 192.168.4.0
static constexpr uint32_t kManagementApMask = 0xFFFFFF00u;     // /24

enum class Vendor : uint8_t {
  Generic = 0,
  TpLink,
  Ruijie,
  Tenda,
  Other,
};

enum class ReachabilityStatus : uint8_t {
  Unknown = 0,
  Disabled,
  Online,
  NetworkReachable,
  ManagementReachable,
  AuthFailed,  // Reserved. Not produced. No AP authentication/configuration feature.
  Unreachable,
};

enum class ManagementTransport : uint8_t {
  None = 0,
  Http,
  Https,
};

enum class CheckEnqueueStatus : uint8_t {
  Ok = 0,
  NotFound,
  Busy,
  StorageRecovery,
  WorkerUnavailable,
};

enum class CheckJobState : uint8_t {
  Idle = 0,
  Queued,
  Running,
  Completed,
  Failed,
};

enum class CrudStatus : uint8_t {
  Ok = 0,
  InvalidRequest,
  InvalidIp,
  IpReserved,
  IpNotOnLan,
  DuplicateIp,
  LimitReached,
  EthernetNotReady,
  NotFound,
  StorageRecovery,
  StorageError,
  CredentialError,
};

inline const char *vendorLabel(Vendor vendor) {
  switch (vendor) {
    case Vendor::TpLink:
      return "tp-link";
    case Vendor::Ruijie:
      return "ruijie";
    case Vendor::Tenda:
      return "tenda";
    case Vendor::Other:
      return "other";
    case Vendor::Generic:
    default:
      return "generic";
  }
}

inline Vendor parseVendor(const char *raw) {
  if (raw == nullptr || raw[0] == '\0') return Vendor::Generic;
  String value = raw;
  value.trim();
  value.toLowerCase();
  if (value == "tp-link") return Vendor::TpLink;
  if (value == "ruijie") return Vendor::Ruijie;
  if (value == "tenda") return Vendor::Tenda;
  if (value == "other") return Vendor::Other;
  if (value == "generic") return Vendor::Generic;
  return Vendor::Generic;
}

inline bool parseIpv4Packed(const char *text, uint32_t &outPacked) {
  if (text == nullptr) return false;
  String value = text;
  value.trim();
  if (value.length() == 0) return false;

  uint16_t octets[4] = {0, 0, 0, 0};
  uint8_t octetIndex = 0;
  uint16_t current = 0;
  bool digitSeen = false;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value.charAt(i);
    if (c >= '0' && c <= '9') {
      digitSeen = true;
      current = static_cast<uint16_t>(current * 10u + static_cast<uint16_t>(c - '0'));
      if (current > 255) return false;
    } else if (c == '.') {
      if (!digitSeen || octetIndex >= 3) return false;
      octets[octetIndex++] = current;
      current = 0;
      digitSeen = false;
    } else {
      return false;
    }
  }
  if (!digitSeen || octetIndex != 3) return false;
  octets[3] = current;
  outPacked = (static_cast<uint32_t>(octets[0]) << 24) |
              (static_cast<uint32_t>(octets[1]) << 16) |
              (static_cast<uint32_t>(octets[2]) << 8) |
              static_cast<uint32_t>(octets[3]);
  return true;
}

inline uint32_t ipv4Network(uint32_t ip, uint32_t mask) { return ip & mask; }

inline uint32_t ipv4Broadcast(uint32_t ip, uint32_t mask) {
  return (ip & mask) | (~mask);
}

inline bool ipv4OnSubnet(uint32_t ip, uint32_t networkIp, uint32_t mask) {
  return (ip & mask) == (networkIp & mask);
}

// RFC1918 private ranges only — not a specific site subnet (no hardcoding).
inline bool ipv4IsPrivateLan(uint32_t ip) {
  if ((ip & 0xFF000000u) == 0x0A000000u) return true;       // 10.0.0.0/8
  if ((ip & 0xFFF00000u) == 0xAC100000u) return true;       // 172.16.0.0/12
  if ((ip & 0xFFFF0000u) == 0xC0A80000u) return true;       // 192.168.0.0/16
  return false;
}

inline bool ipv4IsUnusableHost(uint32_t ip) {
  if (ip == 0 || ip == 0xFFFFFFFFu) return true;
  if ((ip & 0xFF000000u) == 0x7F000000u) return true;       // 127.0.0.0/8
  if ((ip & 0xFFFF0000u) == 0xA9FE0000u) return true;       // 169.254.0.0/16
  if ((ip & 0xF0000000u) >= 0xE0000000u) return true;       // multicast / reserved
  return false;
}

enum class IpCheckResult : uint8_t {
  Ok = 0,
  InvalidIp,
  EthernetNotReady,
  Reserved,
  NotOnLan,
};

// Live Ethernet must be up. Accepts same-subnet hosts or any other RFC1918
// private address (routed via MikroTik). Sync/Check prove reachability.
// Does not hardcode site subnets (e.g. 192.168.88.0/24). Does not scan or ping.
inline IpCheckResult validateManagementIp(const char *candidateIp,
                                          const char *liveEsp32Ip,
                                          const char *liveGatewayIp,
                                          const char *liveSubnetMask) {
  uint32_t candidate = 0;
  if (!parseIpv4Packed(candidateIp, candidate)) return IpCheckResult::InvalidIp;
  if (ipv4IsUnusableHost(candidate)) return IpCheckResult::InvalidIp;

  uint32_t esp32Ip = 0;
  uint32_t mask = 0;
  if (!parseIpv4Packed(liveEsp32Ip, esp32Ip) ||
      !parseIpv4Packed(liveSubnetMask, mask) || mask == 0 || esp32Ip == 0) {
    return IpCheckResult::EthernetNotReady;
  }

  if (ipv4OnSubnet(candidate, kManagementApNetwork, kManagementApMask)) {
    return IpCheckResult::Reserved;
  }

  if (candidate == esp32Ip) return IpCheckResult::Reserved;

  uint32_t gateway = 0;
  if (parseIpv4Packed(liveGatewayIp, gateway) && gateway != 0 &&
      candidate == gateway) {
    return IpCheckResult::Reserved;
  }

  // Same subnet: also reject that subnet's network / broadcast addresses.
  if (ipv4OnSubnet(candidate, esp32Ip, mask)) {
    const uint32_t network = ipv4Network(esp32Ip, mask);
    const uint32_t broadcast = ipv4Broadcast(esp32Ip, mask);
    if (candidate == network || candidate == broadcast) {
      return IpCheckResult::Reserved;
    }
    return IpCheckResult::Ok;
  }

  // Routed private LAN (any RFC1918 site/subnet — not hardcoded).
  if (!ipv4IsPrivateLan(candidate)) return IpCheckResult::NotOnLan;
  return IpCheckResult::Ok;
}

inline const char *reachabilityLabel(ReachabilityStatus status) {
  switch (status) {
    case ReachabilityStatus::Disabled:
      return "disabled";
    case ReachabilityStatus::Online:
      return "online";
    case ReachabilityStatus::NetworkReachable:
      return "network_reachable";
    case ReachabilityStatus::ManagementReachable:
      return "management_reachable";
    case ReachabilityStatus::AuthFailed:
      return "auth_failed";
    case ReachabilityStatus::Unreachable:
      return "unreachable";
    case ReachabilityStatus::Unknown:
    default:
      return "unknown";
  }
}

inline const char *jobStateLabel(CheckJobState state) {
  switch (state) {
    case CheckJobState::Queued:
      return "queued";
    case CheckJobState::Running:
      return "running";
    case CheckJobState::Completed:
      return "completed";
    case CheckJobState::Failed:
      return "failed";
    case CheckJobState::Idle:
    default:
      return "idle";
  }
}

// Stage C classification. Never returns AuthFailed (no authentication).
inline ReachabilityStatus classifyReachability(bool disabled, bool ethernetReady,
                                               bool icmpOk, bool tcpOk) {
  if (disabled) return ReachabilityStatus::Disabled;
  if (!ethernetReady) return ReachabilityStatus::Unknown;
  if (icmpOk && tcpOk) return ReachabilityStatus::Online;
  if (icmpOk && !tcpOk) return ReachabilityStatus::NetworkReachable;
  if (!icmpOk && tcpOk) return ReachabilityStatus::ManagementReachable;
  return ReachabilityStatus::Unreachable;
}

inline bool reachabilityIsSuccessful(ReachabilityStatus status) {
  return status == ReachabilityStatus::Online ||
         status == ReachabilityStatus::NetworkReachable ||
         status == ReachabilityStatus::ManagementReachable;
}

inline const char *checkEnqueueCode(CheckEnqueueStatus status) {
  switch (status) {
    case CheckEnqueueStatus::Ok:
      return "OK";
    case CheckEnqueueStatus::NotFound:
      return "ACCESS_POINT_NOT_FOUND";
    case CheckEnqueueStatus::Busy:
      return "ACCESS_POINT_CHECK_BUSY";
    case CheckEnqueueStatus::StorageRecovery:
      return "STORAGE_RECOVERY_IN_PROGRESS";
    case CheckEnqueueStatus::WorkerUnavailable:
      return "CHECK_FAILED";
    default:
      return "CHECK_FAILED";
  }
}

inline const char *checkEnqueueMessage(CheckEnqueueStatus status) {
  switch (status) {
    case CheckEnqueueStatus::Ok:
      return "Check queued";
    case CheckEnqueueStatus::NotFound:
      return "Access point not found";
    case CheckEnqueueStatus::Busy:
      return "An access point check is already running";
    case CheckEnqueueStatus::StorageRecovery:
      return "Storage recovery in progress";
    case CheckEnqueueStatus::WorkerUnavailable:
      return "Access point check worker is unavailable";
    default:
      return "Unable to start access point check";
  }
}

inline int checkEnqueueHttpStatus(CheckEnqueueStatus status) {
  switch (status) {
    case CheckEnqueueStatus::Ok:
      return 202;
    case CheckEnqueueStatus::NotFound:
      return 404;
    case CheckEnqueueStatus::Busy:
    case CheckEnqueueStatus::StorageRecovery:
    case CheckEnqueueStatus::WorkerUnavailable:
      return 503;
    default:
      return 500;
  }
}

inline const char *crudCode(CrudStatus status) {
  switch (status) {
    case CrudStatus::Ok:
      return "OK";
    case CrudStatus::InvalidRequest:
      return "INVALID_REQUEST";
    case CrudStatus::InvalidIp:
      return "INVALID_IP";
    case CrudStatus::IpReserved:
      return "IP_RESERVED";
    case CrudStatus::IpNotOnLan:
      return "IP_NOT_ON_LAN";
    case CrudStatus::DuplicateIp:
      return "DUPLICATE_IP";
    case CrudStatus::LimitReached:
      return "LIMIT_REACHED";
    case CrudStatus::EthernetNotReady:
      return "ETHERNET_NOT_READY";
    case CrudStatus::NotFound:
      return "NOT_FOUND";
    case CrudStatus::StorageRecovery:
      return "STORAGE_RECOVERY_IN_PROGRESS";
    case CrudStatus::StorageError:
      return "STORAGE_ERROR";
    case CrudStatus::CredentialError:
      return "CREDENTIAL_PROTECT_FAILED";
    default:
      return "INVALID_REQUEST";
  }
}

inline const char *crudMessage(CrudStatus status) {
  switch (status) {
    case CrudStatus::Ok:
      return "OK";
    case CrudStatus::InvalidRequest:
      return "Invalid access point request";
    case CrudStatus::InvalidIp:
      return "Invalid IPv4 management address";
    case CrudStatus::IpReserved:
      return "Management IP is reserved";
    case CrudStatus::IpNotOnLan:
      return "Management IP must be a private LAN address reachable via Ethernet";
    case CrudStatus::DuplicateIp:
      return "Management IP is already registered";
    case CrudStatus::LimitReached:
      return "Maximum of 8 access points reached";
    case CrudStatus::EthernetNotReady:
      return "Ethernet does not have a usable IP";
    case CrudStatus::NotFound:
      return "Access point not found";
    case CrudStatus::StorageRecovery:
      return "Storage recovery in progress";
    case CrudStatus::StorageError:
      return "Unable to persist access points";
    case CrudStatus::CredentialError:
      return "Unable to protect access point password";
    default:
      return "Invalid access point request";
  }
}

inline int crudHttpStatus(CrudStatus status) {
  switch (status) {
    case CrudStatus::Ok:
      return 200;
    case CrudStatus::NotFound:
      return 404;
    case CrudStatus::EthernetNotReady:
    case CrudStatus::StorageRecovery:
      return 503;
    case CrudStatus::StorageError:
    case CrudStatus::CredentialError:
      return 500;
    default:
      return 400;
  }
}

}  // namespace ExternalAccessPoint
