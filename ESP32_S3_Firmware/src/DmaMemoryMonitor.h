#pragma once

#include <Arduino.h>

#include <esp_heap_caps.h>

// Tracks ESP32 internal DMA-capable heap used by SPI3/W5500 priv TX/RX
// buffers (setup_dma_priv_buffer in ESP-IDF spi_master.c). Exhaustion there
// surfaces as "Failed to allocate priv TX/RX buffer" and can cascade into
// abort()/LoadProhibited if the driver proceeds with invalid state.
//
// Architecture note: SD (FSPI) uses Arduino SPI FIFO polling and does NOT call
// setup_dma_priv_buffer. W5500 (SPI3_HOST + SPI_DMA_CH_AUTO) does. Both still
// compete for internal SRAM; large INTERNAL allocations can fragment the DMA
// pool that W5500 needs.
namespace DmaMemoryMonitor {

// Minimum largest-free DMA block required before starting a new RouterOS TCP
// session / sales-chart rebuild (W5500 SPI may need ~1.5 KB priv buffers).
// Also used to pace in-flight HTTP chunk TX. Do not lower this threshold.
static constexpr size_t kMinLargestDmaBlockForEthTx = 1536;

// Admit NEW HTTP responses only when largest DMA is at least this large.
// Proven multi-client crash (Admin SPA + 2 portal phones): many concurrent
// /api/health + /api/status + portal JSON responses started while largest was
// still ≥1536, then collectively drove dma_largest to 24 → W5500 RX
// setup_dma_priv_buffer(~444) failed → LoadProhibited EXCVADDR=0. Leaving
// ~3 KB free at admit time reserves room for one W5500 RX bounce (~504) plus
// a small TX frame while paced streams finish.
static constexpr size_t kMinLargestDmaBlockForHttpStart = 3072;

// Below this, W5500 RX cannot safely allocate a priv bounce buffer. Enter
// emergency quiesce (drop SSE / reject new HTTP) rather than let IDF deref NULL.
static constexpr size_t kCriticalDmaFloorForW5500Rx = 768;

// When portal customers are paying/activating, defer large Admin SPA assets
// (≥32 KB) unless this much contiguous DMA remains. Proven 2026-08-31 crash:
// 798 KB JS admitted at largest=8692 then collapsed to 172 during concurrent
// portal-save + router worker + sd-readJson.
static constexpr size_t kMinLargestDmaBlockForLargeAssetWithPortal = 12288;

// SoftAP often steadies at ~4084 largest DMA (just under 4K). Prefer this
// extra margin briefly, then proceed if ETH TX (1536) is available. Never
// use this as a hard fail — that blocks Step 4 Finish while 4084 is healthy.
static constexpr size_t kMinLargestDmaBlockForSoftApSafeRos = 4096;

struct Snapshot {
  size_t freeDma         = 0;
  size_t largestDma      = 0;
  size_t minDma          = 0;
  size_t freeInternal    = 0;
  size_t largestInternal = 0;
};

Snapshot readSnapshot();
void logSnapshot(const char *label);
void logSnapshot(const char *label, const Snapshot &s);

// Temporary Step-4 provenance: one line with task name. Not a DMA gate.
void logTrace(const char *stage);

// Register heap_caps failed-alloc hook (size, caps, originating function).
// Safe to call once at boot; idempotent.
void install();

// Called from loop(): logs at most every 5s unless DMA/internal values shift
// by a meaningful margin (see implementation).
void periodicLog();

bool hasEthTransmitHeadroom();

// True when largest DMA can admit a new HTTP response body (stricter than TX).
bool hasHttpServeHeadroom();

// True when largest DMA is below the W5500 RX survival floor.
bool isEthDmaCritical();

// Set by heap alloc-fail hook when W5500 SPI priv bounce allocation fails.
// Holds emergency quiesce until DMA recovers (see tickEmergencyRecovery).
bool isEthDmaEmergency();

// Called from heap_caps failed-alloc callback on W5500 SPI bounce failures only.
void signalW5500SpiAllocFailure();

// Clear emergency flag once dma_largest recovers; call from loop().
void tickEmergencyRecovery();

// True when largest DMA block is at least `minLargest` (default ETH TX gate).
bool hasDmaHeadroom(size_t minLargest = kMinLargestDmaBlockForEthTx);

// Block until DMA headroom recovers or timeout. Call only from router_worker /
// loopTask — never from async_tcp. Returns false on timeout (caller should
// fail with ETH_DMA_LOW rather than crash into SPI priv RX NULL).
bool waitForDmaHeadroom(uint32_t timeoutMs, size_t minLargest,
                        uint32_t pollMs = 50);

// Prefer SoftAP-safe margin, then require only ETH TX (1536). Hard-fail
// ETH_DMA_LOW only when largest stays below 1536. Worker/loop only.
bool waitForRouterOsConnectHeadroom(uint32_t preferTimeoutMs = 1500);

// Shared paced HTTP response slots (Admin SPA + ApiServer JSON). Caps how many
// concurrent body streams may drive W5500 SPI DMA at once.
static constexpr int kMaxPacedHttpInFlight = 2;
bool tryAcquireHttpSlot();
void releaseHttpSlot();
int pacedHttpInFlight();

// RAII: logs DMA+INTERNAL before and after a labeled operation, including
// signed deltas so serial traces show which op shrinks the DMA pool.
class ScopedProbe {
 public:
  explicit ScopedProbe(const char *label);
  ~ScopedProbe();

  ScopedProbe(const ScopedProbe &) = delete;
  ScopedProbe &operator=(const ScopedProbe &) = delete;

 private:
  const char *_label;
  Snapshot _before;
};

}  // namespace DmaMemoryMonitor
