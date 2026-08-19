#include "ap/GenericApDriver.h"

#include <Arduino.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_netif.h>
#include <lwip/inet.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include <ping/ping_sock.h>

#include "DmaMemoryMonitor.h"

namespace {

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
  auto *ctx = static_cast<PingWait *>(args);
  esp_ping_stop(handle);
  esp_ping_delete_session(handle);
  if (!ctx) return;
  ctx->ended = true;
  if (ctx->done) xSemaphoreGive(ctx->done);
}

uint32_t resolveEthernetPingInterface() {
  esp_netif_t *ethNetif = esp_netif_get_handle_from_ifkey("ETH_DEF");
  if (!ethNetif) return 0;
  return esp_netif_get_netif_impl_index(ethNetif);
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
  if (xSemaphoreTake(wait.done, waitTicks) != pdTRUE) {
    if (!wait.ended) {
      esp_ping_stop(ping);
      xSemaphoreTake(wait.done, pdMS_TO_TICKS(500));
    }
  }
  vSemaphoreDelete(wait.done);

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
  const int rc = ::connect(sock, reinterpret_cast<sockaddr *>(&addr),
                           sizeof(addr));
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
