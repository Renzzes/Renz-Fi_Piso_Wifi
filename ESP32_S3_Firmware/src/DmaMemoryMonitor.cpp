#include "DmaMemoryMonitor.h"

#include <esp_heap_caps.h>
#include <esp_rom_sys.h>

namespace {

uint32_t g_lastLogMs           = 0;
size_t   g_lastFreeDma         = SIZE_MAX;
size_t   g_lastLargestDma      = SIZE_MAX;
size_t   g_lastMinDma          = SIZE_MAX;
size_t   g_lastFreeInternal    = SIZE_MAX;
size_t   g_lastLargestInternal = SIZE_MAX;
bool     g_hookInstalled       = false;

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
  esp_rom_printf(
      "[dma-alloc-fail] size=%u caps=0x%08x fn=%s dma_free=%u dma_largest=%u "
      "internal_free=%u\n",
      (unsigned)size, (unsigned)caps, fn,
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
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
  return heap_caps_get_largest_free_block(MALLOC_CAP_DMA) >=
         kMinLargestDmaBlockForEthTx;
}

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
