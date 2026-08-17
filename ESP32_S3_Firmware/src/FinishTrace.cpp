#include "FinishTrace.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace FinishTrace {
namespace {

constexpr uint32_t kSlowStageMs      = 2000;
constexpr uint32_t kDeadlockStageMs  = 10000;
constexpr int      kMaxBlockingDepth = 8;

volatile bool        g_pipelineActive = false;
volatile uint32_t    g_stageStartMs   = 0;
volatile const char *g_stage          = "idle";
TaskHandle_t         g_heartbeat      = nullptr;

struct BlockingSlot {
  bool        active;
  const char *name;
  const char *waitingFor;
  const char *reason;
  const char *state;
  const char *error;
  uint32_t    beginMs;
  uint32_t    timeoutMs;
  uint32_t    retryCount;
  bool        success;
  bool        noTimeoutWarned;
};

BlockingSlot g_blockingStack[kMaxBlockingDepth];
int          g_blockingDepth = 0;

void logSlowWarnings(uint32_t elapsedMs) {
  if (elapsedMs >= kDeadlockStageMs) {
    Serial.println(F("[finish-stage] POSSIBLE DEADLOCK"));
  } else if (elapsedMs >= kSlowStageMs) {
    Serial.println(F("[finish-stage] WARNING SLOW STAGE"));
  }
}

void logBlockingBegin(const BlockingSlot &slot) {
  Serial.printf("[finish-op] BEGIN %s\n", slot.name ? slot.name : "?");
  Serial.printf("[finish-op] Timeout configured=%ums\n",
                static_cast<unsigned>(slot.timeoutMs));
  Serial.printf("[finish-op] Current retry count=%u\n",
                static_cast<unsigned>(slot.retryCount));
  Serial.printf("[finish-op] Reason for waiting=%s\n",
                slot.reason ? slot.reason : "(none)");
  Serial.printf("[finish-op] Current state=%s\n",
                slot.state ? slot.state : "(none)");
  if (slot.timeoutMs == 0) {
    // Diagnostic only: FinishTrace does not cancel or interrupt the call.
    // Filesystem helpers intentionally pass timeoutMs=0 — Arduino SD/File I/O
    // is synchronous and cannot be aborted by assigning a non-zero metadata
    // value here. A hang is a real reliability risk until a separate
    // enforcement boundary exists (worker policy / FS task).
    Serial.println(
        F("[finish-op] WARNING NO TIMEOUT ENFORCEMENT (diagnostic only)"));
    Serial.println(
        F("[finish-op] WARNING Underlying sync I/O can still block "
          "indefinitely; timeoutMs metadata does not interrupt it."));
  }
}

void logBlockingWaiting(const BlockingSlot &slot) {
  const uint32_t elapsed = millis() - slot.beginMs;
  Serial.printf(
      "[finish-op] WAITING operation=%s elapsed=%ums retry=%u waiting_for=%s\n",
      slot.name ? slot.name : "?", static_cast<unsigned>(elapsed),
      static_cast<unsigned>(slot.retryCount),
      slot.waitingFor ? slot.waitingFor : "?");
}

BlockingSlot *topBlockingSlot() {
  if (g_blockingDepth <= 0) return nullptr;
  BlockingSlot &slot = g_blockingStack[g_blockingDepth - 1];
  return slot.active ? &slot : nullptr;
}

void heartbeatTask(void *) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (g_pipelineActive) {
      const uint32_t elapsed = millis() - g_stageStartMs;
      Serial.printf("[finish-stage] HEARTBEAT stage=%s elapsed=%ums\n",
                    g_stage ? g_stage : "?",
                    static_cast<unsigned>(elapsed));
      logSlowWarnings(elapsed);
    }
    BlockingSlot *slot = topBlockingSlot();
    if (slot && g_pipelineActive) {
      logBlockingWaiting(*slot);
    }
  }
}

void setActiveStage(const char *name) {
  g_stage        = name ? name : "?";
  g_stageStartMs = millis();
}

