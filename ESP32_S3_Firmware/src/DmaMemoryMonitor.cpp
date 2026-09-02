#include "DmaMemoryMonitor.h"

#include <esp_debug_helpers.h>
#include <esp_heap_caps.h>
#include <esp_rom_sys.h>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

uint32_t g_lastLogMs           = 0;
size_t   g_lastFreeDma         = SIZE_MAX;
size_t   g_lastLargestDma      = SIZE_MAX;
size_t   g_lastMinDma          = SIZE_MAX;
size_t   g_lastFreeInternal    = SIZE_MAX;
size_t   g_lastLargestInternal = SIZE_MAX;
bool     g_hookInstalled       = false;
volatile bool g_ethDmaEmergency = false;
uint32_t g_ethDmaEmergencyUntilMs = 0;

constexpr uint32_t kW5500DmaCaps =
    static_cast<uint32_t>(MALLOC_CAP_8BIT | MALLOC_CAP_DMA);

bool isW5500SpiBounceAlloc(size_t size, uint32_t caps, const char *task) {
  if (caps != kW5500DmaCaps) return false;
  if (size < 32 || size > 1600) return false;
  if (!task) return true;
  if (strstr(task, "w5500") != nullptr) return true;
  if (strstr(task, "emac_w5500") != nullptr) return true;
  return strcmp(task, "async_tcp") == 0;
}

size_t absDelta(size_t a, size_t b) { return a > b ? a - b : b - a; }

int32_t signedDelta(size_t after, size_t before) {
  if (after >= before) return static_cast<int32_t>(after - before);
  return -static_cast<int32_t>(before - after);
}

void printSnapshot(const char *label, const DmaMemoryMonitor::Snapshot &s) {
  Serial.printf(
      "[dma] %s free=%u largest=%u minimum=%u internal=%u internalLargest=%u\n",
      label ? label : "snapshot", static_cast<unsigned>(s.freeDma),
      static_cast<unsigned>(s.largestDma), static_cast<unsigned>(s.minDma),
      static_cast<unsigned>(s.freeInternal),
      static_cast<unsigned>(s.largestInternal));
}

bool changedSignificantly(const DmaMemoryMonitor::Snapshot &s) {
  if (g_lastFreeDma == SIZE_MAX) return true;
  if (absDelta(s.freeDma, g_lastFreeDma) >= 512) return true;
  if (absDelta(s.largestDma, g_lastLargestDma) >= 512) return true;
  if (absDelta(s.minDma, g_lastMinDma) >= 512) return true;
  if (absDelta(s.freeInternal, g_lastFreeInternal) >= 2048) return true;
  if (absDelta(s.largestInternal, g_lastLargestInternal) >= 2048) return true;
  return false;
}

void remember(const DmaMemoryMonitor::Snapshot &s) {
  g_lastFreeDma           = s.freeDma;
  g_lastLargestDma        = s.largestDma;
  g_lastMinDma            = s.minDma;
  g_lastFreeInternal      = s.freeInternal;
  g_lastLargestInternal   = s.largestInternal;
}

void onAllocFailed(size_t size, uint32_t caps, const char *functionName) {
  // Keep this minimal — may run under allocation failure paths.
  const char *fn = functionName ? functionName : "?";
  const char *task = pcTaskGetName(nullptr);
  esp_rom_printf(
      "[dma-alloc-fail] size=%u caps=0x%08x fn=%s task=%s dma_free=%u "
      "dma_largest=%u internal_free=%u\n",
      (unsigned)size, (unsigned)caps, fn, task ? task : "?",
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  // 0x80c = 8BIT|DMA|INTERNAL — WiFi/coex malloc_internal, not W5500
  // aligned_alloc (0x808). Print a short backtrace only for that class.
  if (isW5500SpiBounceAlloc(size, caps, task)) {
    esp_rom_printf("[dma-alloc-fail] class=w5500-spi-bounce\n");
    DmaMemoryMonitor::signalW5500SpiAllocFailure();
    return;
  }
  if (caps == (MALLOC_CAP_8BIT | MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL) ||
      size == 1624) {
    esp_rom_printf(
        "[dma-alloc-fail] class=wifi-coex-internal-dma (not W5500 spi bounce)\n");
    esp_backtrace_print(8);
  }
}

}  // namespace

