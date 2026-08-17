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

enum class IpCheckResult : uint8_t {
  Ok = 0,
  InvalidIp,
  EthernetNotReady,
  Reserved,
  NotOnLan,
};

// Live Ethernet LAN only. Does not scan, ping, or use NetworkSettings defaults.
inline IpCheckResult validateManagementIp(const char *candidateIp,
                                          const char *liveEsp32Ip,
                                          const char *liveGatewayIp,
                                          const char *liveSubnetMask) {
  uint32_t candidate = 0;
  if (!parseIpv4Packed(candidateIp, candidate)) return IpCheckResult::InvalidIp;

  uint32_t esp32Ip = 0;
  uint32_t mask = 0;
  if (!parseIpv4Packed(liveEsp32Ip, esp32Ip) ||
      !parseIpv4Packed(liveSubnetMask, mask) || mask == 0 || esp32Ip == 0) {
    return IpCheckResult::EthernetNotReady;
  }

  if (ipv4OnSubnet(candidate, kManagementApNetwork, kManagementApMask)) {
    return IpCheckResult::Reserved;
  }

  const uint32_t network = ipv4Network(esp32Ip, mask);
  const uint32_t broadcast = ipv4Broadcast(esp32Ip, mask);
  if (candidate == network || candidate == broadcast || candidate == esp32Ip) {
    return IpCheckResult::Reserved;
  }

  uint32_t gateway = 0;
  if (parseIpv4Packed(liveGatewayIp, gateway) && gateway != 0 &&
      candidate == gateway) {
    return IpCheckResult::Reserved;
  }

  if (!ipv4OnSubnet(candidate, esp32Ip, mask)) return IpCheckResult::NotOnLan;
  return IpCheckResult::Ok;
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
      return "Management IP is not on the live Ethernet LAN";
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