int pushBlockingSlot(const BlockingOpConfig &cfg) {
  if (!cfg.name || !cfg.name[0]) return -1;
  if (g_blockingDepth >= kMaxBlockingDepth) return -1;
  BlockingSlot &slot = g_blockingStack[g_blockingDepth];
  slot.active          = true;
  slot.name            = cfg.name;
  slot.waitingFor      = cfg.waitingFor ? cfg.waitingFor : "?";
  slot.reason          = cfg.reason ? cfg.reason : "(none)";
  slot.state           = cfg.state ? cfg.state : "(none)";
  slot.error           = nullptr;
  slot.beginMs         = millis();
  slot.timeoutMs       = cfg.timeoutMs;
  slot.retryCount      = cfg.retryCount;
  slot.success         = true;
  slot.noTimeoutWarned = (cfg.timeoutMs == 0);
  const int index      = g_blockingDepth;
  ++g_blockingDepth;
  if (g_pipelineActive) logBlockingBegin(slot);
  return index;
}

void popBlockingSlot(int index, bool success, const char *error) {
  if (index < 0 || index >= g_blockingDepth) return;
  BlockingSlot &slot = g_blockingStack[index];
  if (!slot.active) return;
  const uint32_t elapsed = millis() - slot.beginMs;
  if (g_pipelineActive) {
    Serial.printf("[finish-op] END %s\n", slot.name ? slot.name : "?");
    Serial.printf("[finish-op] elapsed=%ums\n", static_cast<unsigned>(elapsed));
    Serial.printf("[finish-op] result=%s\n", success ? "success" : "failure");
    Serial.printf("[finish-op] error=%s\n",
                  error ? error : (success ? "(none)" : "unknown"));
  }
  slot.active = false;
  if (index == g_blockingDepth - 1) {
    --g_blockingDepth;
  }
}

}  // namespace

void beginHeartbeatTask() {
  if (g_heartbeat) return;
  if (xTaskCreate(heartbeatTask, "finish_hb", 3072, nullptr, 1, &g_heartbeat) !=
      pdPASS) {
    g_heartbeat = nullptr;
  }
}

void enterPipeline() {
  beginHeartbeatTask();
  g_pipelineActive = true;
  setActiveStage("pipeline-start");
}

void exitPipeline() {
  g_pipelineActive = false;
  g_stage          = "idle";
  g_blockingDepth  = 0;
  for (int i = 0; i < kMaxBlockingDepth; ++i) {
    g_blockingStack[i].active = false;
  }
}

bool pipelineActive() { return g_pipelineActive; }

void jobLifecycle(uint32_t jobId, const char *transition) {
  Serial.printf("[finish-stage] JOB jobId=%u transition=%s\n",
                static_cast<unsigned>(jobId), transition ? transition : "?");
}

const char *currentStage() {
  return const_cast<const char *>(g_stage ? g_stage : "?");
}

BlockingOpScope::BlockingOpScope(const BlockingOpConfig &cfg)
    : _active(false),
      _ownedSlot(false),
      _stackIndex(-1),
      _success(true),
      _error(nullptr) {
  if (!g_pipelineActive || !cfg.name || !cfg.name[0]) return;
  _stackIndex = pushBlockingSlot(cfg);
  if (_stackIndex < 0) return;
  _active    = true;
  _ownedSlot = true;
}

BlockingOpScope::BlockingOpScope(const char *name)
    : _active(false),
      _ownedSlot(false),
      _stackIndex(-1),
      _success(true),
      _error(nullptr) {
  (void)name;
}

BlockingOpScope::~BlockingOpScope() {
  if (!_active || !_ownedSlot) return;
  popBlockingSlot(_stackIndex, _success, _error);
}

void BlockingOpScope::fail(const char *error) {
  if (!_active) return;
  _success = false;
  if (error && error[0]) _error = error;
  BlockingSlot *slot = (_stackIndex >= 0 && _stackIndex < g_blockingDepth)
                           ? &g_blockingStack[_stackIndex]
                           : topBlockingSlot();
  if (slot) slot->success = false;
}

void BlockingOpScope::setRetry(uint32_t count) {
  BlockingSlot *slot = topBlockingSlot();
  if (slot) slot->retryCount = count;
}

void BlockingOpScope::setState(const char *state) {
  BlockingSlot *slot = topBlockingSlot();
  if (slot && state) slot->state = state;
}

void BlockingOpScope::setWaitingFor(const char *waitingFor) {
  BlockingSlot *slot = topBlockingSlot();
  if (slot && waitingFor) slot->waitingFor = waitingFor;
}

