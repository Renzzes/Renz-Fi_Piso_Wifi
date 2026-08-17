#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "Models.h"
#include "RouterCommandScratch.h"
#include "RouterOsClient.h"
#include "SetupRouterConnectionManager.h"

class EthernetManager;
class EventBus;
class InstallationStateManager;
class RouterProvisioningEngine;
class RouterProvisioningManager;
class RouterPlatform;
class SetupProvisioningManager;

// Runs RouterOS TCP/API work on a dedicated FreeRTOS task. This is the
// SINGLE owner of MikroTikDriver/RouterOsClient in the firmware — no HTTP
// handler, timer, background task, or diagnostics task may talk to
// RouterOS directly. Callers use one of three patterns:
//  - Setup wizard HTTP handlers enqueue a job and return immediately
//    (HTTP 202 + jobId) instead of blocking on AsyncWebServer while
//    RouterOS is probed — see pollJob().
//  - Admin dashboard HTTP handlers (test/save/wireless/cache sync) use
//    non-blocking enqueueAdmin*() → HTTP 202 + jobId, then poll
//    GET /api/router/jobs/<id>. AsyncTCP must NEVER wait on RouterOS.
//  - Runtime hotspot activation/deauthorization (PortalSessionManager) use
//    the non-blocking tryEnqueueActivateHotspotUser()/
//    tryEnqueueDeauthorizeHotspotUser(), which run at Critical priority and
//    simply fail fast (false) if the worker is busy — the caller is
//    responsible for retrying (PortalSessionManager's own deferred-work
//    queue already does this).
// TcpDiagnostic/ApiProtocolDiagnostic/ApplyConfiguration are debug/legacy
// paths not reachable from the current wizard UI and remain on the older
// blocking dispatch() path (see runApply/runTcpDiagnostic/
// runApiProtocolDiagnostic).
class RouterProvisioningWorker {
 public:
  struct Result {
    int    httpStatus = 500;
    String body;
    bool   ok = false;
    // ROS health reason on failure — never a secret. Empty → "job_failed".
    String healthReason;
  };

  // Lifecycle of a single async job — only ever one at a time (matches the
  // single-slot queue/worker design and the "single Router Worker" rule).
  enum class JobState : uint8_t { Queued, Running, Completed, Failed };

  struct JobRecord {
    uint32_t   jobId = 0;
    JobState   state = JobState::Queued;
    Result     result;
    // Live progress, populated only for job types that report it
    // (FinishSetupProvisioning, ExistingNetworkScan) — empty otherwise.
    String     stageId;
    String     stageLabel;
  };

  struct EnqueueOutcome {
    bool     accepted = false;  // false => caller should 503
    uint32_t jobId = 0;
    const char *rejectCode = nullptr;
    const char *rejectMessage = nullptr;
  };

  void begin(EthernetManager *eth, SetupRouterConnectionManager *routerConnection,
             RouterProvisioningManager *routerProvisioning,
             SetupProvisioningManager *setupProvisioning,
             InstallationStateManager *installation,
             RouterProvisioningEngine *finishEngine = nullptr,
             RouterPlatform *routerPlatform = nullptr,
             EventBus *events = nullptr);

  bool isBusy() const;
  /** Ethernet link UP and a non-zero local IP — required before Activate enqueue. */
  bool ethernetReadyForHotspot() const;
  String ethernetIpLabel() const;

  // Fire-and-forget: if the worker is idle, enqueues a Wi-Fi discovery job
  // and returns immediately without waiting for it to finish (the caller
  // does not receive the result directly — the job stores it into
  // WifiDiscoveryCache when it completes, same as a normal
  // runListWifiNetworks() call). Returns false (no-op, no queuing) when the
  // worker is already busy, so this can never stack a second job behind the
  // one in flight. Used to opportunistically refresh a stale cache without
  // making the current HTTP request pay the RouterOS round trip.
  bool tryRefreshWifiNetworksInBackground();

