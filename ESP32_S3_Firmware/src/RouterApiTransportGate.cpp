#include "RouterApiTransportGate.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "Config.h"
#include "FinishTrace.h"

namespace {

SemaphoreHandle_t &gateMutex() {
  static SemaphoreHandle_t handle = xSemaphoreCreateMutex();
  return handle;
}

uint32_t  g_jobId = 0;
uint32_t  g_jobDeadlineMs = 0;
uint32_t  g_lastConnectAttemptMs = 0;
uint32_t  g_backoffUntilMs = 0;
uint32_t  g_backoffMs = 0;
bool      g_sessionActive = false;
uint8_t   g_queueDepth = 0;

// CPU protection state — last command pacing timestamp and last observed
// /system/resource/print cpu-load sample (never actively polled for this).
uint32_t  g_lastCommandCompletedMs = 0;
bool      g_haveLastCommand = false;
uint8_t   g_lastCpuLoadPercent = 255;  // 255 = unknown
uint32_t  g_lastCpuSampleMs = 0;

// Command-pacing priority of whatever RouterOS command is about to be sent
// next. Defaults to Normal so any call site that doesn't explicitly guard
// with RouterPriorityGuard behaves exactly as before this feature existed.
RouterApiTransportGate::RouterJobPriority g_currentPriority =
    RouterApiTransportGate::RouterJobPriority::Normal;

RouterApiTransportGate::RouterHealth g_health =
    RouterApiTransportGate::RouterHealth::Unknown;
uint8_t  g_healthFailCount = 0;
uint32_t g_recoveryDwellUntilMs = 0;
uint32_t g_lastProbeAttemptMs = 0;
bool     g_probeDesired = false;

uint32_t tierDelayForCpuPercent(uint8_t percent) {
  if (percent == 255) return RenzFiConfig::ROUTER_CMD_DELAY_TIER1_MS;
  if (percent < RenzFiConfig::ROUTER_CPU_TIER1_MAX_PERCENT) {
    return RenzFiConfig::ROUTER_CMD_DELAY_TIER1_MS;
  }
  if (percent < RenzFiConfig::ROUTER_CPU_TIER2_MAX_PERCENT) {
    return RenzFiConfig::ROUTER_CMD_DELAY_TIER2_MS;
  }
  if (percent < RenzFiConfig::ROUTER_CPU_TIER3_MAX_PERCENT) {
    return RenzFiConfig::ROUTER_CMD_DELAY_TIER3_MS;
  }
  if (percent < RenzFiConfig::ROUTER_CPU_TIER4_MAX_PERCENT) {
    return RenzFiConfig::ROUTER_CMD_DELAY_TIER4_MS;
  }
  return RenzFiConfig::ROUTER_CMD_DELAY_TIER5_MS;
}

void withMutex(void (*fn)()) {
  SemaphoreHandle_t mutex = gateMutex();
  if (!mutex) {
    fn();
    return;
  }
  if (xSemaphoreTake(mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
    fn();
    xSemaphoreGive(mutex);
  }
}

uint32_t millisUntil(uint32_t targetMs) {
  const uint32_t now = millis();
  if (targetMs <= now) return 0;
  return targetMs - now;
}

}  // namespace

void RouterApiTransportGate::logCooldownRemaining(uint32_t remainingMs) {
  // Rate-limit: waitUntilConnectAllowed slices every 100ms; printing each
  // slice floods Serial while another task (often async_tcp) may be blocked
  // waiting for the same RouterOS job to finish.
  static uint32_t s_lastLogMs = 0;
  static uint32_t s_lastLoggedRemaining = 0xFFFFFFFFu;
  const uint32_t now = millis();
  const bool firstSlice = (s_lastLoggedRemaining == 0xFFFFFFFFu) ||
                          (remainingMs > s_lastLoggedRemaining);
  if (!firstSlice && (now - s_lastLogMs) < 1000 && remainingMs > 100) {
    return;
  }
  s_lastLogMs = now;
  s_lastLoggedRemaining = remainingMs;
  Serial.printf("[router-api] cooldown remaining=%u\n",
                static_cast<unsigned>(remainingMs));
}

void RouterApiTransportGate::beginJob(uint32_t jobId, uint32_t deadlineMs) {
  SemaphoreHandle_t mutex = gateMutex();
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
    g_jobId         = jobId;
    g_jobDeadlineMs = deadlineMs;
    xSemaphoreGive(mutex);
  }
}