void BlockingOpScope::updateActiveState(const char *state,
                                        const char *waitingFor) {
  BlockingSlot *slot = topBlockingSlot();
  if (!slot) return;
  if (state) slot->state = state;
  if (waitingFor) slot->waitingFor = waitingFor;
}

void BlockingOpScope::setActiveRetry(uint32_t count) {
  BlockingSlot *slot = topBlockingSlot();
  if (slot) slot->retryCount = count;
}

bool BlockingOpScope::hasActiveBlockingOp() { return topBlockingSlot() != nullptr; }

StageScope::StageScope(const char *name)
    : _name(name ? name : "?"), _beginMs(millis()), _success(true) {
  setActiveStage(_name);
  Serial.printf("[finish-stage] BEGIN %s millis=%u\n", _name,
                static_cast<unsigned>(_beginMs));
}

StageScope::~StageScope() {
  const uint32_t elapsed = millis() - _beginMs;
  logSlowWarnings(elapsed);
  Serial.printf("[finish-stage] END %s millis=%u elapsed=%ums success=%s\n",
                _name, static_cast<unsigned>(millis()),
                static_cast<unsigned>(elapsed), _success ? "true" : "false");
}

void StageScope::setSuccess(bool ok) { _success = ok; }

void StageScope::fail() { _success = false; }

OpScope::OpScope(const char *name)
    : _name(name && name[0] ? name : nullptr),
      _beginMs(millis()),
      _success(true),
      _active(name && name[0] && g_pipelineActive) {
  if (_active) {
    Serial.printf("[finish-op] BEGIN %s\n", _name);
  }
}

OpScope::~OpScope() {
  if (!_active) return;
  const uint32_t elapsed = millis() - _beginMs;
  Serial.printf("[finish-op] END %s elapsed=%ums success=%s\n", _name,
                static_cast<unsigned>(elapsed), _success ? "true" : "false");
}

void OpScope::fail() {
  if (_active) _success = false;
}

void opEvent(const char *msg) {
  Serial.printf("[finish-op] %s\n", msg ? msg : "");
}

void opReturn(const char *context, bool success) {
  Serial.printf("[finish-op] RETURN %s success=%s\n", context ? context : "?",
                success ? "true" : "false");
}

void portalHttpGetReceived(const char *filename) {
  Serial.printf("[finish-op] ESP HTTP GET received file=%s\n",
                filename ? filename : "?");
  BlockingOpScope::updateActiveState("serving portal asset", "/portal GET");
}

void portalHttpResponseCompleted(const char *filename) {
  Serial.printf("[finish-op] ESP HTTP response completed file=%s\n",
                filename ? filename : "?");
  BlockingOpScope::updateActiveState("portal response sent", "/portal GET");
}

BlockingOpConfig routerApiOp(const char *name, const char *reason) {
  return BlockingOpConfig{name,
                          "RouterOS reply",
                          RenzFiConfig::SETUP_ROUTER_IO_TIMEOUT_MS,
                          0,
                          reason ? reason : name,
                          "executing"};
}

BlockingOpConfig tcpConnectOp(const char *name) {
  return BlockingOpConfig{name,
                          "TCP response",
                          RenzFiConfig::SETUP_ROUTER_CONNECT_TIMEOUT_MS,
                          0,
                          "RouterOS API TCP connect",
                          "connecting"};
}

BlockingOpConfig storageWriteOp(const char *name, const char *media) {
  // timeoutMs=0 is intentional metadata: no interruptible FS timeout exists.
  return BlockingOpConfig{name, media, 0, 0, "filesystem persist", "writing"};
}

BlockingOpConfig storageReadOp(const char *name, const char *media) {
  // timeoutMs=0 is intentional metadata: no interruptible FS timeout exists.
  return BlockingOpConfig{name, media, 0, 0, "filesystem read", "reading"};
}

BlockingOpConfig fixedDelayOp(const char *name, uint32_t delayMs) {
  return BlockingOpConfig{name, "fixed delay", delayMs, 0, "post-fetch settle",
                          "delaying"};
}

BlockingOpConfig portalFetchOp(const char *name) {
  return BlockingOpConfig{name,
                          "RouterOS /tool/fetch + ESP HTTP",
                          RenzFiConfig::ROUTER_WORKER_JOB_TIMEOUT_MS,
                          0,
                          "MikroTik portal self-fetch",
                          "fetching"};
}

}  // namespace FinishTrace
