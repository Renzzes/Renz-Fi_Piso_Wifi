#include "ap/GenericApDriver.h"

#include <Arduino.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_netif.h>
#include <lwip/etharp.h>
#include <lwip/inet.h>
#include <lwip/netif.h>
#include <lwip/sockets.h>
#include <lwip/tcpip.h>
#include <ping/ping_sock.h>

#include "DmaMemoryMonitor.h"

namespace {

constexpr uint32_t kPingWaitMs = 2700;

struct PingWait {
  SemaphoreHandle_t done = nullptr;
  bool success = false;
  uint32_t latencyMs = 0;
  bool ended = false;
};

void onPingSuccess(esp_ping_handle_t handle, void *args) {
  auto *ctx = static_cast<PingWait *>(args);
  if (!ctx) return;
  uint32_t elapsedMs = 0;
  esp_ping_get_profile(handle, ESP_PING_PROF_TIMEGAP, &elapsedMs,
                       sizeof(elapsedMs));
  ctx->success = true;
  ctx->latencyMs = elapsedMs;
}

void onPingTimeout(esp_ping_handle_t handle, void *args) {
  (void)handle;
  auto *ctx = static_cast<PingWait *>(args);
  if (!ctx) return;
  ctx->success = false;
}

void onPingEnd(esp_ping_handle_t handle, void *args) {
  (void)handle;
  auto *ctx = static_cast<PingWait *>(args);
  if (!ctx) return;
  ctx->ended = true;
  if (ctx->done) xSemaphoreGive(ctx->done);
}

uint32_t resolveEthernetPingInterface() {
  esp_netif_t *ethNetif = esp_netif_get_handle_from_ifkey("ETH_DEF");
  if (!ethNetif) return 0;
  return esp_netif_get_netif_impl_index(ethNetif);
}

bool resolveEthernetLocalIp(in_addr_t &out) {
  out = 0;
  esp_netif_t *ethNetif = esp_netif_get_handle_from_ifkey("ETH_DEF");
  if (!ethNetif) return false;
  esp_netif_ip_info_t info{};
  if (esp_netif_get_ip_info(ethNetif, &info) != ESP_OK) return false;
  out = info.ip.addr;
  return out != 0;
}

// Caller must hold LOCK_TCPIP_CORE().
struct netif *resolveEthernetLwipNetifLocked() {
  const uint32_t iface = resolveEthernetPingInterface();
  if (iface == 0) return nullptr;
  return netif_get_by_index(static_cast<u8_t>(iface));
}

bool ipv4SameSubnet(in_addr_t a, in_addr_t b, in_addr_t mask) {
  if (mask == 0) return false;
  return (a & mask) == (b & mask);
}

// Resolve neighbor MAC. Never holds TCPIP core lock across vTaskDelay.
bool resolveNeighborMac(struct netif *netif, const ip4_addr_t &ip,
                        struct eth_addr &out) {
  if (netif == nullptr) return false;

  struct eth_addr *ethRet = nullptr;
  const ip4_addr_t *ipRet = nullptr;

  LOCK_TCPIP_CORE();
  if (etharp_find_addr(netif, &ip, &ethRet, &ipRet) >= 0 && ethRet != nullptr) {
    out = *ethRet;
    UNLOCK_TCPIP_CORE();
    return true;
  }
  (void)etharp_query(netif, &ip, nullptr);
  UNLOCK_TCPIP_CORE();

