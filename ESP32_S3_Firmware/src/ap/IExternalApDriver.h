#pragma once

#include <stdint.h>

#include "ExternalAccessPointTypes.h"

// Reachability probe contract only. Not a vendor configuration driver.
// Implementations must not authenticate, send configuration, or download AP UI.
namespace ExternalAccessPoint {

struct ProbeTarget {
  const char *managementIp = nullptr;
};

struct ProbeResult {
  bool icmpOk = false;
  bool tcpOk = false;
  bool icmpLatencyValid = false;
  bool tcpLatencyValid = false;
  uint32_t icmpLatencyMs = 0;
  uint32_t tcpLatencyMs = 0;
  ManagementTransport transport = ManagementTransport::None;
  const char *errorCode = nullptr;
};

class IExternalApDriver {
 public:
  virtual ~IExternalApDriver() = default;
  virtual ProbeResult probe(const ProbeTarget &target) = 0;
};

}  // namespace ExternalAccessPoint