void RouterApiTransportGate::endJob(uint32_t jobId, bool success,
                                    const char *failReason) {
  SemaphoreHandle_t mutex = gateMutex();
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
    if (g_jobId == jobId) {
      g_jobId         = 0;
      g_jobDeadlineMs = 0;
    }
    if (success) {
      g_backoffMs            = 0;
      g_backoffUntilMs       = 0;
      g_lastConnectAttemptMs = 0;
    }
    xSemaphoreGive(mutex);
  }
  if (success) {
    noteJobSuccess();
  } else {
    noteJobFailure(failReason && failReason[0] ? failReason : "job_failed");
  }
}

const char *RouterApiTransportGate::healthLabel(RouterHealth state) {
  switch (state) {
    case RouterHealth::Unknown:
      return "UNKNOWN";
    case RouterHealth::Connecting:
      return "CONNECTING";
    case RouterHealth::Healthy:
      return "HEALTHY";
    case RouterHealth::Degraded:
      return "DEGRADED";
    case RouterHealth::Unavailable:
      return "UNAVAILABLE";
    case RouterHealth::Cooldown:
      return "COOLDOWN";
    case RouterHealth::Probing:
      return "PROBING";
    case RouterHealth::Recovering:
      return "RECOVERING";
  }
  return "UNKNOWN";
}

const char *RouterApiTransportGate::healthLabel() {
  return healthLabel(health());
}

RouterApiTransportGate::RouterHealth RouterApiTransportGate::health() {
  SemaphoreHandle_t mutex = gateMutex();
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    const RouterHealth h = g_health;
    xSemaphoreGive(mutex);
    return h;
  }
  return RouterHealth::Unknown;
}

void RouterApiTransportGate::setHealth(RouterHealth next, const char *reason) {
  RouterHealth previous = RouterHealth::Unknown;
  SemaphoreHandle_t mutex = gateMutex();
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    previous = g_health;
    if (previous == next) {
      xSemaphoreGive(mutex);
      return;
    }
    g_health = next;
    xSemaphoreGive(mutex);
  } else {
    return;
  }
  Serial.printf("[ros-health] state=%s reason=%s\n", healthLabel(next),
                reason ? reason : "");
  (void)previous;
}

void RouterApiTransportGate::noteJobSuccess() {
  SemaphoreHandle_t mutex = gateMutex();
  RouterHealth previous = RouterHealth::Unknown;
  bool startDwell = false;
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    previous = g_health;
    g_healthFailCount = 0;
    g_probeDesired = false;
    const bool customerCritical =
        g_currentPriority ==
        RouterApiTransportGate::RouterJobPriority::Critical;
    if (customerCritical || previous == RouterHealth::Healthy ||
        previous == RouterHealth::Unknown ||
        previous == RouterHealth::Connecting) {
      // Paid Activate/Deauth/Pause success is itself the readiness proof.
      g_health = RouterHealth::Healthy;
      g_recoveryDwellUntilMs = 0;
    } else if (previous == RouterHealth::Probing ||
               previous == RouterHealth::Unavailable ||
               previous == RouterHealth::Cooldown ||
               previous == RouterHealth::Degraded ||
               previous == RouterHealth::Recovering) {
      g_health = RouterHealth::Recovering;
      g_recoveryDwellUntilMs =
          millis() + RenzFiConfig::ROUTER_HEALTH_RECOVERY_DWELL_MS;
      startDwell = true;
    }
    xSemaphoreGive(mutex);
  }
  if (previous != RouterHealth::Healthy && health() == RouterHealth::Healthy) {
    Serial.println("[ros-health] state=HEALTHY reason=job_ok");
  } else if (startDwell) {
    Serial.printf(
        "[ros-health] state=RECOVERING reason=job_ok dwell_ms=%u\n",
        static_cast<unsigned>(RenzFiConfig::ROUTER_HEALTH_RECOVERY_DWELL_MS));
  }
}