  for (int attempt = 0; attempt < 12; ++attempt) {
    vTaskDelay(pdMS_TO_TICKS(50));
    ethRet = nullptr;
    ipRet = nullptr;
    LOCK_TCPIP_CORE();
    const bool found =
        etharp_find_addr(netif, &ip, &ethRet, &ipRet) >= 0 && ethRet != nullptr;
    if (found) {
      out = *ethRet;
      UNLOCK_TCPIP_CORE();
      return true;
    }
    UNLOCK_TCPIP_CORE();
  }
  return false;
}

// Same-subnet APs on a different L2 segment (e.g. guest bridge) are reached
// via the default gateway. Install a temporary static ARP entry so lwIP sends
// to the router MAC instead of failing ARP on the local Ethernet segment.
//
// FORENSIC (2026-08-23): Calling netif_get_by_index / etharp_* from
// ap_check_worker WITHOUT LOCK_TCPIP_CORE() asserted and rebooted:
//   assert failed: netif_get_by_index
//   (Required to lock TCPIP core functionality!)
class GatewayArpRoute {
 public:
  GatewayArpRoute() = default;
  GatewayArpRoute(const GatewayArpRoute &) = delete;
  GatewayArpRoute &operator=(const GatewayArpRoute &) = delete;

  bool install(const char *targetIp) {
    if (targetIp == nullptr || targetIp[0] == '\0') return false;

#if !ETHARP_SUPPORT_STATIC_ENTRIES
    (void)targetIp;
    return false;
#else
    esp_netif_t *ethNetif = esp_netif_get_handle_from_ifkey("ETH_DEF");
    if (ethNetif == nullptr) return false;
    esp_netif_ip_info_t info{};
    if (esp_netif_get_ip_info(ethNetif, &info) != ESP_OK) return false;
    if (info.ip.addr == 0 || info.gw.addr == 0 || info.netmask.addr == 0) {
      return false;
    }

    ip4_addr_t target{};
    if (!ip4addr_aton(targetIp, &target)) return false;
    // Off-subnet targets already go via the gateway — no ARP override needed.
    if (!ipv4SameSubnet(info.ip.addr, target.addr, info.netmask.addr)) {
      return true;
    }
    if (target.addr == info.ip.addr) return true;

    LOCK_TCPIP_CORE();
    netif_ = resolveEthernetLwipNetifLocked();
    UNLOCK_TCPIP_CORE();
    if (netif_ == nullptr) return false;

    ip4_addr_t gateway{};
    gateway.addr = info.gw.addr;
    struct eth_addr gatewayMac{};
    if (!resolveNeighborMac(netif_, gateway, gatewayMac)) {
      Serial.println("[ap-check] gateway mac unresolved for routed AP probe");
      return false;
    }

    LOCK_TCPIP_CORE();
    const err_t added = etharp_add_static_entry(&target, &gatewayMac);
    UNLOCK_TCPIP_CORE();
    if (added != ERR_OK) {
      Serial.println("[ap-check] static arp install failed");
      return false;
    }

    target_ = target;
    installed_ = true;
    Serial.printf("[ap-check] routed-via-gw target=%s gw=%s\n", targetIp,
                  ip4addr_ntoa(&gateway));
    return true;
#endif
  }

  ~GatewayArpRoute() { remove(); }

 private:
  bool installed_ = false;
  ip4_addr_t target_{};
  struct netif *netif_ = nullptr;

  void remove() {
#if ETHARP_SUPPORT_STATIC_ENTRIES
    if (!installed_) return;
    LOCK_TCPIP_CORE();
    (void)etharp_remove_static_entry(&target_);
    UNLOCK_TCPIP_CORE();
    installed_ = false;
#endif
  }
};

void teardownPingSession(esp_ping_handle_t ping, PingWait &wait) {
  if (ping == nullptr) return;
  if (!wait.ended) {
    esp_ping_stop(ping);
    if (wait.done &&
        xSemaphoreTake(wait.done, pdMS_TO_TICKS(kPingWaitMs)) != pdTRUE) {
      // Session did not signal end; avoid deleting the semaphore while the
      // ping callback may still be running.
      vTaskDelay(pdMS_TO_TICKS(50));
    }
  }
  esp_ping_delete_session(ping);
  vTaskDelay(pdMS_TO_TICKS(20));
}

}  // namespace

