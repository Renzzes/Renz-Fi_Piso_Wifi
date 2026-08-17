#pragma once

#include <Arduino.h>
#include <ESP.h>
#include <freertos/FreeRTOS.h>

#include "Config.h"

// Stack/heap diagnostics for the router_worker FreeRTOS task.
namespace RouterWorkerDiagnostics {

// Always-on stack margin for validation checkpoints (router_worker task).
inline void logStackHighWaterMark(const char *checkpoint) {
  const UBaseType_t hwm = uxTaskGetStackHighWaterMark(nullptr);
  Serial.printf("[router-worker] HWM=%u words (%u bytes free) checkpoint=%s\n",
                static_cast<unsigned>(hwm),
                static_cast<unsigned>(hwm * sizeof(StackType_t)),
                checkpoint ? checkpoint : "");
}

inline void logStage(const char *stage) {
#if RENZFI_VERBOSE_ROUTER_API
  logStackHighWaterMark(stage);
  Serial.printf("[router-worker] heap free=%u largest=%u stage=%s\n",
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getMaxAllocHeap()),
                stage ? stage : "");
#else
  (void)stage;
#endif
}

inline void logInspectCommand(const char *command, const char *when) {
#if RENZFI_VERBOSE_ROUTER_API
  char stage[48];
  snprintf(stage, sizeof(stage), "inspect-%s-%s", command ? command : "cmd",
           when ? when : "");
  logStage(stage);
#else
  (void)command;
  (void)when;
#endif
}

// Always-on (production) stack safety net — cheap (one FreeRTOS call, no
// allocation) and silent unless margin is actually low, so it is safe to
// sprinkle at every stage of a RouterOS call chain without spamming serial
// output in normal operation. Call from the router_worker task only.
inline void checkStackMargin(const char *checkpoint,
                             UBaseType_t warnThresholdWords = 1536) {
  const UBaseType_t hwm = uxTaskGetStackHighWaterMark(nullptr);
  if (hwm < warnThresholdWords) {
    Serial.printf(
        "[router-worker] WARNING low stack margin hwm=%u words (%u bytes) "
        "checkpoint=%s\n",
        static_cast<unsigned>(hwm), static_cast<unsigned>(hwm * sizeof(StackType_t)),
        checkpoint ? checkpoint : "");
  }
}

}  // namespace RouterWorkerDiagnostics
