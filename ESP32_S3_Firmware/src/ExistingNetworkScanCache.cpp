#include "ExistingNetworkScanCache.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace ExistingNetworkScanCache {

namespace {

SemaphoreHandle_t &mutexHandle() {
  static SemaphoreHandle_t handle = xSemaphoreCreateMutex();
  return handle;
}

String   g_body;
int      g_httpStatus = 0;
bool     g_hasData    = false;
uint32_t g_updatedAtMs = 0;

}  // namespace

void store(int httpStatus, const String &body) {
  SemaphoreHandle_t mutex = mutexHandle();
  if (!mutex || xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;
  g_body        = body;
  g_httpStatus  = httpStatus;
  g_hasData     = true;
  g_updatedAtMs = millis();
  xSemaphoreGive(mutex);
}

bool getAny(int &httpStatusOut, String &bodyOut, uint32_t &ageMsOut) {
  SemaphoreHandle_t mutex = mutexHandle();
  if (!mutex || xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
  bool ok = false;
  if (g_hasData) {
    httpStatusOut = g_httpStatus;
    bodyOut       = g_body;
    ageMsOut      = millis() - g_updatedAtMs;
    ok            = true;
  }
  xSemaphoreGive(mutex);
  return ok;
}

bool hasAny() {
  SemaphoreHandle_t mutex = mutexHandle();
  if (!mutex || xSemaphoreTake(mutex, pdMS_TO_TICKS(200)) != pdTRUE) return false;
  const bool result = g_hasData;
  xSemaphoreGive(mutex);
  return result;
}

void clear() {
  SemaphoreHandle_t mutex = mutexHandle();
  if (!mutex || xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;
  g_body.clear();
  g_httpStatus  = 0;
  g_hasData     = false;
  g_updatedAtMs = 0;
  xSemaphoreGive(mutex);
}

}  // namespace ExistingNetworkScanCache