bool GenericApDriver::pingOnce(const char *ip, bool &ok, uint32_t &latencyMs,
                               const char *&errorCode) {
  ok = false;
  latencyMs = 0;
  errorCode = "ICMP_FAILED";
  if (ip == nullptr || ip[0] == '\0') return false;

  ip_addr_t target{};
  if (!ipaddr_aton(ip, &target)) {
    errorCode = "ICMP_FAILED";
    return false;
  }

  PingWait wait;
  wait.done = xSemaphoreCreateBinary();
  if (!wait.done) {
    errorCode = "ICMP_FAILED";
    return false;
  }

  esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
  config.target_addr = target;
  config.count = 1;
  config.interval_ms = 0;
  config.timeout_ms = kIcmpTimeoutMs;
  config.data_size = 32;
  // Force ICMP over Ethernet; default netif may be the setup AP.
  const uint32_t iface = resolveEthernetPingInterface();
  if (iface > 0) config.interface = iface;

  esp_ping_callbacks_t callbacks{};
  callbacks.on_ping_success = onPingSuccess;
  callbacks.on_ping_timeout = onPingTimeout;
  callbacks.on_ping_end = onPingEnd;
  callbacks.cb_args = &wait;

  esp_ping_handle_t ping = nullptr;
  const esp_err_t created = esp_ping_new_session(&config, &callbacks, &ping);
  if (created != ESP_OK || ping == nullptr) {
    vSemaphoreDelete(wait.done);
    errorCode = "ICMP_FAILED";
    return false;
  }

  const esp_err_t started = esp_ping_start(ping);
  if (started != ESP_OK) {
    esp_ping_delete_session(ping);
    vSemaphoreDelete(wait.done);
    errorCode = "ICMP_FAILED";
    return false;
  }

  const TickType_t waitTicks = pdMS_TO_TICKS(kIcmpTimeoutMs + 700);
  if (xSemaphoreTake(wait.done, waitTicks) != pdTRUE && !wait.ended) {
    esp_ping_stop(ping);
    if (wait.done) {
      xSemaphoreTake(wait.done, pdMS_TO_TICKS(500));
    }
  }
  teardownPingSession(ping, wait);
  if (wait.done) {
    vSemaphoreDelete(wait.done);
    wait.done = nullptr;
  }

  ok = wait.success;
  if (ok) {
    latencyMs = wait.latencyMs;
    errorCode = nullptr;
  } else {
    errorCode = "ICMP_TIMEOUT";
  }
  return true;
}

bool GenericApDriver::tcpConnect(const char *ip, uint16_t port,
                                 uint32_t timeoutMs, bool &ok,
                                 uint32_t &latencyMs) {
  ok = false;
  latencyMs = 0;
  if (ip == nullptr || ip[0] == '\0') return false;

  const int sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock < 0) return false;

  in_addr_t localIp = 0;
  if (resolveEthernetLocalIp(localIp)) {
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = localIp;
    local.sin_port = 0;
    if (::bind(sock, reinterpret_cast<sockaddr *>(&local), sizeof(local)) !=
        0) {
      Serial.printf("[ap-check] tcp port=%u bind errno=%d\n",
                    static_cast<unsigned>(port), errno);
    }
  }

  const int flags = fcntl(sock, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_aton(ip, &addr.sin_addr) == 0) {
    ::close(sock);
    return false;
  }

  const uint32_t startedMs = millis();
  const int rc =
      ::connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
  if (rc == 0) {
    latencyMs = millis() - startedMs;
    ok = true;
    ::close(sock);
    return true;
  }

  if (errno != EINPROGRESS && errno != EALREADY) {
    Serial.printf("[ap-check] tcp port=%u connect errno=%d\n",
                  static_cast<unsigned>(port), errno);
    ::close(sock);
    return true;
  }

  fd_set writeSet;
  FD_ZERO(&writeSet);
  FD_SET(sock, &writeSet);
  timeval tv{};
  tv.tv_sec = static_cast<long>(timeoutMs / 1000u);
  tv.tv_usec = static_cast<long>((timeoutMs % 1000u) * 1000u);
  const int selected = ::select(sock + 1, nullptr, &writeSet, nullptr, &tv);
  if (selected > 0 && FD_ISSET(sock, &writeSet)) {
    int soError = 0;
    socklen_t len = sizeof(soError);
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &soError, &len) == 0 &&
        soError == 0) {
      ok = true;
    }
  }
  latencyMs = millis() - startedMs;
  ::close(sock);
  return true;
}

