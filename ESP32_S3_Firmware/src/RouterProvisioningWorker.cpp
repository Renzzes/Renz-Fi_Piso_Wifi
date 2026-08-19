#include "RouterCommandScratch.h"
#include "RouterProvisioningWorker.h"

#include <memory>
#include <vector>

#include "Config.h"
#include "EthernetManager.h"
#include "EventBus.h"
#include "ExistingNetworkScanner.h"
#include "ExistingNetworkScan.h"
#include "InstallationStateManager.h"
#include "JsonHeap.h"
#include "RouterApiTransportGate.h"
#include "RouterOsClient.h"
#include "RouterProvisioningEngine.h"
#include "router/RouterPlatform.h"
#include "RouterWirelessAdapter.h"
#include "RouterProvisioningManager.h"
#include "RouterWorkerDiagnostics.h"
#include "SetupProvisioningManager.h"
#include "SetupStatusContext.h"
#include "StorageManager.h"
#include "ExistingNetworkScanCache.h"
#include "WifiDiscoveryCache.h"
#include "ActivationLatencyTrace.h"
#include "FinishTrace.h"

namespace {

constexpr uint32_t kDispatchTimeoutMs =
    RenzFiConfig::ROUTER_WORKER_JOB_TIMEOUT_MS + 5000U;

String buildErrorBody(const String &error, const String &code,
                      const String &stage = "") {
  DynamicJsonDocument doc(384);
  doc["success"] = false;
  doc["error"]   = error;
  doc["code"]    = code;
  if (!stage.isEmpty()) doc["stage"] = stage;
  String out;
  serializeJson(doc, out);
  return out;
}

RouterProvisioningWorker::Result busyResult() {
  RouterProvisioningWorker::Result out;
  out.httpStatus = 503;
  out.body       = buildErrorBody("Router worker is busy", "ROUTER_WORKER_BUSY");
  out.ok         = false;
  return out;
}

RouterProvisioningWorker::Result timeoutResult() {
  RouterProvisioningWorker::Result out;
  out.httpStatus = 504;
  out.body       = buildErrorBody("Router operation timed out", "ROUTER_JOB_TIMEOUT",
                                  "worker");
  out.ok         = false;
  return out;
}

void classifyRouterCollectError(const String &error, String &codeOut,
                                String &healthReasonOut, bool refresh) {
  if (error.indexOf("username is not configured") >= 0 ||
      error.indexOf("password is not configured") >= 0 ||
      error.indexOf("IP is not configured") >= 0 ||
      error.indexOf("host is not configured") >= 0 ||
      error.indexOf("settings not available") >= 0 ||
      error.indexOf("Unable to load saved router credentials") >= 0 ||
      error.indexOf("credentials are not configured") >= 0) {
    codeOut = "ROUTER_CREDENTIALS_MISSING";
    healthReasonOut = "credentials_missing";
    return;
  }
  if (error.indexOf("Invalid RouterOS API username or password") >= 0 ||
      error.indexOf("LOGIN_FAILED") >= 0) {
    codeOut = "ROUTER_AUTH_FAILED";
    healthReasonOut = "auth_failed";
    return;
  }
  if (error.indexOf("TCP connect") >= 0 ||
      error.indexOf("Ethernet link is down") >= 0 ||
      error.indexOf("unreachable") >= 0 ||
      error.indexOf("API unavailable") >= 0) {
    codeOut = "ROUTER_UNREACHABLE";
    healthReasonOut = "router_unreachable";
    return;
  }
  codeOut = refresh ? "ROUTER_CACHE_REFRESH_FAILED" : "ROUTER_CACHE_SYNC_FAILED";
  healthReasonOut = "job_failed";
}

void applyCacheJobFailure(RouterProvisioningWorker::Result &result,
                          RouterPlatform *platform, bool refresh) {
  String error =
      platform ? platform->lastCollectError()
               : String("Router platform unavailable");
  if (error.isEmpty()) {
    error = refresh ? String("Unable to refresh router information from RouterOS")
                    : String("Unable to synchronize router configuration from RouterOS");
  }
  String code;
  String healthReason;
  classifyRouterCollectError(error, code, healthReason, refresh);
  result.body         = buildErrorBody(error, code);
  result.httpStatus   = 503;
  result.ok           = false;
  result.healthReason = healthReason;
}

bool openPersistedRouterClient(EthernetManager *eth,
                               SetupRouterConnectionManager *routerConnection,
                               std::unique_ptr<RouterOsClient> &clientOut,
                               String &errorOut, String &errorCodeOut) {
  if (!eth || !routerConnection) {
    errorOut     = "Router connection unavailable";
    errorCodeOut = "INTERNAL_ERROR";
    return false;
  }
  SetupRouterConnectionManager::ResolvedRouterCredentials credentials;
  SetupRouterConnectionManager::OperationResult credResult;
  if (!routerConnection->resolveRouterCredentials(
          SetupRouterConnectionManager::RouterCredentialSource::Persisted, nullptr,
          credentials, credResult)) {
    errorOut     = credResult.errorMessage;
    errorCodeOut = credResult.errorCode;
    return false;
  }
  clientOut.reset(new (std::nothrow) RouterOsClient());
  if (!clientOut) {
    errorOut     = "Router client unavailable";
    errorCodeOut = "ROUTER_PLAN_UNAVAILABLE";
    return false;
  }
  const auto input = credentials.toRouterInput();
  clientOut->setTimeouts(RenzFiConfig::SETUP_ROUTER_CONNECT_TIMEOUT_MS,
                         RenzFiConfig::SETUP_ROUTER_IO_TIMEOUT_MS);
  clientOut->setCredentials(input.host, input.username, input.password, input.apiPort);
  clientOut->setCredentialSource("setup-persisted");
  if (!clientOut->connect()) {
    errorOut     = clientOut->lastError();
    errorCodeOut = clientOut->lastErrorCode();
    clientOut.reset();
    return false;
  }
  if (!clientOut->login()) {
    errorOut     = clientOut->lastError();
    errorCodeOut = clientOut->lastErrorCode();
    clientOut->disconnect();
    clientOut.reset();
    return false;
  }
  (void)eth;
  return true;
}

void fillWorkerSetupStatus(SetupProvisioningManager *provisioning,
                           EthernetManager *eth,
                           RouterProvisioningManager *routerProvisioning,
                           JsonObject data) {
  if (!provisioning || !eth) return;
  provisioning->fillSetupStatus(
      data, eth, buildSetupStatusContext(routerProvisioning, nullptr));
}

String attrFromReply(const RouterOsClient::CommandResult &result, uint8_t row,
                     const char *keyName) {
  if (row >= result.replyCount || !keyName || !keyName[0]) return String();
  const auto &record = result.replyAt(row);
  for (uint8_t i = 0; i < record.attrCount; ++i) {
    String key;
    String value;
    if (RouterOsClient::parseAttr(record.attr(i), key, value) &&
        key.equalsIgnoreCase(keyName)) {
      return value;
    }
  }
  return String();
}

}  // namespace

const char *RouterProvisioningWorker::opTypeLabel(OpType type) {
  switch (type) {
    case OpType::TestConnection:
      return "test";
    case OpType::SaveConnection:
      return "save";
    case OpType::ApplyConfiguration:
      return "apply";
    case OpType::TcpDiagnostic:
      return "tcp-diagnostic";
    case OpType::ApiProtocolDiagnostic:
      return "api-protocol-diagnostic";
    case OpType::ExistingNetworkScan:
      return "existing-network-scan";
    case OpType::ListWifiNetworks:
      return "list-wifi-networks";
    case OpType::ConfigureExistingNetwork:
      return "configure-existing-network";
    case OpType::FinishSetupProvisioning:
      return "finish-setup-provisioning";
    case OpType::ActivateHotspotUser:
      return "activate-hotspot-user";
    case OpType::DeauthorizeHotspotUser:
      return "deauthorize-hotspot-user";
    case OpType::PauseHotspotUser:
      return "pause-hotspot-user";
    case OpType::VerifyHotspotActive:
      return "verify-hotspot-active";
    case OpType::HealthProbe:
      return "health-probe";
    case OpType::AdminTestConnection:
      return "admin-test";
    case OpType::AdminSaveSettings:
      return "admin-save-settings";
    case OpType::AdminSaveWireless:
      return "admin-save-wireless";
    case OpType::AdminSyncCache:
      return "admin-sync-cache";
    case OpType::AdminRefreshCache:
      return "admin-refresh-cache";
    case OpType::AdminUserProfileOp:
      return "admin-user-profile";
    case OpType::AccessPointDetect:
      return "access-point-detect";
    default:
      return "unknown";
  }
}

const char *RouterProvisioningWorker::jobStateLabel(JobState state) {
  switch (state) {
    case JobState::Queued:
      return "queued";
    case JobState::Running:
      return "running";
    case JobState::Completed:
      return "completed";
    case JobState::Failed:
      return "failed";
    default:
      return "unknown";
  }
}

void RouterProvisioningWorker::begin(
    EthernetManager *eth, SetupRouterConnectionManager *routerConnection,
    RouterProvisioningManager *routerProvisioning,
    SetupProvisioningManager *setupProvisioning,
    InstallationStateManager *installation,
    RouterProvisioningEngine *finishEngine,
    RouterPlatform *routerPlatform,
    EventBus *events) {
  _eth                = eth;
  _routerConnection   = routerConnection;
  _routerProvisioning = routerProvisioning;
  _setupProvisioning  = setupProvisioning;
  _installation       = installation;
  _finishEngine       = finishEngine;
  _routerPlatform     = routerPlatform;
  _events             = events;

  if (_task) return;

  _dispatchMutex = xSemaphoreCreateMutex();
  _hotspotOutcomeQueue = xQueueCreate(1, sizeof(HotspotOutcome));
  _doneSem       = xSemaphoreCreateBinary();
  _queue = xQueueCreate(RenzFiConfig::ROUTER_WORKER_QUEUE_DEPTH, sizeof(uint8_t));
  if (!_dispatchMutex || !_hotspotOutcomeQueue || !_doneSem || !_queue) {
    Serial.println("[router-worker] init failed: queue/mutex unavailable");
    return;
  }

  const BaseType_t coreId = RenzFiConfig::ROUTER_WORKER_CORE_AFFINITY < 0
                                ? tskNO_AFFINITY
                                : RenzFiConfig::ROUTER_WORKER_CORE_AFFINITY;

  BaseType_t ok = xTaskCreatePinnedToCore(
      taskEntry, "router_worker", RenzFiConfig::RENZFI_ROUTER_WORKER_STACK_WORDS, this,
      1, &_task, coreId);
  if (ok != pdPASS) {
    Serial.println("[router-worker] init failed: task create rejected");
    _task = nullptr;
    return;
  }

  Serial.printf(
      "[router-worker] started stackWords=%u stackBytes=%u coreAffinity=%d\n",
      static_cast<unsigned>(RenzFiConfig::RENZFI_ROUTER_WORKER_STACK_WORDS),
      static_cast<unsigned>(RenzFiConfig::RENZFI_ROUTER_WORKER_STACK_WORDS *
                            sizeof(StackType_t)),
      static_cast<int>(RenzFiConfig::ROUTER_WORKER_CORE_AFFINITY));
}

