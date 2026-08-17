#pragma once

#include <Arduino.h>
#include <ESP.h>
#include <freertos/FreeRTOS.h>

// Temporary async_tcp / setup-handler stack diagnostics.
// Logs uxTaskGetStackHighWaterMark for whichever task is running (typically
// async_tcp on GET /api/setup/router/jobs/*).
namespace WebStackDiagnostics {

inline UBaseType_t stackHighWaterMarkWords() {
  return uxTaskGetStackHighWaterMark(nullptr);
}

inline void logStage(const char *tag, const char *stage) {
  Serial.printf("[%s] stage=%s stack_hwm_words=%u heap_free=%u heap_largest=%u\n",
                tag ? tag : "web-stack",
                stage ? stage : "",
                static_cast<unsigned>(stackHighWaterMarkWords()),
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getMaxAllocHeap()));
}

inline void logJsonBuild(const char *tag, const char *stage, size_t envelopeCapacity,
                         size_t envelopeUsage, size_t resultCapacity,
                         size_t resultUsage, size_t responseBodyLen,
                         size_t serializedLen) {
  Serial.printf(
      "[%s] stage=%s stack_hwm_words=%u heap_free=%u "
      "responseBodyLen=%u envelopeCap=%u envelopeUse=%u "
      "resultCap=%u resultUse=%u serializedLen=%u\n",
      tag ? tag : "job-status-diag",
      stage ? stage : "",
      static_cast<unsigned>(stackHighWaterMarkWords()),
      static_cast<unsigned>(ESP.getFreeHeap()),
      static_cast<unsigned>(responseBodyLen),
      static_cast<unsigned>(envelopeCapacity),
      static_cast<unsigned>(envelopeUsage),
      static_cast<unsigned>(resultCapacity),
      static_cast<unsigned>(resultUsage),
      static_cast<unsigned>(serializedLen));
}

}  // namespace WebStackDiagnostics
