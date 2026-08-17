#pragma once

#include <ArduinoJson.h>
#include <esp_heap_caps.h>

// RAII heap wrapper for DynamicJsonDocument.
// Use in AsyncWebServer callbacks to avoid blowing the async_tcp task stack
// with large on-stack JSON buffers (JSON_DOC_MEDIUM/LARGE).
class HeapJsonDocument {
 public:
  explicit HeapJsonDocument(size_t capacity)
      : _doc(new DynamicJsonDocument(capacity)), _capacity(capacity) {}

  ~HeapJsonDocument() { delete _doc; }

  HeapJsonDocument(const HeapJsonDocument &) = delete;
  HeapJsonDocument &operator=(const HeapJsonDocument &) = delete;

  DynamicJsonDocument &doc() { return *_doc; }
  operator DynamicJsonDocument &() { return *_doc; }
  operator JsonDocument &() { return *_doc; }

  size_t capacity() const { return _capacity; }

 private:
  DynamicJsonDocument *_doc;
  size_t _capacity;
};

// ArduinoJson 7 pool allocator that prefers PSRAM.
//
// Sales chart / history / records parse CPU-side JSON. Those buffers are never
// handed to the W5500 SPI DMA engine. Allocating them from MALLOC_CAP_DMA /
// INTERNAL (the ESP32 malloc default for blocks below
// CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL) fragments the same pool that
// spi_master::setup_dma_priv_buffer needs for 54-byte aligned TX bounce
// buffers (caps=0x00000808 = DMA|INTERNAL).
struct PsramAllocator : ArduinoJson::Allocator {
  void *allocate(size_t size) override {
    if (size == 0) return nullptr;
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    return p;
  }

  void deallocate(void *pointer) override {
    heap_caps_free(pointer);
  }

  void *reallocate(void *ptr, size_t new_size) override {
    if (!ptr) return allocate(new_size);
    if (new_size == 0) {
      deallocate(ptr);
      return nullptr;
    }
    void *p =
        heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_8BIT);
    return p;
  }
};

inline PsramAllocator &psramJsonAllocator() {
  static PsramAllocator allocator;
  return allocator;
}

// JsonDocument control block stays on the caller's stack (~tens of bytes).
// The growable pool lives in PSRAM via PsramAllocator.
class PsramJsonDocument {
 public:
  PsramJsonDocument() : _doc(&psramJsonAllocator()) {}

  JsonDocument &doc() { return _doc; }
  operator JsonDocument &() { return _doc; }

 private:
  JsonDocument _doc;
};
