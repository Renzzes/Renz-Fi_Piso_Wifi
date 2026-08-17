#include "MemoryDiagnostics.h"

#include <esp_heap_caps.h>

#include "DmaMemoryMonitor.h"
#include "EventBus.h"
#include "PortalSessionManager.h"
#include "RouterApiTransportGate.h"
#include "RouterProvisioningWorker.h"
#include "WifiDiscoveryCache.h"

namespace {

RouterProvisioningWorker *g_worker         = nullptr;
EventBus *g_events                         = nullptr;
PortalSessionManager *g_portalSessions     = nullptr;
volatile bool g_inspectionActive   = false;
uint32_t g_lastMemLogMs            = 0;

}  // namespace

namespace MemoryDiagnostics {

void begin(RouterProvisioningWorker *worker, EventBus *events,
             PortalSessionManager *portalSessions) {
  g_worker         = worker;
  g_events         = events;
  g_portalSessions = portalSessions;
}

void setInspectionActive(bool active) { g_inspectionActive = active; }

bool inspectionActive() { return g_inspectionActive; }

void periodicLog() {
  const uint32_t now = millis();
  if (now - g_lastMemLogMs < 10000) return;
  g_lastMemLogMs = now;

  const size_t heapFree     = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  const size_t heapLargest  = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  const size_t heapMinimum  = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
  const size_t dmaFree      = heap_caps_get_free_size(MALLOC_CAP_DMA);
  const size_t dmaLargest   = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
  const size_t dmaMinimum   = heap_caps_get_minimum_free_size(MALLOC_CAP_DMA);

  const uint8_t queueDepth = g_worker && g_worker->isBusy() ? 1 : 0;
  const uint8_t jobs       = g_worker && g_worker->isBusy() ? 1 : 0;
  const uint8_t sseClients =
      g_events && g_events->clientCount() > 255 ? 255 : static_cast<uint8_t>(
                                                      g_events ? g_events->clientCount() : 0);
  const uint8_t inspection = g_inspectionActive ? 1 : 0;
  const uint8_t wifiCache  = WifiDiscoveryCache::hasAny() ? 1 : 0;
  const uint8_t portalActive =
      g_portalSessions && g_portalSessions->hasActiveClientSession() ? 1 : 0;
  // 255 = intentional UNKNOWN sentinel (stale/missing sample), not 255% CPU.
  const uint8_t rosCpu = RouterApiTransportGate::lastObservedCpuLoadPercent();
  const char *rosCpuLabel =
      (rosCpu == 255) ? "unknown" : nullptr;

  if (rosCpuLabel) {
    Serial.printf(
        "[mem] heap=%u largest=%u minimum=%u dma=%u largest=%u minimum=%u "
        "jobs=%u queue=%u sse=%u inspection=%u wificache=%u portal=%u "
        "roscpu=%s\n",
        static_cast<unsigned>(heapFree), static_cast<unsigned>(heapLargest),
        static_cast<unsigned>(heapMinimum), static_cast<unsigned>(dmaFree),
        static_cast<unsigned>(dmaLargest), static_cast<unsigned>(dmaMinimum),
        static_cast<unsigned>(jobs), static_cast<unsigned>(queueDepth),
        static_cast<unsigned>(sseClients), static_cast<unsigned>(inspection),
        static_cast<unsigned>(wifiCache), static_cast<unsigned>(portalActive),
        rosCpuLabel);
  } else {
    Serial.printf(
        "[mem] heap=%u largest=%u minimum=%u dma=%u largest=%u minimum=%u "
        "jobs=%u queue=%u sse=%u inspection=%u wificache=%u portal=%u "
        "roscpu=%u\n",
        static_cast<unsigned>(heapFree), static_cast<unsigned>(heapLargest),
        static_cast<unsigned>(heapMinimum), static_cast<unsigned>(dmaFree),
        static_cast<unsigned>(dmaLargest), static_cast<unsigned>(dmaMinimum),
        static_cast<unsigned>(jobs), static_cast<unsigned>(queueDepth),
        static_cast<unsigned>(sseClients), static_cast<unsigned>(inspection),
        static_cast<unsigned>(wifiCache), static_cast<unsigned>(portalActive),
        static_cast<unsigned>(rosCpu));
  }

  DmaMemoryMonitor::logSnapshot("periodic-dma");
}

}  // namespace MemoryDiagnostics