void RouterApiTransportGate::noteJobFailure(const char *reason) {
  SemaphoreHandle_t mutex = gateMutex();
  RouterHealth previous = RouterHealth::Unknown;
  RouterHealth next = RouterHealth::Degraded;
  uint8_t fails = 0;
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    previous = g_health;
    if (g_healthFailCount < 255) ++g_healthFailCount;
    fails = g_healthFailCount;
    if (fails >= RenzFiConfig::ROUTER_HEALTH_FAILS_TO_UNAVAILABLE) {
      next = RouterHealth::Unavailable;
      g_health = RouterHealth::Unavailable;
      // Align with transport backoff window as cooldown.
      if (g_backoffUntilMs == 0) {
        g_backoffUntilMs = millis() + RenzFiConfig::ROUTER_API_BACKOFF_INITIAL_MS;
      }
    } else {
      next = RouterHealth::Degraded;
      g_health = RouterHealth::Degraded;
    }
    g_recoveryDwellUntilMs = 0;
    g_probeDesired = false;
    xSemaphoreGive(mutex);
  }
  if (previous != next) {
    Serial.printf("[ros-health] state=%s reason=%s failures=%u\n",
                  healthLabel(next), reason ? reason : "fail",
                  static_cast<unsigned>(fails));
  }
}

void RouterApiTransportGate::tickHealth(uint32_t nowMs) {
  SemaphoreHandle_t mutex = gateMutex();
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;

  if (g_health == RouterHealth::Recovering && g_recoveryDwellUntilMs != 0 &&
      static_cast<int32_t>(nowMs - g_recoveryDwellUntilMs) >= 0) {
    g_health = RouterHealth::Healthy;
    g_recoveryDwellUntilMs = 0;
    g_healthFailCount = 0;
    xSemaphoreGive(mutex);
    Serial.println("[ros-health] state=HEALTHY reason=recovery_dwell_complete");
    return;
  }

  if (g_health == RouterHealth::Unavailable) {
    const uint32_t coolUntil = g_backoffUntilMs;
    if (coolUntil != 0 && static_cast<int32_t>(nowMs - coolUntil) < 0) {
      if (g_health != RouterHealth::Cooldown) {
        g_health = RouterHealth::Cooldown;
        xSemaphoreGive(mutex);
        Serial.println("[ros-health] state=COOLDOWN reason=post_failure");
        return;
      }
    } else {
      // Cooldown elapsed — desire a single lightweight probe (no continuous poll).
      g_health = RouterHealth::Cooldown;
      g_probeDesired = true;
    }
  } else if (g_health == RouterHealth::Cooldown) {
    const uint32_t coolUntil = g_backoffUntilMs;
    if (coolUntil == 0 || static_cast<int32_t>(nowMs - coolUntil) >= 0) {
      g_probeDesired = true;
    }
  } else if (g_health == RouterHealth::Degraded) {
    // Single failure: after transport backoff, desire one probe (not idle poll).
    const uint32_t coolUntil = g_backoffUntilMs;
    if (coolUntil == 0 || static_cast<int32_t>(nowMs - coolUntil) >= 0) {
      g_probeDesired = true;
    }
  }

  xSemaphoreGive(mutex);
}

bool RouterApiTransportGate::wantsHealthProbe(uint32_t nowMs) {
  SemaphoreHandle_t mutex = gateMutex();
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(200)) != pdTRUE) return false;
  bool want = g_probeDesired;
  if (want && g_lastProbeAttemptMs != 0 &&
      (nowMs - g_lastProbeAttemptMs) <
          RenzFiConfig::ROUTER_HEALTH_PROBE_MIN_INTERVAL_MS) {
    want = false;
  }
  // Never probe while Healthy/Recovering/Connecting — idle = no ROS login.
  if (g_health == RouterHealth::Healthy ||
      g_health == RouterHealth::Recovering ||
      g_health == RouterHealth::Connecting ||
      g_health == RouterHealth::Probing) {
    want = false;
  }
  if (g_health != RouterHealth::Unavailable &&
      g_health != RouterHealth::Cooldown &&
      g_health != RouterHealth::Degraded &&
      g_health != RouterHealth::Unknown) {
    want = false;
  }
  xSemaphoreGive(mutex);
  return want;
}

