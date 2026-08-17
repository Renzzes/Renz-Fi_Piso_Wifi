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
static constexpr size_t kMinLargestDmaBlockForEthTx = 1536;

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

// Register heap_caps failed-alloc hook (size, caps, originating function).
// Safe to call once at boot; idempotent.
void install();

// Called from loop(): logs at most every 5s unless DMA/internal values shift
// by a meaningful margin (see implementation).
void periodicLog();

bool hasEthTransmitHeadroom();

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