bool RouterProvisioningWorker::isBusy() const { return _running; }

bool RouterProvisioningWorker::ethernetReadyForHotspot() const {
  if (!_eth || !_eth->linkUp() || !_eth->hasIp()) return false;
  const String ip = _eth->ip();
  return ip.length() > 0 && ip != "0.0.0.0";
}

String RouterProvisioningWorker::ethernetIpLabel() const {
  if (!_eth || !_eth->hasIp()) return String("0.0.0.0");
  const String ip = _eth->ip();
  if (ip.length() == 0) return String("0.0.0.0");
  return ip;
}

bool RouterProvisioningWorker::tryRefreshWifiNetworksInBackground() {
  if (storageRecoveryBlocksAdmin()) {
    Serial.println(
        "[router-worker] admin job deferred reason=storage_recovery");
    return false;
  }
  WorkSlot prepared;
  prepared.type = OpType::ListWifiNetworks;
  const bool accepted = enqueueFireAndForget(prepared);
  if (accepted) {
    Serial.println(
        "[router-worker] background refresh dispatch type=list-wifi-networks");
  }
  return accepted;
}

bool RouterProvisioningWorker::tryEnqueueActivateHotspotUser(const HotspotUser &user) {
  if (!ethernetReadyForHotspot()) {
    static uint32_t s_lastEthLogMs = 0;
    const uint32_t now = millis();
    if (s_lastEthLogMs == 0 || (now - s_lastEthLogMs) >= 5000U) {
      s_lastEthLogMs = now;
      Serial.printf("[activate] deferred reason=ethernet_not_ready ip=%s\n",
                    ethernetIpLabel().c_str());
    }
    return false;
  }
  if (!RouterApiTransportGate::allowsHotspotActivate()) {
    Serial.println(
        "[router-worker] activate deferred reason=router_unavailable");
    return false;
  }
  WorkSlot prepared;
  prepared.type = OpType::ActivateHotspotUser;
  prepared.hotspotUser = user;
  const bool accepted = enqueueFireAndForget(prepared, true);
  if (accepted) {
    Serial.println(
        "[router-worker] dispatch type=activate-hotspot-user priority=critical");
  }
  return accepted;
}

bool RouterProvisioningWorker::tryEnqueueDeauthorizeHotspotUser(
    const String &mac, uint32_t sessionGeneration) {
  if (!RouterApiTransportGate::allowsHotspotDeauth()) {
    Serial.println(
        "[router-worker] deauth deferred reason=router_unavailable");
    return false;
  }
  WorkSlot prepared;
  prepared.type = OpType::DeauthorizeHotspotUser;
  prepared.hotspotDeauthMac = mac;
  prepared.hotspotGeneration = sessionGeneration;
  const bool accepted = enqueueFireAndForget(prepared, true);
  if (accepted) {
    Serial.printf(
        "[router-worker] dispatch type=deauthorize-hotspot-user "
        "priority=critical gen=%u\n",
        static_cast<unsigned>(sessionGeneration));
  }
  return accepted;
}

bool RouterProvisioningWorker::tryEnqueuePauseHotspotUser(
    const String &mac, uint32_t sessionGeneration) {
  if (!RouterApiTransportGate::allowsHotspotDeauth()) {
    Serial.println(
        "[router-worker] pause deferred reason=router_unavailable");
    return false;
  }
  WorkSlot prepared;
  prepared.type = OpType::PauseHotspotUser;
  prepared.hotspotDeauthMac = mac;
  prepared.hotspotGeneration = sessionGeneration;
  const bool accepted = enqueueFireAndForget(prepared, true);
  if (accepted) {
    Serial.printf(
        "[router-worker] dispatch type=pause-hotspot-user priority=critical "
        "gen=%u\n",
        static_cast<unsigned>(sessionGeneration));
  }
  return accepted;
}

bool RouterProvisioningWorker::tryEnqueueVerifyHotspotActive(
    const String &mac, uint32_t sessionGeneration) {
  if (!RouterApiTransportGate::allowsHotspotVerify()) {
    static uint32_t s_lastLogMs = 0;
    const uint32_t now = millis();
    if (s_lastLogMs == 0 || (now - s_lastLogMs) >= 30000U) {
      s_lastLogMs = now;
      Serial.println(
          "[router-worker] verify skipped reason=router_unavailable");
    }
    return false;
  }
  WorkSlot prepared;
  prepared.type = OpType::VerifyHotspotActive;
  prepared.hotspotDeauthMac = mac;
  prepared.hotspotGeneration = sessionGeneration;
  // Same outcome mailbox as other hotspot jobs — do not enqueue while a
  // prior Activate/Pause/Deauth/Verify result is still unread.
  const bool accepted = enqueueFireAndForget(prepared, true);
  if (accepted) {
    Serial.println(
        "[router-worker] dispatch type=verify-hotspot-active priority=normal");
  }
  return accepted;
}

bool RouterProvisioningWorker::tryEnqueueHealthProbe() {
  if (!RouterApiTransportGate::wantsHealthProbe(millis())) return false;
  WorkSlot prepared;
  prepared.type = OpType::HealthProbe;
  // Probe must not block behind an unread hotspot outcome, but also must not
  // require the outcome mailbox (no HotspotOutcome published).
  const bool accepted = enqueueFireAndForget(prepared, false);
  if (accepted) {
    RouterApiTransportGate::beginHealthProbe();
    Serial.println("[router-worker] dispatch type=health-probe");
  }
  return accepted;
}

bool RouterProvisioningWorker::enqueueFireAndForget(
    const WorkSlot &prepared, bool requireHotspotOutcomeCapacity) {
  if (!_queue || !_dispatchMutex || !_doneSem) return false;
  if (xSemaphoreTake(_dispatchMutex, 0) != pdTRUE) return false;
  if (_running ||
      (requireHotspotOutcomeCapacity &&
       (!_hotspotOutcomeQueue ||
        uxQueueMessagesWaiting(_hotspotOutcomeQueue) != 0))) {
    xSemaphoreGive(_dispatchMutex);
    return false;
  }

  _slot = prepared;
  _slot.jobId = 0;
  _slot.result = Result{};
  _running = true;
  xSemaphoreTake(_doneSem, 0);
  uint8_t wake = 1;
  if (xQueueSend(_queue, &wake, 0) != pdTRUE) {
    _running = false;
    xSemaphoreGive(_dispatchMutex);
    return false;
  }
  xSemaphoreGive(_dispatchMutex);
  return true;
}

bool RouterProvisioningWorker::takeHotspotOutcome(HotspotOutcome &out) {
  return _hotspotOutcomeQueue &&
         xQueueReceive(_hotspotOutcomeQueue, &out, 0) == pdTRUE;
}

String RouterProvisioningWorker::ensureRouterCredentialsForHotspotJob() {
  // Storage-only reconciliation (0 RouterOS commands). Always re-reads disk
  // so SD remount cannot leave a stale in-memory OK flag.
  if (!_finishEngine) {
    Serial.println(
        "[activate] stage=credentials FAIL reason=finish_engine_unavailable");
    return String("Router credential service unavailable");
  }
  Serial.println("[activate] stage=credentials reconcile_begin");
  String error;
  if (_finishEngine->ensureProductionRouterCredentials(error)) {
    Serial.println("[activate] stage=credentials OK");
    return String();
  }
  Serial.printf("[activate] stage=credentials FAIL reason=%s\n",
                error.isEmpty() ? "not_configured" : error.c_str());
  return error.isEmpty() ? String("RouterOS API credentials are not configured")
                         : error;
}

String RouterProvisioningWorker::hotspotFailureReason(
    const String &credentialError, const char *fallback) {
  if (!credentialError.isEmpty()) return credentialError;
  const String driverError =
      _routerPlatform ? _routerPlatform->lastHotspotError() : String();
  if (!driverError.isEmpty()) return driverError;
  if (!_routerPlatform) return String("Router platform unavailable");
  return String(fallback);
}