  // Non-blocking enqueue API used by the setup wizard's HTTP handlers.
  // Each returns {accepted:false} without touching MikroTik when a job is
  // already in flight (Single Router Worker Rule) — never queues a second
  // job, never reconnects, never starts a second RouterOS client.
  EnqueueOutcome enqueueTest(const SetupRouterConnectionManager::RouterInput &input);
  EnqueueOutcome enqueueSave(const SetupRouterConnectionManager::RouterInput &input);
  EnqueueOutcome enqueueExistingNetworkScan();
  EnqueueOutcome enqueueConfigureExistingNetwork(const char *requestJson);
  EnqueueOutcome enqueueFinishSetup(const char *requestJson = nullptr);

  // Non-blocking, fire-and-forget: used by PortalSessionManager for
  // runtime hotspot activation/deauthorization. Runs at Critical priority
  // (never delayed behind discovery/provisioning work). Returns false
  // without touching MikroTik when the worker is already busy — the
  // caller is expected to retry via its own deferred-work queue rather
  // than this method queuing a second job (Single Router Worker Rule).
  bool tryEnqueueActivateHotspotUser(const HotspotUser &user);
  bool tryEnqueueDeauthorizeHotspotUser(const String &mac,
                                        uint32_t sessionGeneration = 0);
  bool tryEnqueuePauseHotspotUser(const String &mac,
                                  uint32_t sessionGeneration = 0);
  // Coalesced Active presence check — PortalSessionManager only, never from
  // async_tcp. Outcome ok=query succeeded; reason "not_active" when empty.
  bool tryEnqueueVerifyHotspotActive(const String &mac,
                                     uint32_t sessionGeneration = 0);
  /** Health FSM readiness probe — only when gate wantsHealthProbe(). */
  bool tryEnqueueHealthProbe();

  // PortalSessionManager drains these from loop() — 0 RouterOS traffic.
  enum class HotspotOutcomeKind : uint8_t {
    Activate = 1,
    Pause,
    Deauthorize,
    VerifyActive,
  };
  struct HotspotOutcome {
    HotspotOutcomeKind kind = HotspotOutcomeKind::Activate;
    bool ok                 = false;
    char mac[18]            = {};
    // Exact failure text for the portal/admin UI — never fail silently.
    char reason[128]        = {};
    uint32_t generation     = 0;
    uint32_t authorizedAtMs = 0;
    uint32_t grantedSeconds = 0;
    uint32_t existingUserUptime = 0;
    uint32_t existingUserLimit = 0;
    uint32_t newUserLimit = 0;
    uint32_t activeUptime = 0;
    uint32_t activeSessionTimeLeft = 0;
    bool activeLoginSuccess = false;
    bool activeVerifySuccess = false;
    bool usedActiveSet = false;
  };
  bool takeHotspotOutcome(HotspotOutcome &out);

  using IdleCallback = void (*)(void *ctx);
  void setIdleCallback(IdleCallback callback, void *ctx);

  // Blocking dispatch wrappers for the admin dashboard's router routes
  // (/api/router/test, /settings, /wireless, /cache/sync, /cache/refresh).
  // Preserve the exact synchronous response contract those handlers had
  // before (caller waits, gets a final JSON body) — the only change is
  // that MikroTikDriver is now only ever invoked from this worker's task,
  // not directly from async_tcp.
  // Non-blocking Admin Dashboard enqueue — copies request payload into the
  // worker slot and returns immediately. Caller must poll pollJob()/HTTP.
  EnqueueOutcome enqueueAdminTest(const String &overrideSettingsJson);
  EnqueueOutcome enqueueAdminSaveSettings(const String &settingsJson);
  EnqueueOutcome enqueueAdminSaveWireless(const String &wirelessJson);
  EnqueueOutcome enqueueAdminSyncCache(const String &successMessage);
  EnqueueOutcome enqueueAdminRefreshCache(const String &successMessage);
  EnqueueOutcome enqueueAdminUserProfileOp(const String &requestJson);

  // Mutex-protected snapshot read of the most recently enqueued job. Returns
  // false when jobId doesn't match the tracked job (never existed, or the
  // device rebooted since — callers disambiguate via bootInstanceId).
  bool pollJob(uint32_t jobId, JobRecord &out) const;
  static const char *jobStateLabel(JobState state);

