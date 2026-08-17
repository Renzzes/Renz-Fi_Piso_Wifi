#pragma once

#include "ap/IExternalApDriver.h"

// ICMP echo (count=1) plus TCP connect to port 80, then 443.
// No HTTP request body, no authentication, no vendor API.
class GenericApDriver : public ExternalAccessPoint::IExternalApDriver {
 public:
  ExternalAccessPoint::ProbeResult probe(
      const ExternalAccessPoint::ProbeTarget &target) override;

 private:
  static constexpr uint32_t kIcmpTimeoutMs = 2000;
  static constexpr uint32_t kTcpTimeoutMs = 2000;

  bool pingOnce(const char *ip, bool &ok, uint32_t &latencyMs,
                const char *&errorCode);
  bool tcpConnect(const char *ip, uint16_t port, uint32_t timeoutMs,
                  bool &ok, uint32_t &latencyMs);
};