void RouterProvisioningWorker::publishHotspotOutcome(HotspotOutcomeKind kind,
                                                     const String &mac,
                                                     bool ok,
                                                     const String &reason,
                                                     uint32_t generation,
                                                     const ActivateAuthTrace *authTrace) {
  if (!_hotspotOutcomeQueue) return;
  HotspotOutcome outcome;
  outcome.kind = kind;
  outcome.ok = ok;
  outcome.generation = generation;
  outcome.mac[0] = '\0';
  outcome.reason[0] = '\0';
  if (mac.length() > 0) {
    mac.toCharArray(outcome.mac, sizeof(outcome.mac));
  }
  if (reason.length() > 0) {
    reason.toCharArray(outcome.reason, sizeof(outcome.reason));
  }
  if (authTrace) {
    outcome.authorizedAtMs = authTrace->authorizedAtMs;
    outcome.grantedSeconds = authTrace->grantedSeconds;
    outcome.existingUserUptime = authTrace->existingUserUptime;
    outcome.existingUserLimit = authTrace->existingUserLimit;
    outcome.newUserLimit = authTrace->newUserLimit;
    outcome.activeUptime = authTrace->activeUptime;
    outcome.activeSessionTimeLeft = authTrace->activeSessionTimeLeft;
    outcome.activeLoginSuccess = authTrace->activeLoginSuccess;
    outcome.activeVerifySuccess = authTrace->activeVerifySuccess;
    outcome.usedActiveSet = authTrace->usedActiveSet;
  }
  // Bounded one-deep mailbox. Never replace a newer generation with an older
  // one. Same-generation newest wins so the latest Activate is not dropped.
  if (xQueueSend(_hotspotOutcomeQueue, &outcome, pdMS_TO_TICKS(100)) !=
      pdTRUE) {
    HotspotOutcome discarded;
    if (xQueueReceive(_hotspotOutcomeQueue, &discarded, 0) == pdTRUE) {
      const bool keepDiscarded =
          discarded.generation > outcome.generation &&
          discarded.generation != 0;
      Serial.printf(
          "[router-worker] hotspot outcome mailbox prior kind=%u gen=%u "
          "incoming kind=%u gen=%u keep=%s\n",
          static_cast<unsigned>(discarded.kind),
          static_cast<unsigned>(discarded.generation),
          static_cast<unsigned>(outcome.kind),
          static_cast<unsigned>(outcome.generation),
          keepDiscarded ? "prior" : "incoming");
      if (keepDiscarded) {
        outcome = discarded;
      }
    }
    if (xQueueSend(_hotspotOutcomeQueue, &outcome, 0) != pdTRUE) {
      Serial.println(
          "[router-worker] ERROR hotspot outcome could not be published");
    }
  }
}

void RouterProvisioningWorker::setIdleCallback(IdleCallback callback,
                                               void *ctx) {
  if (!_dispatchMutex ||
      xSemaphoreTake(_dispatchMutex, pdMS_TO_TICKS(200)) != pdTRUE) {
    return;
  }
  _idleCallback = callback;
  _idleCallbackCtx = ctx;
  xSemaphoreGive(_dispatchMutex);
}

bool RouterProvisioningWorker::hasActiveApplyJob() const {
  return _running && _activeType == OpType::ApplyConfiguration;
}

RouterProvisioningWorker::Result RouterProvisioningWorker::dispatch(
    const WorkSlot &prepared) {
  if (!_queue || !_dispatchMutex || !_doneSem) {
    Result out;
    out.httpStatus = 503;
    out.body = buildErrorBody("Router worker unavailable", "INTERNAL_ERROR");
    return out;
  }

  if (xSemaphoreTake(_dispatchMutex, pdMS_TO_TICKS(200)) != pdTRUE) {
    return busyResult();
  }
  if (_running) {
    xSemaphoreGive(_dispatchMutex);
    return busyResult();
  }

  _slot = prepared;
  _slot.jobId  = 0;  // blocking dispatch — not part of the async job registry
  _slot.result = Result{};
  _running = true;

  xSemaphoreTake(_doneSem, 0);
  uint8_t wake = 1;
  if (xQueueSend(_queue, &wake, 0) != pdTRUE) {
    _running = false;
    xSemaphoreGive(_dispatchMutex);
    return busyResult();
  }

  Serial.printf("[router-worker] dispatch type=%s\n",
                RouterProvisioningWorker::opTypeLabel(prepared.type));

  if (xSemaphoreTake(_doneSem, pdMS_TO_TICKS(kDispatchTimeoutMs)) != pdTRUE) {
    xSemaphoreGive(_dispatchMutex);
    return timeoutResult();
  }

  const Result out = _slot.result;
  xSemaphoreGive(_dispatchMutex);
  return out;
}

RouterProvisioningWorker::EnqueueOutcome RouterProvisioningWorker::enqueueInternal(
    const WorkSlot &prepared) {
  EnqueueOutcome out;
  if (!_queue || !_dispatchMutex || !_doneSem) return out;

  if (xSemaphoreTake(_dispatchMutex, pdMS_TO_TICKS(200)) != pdTRUE) return out;
  if (_running) {
    xSemaphoreGive(_dispatchMutex);
    return out;
  }

  _slot = prepared;
  _slot.result = Result{};

  ++_nextJobId;
  if (_nextJobId == 0) _nextJobId = 1;  // 0 is reserved for "no job"
  _slot.jobId = _nextJobId;
  _lastJob = JobRecord{};
  _lastJob.jobId = _nextJobId;
  _lastJob.state = JobState::Queued;
  _running = true;

  xSemaphoreTake(_doneSem, 0);
  uint8_t wake = 1;
  if (xQueueSend(_queue, &wake, 0) != pdTRUE) {
    _running = false;
    _lastJob = JobRecord{};
    xSemaphoreGive(_dispatchMutex);
    return out;
  }

  out.accepted = true;
  out.jobId    = _nextJobId;

  Serial.printf("[router-worker] enqueued type=%s jobId=%u\n",
                opTypeLabel(prepared.type), static_cast<unsigned>(_nextJobId));

  xSemaphoreGive(_dispatchMutex);
  emitJobEvent(out.jobId, prepared.type, "queued", "", "");
  if (prepared.type == OpType::FinishSetupProvisioning) {
    FinishTrace::jobLifecycle(out.jobId, "QUEUED");
  }
  return out;
}

bool RouterProvisioningWorker::storageRecoveryBlocksAdmin() const {
  if (!_finishEngine) return false;
  StorageManager *storage = _finishEngine->storage();
  if (!storage) return false;
  switch (storage->sdLifecycle()) {
    case StorageManager::SdLifecycle::Mounting:
    case StorageManager::SdLifecycle::Remounting:
    case StorageManager::SdLifecycle::Syncing:
    case StorageManager::SdLifecycle::Degraded:
      return true;
    default:
      return false;
  }
}

RouterProvisioningWorker::EnqueueOutcome
RouterProvisioningWorker::enqueueAdminPrepared(const WorkSlot &prepared,
                                               const char *queuedLabel) {
  EnqueueOutcome out;
  StorageManager *storage =
      _finishEngine ? _finishEngine->storage() : nullptr;
  const char *sdState = storage ? storage->sdLifecycleName() : "none";
  if (storageRecoveryBlocksAdmin()) {
    Serial.printf(
        "[router-recovery-gate] state=%s allowed=no reason=storage_recovery\n",
        sdState);
    Serial.printf(
        "[router-worker] admin job deferred reason=storage_recovery type=%s\n",
        opTypeLabel(prepared.type));
    out.rejectCode = "ROUTER_RECOVERY_IN_PROGRESS";
    out.rejectMessage =
        "Router operations are temporarily unavailable while recovery is in progress.";
    return out;
  }
  Serial.printf(
      "[router-recovery-gate] state=%s allowed=yes reason=storage_ready\n",
      sdState);
  out = enqueueInternal(prepared);
  if (out.accepted) {
    Serial.printf("[admin-router-job] queued id=%u type=%s\n",
                  static_cast<unsigned>(out.jobId),
                  queuedLabel ? queuedLabel : opTypeLabel(prepared.type));
  } else {
    out.rejectCode = "ROUTER_WORKER_BUSY";
    out.rejectMessage = "Router worker is busy";
  }
  return out;
}

RouterProvisioningWorker::EnqueueOutcome RouterProvisioningWorker::enqueueTest(
    const SetupRouterConnectionManager::RouterInput &input) {
  WorkSlot prepared;
  prepared.type = OpType::TestConnection;
  prepared.routerInput = input;
  return enqueueInternal(prepared);
}

RouterProvisioningWorker::EnqueueOutcome RouterProvisioningWorker::enqueueSave(
    const SetupRouterConnectionManager::RouterInput &input) {
  WorkSlot prepared;
  prepared.type = OpType::SaveConnection;
  prepared.routerInput = input;
  return enqueueInternal(prepared);
}

RouterProvisioningWorker::EnqueueOutcome
RouterProvisioningWorker::enqueueExistingNetworkScan() {
  WorkSlot prepared;
  prepared.type = OpType::ExistingNetworkScan;
  return enqueueInternal(prepared);
}

RouterProvisioningWorker::EnqueueOutcome
RouterProvisioningWorker::enqueueConfigureExistingNetwork(const char *requestJson) {
  WorkSlot prepared;
  prepared.type = OpType::ConfigureExistingNetwork;
  prepared.requestJson = requestJson ? requestJson : "";
  return enqueueInternal(prepared);
}

RouterProvisioningWorker::EnqueueOutcome
RouterProvisioningWorker::enqueueFinishSetup(const char *requestJson) {
  WorkSlot prepared;
  prepared.type = OpType::FinishSetupProvisioning;
  prepared.requestJson = requestJson ? requestJson : "";
  return enqueueInternal(prepared);
}

bool RouterProvisioningWorker::pollJob(uint32_t jobId, JobRecord &out) const {
  if (!_dispatchMutex || jobId == 0) return false;
  if (xSemaphoreTake(_dispatchMutex, pdMS_TO_TICKS(200)) != pdTRUE) return false;
  const bool found = (_lastJob.jobId == jobId);
  if (found) out = _lastJob;
  xSemaphoreGive(_dispatchMutex);
  return found;
}