namespace DmaMemoryMonitor {

Snapshot readSnapshot() {
  Snapshot s;
  s.freeDma         = heap_caps_get_free_size(MALLOC_CAP_DMA);
  s.largestDma      = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
  s.minDma          = heap_caps_get_minimum_free_size(MALLOC_CAP_DMA);
  s.freeInternal    = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  s.largestInternal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  return s;
}

void logSnapshot(const char *label) { printSnapshot(label, readSnapshot()); }

void logSnapshot(const char *label, const Snapshot &s) {
  printSnapshot(label, s);
}

void logTrace(const char *stage) {
  const Snapshot s = readSnapshot();
  const char *task = pcTaskGetName(nullptr);
  Serial.printf(
      "[dma-trace] stage=%s task=%s free=%u largest=%u internalFree=%u "
      "internalLargest=%u\n",
      stage ? stage : "?", task ? task : "?",
      static_cast<unsigned>(s.freeDma), static_cast<unsigned>(s.largestDma),
      static_cast<unsigned>(s.freeInternal),
      static_cast<unsigned>(s.largestInternal));
}

void install() {
  if (g_hookInstalled) return;
  const esp_err_t err = heap_caps_register_failed_alloc_callback(onAllocFailed);
  g_hookInstalled = (err == ESP_OK);
  Serial.printf("[dma] alloc-fail hook %s\n",
                g_hookInstalled ? "installed" : "FAILED");
  logSnapshot("boot");
}

void periodicLog() {
  const uint32_t now = millis();
  const Snapshot s   = readSnapshot();
  if (now - g_lastLogMs < 5000 && !changedSignificantly(s)) return;
  g_lastLogMs = now;
  remember(s);
  printSnapshot("periodic", s);
}

bool hasEthTransmitHeadroom() {
  return hasDmaHeadroom(kMinLargestDmaBlockForEthTx);
}

bool hasHttpServeHeadroom() {
  if (g_ethDmaEmergency) return false;
  return hasDmaHeadroom(kMinLargestDmaBlockForHttpStart);
}

bool isEthDmaEmergency() { return g_ethDmaEmergency; }

void signalW5500SpiAllocFailure() {
  g_ethDmaEmergency = true;
  g_ethDmaEmergencyUntilMs = millis() + 8000;
  esp_rom_printf(
      "[dma-emergency] W5500 SPI bounce alloc failed — HTTP/SSE quiesce\n");
}

void tickEmergencyRecovery() {
  if (!g_ethDmaEmergency) return;
  const uint32_t now = millis();
  if ((int32_t)(now - g_ethDmaEmergencyUntilMs) < 0) return;
  if (!hasDmaHeadroom(kMinLargestDmaBlockForHttpStart)) return;
  g_ethDmaEmergency = false;
  Serial.println("[dma-emergency] cleared — DMA recovered");
}

bool isEthDmaCritical() {
  return g_ethDmaEmergency ||
         !hasDmaHeadroom(kCriticalDmaFloorForW5500Rx);
}

bool hasDmaHeadroom(size_t minLargest) {
  return heap_caps_get_largest_free_block(MALLOC_CAP_DMA) >= minLargest;
}

bool waitForDmaHeadroom(uint32_t timeoutMs, size_t minLargest, uint32_t pollMs) {
  if (hasDmaHeadroom(minLargest)) return true;
  const uint32_t start = millis();
  const uint32_t step  = pollMs == 0 ? 50 : pollMs;
  DmaMemoryMonitor::logTrace("dma-wait-enter");
  while (!hasDmaHeadroom(minLargest)) {
    if ((millis() - start) >= timeoutMs) {
      DmaMemoryMonitor::logTrace("dma-wait-timeout");
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(step));
  }
  DmaMemoryMonitor::logTrace("dma-wait-ok");
  return true;
}

bool waitForRouterOsConnectHeadroom(uint32_t preferTimeoutMs) {
  if (hasDmaHeadroom(kMinLargestDmaBlockForSoftApSafeRos)) {
    return true;
  }
  if (preferTimeoutMs > 0) {
    (void)waitForDmaHeadroom(preferTimeoutMs,
                             kMinLargestDmaBlockForSoftApSafeRos);
    if (hasDmaHeadroom(kMinLargestDmaBlockForSoftApSafeRos) ||
        hasEthTransmitHeadroom()) {
      return true;
    }
  }
  if (hasEthTransmitHeadroom()) {
    return true;
  }
  return waitForDmaHeadroom(3000, kMinLargestDmaBlockForEthTx);
}

namespace {
int g_pacedHttpInFlight = 0;
}  // namespace

bool tryAcquireHttpSlot() {
  if (g_pacedHttpInFlight >= kMaxPacedHttpInFlight) return false;
  g_pacedHttpInFlight++;
  return true;
}

void releaseHttpSlot() {
  if (g_pacedHttpInFlight > 0) g_pacedHttpInFlight--;
}

int pacedHttpInFlight() { return g_pacedHttpInFlight; }

ScopedProbe::ScopedProbe(const char *label)
    : _label(label ? label : "op"), _before(readSnapshot()) {
  char buf[96];
  snprintf(buf, sizeof(buf), "%s:before", _label);
  printSnapshot(buf, _before);
}

ScopedProbe::~ScopedProbe() {
  const Snapshot after = readSnapshot();
  char buf[96];
  snprintf(buf, sizeof(buf), "%s:after", _label);
  printSnapshot(buf, after);
  Serial.printf(
      "[dma] %s:delta dmaFree=%ld dmaLargest=%ld internalFree=%ld "
      "internalLargest=%ld\n",
      _label, (long)signedDelta(after.freeDma, _before.freeDma),
      (long)signedDelta(after.largestDma, _before.largestDma),
      (long)signedDelta(after.freeInternal, _before.freeInternal),
      (long)signedDelta(after.largestInternal, _before.largestInternal));
}

}  // namespace DmaMemoryMonitor
