#pragma once

#include <Arduino.h>

// Global RouterOS API transport policy: one session, connect cooldown, failure
// backoff, health FSM, and auditable serial logs. Only the
// RouterProvisioningWorker task should drive beginJob/endJob around
// RouterOsClient usage.
class RouterApiTransportGate {
 public:
  // Command-pacing priority tiers (see waitBeforeCommand()). There is never
  // a real conflict to arbitrate between concurrently-executing commands —
  // RouterOsClient::acquireSession() already guarantees only one RouterOS
  // command runs system-wide at a time — this only controls how the *next*
  // command is paced/gated relative to observed MikroTik CPU load.
  enum class RouterJobPriority : uint8_t {
    Critical,  // login/voucher/session hotspot-activation — never delayed behind discovery
    Normal,    // provisioning + Setup-essential reads (incl. list-wifi / existing scan)
    Low,       // optional/background inventory only — may pause above CPU pause threshold
  };

  // Non-blocking RouterOS availability FSM. Idle systems with no pending work
  // must not poll RouterOS merely to keep this state fresh.
  enum class RouterHealth : uint8_t {
    Unknown = 0,
    Connecting,
    Healthy,
    Degraded,
    Unavailable,
    Cooldown,
    Probing,
    Recovering,
  };

  static void beginJob(uint32_t jobId, uint32_t deadlineMs);
  static void endJob(uint32_t jobId, bool success, const char *failReason = nullptr);

  static RouterHealth health();
  static const char *healthLabel();
  static const char *healthLabel(RouterHealth state);
  /** Advance COOLDOWN→probe desire and RECOVERING→HEALTHY dwell (non-blocking). */
  static void tickHealth(uint32_t nowMs);
  static bool wantsHealthProbe(uint32_t nowMs);
  static void beginHealthProbe();
  static void endHealthProbe(bool ok);

  static bool allowsHotspotActivate();
  static bool allowsHotspotVerify();
  static bool allowsHotspotDeauth();
  static bool allowsAdminNonEssential();

  static uint32_t currentJobId();
  static bool jobExpired();
  // Read-only diagnostic accessor — the absolute millis() deadline set by the
  // most recent beginJob() call (0 if no job is active). Used only for
  // login-regression logging; never mutates gate state.
  static uint32_t currentJobDeadlineMs();
  // Milliseconds remaining before jobExpired() becomes true. Returns a large
  // sentinel when no job is active (callers treat as "budget OK").
  static uint32_t remainingJobBudgetMs();

  // Blocks with vTaskDelay until min connect interval and backoff elapse.
  static bool waitUntilConnectAllowed();
  /** After a successful job, allow the next TCP connect without min-interval wait. */
  static void clearConnectThrottleAfterSuccess();
  static void recordConnectAttempt();
  static void recordFailure();

  static bool acquireSession();
  static void releaseSession();

  static void setQueueDepth(uint8_t depth);
  static void logQueueDepth();

  // ── MikroTik CPU protection ────────────────────────────────────────────
  // Enforces a minimum spacing between any two RouterOS commands (any
  // caller, any command path), widening across 5 tiers as observed CPU load
  // climbs (see Config.h ROUTER_CMD_DELAY_TIER1..5_MS). Above the pause
  // threshold, a Low-priority current command instead loop-waits (never
  // sends) until CPU recovers or the job's own deadline expires — bounded
  // by jobExpired(), so this can never turn into an unbounded wait.
  static void waitBeforeCommand();
  // Records that a command just finished (success or failure) so the next
  // waitBeforeCommand() call measures the gap from *this* point.
  static void recordCommandCompleted();

  // Fed opportunistically from existing /system/resource/print reads (never
  // from a dedicated poll). percent=255 means "unknown/not sampled yet".
  static void recordObservedCpuLoad(uint8_t percent);
  static uint8_t lastObservedCpuLoadPercent();
  // True when the most recent (non-stale) CPU sample is at/above the safe
  // threshold — callers should skip non-essential/duplicate discovery work.
  static bool cpuUnderPressure();

  // ── Job priority (see RouterJobPriority / RouterPriorityGuard below) ───
  static void setPriority(RouterJobPriority priority);
  static RouterJobPriority currentPriority();

 private:
  static void logCooldownRemaining(uint32_t remainingMs);
  static void setHealth(RouterHealth next, const char *reason);
  static void noteJobSuccess();
  static void noteJobFailure(const char *reason);
};

// RAII helper: temporarily raises/lowers the gate's command-pacing priority
// for the lifetime of the guard, restoring the previous value on scope
// exit (safe to nest, e.g. a Critical hotspot-auth call made from within a
// Normal-priority provisioning job).
class RouterPriorityGuard {
 public:
  explicit RouterPriorityGuard(RouterApiTransportGate::RouterJobPriority priority)
      : _previous(RouterApiTransportGate::currentPriority()) {
    RouterApiTransportGate::setPriority(priority);
  }
  ~RouterPriorityGuard() { RouterApiTransportGate::setPriority(_previous); }

  RouterPriorityGuard(const RouterPriorityGuard &) = delete;
  RouterPriorityGuard &operator=(const RouterPriorityGuard &) = delete;

 private:
  RouterApiTransportGate::RouterJobPriority _previous;
};