void RouterProvisioningWorker::setJobRunning(uint32_t jobId) {
  if (!_dispatchMutex) return;
  if (xSemaphoreTake(_dispatchMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    if (_lastJob.jobId == jobId) _lastJob.state = JobState::Running;
    xSemaphoreGive(_dispatchMutex);
  }
}

void RouterProvisioningWorker::setJobFinished(uint32_t jobId, const Result &result) {
  if (!_dispatchMutex) return;
  if (xSemaphoreTake(_dispatchMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    if (_lastJob.jobId == jobId) {
      _lastJob.result = result;
      _lastJob.state  = result.ok ? JobState::Completed : JobState::Failed;
    }
    xSemaphoreGive(_dispatchMutex);
  }
}

void RouterProvisioningWorker::onJobProgress(uint32_t jobId, const char *stageId,
                                             const char *label) {
  if (_dispatchMutex && xSemaphoreTake(_dispatchMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    if (_lastJob.jobId == jobId) {
      _lastJob.stageId    = stageId ? stageId : "";
      _lastJob.stageLabel = label ? label : "";
    }
    xSemaphoreGive(_dispatchMutex);
  }
  emitJobEvent(jobId, _activeType, "running", stageId ? stageId : "",
              label ? label : "");
}

namespace {
struct ProgressCtx {
  RouterProvisioningWorker *worker;
  uint32_t jobId;
};
}  // namespace

void RouterProvisioningWorker::progressTrampoline(void *ctx, const char *stageId,
                                                   const char *label) {
  auto *p = static_cast<ProgressCtx *>(ctx);
  if (p && p->worker) p->worker->onJobProgress(p->jobId, stageId, label);
}

void RouterProvisioningWorker::emitJobEvent(uint32_t jobId, OpType type,
                                            const char *state, const char *stageId,
                                            const char *stageLabel) const {
  if (!_events) return;
  DynamicJsonDocument doc(224);
  doc["jobId"] = jobId;
  doc["type"]  = opTypeLabel(type);
  doc["state"] = state;
  if (stageId && stageId[0]) doc["stage"] = stageId;
  if (stageLabel && stageLabel[0]) doc["label"] = stageLabel;
  String json;
  serializeJson(doc, json);
  // Setup wizard historically listens for "setup.job"; Admin Dashboard also
  // receives the same payload under "router.job" for plane-appropriate naming.
  _events->emit("setup.job", json);
  _events->emit("router.job", json);
}

RouterProvisioningWorker::Result RouterProvisioningWorker::runApply(
    const char *requestJson) {
  if (!requestJson || requestJson[0] == '\0') {
    Result out;
    out.httpStatus = 400;
    out.body       = buildErrorBody("Request body required", "INVALID_JSON");
    return out;
  }
  WorkSlot prepared;
  prepared.type = OpType::ApplyConfiguration;
  prepared.requestJson = requestJson;
  return dispatch(prepared);
}

RouterProvisioningWorker::Result RouterProvisioningWorker::runTcpDiagnostic(
    const String &host, uint16_t port, uint8_t iterations) {
#if !RENZFI_ROUTER_TCP_DIAGNOSTIC
  (void)host;
  (void)port;
  (void)iterations;
  Result out;
  out.httpStatus = 404;
  out.body = buildErrorBody("TCP diagnostic disabled in production firmware",
                            "ROUTER_DIAG_DISABLED");
  return out;
#else
  WorkSlot prepared;
  prepared.type = OpType::TcpDiagnostic;
  prepared.diagHost = host;
  prepared.diagPort = port ? port : 8728;
  prepared.diagIterations = iterations;
  return dispatch(prepared);
#endif
}

RouterProvisioningWorker::Result RouterProvisioningWorker::runApiProtocolDiagnostic(
    const SetupRouterConnectionManager::RouterInput &input) {
#if !RENZFI_ROUTER_API_PROTOCOL_DIAGNOSTIC
  (void)input;
  Result out;
  out.httpStatus = 404;
  out.body = buildErrorBody("API protocol diagnostic disabled in production firmware",
                            "ROUTER_DIAG_DISABLED");
  return out;
#else
  WorkSlot prepared;
  prepared.type = OpType::ApiProtocolDiagnostic;
  prepared.routerInput = input;
  return dispatch(prepared);
#endif
}

RouterProvisioningWorker::Result RouterProvisioningWorker::runListWifiNetworks() {
  WorkSlot prepared;
  prepared.type = OpType::ListWifiNetworks;
  return dispatch(prepared);
}

RouterProvisioningWorker::EnqueueOutcome
RouterProvisioningWorker::enqueueAdminTest(const String &overrideSettingsJson) {
  WorkSlot prepared;
  prepared.type = OpType::AdminTestConnection;
  prepared.requestJson = overrideSettingsJson;
  return enqueueAdminPrepared(prepared, "admin-test");
}

RouterProvisioningWorker::EnqueueOutcome
RouterProvisioningWorker::enqueueAdminSaveSettings(const String &settingsJson) {
  WorkSlot prepared;
  prepared.type = OpType::AdminSaveSettings;
  prepared.requestJson = settingsJson;
  return enqueueAdminPrepared(prepared, "admin-save-settings");
}

RouterProvisioningWorker::EnqueueOutcome
RouterProvisioningWorker::enqueueAdminSaveWireless(const String &wirelessJson) {
  WorkSlot prepared;
  prepared.type = OpType::AdminSaveWireless;
  prepared.requestJson = wirelessJson;
  return enqueueAdminPrepared(prepared, "admin-save-wireless");
}

RouterProvisioningWorker::EnqueueOutcome
RouterProvisioningWorker::enqueueAdminSyncCache(const String &successMessage) {
  WorkSlot prepared;
  prepared.type = OpType::AdminSyncCache;
  prepared.adminMessage = successMessage;
  return enqueueAdminPrepared(prepared, "admin-sync-cache");
}

RouterProvisioningWorker::EnqueueOutcome
RouterProvisioningWorker::enqueueAdminRefreshCache(const String &successMessage) {
  WorkSlot prepared;
  prepared.type = OpType::AdminRefreshCache;
  prepared.adminMessage = successMessage;
  return enqueueAdminPrepared(prepared, "admin-refresh-cache");
}

RouterProvisioningWorker::EnqueueOutcome
RouterProvisioningWorker::enqueueAdminUserProfileOp(const String &requestJson) {
  WorkSlot prepared;
  prepared.type = OpType::AdminUserProfileOp;
  prepared.requestJson = requestJson;
  return enqueueAdminPrepared(prepared, "admin-user-profile");
}

RouterProvisioningWorker::EnqueueOutcome
RouterProvisioningWorker::enqueueAccessPointDetect(const String &requestJson) {
  WorkSlot prepared;
  prepared.type = OpType::AccessPointDetect;
  prepared.requestJson = requestJson;
  return enqueueAdminPrepared(prepared, "access-point-detect");
}

void RouterProvisioningWorker::runOp(WorkSlot &slot) {
  struct ScratchGuard {
    explicit ScratchGuard(RouterOsClient::CommandResult *scratch) {
      RouterCommandScratchContext::bind(scratch);
    }
    ~ScratchGuard() { RouterCommandScratchContext::unbind(); }
  } scratchGuard(&_cmdScratch);

  // Priority assignment (MikroTik CPU protection + health FSM):
  // - Critical: activate / deauth / pause only — never Verify.
  // - Normal: VerifyActive, health probe, Setup-essential, Admin.
  // - Low: optional/background inventory only (none currently assigned).
  RouterApiTransportGate::RouterJobPriority priority =
      RouterApiTransportGate::RouterJobPriority::Normal;
  if (slot.type == OpType::ActivateHotspotUser ||
      slot.type == OpType::DeauthorizeHotspotUser ||
      slot.type == OpType::PauseHotspotUser) {
    priority = RouterApiTransportGate::RouterJobPriority::Critical;
  }
  RouterPriorityGuard priorityGuard(priority);

  const uint32_t deadline =
      millis() + RenzFiConfig::ROUTER_WORKER_JOB_TIMEOUT_MS;
  const uint32_t opToken = millis() ? millis() : 1;
  RouterApiTransportGate::beginJob(opToken, deadline);
  RouterApiTransportGate::setQueueDepth(0);
  RouterApiTransportGate::logQueueDepth();

  RouterWorkerDiagnostics::logStage("job-start");
  Serial.printf("[router-worker] started type=%s\n", opTypeLabel(slot.type));

  slot.result = Result{};

  if (slot.type == OpType::TestConnection) {
    const auto connResult = _routerConnection->testConnection(slot.routerInput);
    if (!connResult.success) {
      slot.result.httpStatus = connResult.httpStatus;
      slot.result.body = buildErrorBody(connResult.errorMessage, connResult.errorCode,
                                        connResult.stage);
      slot.result.ok   = false;
    } else {
      HeapJsonDocument responseDoc(RenzFiConfig::JSON_DOC_MEDIUM);
      DynamicJsonDocument &envelope = responseDoc.doc();
      envelope["success"]           = true;
      envelope["message"]           = connResult.errorMessage;
      JsonObject data               = envelope.createNestedObject("data");
      data["validationCode"]        = connResult.validationCode;
      data["routerIdentity"]        = connResult.routerIdentity;
      data["routerBoard"]           = connResult.routerBoard;
      data["routerOs"]              = connResult.routerOs;
      serializeJson(envelope, slot.result.body);
      slot.result.httpStatus = 200;
      slot.result.ok         = true;
    }
  } else if (slot.type == OpType::SaveConnection) {
    const auto connResult = _routerConnection->saveConnection(slot.routerInput);
    if (!connResult.success) {
      slot.result.httpStatus = connResult.httpStatus;
      slot.result.body = buildErrorBody(connResult.errorMessage, connResult.errorCode,
                                        connResult.stage);
      slot.result.ok   = false;
    } else {
      HeapJsonDocument responseDoc(RenzFiConfig::JSON_DOC_MEDIUM);
      DynamicJsonDocument &envelope = responseDoc.doc();
      envelope["success"]           = true;
      envelope["message"]           = connResult.errorMessage;
      JsonObject data               = envelope.createNestedObject("data");
      data["validationCode"]        = connResult.validationCode;
      data["routerIdentity"]        = connResult.routerIdentity;
      data["nextStep"]              = "wifi";
      if (_finishEngine) {
        String syncError;
        // Setup Save stays HTTP 200 on sync failure (Finish re-runs the sync
        // and hard-fails), but the outcome is reported so the wizard/support
        // can see that production activation would still be unconfigured.
        const bool synced =
            _finishEngine->syncProductionRouterCredentials(syncError);
        data["credentialSyncOk"] = synced;
        if (!synced) {
          data["credentialSyncError"] = syncError;
          Serial.printf("[router-worker] production credential sync failed: %s\n",
                        syncError.c_str());
        }
      }
      if (_setupProvisioning && _eth) {
        fillWorkerSetupStatus(_setupProvisioning, _eth, _routerProvisioning, data);
      }
      serializeJson(envelope, slot.result.body);
      slot.result.httpStatus = 200;
      slot.result.ok         = true;
      // Router credentials just changed — any previously cached compatibility
      // scan is now stale and must not be reused (Rule #1: reuse cached scan
      // result unless credentials change).
      ExistingNetworkScanCache::clear();
    }
  } else if (slot.type == OpType::TcpDiagnostic) {
#if RENZFI_ROUTER_TCP_DIAGNOSTIC
    RouterOsClient *client = new (std::nothrow) RouterOsClient();
    if (!client) {
      slot.result.httpStatus = 503;
      slot.result.body =
          buildErrorBody("Router diagnostic unavailable", "ROUTER_PLAN_UNAVAILABLE");
    } else {
      client->setTimeouts(RenzFiConfig::SETUP_ROUTER_CONNECT_TIMEOUT_MS,
                          RenzFiConfig::SETUP_ROUTER_IO_TIMEOUT_MS);
      client->setCredentials(slot.diagHost, "", "", slot.diagPort);

      uint8_t succeeded = 0;
      uint8_t failed    = 0;
      String  lastError;
      for (uint8_t i = 0; i < slot.diagIterations; ++i) {
        if (RouterApiTransportGate::jobExpired()) break;
        if (client->connect()) {
          ++succeeded;
          client->disconnect("success");
        } else {
          ++failed;
          lastError = client->lastError();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
      }
      delete client;

      HeapJsonDocument responseDoc(RenzFiConfig::JSON_DOC_MEDIUM);
      DynamicJsonDocument &envelope = responseDoc.doc();
      envelope["success"]           = true;
      envelope["message"]           = "TCP connect/disconnect diagnostic complete";
      JsonObject data               = envelope.createNestedObject("data");
      data["host"]                  = slot.diagHost;
      data["port"]                  = slot.diagPort;
      data["attempts"]              = slot.diagIterations;
      data["succeeded"]             = succeeded;
      data["failed"]                = failed;
      if (failed > 0) data["lastError"] = lastError;
      serializeJson(envelope, slot.result.body);
      slot.result.httpStatus = 200;
      slot.result.ok         = true;
    }
#endif
  } else if (slot.type == OpType::ApiProtocolDiagnostic) {
#if RENZFI_ROUTER_API_PROTOCOL_DIAGNOSTIC
    SetupRouterConnectionManager::RouterInput resolved = slot.routerInput;
    SetupRouterConnectionManager::OperationResult prep;
    if (!_routerConnection ||
        !_routerConnection->resolveCredentialsForApi(resolved, prep)) {
      slot.result.httpStatus = prep.httpStatus ? prep.httpStatus : 400;
      slot.result.body =
          buildErrorBody(prep.errorMessage, prep.errorCode, prep.stage);
    } else {
      RouterOsClient *client = new (std::nothrow) RouterOsClient();
      if (!client) {
        slot.result.httpStatus = 503;
        slot.result.body =
            buildErrorBody("Router diagnostic unavailable", "ROUTER_PLAN_UNAVAILABLE");
      } else {
        client->setTimeouts(RenzFiConfig::SETUP_ROUTER_CONNECT_TIMEOUT_MS,
                              RenzFiConfig::SETUP_ROUTER_IO_TIMEOUT_MS);
        client->setCredentials(resolved.host, resolved.username, resolved.password,
                               resolved.apiPort);

        RouterOsClient::ProtocolTranscript transcript;
        const bool ok = client->runProtocolDiagnostic(transcript);
        delete client;

        HeapJsonDocument responseDoc(RenzFiConfig::JSON_DOC_MEDIUM);
        DynamicJsonDocument &envelope = responseDoc.doc();
        envelope["success"]           = ok;
        envelope["message"]           = ok ? "RouterOS API protocol diagnostic complete"
                                           : "RouterOS API protocol diagnostic failed";
        JsonObject data               = envelope.createNestedObject("data");
        data["loginOk"]               = transcript.loginOk;
        data["identityOk"]            = transcript.identityOk;
        data["loginSentenceTypes"]    = transcript.loginSentenceTypes;
        data["identitySentenceTypes"] = transcript.identitySentenceTypes;
        data["identityAttributes"]    = transcript.identityAttributes;
        if (!transcript.stage.isEmpty()) data["stage"] = transcript.stage;
        if (!transcript.errorCode.isEmpty()) data["code"] = transcript.errorCode;
        if (!transcript.errorMessage.isEmpty()) {
          data["errorMessage"] = transcript.errorMessage;
        }
        serializeJson(envelope, slot.result.body);
        slot.result.httpStatus = ok ? 200 : 503;
        slot.result.ok         = ok;
      }
    }
#endif
  } else if (slot.type == OpType::ExistingNetworkScan) {
    std::unique_ptr<RouterOsClient> client(new (std::nothrow) RouterOsClient());
    std::unique_ptr<ExistingNetworkScanner> scanner(
        client ? new (std::nothrow) ExistingNetworkScanner(*client) : nullptr);
    if (!client || !scanner) {
      slot.result.httpStatus = 503;
      slot.result.body =
          buildErrorBody("Router scan temporarily unavailable", "ROUTER_PLAN_UNAVAILABLE");
    } else {
      HeapJsonDocument responseDoc(RenzFiConfig::JSON_DOC_LARGE);
      DynamicJsonDocument &envelope = responseDoc.doc();
      JsonObject data               = envelope.createNestedObject("data");
      ProgressCtx progressCtx{this, slot.jobId};
      const auto result = scanner->run(_eth, _routerConnection, _installation, data,
                                       slot.jobId != 0 ? &progressTrampoline : nullptr,
                                       &progressCtx);
      if (!result.success) {
        HeapJsonDocument failureDoc(RenzFiConfig::JSON_DOC_SMALL);
        DynamicJsonDocument &failureEnvelope = failureDoc.doc();
        failureEnvelope["success"]        = false;
        failureEnvelope["error"]          = result.errorMessage;
        failureEnvelope["code"]           = result.errorCode;
        failureEnvelope["confirmAllowed"] = false;
        failureEnvelope["status"] =
            ExistingNetworkScan::failureStatusFromErrorCode(result.errorCode);
        if (!result.stage.isEmpty()) failureEnvelope["stage"] = result.stage;
        serializeJson(failureEnvelope, slot.result.body);
        slot.result.httpStatus = result.httpStatus;
        slot.result.ok         = false;
      } else {
        data["scanId"] = String("scan-") + String(millis());
        envelope["success"] = true;
        envelope["message"] = result.errorMessage;
        serializeJson(envelope, slot.result.body);
        slot.result.httpStatus = 200;
        slot.result.ok         = true;
        // Rule #1: this is the ONE real RouterOS inspection for this step.
        // Cache it so every subsequent view of the Compatible Network panel
        // (Back/Next, reload, resume) reuses this result instead of
        // re-scanning MikroTik, until the user presses Rescan or the router
        // connection is re-saved (ExistingNetworkScanCache::clear() above).
        ExistingNetworkScanCache::store(slot.result.httpStatus, slot.result.body);
      }
    }
  } else if (slot.type == OpType::ListWifiNetworks) {
    RouterApiTransportGate::clearConnectThrottleAfterSuccess();
    std::unique_ptr<RouterOsClient> client;
    String connectError, connectCode;
    RouterWorkerDiagnostics::checkStackMargin("list-wifi-before-connect");
    if (!openPersistedRouterClient(_eth, _routerConnection, client, connectError,
                                   connectCode)) {
      slot.result.httpStatus = 502;
      slot.result.body =
          buildErrorBody(connectError, connectCode.isEmpty() ? "ROUTER_CONNECT_FAILED"
                                                             : connectCode);
      Serial.printf("[router-worker] list-wifi connect failed code=%s\n",
                    connectCode.c_str());
    } else {
      client->setTimeouts(RenzFiConfig::ROUTER_WIFI_DISCOVERY_CONNECT_MS,
                          RenzFiConfig::ROUTER_WIFI_DISCOVERY_CMD_MS);
      HeapJsonDocument responseDoc(RenzFiConfig::JSON_DOC_MEDIUM);
      DynamicJsonDocument &envelope = responseDoc.doc();
      JsonArray data                = envelope.createNestedArray("data");
      RouterWireless::ListNetworksResult listResult;
      if (!RouterWireless::listNetworks(*client, data, listResult)) {
        envelope["success"] = false;
        envelope["error"]   = listResult.error;
        envelope["code"]    = listResult.code.isEmpty() ? "WIFI_SCAN_FAILED"
                                                        : listResult.code;
        serializeJson(envelope, slot.result.body);
        slot.result.httpStatus = 502;
        slot.result.ok         = false;
        Serial.printf("[router-worker] list-wifi failed code=%s err=%s\n",
                      listResult.code.c_str(), listResult.error.c_str());
      } else {
        envelope["success"] = true;
        envelope["code"]    = listResult.code;
        envelope["message"] = listResult.message;
        envelope["driver"]  = listResult.driver;
        JsonObject summary  = envelope.createNestedObject("summary");
        summary["interfaceCount"]  = listResult.interfaceCount;
        summary["configuredCount"] = listResult.configuredCount;
        summary["disabledCount"]   = listResult.disabledCount;
        serializeJson(envelope, slot.result.body);
        slot.result.httpStatus = 200;
        slot.result.ok         = true;
        Serial.printf("[router-worker] list-wifi complete code=%s interfaces=%u\n",
                      listResult.code.c_str(),
                      static_cast<unsigned>(listResult.interfaceCount));
      }
      client->disconnect("success");
    }
    RouterWorkerDiagnostics::checkStackMargin("list-wifi-after-complete");
    // Always cache the outcome (success or structured failure) regardless
    // of whether this job was dispatched synchronously (an HTTP request is
    // waiting on it) or as a fire-and-forget background refresh (nobody is
    // waiting) — this is the single point that makes repeated Step 3
    // opens/reloads serve from cache instead of re-touching MikroTik. See
    // WifiDiscoveryCache / Config::WIFI_DISCOVERY_CACHE_TTL_MS.
    WifiDiscoveryCache::store(slot.result.httpStatus, slot.result.body);
  } else if (slot.type == OpType::ConfigureExistingNetwork) {
    HeapJsonDocument bodyDoc(RenzFiConfig::JSON_DOC_LARGE);
    DynamicJsonDocument &body = bodyDoc.doc();
    if (deserializeJson(body, slot.requestJson)) {
      slot.result.httpStatus = 400;
      slot.result.body       = buildErrorBody("Invalid JSON body", "INVALID_JSON");
    } else {
      std::unique_ptr<RouterOsClient> client;
      String connectError, connectCode;
      const bool haveClient =
          openPersistedRouterClient(_eth, _routerConnection, client, connectError,
                                    connectCode);
      if (!haveClient) {
        slot.result.httpStatus = 502;
        slot.result.body       = buildErrorBody(connectError, connectCode);
      } else {
        HeapJsonDocument responseDoc(RenzFiConfig::JSON_DOC_LARGE);
        DynamicJsonDocument &envelope = responseDoc.doc();
        JsonObject data               = envelope.createNestedObject("data");
        const auto result = _routerProvisioning->configureExistingNetwork(
            body.as<JsonObjectConst>(), data, client.get(), _finishEngine);
        client->disconnect("success");
        if (!result.success) {
          envelope["success"] = false;
          envelope["error"]   = result.errorMessage;
          envelope["code"]    = result.errorCode;
          if (!result.stage.isEmpty()) envelope["stage"] = result.stage;
          serializeJson(envelope, slot.result.body);
          slot.result.httpStatus = result.httpStatus;
          slot.result.ok         = false;
        } else {
          if (_setupProvisioning && _eth) {
            fillWorkerSetupStatus(_setupProvisioning, _eth, _routerProvisioning,
                                  data);
          }
          _routerProvisioning->fillNetworkModeStatus(data);
          envelope["success"] = true;
          envelope["message"] = result.errorMessage;
          serializeJson(envelope, slot.result.body);
          slot.result.httpStatus = 200;
          slot.result.ok         = true;
        }
      }
    }
  } else if (slot.type == OpType::FinishSetupProvisioning) {
    if (!_finishEngine) {
      slot.result.httpStatus = 503;
      slot.result.body =
          buildErrorBody("Router provisioning engine unavailable", "INTERNAL_ERROR");
    } else {
      auto portalMode =
          RouterProvisioningEngine::PortalDeploymentMode::ManualExternal;
      if (!slot.requestJson.isEmpty()) {
        HeapJsonDocument bodyDoc(RenzFiConfig::JSON_DOC_SMALL);
        DynamicJsonDocument &body = bodyDoc.doc();
        if (!deserializeJson(body, slot.requestJson)) {
          const char *modeRaw = body["portalDeploymentMode"] | "";
          if (!modeRaw[0] && (body["skipPortalVerify"] | false)) {
            modeRaw = "skipped";
          }
          portalMode =
              RouterProvisioningEngine::parsePortalDeploymentModeLabel(modeRaw);
        }
      }
      _finishEngine->setPortalDeploymentMode(portalMode);
      ProgressCtx progressCtx{this, slot.jobId};
      const auto result = _finishEngine->runFinishPipeline(
          slot.jobId != 0 ? &progressTrampoline : nullptr, &progressCtx);
      const bool lifecycleReady =
          _installation && _installation->isReady();
      bool finishOk = false;
      {
        FinishTrace::StageScope gate("worker finish gate");
        finishOk =
            result.success && result.errorCode.isEmpty() && lifecycleReady;
        if (!finishOk) gate.fail();
      }
      HeapJsonDocument responseDoc(RenzFiConfig::JSON_DOC_MEDIUM);
      DynamicJsonDocument &envelope = responseDoc.doc();
      JsonObject data               = envelope.createNestedObject("data");
      data["stage"]                 = result.stage;
      data["rebootScheduled"]       = finishOk && result.rebootScheduled;
      if (!result.portalDeploymentMode.isEmpty()) {
        data["portalDeploymentMode"] = result.portalDeploymentMode;
      }
      if (!result.portalStatus.isEmpty()) {
        data["portalStatus"] = result.portalStatus;
      }
      data["portalBlocking"] = result.portalBlocking;
      if (_setupProvisioning && _eth) {
        fillWorkerSetupStatus(_setupProvisioning, _eth, _routerProvisioning, data);
      }
      if (!finishOk) {
        envelope["success"] = false;
        envelope["error"]   = result.errorMessage.isEmpty()
                                ? "Finish provisioning did not commit installation state"
                                : result.errorMessage;
        envelope["code"]    = result.errorCode.isEmpty() ? "FINISH_INCOMPLETE"
                                                         : result.errorCode;
        if (!result.stage.isEmpty()) envelope["stage"] = result.stage;
        if (!result.reason.isEmpty()) {
          envelope["reason"] = result.reason;
          data["reason"]     = result.reason;
        }
        serializeJson(envelope, slot.result.body);
        slot.result.httpStatus =
            result.httpStatus >= 400 ? result.httpStatus : 500;
        slot.result.ok         = false;
      } else {
        envelope["success"] = true;
        envelope["message"] = result.errorMessage;
        serializeJson(envelope, slot.result.body);
        slot.result.httpStatus = 200;
        slot.result.ok         = true;
      }
    }
  } else if (slot.type == OpType::ApplyConfiguration) {
    HeapJsonDocument bodyDoc(RenzFiConfig::JSON_DOC_LARGE);
    DynamicJsonDocument &body = bodyDoc.doc();
    if (deserializeJson(body, slot.requestJson)) {
      slot.result.httpStatus = 400;
      slot.result.body       = buildErrorBody("Invalid JSON body", "INVALID_JSON");
    } else {
      HeapJsonDocument responseDoc(RenzFiConfig::JSON_DOC_LARGE);
      DynamicJsonDocument &envelope = responseDoc.doc();
      JsonObject data               = envelope.createNestedObject("data");
      const auto result = _routerProvisioning->applyConfiguration(
          body.as<JsonObjectConst>(), body.as<JsonObjectConst>(), data, data);
      if (!result.success) {
        envelope["success"] = false;
        envelope["error"]   = result.errorMessage;
        envelope["code"]    = result.errorCode;
        if (!result.stage.isEmpty()) envelope["stage"] = result.stage;
        serializeJson(envelope, slot.result.body);
        slot.result.httpStatus = result.httpStatus;
        slot.result.ok         = false;
      } else {
        envelope["success"] = true;
        envelope["message"] = result.errorMessage;
        if (_setupProvisioning && _eth) {
          fillWorkerSetupStatus(_setupProvisioning, _eth, _routerProvisioning,
                                data);
        }
        serializeJson(envelope, slot.result.body);
        slot.result.httpStatus = 200;
        slot.result.ok         = true;
      }
    }
  } else if (slot.type == OpType::ActivateHotspotUser) {
    // Fire-and-forget (jobId==0). Outcome is published for PortalSessionManager
    // so Connected is only claimed after RouterOS authorization succeeds.
    Serial.printf("[activate] stage=worker_job mac=%s remaining=%u\n",
                  slot.hotspotUser.mac.c_str(),
                  (unsigned)slot.hotspotUser.timeoutSeconds);
    activationLatencyTrace().markT4();
    String credentialError = ensureRouterCredentialsForHotspotJob();
    const bool ok =
        credentialError.isEmpty() && _routerPlatform &&
        _routerPlatform->provisionHotspotUser(slot.hotspotUser);
    const String reason =
        ok ? String()
           : hotspotFailureReason(credentialError,
                                  "Hotspot user provisioning failed");
    Serial.printf("[activate] stage=worker_done ok=%s%s%s\n", ok ? "yes" : "no",
                  ok ? "" : " reason=", ok ? "" : reason.c_str());
    slot.result.ok         = ok;
    slot.result.httpStatus = ok ? 200 : 502;
    slot.result.body       = ok ? String("{\"success\":true}")
                                : buildErrorBody(reason.c_str(),
                                                 "HOTSPOT_ACTIVATE_FAILED");
    ActivateAuthTrace authTrace;
    if (ok && _routerPlatform) {
      (void)_routerPlatform->lastActivateAuthTrace(authTrace);
      if (authTrace.grantedSeconds == 0) {
        authTrace.grantedSeconds = slot.hotspotUser.timeoutSeconds;
      }
      if (authTrace.authorizedAtMs == 0) {
        authTrace.authorizedAtMs = millis();
      }
    }
    publishHotspotOutcome(HotspotOutcomeKind::Activate, slot.hotspotUser.mac, ok,
                          reason, slot.hotspotUser.sessionGeneration,
                          ok ? &authTrace : nullptr);
    activationLatencyTrace().markT9();
    Serial.printf("[router-worker] activate-hotspot-user mac=%s ok=%s%s%s\n",
                  slot.hotspotUser.mac.c_str(), ok ? "yes" : "no",
                  ok ? "" : " reason=", ok ? "" : reason.c_str());
  } else if (slot.type == OpType::DeauthorizeHotspotUser) {
    String credentialError = ensureRouterCredentialsForHotspotJob();
    const bool ok =
        credentialError.isEmpty() && _routerPlatform &&
        _routerPlatform->disconnectHotspotUser(slot.hotspotDeauthMac);
    const String reason =
        ok ? String()
           : hotspotFailureReason(credentialError,
                                  "Hotspot user deauthorization failed");
    slot.result.ok         = ok;
    slot.result.httpStatus = ok ? 200 : 502;
    slot.result.body       = ok ? String("{\"success\":true}")
                                : buildErrorBody(reason.c_str(),
                                                 "HOTSPOT_DEAUTH_FAILED");
    publishHotspotOutcome(HotspotOutcomeKind::Deauthorize, slot.hotspotDeauthMac,
                          ok, reason, slot.hotspotGeneration);
    Serial.printf("[router-worker] deauthorize-hotspot-user mac=%s ok=%s%s%s\n",
                  slot.hotspotDeauthMac.c_str(), ok ? "yes" : "no",
                  ok ? "" : " reason=", ok ? "" : reason.c_str());
  } else if (slot.type == OpType::PauseHotspotUser) {
    String credentialError = ensureRouterCredentialsForHotspotJob();
    const bool ok =
        credentialError.isEmpty() && _routerPlatform &&
        _routerPlatform->pauseHotspotUser(slot.hotspotDeauthMac);
    const String reason =
        ok ? String()
           : hotspotFailureReason(credentialError, "Hotspot pause failed");
    slot.result.ok         = ok;
    slot.result.httpStatus = ok ? 200 : 502;
    slot.result.body       = ok ? String("{\"success\":true}")
                                : buildErrorBody(reason.c_str(),
                                                 "HOTSPOT_PAUSE_FAILED");
    publishHotspotOutcome(HotspotOutcomeKind::Pause, slot.hotspotDeauthMac, ok,
                          reason, slot.hotspotGeneration);
    Serial.printf("[router-worker] pause-hotspot-user mac=%s ok=%s%s%s\n",
                  slot.hotspotDeauthMac.c_str(), ok ? "yes" : "no",
                  ok ? "" : " reason=", ok ? "" : reason.c_str());
  } else if (slot.type == OpType::VerifyHotspotActive) {
    String credentialError = ensureRouterCredentialsForHotspotJob();
    bool present = false;
    const bool queryOk =
        credentialError.isEmpty() && _routerPlatform &&
        _routerPlatform->queryHotspotActivePresent(slot.hotspotDeauthMac,
                                                   present);
    // ok = query succeeded. reason distinguishes authorized vs not_active.
    // Transport/credential failure must NOT clear local Connected.
    const String reason =
        !credentialError.isEmpty()
            ? credentialError
            : (!queryOk ? String("Hotspot active verify failed")
                        : (present ? String("authorized")
                                   : String("not_active")));
    slot.result.ok         = queryOk;
    slot.result.httpStatus = queryOk ? 200 : 502;
    slot.result.body       = queryOk ? String("{\"success\":true}")
                                     : buildErrorBody(reason.c_str(),
                                                      "HOTSPOT_VERIFY_FAILED");
    publishHotspotOutcome(HotspotOutcomeKind::VerifyActive,
                          slot.hotspotDeauthMac, queryOk, reason,
                          slot.hotspotGeneration);
    Serial.printf(
        "[router-worker] verify-hotspot-active mac=%s query_ok=%s present=%s\n",
        slot.hotspotDeauthMac.c_str(), queryOk ? "yes" : "no",
        present ? "yes" : "no");
  } else if (slot.type == OpType::HealthProbe) {
    String credErr;
    const bool credsOk =
        !_finishEngine || _finishEngine->ensureProductionRouterCredentials(credErr);
    if (!credsOk) {
      const String msg =
          credErr.isEmpty() ? String("RouterOS API username is not configured")
                            : credErr;
      slot.result.ok           = false;
      slot.result.httpStatus   = 502;
      slot.result.body =
          buildErrorBody(msg, "ROUTER_CREDENTIALS_MISSING");
      slot.result.healthReason = "credentials_missing";
      Serial.println("[router-worker] health-probe ok=no reason=credentials_missing");
    } else {
      const bool ok = _routerPlatform && _routerPlatform->probeApiReady();
      slot.result.ok         = ok;
      slot.result.httpStatus = ok ? 200 : 502;
      slot.result.body =
          ok ? String("{\"success\":true}")
             : buildErrorBody("RouterOS health probe failed",
                              "ROUTER_HEALTH_PROBE_FAILED");
      if (!ok) slot.result.healthReason = "router_unreachable";
      Serial.printf("[router-worker] health-probe ok=%s\n", ok ? "yes" : "no");
    }
  } else if (slot.type == OpType::AdminTestConnection) {
    // Preserves the admin dashboard's existing contract: always 200/success,
    // with the live-or-failed diagnostic embedded in data (see
    // ApiServer.cpp's former "/api/router/test" handler).
    // Use serialized(data) so the envelope never double-copies a MEDIUM
    // result into another MEDIUM doc (silent truncation → false UI failures).
    HeapJsonDocument bodyDoc(RenzFiConfig::JSON_DOC_SMALL);
    DynamicJsonDocument &body = bodyDoc.doc();
    if (!slot.requestJson.isEmpty()) deserializeJson(body, slot.requestJson);
    HeapJsonDocument resultDoc(RenzFiConfig::JSON_DOC_MEDIUM);
    DynamicJsonDocument &result = resultDoc.doc();
    const bool testOk = _routerPlatform &&
                        _routerPlatform->test(body.as<JsonObjectConst>(), result);
    const char *message = testOk ? "Router test passed"
                                 : (result["error"] | "Router test failed");
    String resultJson;
    serializeJson(result, resultJson);
    DynamicJsonDocument envelope(384);
    envelope["success"] = true;
    envelope["data"]    = serialized(resultJson);
    envelope["message"] = message;
    serializeJson(envelope, slot.result.body);
    slot.result.httpStatus = 200;
    slot.result.ok         = true;
  } else if (slot.type == OpType::AdminSaveSettings) {
    HeapJsonDocument bodyDoc(RenzFiConfig::JSON_DOC_SMALL);
    DynamicJsonDocument &body = bodyDoc.doc();
    if (!slot.requestJson.isEmpty()) deserializeJson(body, slot.requestJson);
    const bool saveOk = _routerPlatform && _routerPlatform->save(body.as<JsonObjectConst>());
    if (saveOk) {
      DynamicJsonDocument envelope(256);
      envelope["success"]    = true;
      envelope["data"]["ok"] = true;
      envelope["message"]    = "OK";
      serializeJson(envelope, slot.result.body);
      slot.result.httpStatus = 200;
      slot.result.ok         = true;
    } else {
      slot.result.body = buildErrorBody("Unable to save router settings", "STORAGE_ERROR");
      slot.result.httpStatus = 500;
      slot.result.ok         = false;
    }
  } else if (slot.type == OpType::AdminSaveWireless) {
    HeapJsonDocument bodyDoc(RenzFiConfig::JSON_DOC_SMALL);
    DynamicJsonDocument &body = bodyDoc.doc();
    if (!slot.requestJson.isEmpty()) deserializeJson(body, slot.requestJson);
    HeapJsonDocument resultDoc(RenzFiConfig::JSON_DOC_SMALL);
    DynamicJsonDocument &result = resultDoc.doc();
    const bool wirelessOk = _routerPlatform &&
                            _routerPlatform->saveWireless(body.as<JsonObjectConst>(), result);
    if (wirelessOk) {
      HeapJsonDocument envelopeDoc(RenzFiConfig::JSON_DOC_MEDIUM);
      DynamicJsonDocument &envelope = envelopeDoc.doc();
      envelope["success"] = true;
      envelope["data"].set(result.as<JsonObject>());
      const bool verified = result["verified"] | false;
      envelope["message"] =
          verified ? "Wireless SSID updated and verified"
                   : "SSID change applied. Wireless clients may need to reconnect.";
      serializeJson(envelope, slot.result.body);
      slot.result.httpStatus = 200;
      slot.result.ok         = true;
    } else {
      const char *err = result["error"] | "Failed to update wireless settings";
      slot.result.body       = buildErrorBody(err, "WIRELESS_SAVE_FAILED");
      slot.result.httpStatus = 400;
      slot.result.ok         = false;
    }
  } else if (slot.type == OpType::AdminSyncCache) {
    HeapJsonDocument resultDoc(RenzFiConfig::JSON_DOC_MEDIUM);
    DynamicJsonDocument &result = resultDoc.doc();
    bool credsOk = true;
    if (_finishEngine) {
      String credErr;
      credsOk = _finishEngine->ensureProductionRouterCredentials(credErr);
      if (!credsOk) {
        const String msg =
            credErr.isEmpty() ? String("RouterOS API username is not configured")
                              : credErr;
        slot.result.body         = buildErrorBody(msg, "ROUTER_CREDENTIALS_MISSING");
        slot.result.httpStatus   = 503;
        slot.result.ok           = false;
        slot.result.healthReason = "credentials_missing";
        Serial.println("[router-sync] ok=no stage=admin-sync-cache reason=credentials_missing");
      }
    }
    const bool syncOk =
        credsOk && _routerPlatform && _routerPlatform->synchronizeRouterCache(false);
    if (syncOk && _routerPlatform->fillRouterCache(result)) {
      String resultJson;
      serializeJson(result, resultJson);
      DynamicJsonDocument envelope(384);
      envelope["success"] = true;
      envelope["data"]    = serialized(resultJson);
      envelope["message"] =
          slot.adminMessage.isEmpty() ? "Router configuration synchronized"
                                      : slot.adminMessage;
      serializeJson(envelope, slot.result.body);
      slot.result.httpStatus = 200;
      slot.result.ok         = true;
    } else if (slot.result.body.isEmpty()) {
      const char *reason =
          !_routerPlatform
              ? "platform-unavailable"
              : (!syncOk ? "sync-or-persist-failed" : "cache-not-readable");
      Serial.printf("[router-sync] ok=no stage=admin-sync-cache reason=%s\n", reason);
      applyCacheJobFailure(slot.result, _routerPlatform, false);
    }
  } else if (slot.type == OpType::AdminRefreshCache) {
    HeapJsonDocument resultDoc(RenzFiConfig::JSON_DOC_MEDIUM);
    DynamicJsonDocument &result = resultDoc.doc();
    bool credsOk = true;
    if (_finishEngine) {
      String credErr;
      credsOk = _finishEngine->ensureProductionRouterCredentials(credErr);
      if (!credsOk) {
        const String msg =
            credErr.isEmpty() ? String("RouterOS API username is not configured")
                              : credErr;
        slot.result.body         = buildErrorBody(msg, "ROUTER_CREDENTIALS_MISSING");
        slot.result.httpStatus   = 503;
        slot.result.ok           = false;
        slot.result.healthReason = "credentials_missing";
        Serial.println(
            "[router-refresh] ok=no stage=admin-refresh-cache reason=credentials_missing");
      }
    }
    const bool refreshOk =
        credsOk && _routerPlatform && _routerPlatform->refreshRouterTelemetry();
    if (refreshOk && _routerPlatform->fillRouterCache(result)) {
      String resultJson;
      serializeJson(result, resultJson);
      DynamicJsonDocument envelope(384);
      envelope["success"] = true;
      envelope["data"]    = serialized(resultJson);
      envelope["message"] =
          slot.adminMessage.isEmpty() ? "Router information refreshed"
                                      : slot.adminMessage;
      serializeJson(envelope, slot.result.body);
      slot.result.httpStatus = 200;
      slot.result.ok         = true;
    } else if (slot.result.body.isEmpty()) {
      const char *reason =
          !_routerPlatform
              ? "platform-unavailable"
              : (!refreshOk ? "telemetry-or-persist-failed" : "cache-not-readable");
      Serial.printf("[router-refresh] ok=no stage=admin-refresh-cache reason=%s\n",
                    reason);
      applyCacheJobFailure(slot.result, _routerPlatform, true);
    }
  } else if (slot.type == OpType::AdminUserProfileOp) {
    HeapJsonDocument bodyDoc(RenzFiConfig::JSON_DOC_SMALL);
    DynamicJsonDocument &body = bodyDoc.doc();
    if (!slot.requestJson.isEmpty()) deserializeJson(body, slot.requestJson);
    HeapJsonDocument resultDoc(RenzFiConfig::JSON_DOC_MEDIUM);
    DynamicJsonDocument &result = resultDoc.doc();
    const bool ok =
        _routerPlatform &&
        _routerPlatform->adminUserProfileOp(body.as<JsonObjectConst>(), result);
    if (ok) {
      String resultJson;
      serializeJson(result, resultJson);
      DynamicJsonDocument envelope(384);
      envelope["success"] = true;
      envelope["data"]    = serialized(resultJson);
      envelope["message"] = "OK";
      serializeJson(envelope, slot.result.body);
      slot.result.httpStatus = 200;
      slot.result.ok         = true;
    } else {
      const char *err = result["error"] | "Profile operation failed";
      slot.result.body       = buildErrorBody(err, "ROUTER_PROFILE_OP_FAILED");
      slot.result.httpStatus = 400;
      slot.result.ok         = false;
    }
  } else if (slot.type == OpType::AccessPointDetect) {
    std::vector<String> registeredIps;
    if (!slot.requestJson.isEmpty()) {
      HeapJsonDocument bodyDoc(RenzFiConfig::JSON_DOC_MEDIUM);
      DynamicJsonDocument &body = bodyDoc.doc();
      if (!deserializeJson(body, slot.requestJson) &&
          body["registeredIps"].is<JsonArrayConst>()) {
        for (JsonVariantConst v : body["registeredIps"].as<JsonArrayConst>()) {
          const String ip = v.as<String>();
          if (!ip.isEmpty()) registeredIps.push_back(ip);
        }
      }
    }

    std::unique_ptr<RouterOsClient> client;
    String connectError, connectCode;
    if (!openPersistedRouterClient(_eth, _routerConnection, client, connectError,
                                   connectCode)) {
      slot.result.httpStatus = 502;
      slot.result.body =
          buildErrorBody(connectError, connectCode.isEmpty() ? "ROUTER_CONNECT_FAILED"
                                                             : connectCode);
      slot.result.ok = false;
    } else {
      RouterOsClient::CommandResult arpResult;
      RouterOsClient::CommandResult bridgeHostResult;
      RouterOsClient::CommandResult leaseResult;
      RouterOsClient::initializeCommandResult(arpResult);
      RouterOsClient::initializeCommandResult(bridgeHostResult);
      RouterOsClient::initializeCommandResult(leaseResult);

      const String arpProps[] = {"=.proplist=address,mac-address,interface,status"};
      const bool arpOk = client->executeCommand("/ip/arp/print", arpProps, 1, arpResult);
      if (!arpOk) {
        const String err = client->lastError();
        slot.result.body = buildErrorBody(
            err.isEmpty() ? String("Unable to read MikroTik ARP table") : err,
            "AP_DETECT_FAILED");
        slot.result.httpStatus = 502;
        slot.result.ok = false;
      } else {
        const String bridgeProps[] = {"=.proplist=mac-address,on-interface,bridge"};
        const bool bridgeOk = client->executeCommand("/interface/bridge/host/print",
                                                     bridgeProps, 1, bridgeHostResult);
        const String leaseProps[] = {"=.proplist=address,mac-address,host-name,status"};
        const bool leaseOk = client->executeCommand("/ip/dhcp-server/lease/print",
                                                    leaseProps, 1, leaseResult);

        HeapJsonDocument outDoc(RenzFiConfig::JSON_DOC_LARGE);
        DynamicJsonDocument &out = outDoc.doc();
        out["success"] = true;
        JsonObject data = out["data"].to<JsonObject>();
        JsonArray devices = data["devices"].to<JsonArray>();
        uint16_t filtered = 0;

        for (uint8_t i = 0; i < arpResult.replyCount; ++i) {
          const String ip = attrFromReply(arpResult, i, "address");
          const String mac = attrFromReply(arpResult, i, "mac-address");
          const String iface = attrFromReply(arpResult, i, "interface");
          const String status = attrFromReply(arpResult, i, "status");
          if (ip.isEmpty() || ip == "0.0.0.0" || mac.isEmpty()) {
            filtered++;
            continue;
          }
          bool duplicate = false;
          for (JsonVariantConst existing : devices) {
            if (String(existing["ip"] | "") == ip) {
              duplicate = true;
              break;
            }
          }
          if (duplicate) {
            filtered++;
            continue;
          }
          bool alreadyRegistered = false;
          for (const String &registered : registeredIps) {
            if (registered == ip) {
              alreadyRegistered = true;
              break;
            }
          }
          if (alreadyRegistered) {
            filtered++;
            continue;
          }

          String bridgePort;
          for (uint8_t j = 0; j < bridgeHostResult.replyCount; ++j) {
            if (attrFromReply(bridgeHostResult, j, "mac-address")
                    .equalsIgnoreCase(mac)) {
              bridgePort = attrFromReply(bridgeHostResult, j, "on-interface");
              break;
            }
          }

          String hostname;
          for (uint8_t j = 0; j < leaseResult.replyCount; ++j) {
            const String leaseIp = attrFromReply(leaseResult, j, "address");
            const String leaseMac = attrFromReply(leaseResult, j, "mac-address");
            if ((leaseIp == ip || leaseMac.equalsIgnoreCase(mac))) {
              hostname = attrFromReply(leaseResult, j, "host-name");
              break;
            }
          }

          JsonObject row = devices.add<JsonObject>();
          row["ip"] = ip;
          row["mac"] = mac;
          row["interface"] = iface;
          row["bridgePort"] = bridgePort;
          row["hostname"] = hostname;
          row["status"] = status;
        }

        data["source"] = "mikrotik_arp";
        data["oneTime"] = true;
        data["arpRows"] = arpResult.replyCount;
        data["returned"] = devices.size();
        data["filteredOut"] = filtered;
        if (!bridgeOk) data["bridgeHostWarning"] = "bridge_host_lookup_failed";
        if (!leaseOk) data["leaseWarning"] = "dhcp_lease_lookup_failed";
        out["message"] = "Detection complete";
        serializeJson(out, slot.result.body);
        slot.result.httpStatus = 200;
        slot.result.ok = true;
      }
      client->disconnect("success");
    }
  }

  if (millis() > deadline && !slot.result.ok && slot.result.body.isEmpty()) {
    slot.result = timeoutResult();
  }

  RouterApiTransportGate::endJob(
      opToken, slot.result.ok,
      slot.result.ok ? nullptr : slot.result.healthReason.c_str());
  RouterWorkerDiagnostics::logStage("cleanup");
  Serial.printf("[router-worker] finished type=%s ok=%s http=%d\n",
                opTypeLabel(slot.type), slot.result.ok ? "yes" : "no",
                slot.result.httpStatus);
}

void RouterProvisioningWorker::taskEntry(void *arg) {
  static_cast<RouterProvisioningWorker *>(arg)->taskLoop();
}

void RouterProvisioningWorker::taskLoop() {
  for (;;) {
    uint8_t wake = 0;
    if (xQueueReceive(_queue, &wake, portMAX_DELAY) != pdTRUE) continue;

    _activeType = _slot.type;
    if (_slot.jobId != 0) {
      setJobRunning(_slot.jobId);
      if (_slot.type == OpType::FinishSetupProvisioning) {
        FinishTrace::jobLifecycle(_slot.jobId, "RUNNING");
      }
      if (_slot.type == OpType::AdminTestConnection ||
          _slot.type == OpType::AdminSaveSettings ||
          _slot.type == OpType::AdminSaveWireless ||
          _slot.type == OpType::AdminSyncCache ||
          _slot.type == OpType::AdminRefreshCache ||
          _slot.type == OpType::AdminUserProfileOp) {
        Serial.printf("[admin-router-job] started id=%u type=%s\n",
                      static_cast<unsigned>(_slot.jobId),
                      opTypeLabel(_slot.type));
      }
      emitJobEvent(_slot.jobId, _slot.type, "running", "", "");
    }
    const uint32_t jobStartMs = millis();
    runOp(_slot);
    if (_slot.jobId != 0) {
      if (_slot.type == OpType::FinishSetupProvisioning) {
        FinishTrace::StageScope publish("publish job completed");
        setJobFinished(_slot.jobId, _slot.result);
        FinishTrace::jobLifecycle(_slot.jobId,
                                  _slot.result.ok ? "SUCCESS" : "FAILED");
      } else {
        setJobFinished(_slot.jobId, _slot.result);
      }
      if (_slot.type == OpType::AdminTestConnection ||
          _slot.type == OpType::AdminSaveSettings ||
          _slot.type == OpType::AdminSaveWireless ||
          _slot.type == OpType::AdminSyncCache ||
          _slot.type == OpType::AdminRefreshCache ||
          _slot.type == OpType::AdminUserProfileOp) {
        Serial.printf(
            "[admin-router-job] %s id=%u type=%s elapsed=%lums\n",
            _slot.result.ok ? "completed" : "failed",
            static_cast<unsigned>(_slot.jobId), opTypeLabel(_slot.type),
            static_cast<unsigned long>(millis() - jobStartMs));
      }
      emitJobEvent(_slot.jobId, _slot.type,
                  _slot.result.ok ? "completed" : "failed", "", "");
    }
    _running = false;
    xSemaphoreGive(_doneSem);
    if (_idleCallback) {
      _idleCallback(_idleCallbackCtx);
    }
  }
}
