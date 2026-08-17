#include "BurnInDiagnostics.h"

#if RENZFI_BURN_IN_DIAG

#include <ETH.h>
#include <NetworkClient.h>

#include "Config.h"
#include "EthernetManager.h"
#include "JsonHeap.h"
#include "router/RouterPlatform.h"
#include "RouterProvisioningWorker.h"

namespace {

uint32_t g_lastSampleMs = 0;
uint32_t g_lastRouterProbeMs = 0;
bool g_routerReachable = false;
bool g_routerProbeAttempted = false;

RouterProvisioningWorker *g_routerWorker = nullptr;
EthernetManager *g_eth = nullptr;
RouterPlatform *g_router = nullptr;

String formatKiB(uint32_t bytes) {
  if (bytes < 1024U) {
    return String(bytes) + " B";
  }
  return String(bytes / 1024U) + " KB";
}

String formatStackHwm(TaskHandle_t handle) {
  if (!handle) return "—";
  const UBaseType_t words = uxTaskGetStackHighWaterMark(handle);
  return formatKiB(static_cast<uint32_t>(words) * sizeof(StackType_t));
}

String formatStackHwmByName(const char *taskName) {
  if (!taskName || taskName[0] == '\0') return "—";
#if configUSE_TRACE_FACILITY
  return formatStackHwm(xTaskGetHandle(taskName));
#else
  (void)taskName;
  return "—";
#endif
}

String ethStatusLabel() {
  if (!g_eth) return "Unavailable";
  if (!g_eth->driverReady()) return "Driver down";
  if (!g_eth->linkUp()) return "Link down";
  if (!g_eth->hasIp()) return "No IP";
  return String("Connected (") + g_eth->ip() + ")";
}

bool probeRouterOsTcp(const String &host) {
  if (host.isEmpty() || !g_eth || !g_eth->isServiceReady()) return false;
  NetworkClient client;
  client.setConnectionTimeout(1500);
  client.setTimeout(1500);
  if (!client.connect(host.c_str(), RenzFiConfig::ROUTEROS_API_PORT)) {
    return false;
  }
  client.stop();
  return true;
}

String routerOsStatusLabel(uint32_t nowMs) {
  if (g_routerWorker && g_routerWorker->isBusy()) {
    return "Busy (worker active)";
  }

  if (!g_router) return "Unavailable";

  HeapJsonDocument settingsDoc(RenzFiConfig::JSON_DOC_SMALL);
  DynamicJsonDocument &settings = settingsDoc.doc();
  if (!g_router->load(settings)) {
    return "Not configured";
  }

  const char *host = settings["host"] | "";
  if (!host || host[0] == '\0') {
    return "Not configured";
  }

  if (nowMs - g_lastRouterProbeMs >= RenzFiConfig::BURN_IN_ROUTER_PROBE_INTERVAL_MS) {
    g_lastRouterProbeMs = nowMs;
    g_routerProbeAttempted = true;
    g_routerReachable = probeRouterOsTcp(String(host));
  } else if (!g_routerProbeAttempted) {
    g_routerReachable = probeRouterOsTcp(String(host));
    g_routerProbeAttempted = true;
    g_lastRouterProbeMs = nowMs;
  }

  return g_routerReachable ? "Reachable" : "Unreachable";
}

void logSample(uint32_t nowMs) {
  TaskHandle_t routerWorkerHandle =
      g_routerWorker ? g_routerWorker->taskHandle() : nullptr;

  Serial.println("[health]");
  Serial.printf("FreeHeap: %s\n", formatKiB(ESP.getFreeHeap()).c_str());
  Serial.printf("LargestBlock: %s\n", formatKiB(ESP.getMaxAllocHeap()).c_str());
  Serial.printf("MinFreeHeap: %s\n", formatKiB(ESP.getMinFreeHeap()).c_str());
  Serial.printf("router_worker stack HWM: %s\n", formatStackHwm(routerWorkerHandle).c_str());
  Serial.printf("loopTask stack HWM: %s\n", formatStackHwmByName("loopTask").c_str());
  Serial.printf("async_tcp stack HWM: %s\n", formatStackHwmByName("async_tcp").c_str());
  Serial.printf("wifi stack HWM: %s\n", formatStackHwmByName("wifi").c_str());
  Serial.printf("tiT stack HWM: %s\n", formatStackHwmByName("tiT").c_str());
  Serial.printf("ETH: %s\n", ethStatusLabel().c_str());
  Serial.printf("RouterOS: %s\n", routerOsStatusLabel(nowMs).c_str());
}

}  // namespace

namespace BurnInDiagnostics {

void begin(RouterProvisioningWorker *routerWorker, EthernetManager *eth,
           RouterPlatform *router) {
  g_routerWorker = routerWorker;
  g_eth          = eth;
  g_router       = router;
  g_lastSampleMs = millis();
  Serial.printf(
      "[health] burn-in diagnostics enabled (sample every %u ms, RouterOS probe "
      "every %u ms)\n",
      static_cast<unsigned>(RenzFiConfig::BURN_IN_DIAG_INTERVAL_MS),
      static_cast<unsigned>(RenzFiConfig::BURN_IN_ROUTER_PROBE_INTERVAL_MS));
  logSample(g_lastSampleMs);
}

void loop() {
  const uint32_t nowMs = millis();
  if (nowMs - g_lastSampleMs < RenzFiConfig::BURN_IN_DIAG_INTERVAL_MS) return;
  g_lastSampleMs = nowMs;
  logSample(nowMs);
}

}  // namespace BurnInDiagnostics

#endif  // RENZFI_BURN_IN_DIAG