void RouterApiTransportGate::beginHealthProbe() {
  SemaphoreHandle_t mutex = gateMutex();
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    g_probeDesired = false;
    g_lastProbeAttemptMs = millis();
    g_health = RouterHealth::Probing;
    xSemaphoreGive(mutex);
  }
  Serial.println("[ros-health] state=PROBING reason=readiness_check");
}

void RouterApiTransportGate::endHealthProbe(bool ok) {
  if (ok) {
    noteJobSuccess();
    Serial.println("[ros-health] probe ok");
  } else {
    noteJobFailure("probe_failed");
  }
}

bool RouterApiTransportGate::allowsHotspotActivate() {
  const RouterHealth h = health();
  // Customer-critical Activate may prove readiness during recovery.
  // UNAVAILABLE/COOLDOWN still suppress retry storms.
  return h == RouterHealth::Healthy || h == RouterHealth::Unknown ||
         h == RouterHealth::Recovering || h == RouterHealth::Degraded ||
         h == RouterHealth::Probing || h == RouterHealth::Connecting;
}

bool RouterApiTransportGate::allowsHotspotVerify() {
  // Same recovery window as Activate: UNKNOWN must not leave Connected Active
  // counting forever while HotSpot Active is empty (verify was Healthy-only).
  // UNAVAILABLE/COOLDOWN still suppress RouterOS verify storms.
  const RouterHealth h = health();
  return h == RouterHealth::Healthy || h == RouterHealth::Unknown ||
         h == RouterHealth::Recovering || h == RouterHealth::Degraded ||
         h == RouterHealth::Probing || h == RouterHealth::Connecting;
}

bool RouterApiTransportGate::allowsHotspotDeauth() {
  const RouterHealth h = health();
  return h == RouterHealth::Healthy || h == RouterHealth::Recovering ||
         h == RouterHealth::Degraded || h == RouterHealth::Unknown;
}

bool RouterApiTransportGate::allowsAdminNonEssential() {
  return health() == RouterHealth::Healthy || health() == RouterHealth::Unknown;
}

void RouterApiTransportGate::clearConnectThrottleAfterSuccess() {
  SemaphoreHandle_t mutex = gateMutex();
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
    g_backoffMs            = 0;
    g_backoffUntilMs       = 0;
    g_lastConnectAttemptMs = 0;
    xSemaphoreGive(mutex);
  }
}

uint32_t RouterApiTransportGate::currentJobId() {
  SemaphoreHandle_t mutex = gateMutex();
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    const uint32_t id = g_jobId;
    xSemaphoreGive(mutex);
    return id;
  }
  return 0;
}

uint32_t RouterApiTransportGate::currentJobDeadlineMs() {
  SemaphoreHandle_t mutex = gateMutex();
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    const uint32_t deadline = g_jobDeadlineMs;
    xSemaphoreGive(mutex);
    return deadline;
  }
  return 0;
}

bool RouterApiTransportGate::jobExpired() {
  SemaphoreHandle_t mutex = gateMutex();
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    const bool expired =
        g_jobDeadlineMs != 0 && static_cast<int32_t>(millis() - g_jobDeadlineMs) >= 0;
    xSemaphoreGive(mutex);
    return expired;
  }
  return false;
}

uint32_t RouterApiTransportGate::remainingJobBudgetMs() {
  SemaphoreHandle_t mutex = gateMutex();
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    const uint32_t deadline = g_jobDeadlineMs;
    xSemaphoreGive(mutex);
    if (deadline == 0) {
      return 0x7FFFFFFFu;  // no active job deadline
    }
    const uint32_t now = millis();
    if (static_cast<int32_t>(now - deadline) >= 0) {
      return 0;
    }
    return deadline - now;
  }
  return 0;
}