ExternalAccessPoint::ProbeResult GenericApDriver::probe(
    const ExternalAccessPoint::ProbeTarget &target) {
  ExternalAccessPoint::ProbeResult result;
  if (target.managementIp == nullptr || target.managementIp[0] == '\0') {
    result.errorCode = "ICMP_FAILED";
    return result;
  }

  GatewayArpRoute routedProbe;
  if (!routedProbe.install(target.managementIp)) {
    Serial.println("[ap-check] routed-via-gw setup failed; probing directly");
  }

  const char *icmpError = nullptr;
  pingOnce(target.managementIp, result.icmpOk, result.icmpLatencyMs, icmpError);
  result.icmpLatencyValid = result.icmpOk;
  DmaMemoryMonitor::logSnapshot("ap-check-after-icmp");
  vTaskDelay(pdMS_TO_TICKS(1));

  Serial.printf("[ap-check] icmp result=%s",
                result.icmpOk ? "success" : "timeout");
  if (result.icmpOk) {
    Serial.printf(" latency=%ums\n",
                  static_cast<unsigned>(result.icmpLatencyMs));
  } else {
    Serial.println();
  }

  DmaMemoryMonitor::logSnapshot("ap-check-before-tcp");
  bool tcp80 = false;
  uint32_t tcp80Ms = 0;
  tcpConnect(target.managementIp, 80, kTcpTimeoutMs, tcp80, tcp80Ms);
  vTaskDelay(pdMS_TO_TICKS(1));
  Serial.printf("[ap-check] tcp port=80 result=%s",
                tcp80 ? "success" : "fail");
  if (tcp80) {
    Serial.printf(" latency=%ums\n", static_cast<unsigned>(tcp80Ms));
  } else {
    Serial.println();
  }

  bool tcp443 = false;
  uint32_t tcp443Ms = 0;
  if (!tcp80) {
    tcpConnect(target.managementIp, 443, kTcpTimeoutMs, tcp443, tcp443Ms);
    vTaskDelay(pdMS_TO_TICKS(1));
    Serial.printf("[ap-check] tcp port=443 result=%s",
                  tcp443 ? "success" : "fail");
    if (tcp443) {
      Serial.printf(" latency=%ums\n", static_cast<unsigned>(tcp443Ms));
    } else {
      Serial.println();
    }
  }
  DmaMemoryMonitor::logSnapshot("ap-check-after-tcp");

  if (tcp80) {
    result.tcpOk = true;
    result.tcpLatencyValid = true;
    result.tcpLatencyMs = tcp80Ms;
    result.transport = ExternalAccessPoint::ManagementTransport::Http;
  } else if (tcp443) {
    result.tcpOk = true;
    result.tcpLatencyValid = true;
    result.tcpLatencyMs = tcp443Ms;
    result.transport = ExternalAccessPoint::ManagementTransport::Https;
  } else {
    result.transport = ExternalAccessPoint::ManagementTransport::None;
  }

  if (result.icmpOk && result.tcpOk) {
    result.errorCode = nullptr;
  } else if (result.icmpOk && !result.tcpOk) {
    result.errorCode = "MANAGEMENT_UNREACHABLE";
  } else if (!result.icmpOk && result.tcpOk) {
    result.errorCode = icmpError ? icmpError : "ICMP_TIMEOUT";
  } else {
    result.errorCode = "MANAGEMENT_UNREACHABLE";
  }
  return result;
}