  Result runApply(const char *requestJson);
  Result runTcpDiagnostic(const String &host, uint16_t port, uint8_t iterations);
  Result runApiProtocolDiagnostic(
      const SetupRouterConnectionManager::RouterInput &input);
  Result runListWifiNetworks();

  bool hasActiveApplyJob() const;

  TaskHandle_t taskHandle() const { return _task; }

 private:
  enum class OpType : uint8_t {
    TestConnection = 1,
    SaveConnection,
    ApplyConfiguration,
    TcpDiagnostic,
    ApiProtocolDiagnostic,
    ExistingNetworkScan,
    ListWifiNetworks,
    ConfigureExistingNetwork,
    FinishSetupProvisioning,
    ActivateHotspotUser,
    DeauthorizeHotspotUser,
    PauseHotspotUser,
    VerifyHotspotActive,
    HealthProbe,
    AdminTestConnection,
    AdminSaveSettings,
    AdminSaveWireless,
    AdminSyncCache,
    AdminRefreshCache,
    AdminUserProfileOp,
  };

  struct WorkSlot {
    OpType type = OpType::TestConnection;
    uint32_t jobId = 0;
    SetupRouterConnectionManager::RouterInput routerInput;
    String requestJson;
    String diagHost;
    uint16_t diagPort = 8728;
    uint8_t diagIterations = 0;
    HotspotUser hotspotUser;
    String hotspotDeauthMac;
    uint32_t hotspotGeneration = 0;
    String adminMessage;
    Result result;
  };

  Result dispatch(const WorkSlot &prepared);
  EnqueueOutcome enqueueInternal(const WorkSlot &prepared);
  EnqueueOutcome enqueueAdminPrepared(const WorkSlot &prepared,
                                      const char *queuedLabel);
  bool storageRecoveryBlocksAdmin() const;
  bool enqueueFireAndForget(const WorkSlot &prepared,
                            bool requireHotspotOutcomeCapacity = false);
  void runOp(WorkSlot &slot);
  static const char *opTypeLabel(OpType type);
  void publishHotspotOutcome(HotspotOutcomeKind kind, const String &mac, bool ok,
                             const String &reason = String(),
                             uint32_t generation = 0,
                             const ActivateAuthTrace *authTrace = nullptr);
  String ensureRouterCredentialsForHotspotJob();
  String hotspotFailureReason(const String &credentialError,
                              const char *fallback);

  void setJobRunning(uint32_t jobId);
  void setJobFinished(uint32_t jobId, const Result &result);
  void onJobProgress(uint32_t jobId, const char *stageId, const char *label);
  static void progressTrampoline(void *ctx, const char *stageId, const char *label);
  void emitJobEvent(uint32_t jobId, OpType type, const char *state,
                    const char *stageId, const char *stageLabel) const;

  static void taskEntry(void *arg);
  void taskLoop();

  EthernetManager              *_eth = nullptr;
  SetupRouterConnectionManager *_routerConnection = nullptr;
  RouterProvisioningManager    *_routerProvisioning = nullptr;
  SetupProvisioningManager     *_setupProvisioning = nullptr;
  InstallationStateManager     *_installation = nullptr;
  RouterProvisioningEngine     *_finishEngine = nullptr;
  RouterPlatform               *_routerPlatform = nullptr;
  EventBus                     *_events = nullptr;

  // One lossless outcome slot. A new hotspot job is not accepted until the
  // previous outcome has been consumed by PortalSessionManager.
  QueueHandle_t _hotspotOutcomeQueue = nullptr;
  IdleCallback _idleCallback = nullptr;
  void *_idleCallbackCtx = nullptr;

  mutable SemaphoreHandle_t _dispatchMutex = nullptr;
  SemaphoreHandle_t         _doneSem = nullptr;
  QueueHandle_t             _queue = nullptr;
  TaskHandle_t              _task = nullptr;

  WorkSlot _slot{};
  RouterOsClient::CommandResult _cmdScratch{};
  volatile bool _running = false;
  OpType _activeType = OpType::TestConnection;

  uint32_t  _nextJobId = 0;
  JobRecord _lastJob{};
};