bool RouterApiTransportGate::waitUntilConnectAllowed() {
  uint32_t retry = 0;
  for (;;) {
    if (jobExpired()) return false;

    if (FinishTrace::pipelineActive()) {
      FinishTrace::BlockingOpScope::setActiveRetry(retry);
      FinishTrace::BlockingOpScope::updateActiveState("connect throttle polling",
                                                        "connect cooldown");
    }

    uint32_t waitMs = 0;
    SemaphoreHandle_t mutex = gateMutex();
    if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
      const uint32_t now = millis();
      const uint32_t minIntervalUntil =
          g_lastConnectAttemptMs == 0
              ? 0
              : g_lastConnectAttemptMs + RenzFiConfig::ROUTER_API_MIN_CONNECT_INTERVAL_MS;
      const uint32_t backoffWait = millisUntil(g_backoffUntilMs);
      const uint32_t intervalWait =
          minIntervalUntil > now ? minIntervalUntil - now : 0;
      waitMs = backoffWait > intervalWait ? backoffWait : intervalWait;
      xSemaphoreGive(mutex);
    }

    if (waitMs == 0) return true;

    logCooldownRemaining(waitMs);
    const uint32_t slice = waitMs > 100 ? 100 : waitMs;
    vTaskDelay(pdMS_TO_TICKS(slice));
    ++retry;
  }
}

void RouterApiTransportGate::recordConnectAttempt() {
  SemaphoreHandle_t mutex = gateMutex();
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
    g_lastConnectAttemptMs = millis();
    xSemaphoreGive(mutex);
  }
}

void RouterApiTransportGate::recordFailure() {
  SemaphoreHandle_t mutex = gateMutex();
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
    if (g_backoffMs == 0) {
      g_backoffMs = RenzFiConfig::ROUTER_API_BACKOFF_INITIAL_MS;
    } else if (g_backoffMs < RenzFiConfig::ROUTER_API_BACKOFF_MAX_MS) {
      const uint32_t doubled = g_backoffMs * 2;
      g_backoffMs = doubled > RenzFiConfig::ROUTER_API_BACKOFF_MAX_MS
                        ? RenzFiConfig::ROUTER_API_BACKOFF_MAX_MS
                        : doubled;
    }
    g_backoffUntilMs = millis() + g_backoffMs;
    xSemaphoreGive(mutex);
  }
  // Health transition is applied when the worker job ends (endJob) so a
  // multi-command job does not double-count mid-session IO failures. Connect
  // failures that abort before beginJob still need a signal — callers that
  // only recordFailure without endJob are rare; HotSpot paths use endJob.
}

bool RouterApiTransportGate::acquireSession() {
  SemaphoreHandle_t mutex = gateMutex();
  if (!mutex) return true;
  if (xSemaphoreTake(mutex, pdMS_TO_TICKS(5000)) != pdTRUE) return false;
  if (g_sessionActive) {
    xSemaphoreGive(mutex);
    return false;
  }
  g_sessionActive = true;
  xSemaphoreGive(mutex);
  return true;
}

void RouterApiTransportGate::releaseSession() {
  SemaphoreHandle_t mutex = gateMutex();
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
    g_sessionActive = false;
    xSemaphoreGive(mutex);
  }
}

void RouterApiTransportGate::setQueueDepth(uint8_t depth) {
  SemaphoreHandle_t mutex = gateMutex();
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
    g_queueDepth = depth;
    xSemaphoreGive(mutex);
  }
}

void RouterApiTransportGate::logQueueDepth() {
  SemaphoreHandle_t mutex = gateMutex();
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    Serial.printf("[router-api] queue depth=%u\n",
                  static_cast<unsigned>(g_queueDepth));
    xSemaphoreGive(mutex);
  }
}

void RouterApiTransportGate::recordObservedCpuLoad(uint8_t percent) {
  if (percent > 100) return;  // never store invented/invalid values
  SemaphoreHandle_t mutex = gateMutex();
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    g_lastCpuLoadPercent = percent;
    g_lastCpuSampleMs    = millis();
    xSemaphoreGive(mutex);
  }
  if (percent == 255) return;
  Serial.printf("[router-api] MikroTik CPU stored parsed=%u%%\n",
                static_cast<unsigned>(percent));
  if (percent >= RenzFiConfig::ROUTER_CPU_SAFE_THRESHOLD_PERCENT) {
    Serial.printf("[router-api] MikroTik CPU load=%u%% (>= safe threshold %u%%) — "
                  "slowing command rate, skipping non-essential discovery\n",
                  static_cast<unsigned>(percent),
                  static_cast<unsigned>(RenzFiConfig::ROUTER_CPU_SAFE_THRESHOLD_PERCENT));
  }
}

