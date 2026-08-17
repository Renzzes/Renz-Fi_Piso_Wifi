#include "WifiDiscoveryCache.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace WifiDiscoveryCache {

namespace {

SemaphoreHandle_t &mutexHandle() {
  static SemaphoreHandle_t handle = xSemaphoreCreateMutex();
  return handle;
}

String   g_body;
int      g_httpStatus     = 0;
bool     g_hasData         = false;
uint32_t g_updatedAtMs     = 0;
uint32_t g_lastAttemptMs   = 0;
bool     g_haveLastAttempt = false;

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

bool getFresh(uint32_t maxAgeMs, int &httpStatusOut, String &bodyOut) {
  SemaphoreHandle_t mutex = mutexHandle();
  if (!mutex || xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
  bool ok = false;
  if (g_hasData && (millis() - g_updatedAtMs) < maxAgeMs) {
    httpStatusOut = g_httpStatus;
    bodyOut       = g_body;
    ok            = true;
  }
  xSemaphoreGive(mutex);
  return ok;
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

bool tryBeginAttempt(uint32_t minIntervalMs) {
  SemaphoreHandle_t mutex = mutexHandle();
  if (!mutex || xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
  const uint32_t now = millis();
  bool allowed = true;
  if (g_haveLastAttempt && (now - g_lastAttemptMs) < minIntervalMs) {
    allowed = false;
  }
  if (allowed) {
    g_lastAttemptMs   = now;
    g_haveLastAttempt = true;
  }
  xSemaphoreGive(mutex);
  return allowed;
}

bool hasAny() {
  SemaphoreHandle_t mutex = mutexHandle();
  if (!mutex || xSemaphoreTake(mutex, pdMS_TO_TICKS(200)) != pdTRUE) return false;
  const bool result = g_hasData;
  xSemaphoreGive(mutex);
  return result;
}

}  // namespace WifiDiscoveryCache