uint8_t RouterApiTransportGate::lastObservedCpuLoadPercent() {
  SemaphoreHandle_t mutex = gateMutex();
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    const uint8_t percent = g_lastCpuLoadPercent;
    const uint32_t sampleMs = g_lastCpuSampleMs;
    xSemaphoreGive(mutex);
    if (percent == 255) return 255;
    if (millis() - sampleMs > RenzFiConfig::ROUTER_CPU_SAMPLE_MAX_AGE_MS) return 255;
    return percent;
  }
  return 255;
}

bool RouterApiTransportGate::cpuUnderPressure() {
  const uint8_t percent = lastObservedCpuLoadPercent();
  return percent != 255 && percent >= RenzFiConfig::ROUTER_CPU_SAFE_THRESHOLD_PERCENT;
}

void RouterApiTransportGate::recordCommandCompleted() {
  SemaphoreHandle_t mutex = gateMutex();
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    g_lastCommandCompletedMs = millis();
    g_haveLastCommand        = true;
    xSemaphoreGive(mutex);
  }
}

void RouterApiTransportGate::setPriority(RouterJobPriority priority) {
  SemaphoreHandle_t mutex = gateMutex();
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    g_currentPriority = priority;
    xSemaphoreGive(mutex);
  }
}

RouterApiTransportGate::RouterJobPriority RouterApiTransportGate::currentPriority() {
  SemaphoreHandle_t mutex = gateMutex();
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    const RouterJobPriority priority = g_currentPriority;
    xSemaphoreGive(mutex);
    return priority;
  }
  return RouterJobPriority::Normal;
}

void RouterApiTransportGate::waitBeforeCommand() {
  const RouterJobPriority priority = currentPriority();
  bool loggedPause = false;
  uint32_t retry = 0;

  for (;;) {
    if (jobExpired()) return;

    if (FinishTrace::pipelineActive()) {
      FinishTrace::BlockingOpScope::setActiveRetry(retry);
    }

    const uint8_t cpuPercent = lastObservedCpuLoadPercent();
    const bool pauseLowPriority =
        priority == RouterJobPriority::Low && cpuPercent != 255 &&
        cpuPercent >= RenzFiConfig::ROUTER_CPU_PAUSE_THRESHOLD_PERCENT;
    if (pauseLowPriority) {
      if (!loggedPause) {
        Serial.printf(
            "[router-api] MikroTik CPU load=%u%% (>= pause threshold %u%%) — "
            "pausing Low-priority command until CPU recovers or job deadline\n",
            static_cast<unsigned>(cpuPercent),
            static_cast<unsigned>(RenzFiConfig::ROUTER_CPU_PAUSE_THRESHOLD_PERCENT));
        loggedPause = true;
      }
      if (FinishTrace::pipelineActive()) {
        FinishTrace::BlockingOpScope::updateActiveState("CPU pressure pause",
                                                        "RouterOS CPU recovery");
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      ++retry;
      continue;
    }

    const uint32_t minGapMs = tierDelayForCpuPercent(cpuPercent);
    uint32_t waitMs = 0;
    SemaphoreHandle_t mutex = gateMutex();
    if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      if (g_haveLastCommand) {
        const uint32_t elapsed = millis() - g_lastCommandCompletedMs;
        waitMs = elapsed < minGapMs ? (minGapMs - elapsed) : 0;
      }
      xSemaphoreGive(mutex);
    }

    if (waitMs == 0) return;
    if (FinishTrace::pipelineActive()) {
      FinishTrace::BlockingOpScope::updateActiveState("command pacing delay",
                                                      "RouterOS CPU pacing");
    }
    const uint32_t slice = waitMs > 50 ? 50 : waitMs;
    vTaskDelay(pdMS_TO_TICKS(slice));
    ++retry;
  }
}
