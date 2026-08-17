#include "PortalSessionManager.h"

#include "SessionManager.h"

#include "ActivationLatencyTrace.h"
#include "CoinLatencyTrace.h"
#include "CoinManager.h"
#include "Config.h"
#include "DmaMemoryMonitor.h"
#include "JsonHeap.h"
#include "Models.h"
#include "RouterApiTransportGate.h"
#include "RouterProvisioningWorker.h"
#include "SalesTime.h"
#include "VoucherManager.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <limits.h>
#include <time.h>

namespace {
String addSecondsToRecordedAt(const String& recordedAt, uint32_t seconds) {
  return salesAddSecondsToIso(recordedAt, seconds);
}

long secondsUntilRecordedAt(const String& recordedAt) {
  return salesSecondsUntilIso(recordedAt);
}

bool voucherWallRemaining(JsonObject session, long& remainingOut) {
  remainingOut = -1;
  const String serviceExpiresAt = session["serviceExpiresAt"] | "";
  if (!serviceExpiresAt.isEmpty()) {
    remainingOut = secondsUntilRecordedAt(serviceExpiresAt);
    return remainingOut >= 0;
  }
  const time_t wallNow = time(nullptr);
  const time_t expiryEpoch =
      static_cast<time_t>(session["serviceExpiresEpoch"] | 0UL);
  if (wallNow >= 1704067200 && expiryEpoch > 0) {
    remainingOut =
        expiryEpoch > wallNow ? static_cast<long>(expiryEpoch - wallNow) : 0L;
    return true;
  }
  return false;
}

long remainingFromExpiresMs(uint32_t expiresAtMs, uint32_t nowMs) {
  if (expiresAtMs == 0) return -1;
  const int32_t delta = static_cast<int32_t>(expiresAtMs - nowMs);
  if (delta <= 0) return 0;
  return static_cast<long>(static_cast<uint32_t>(delta) / 1000U);
}

void clearSessionClockUnlocked(JsonObject session) {
  session["authorizedAtMs"] = 0U;
  session["grantedSeconds"] = 0U;
  session["expiresAtMs"] = 0U;
}

void freezeSessionClockUnlocked(JsonObject session) {
  const uint32_t expiresAtMs = session["expiresAtMs"] | 0U;
  if (expiresAtMs > 0) {
    session["secondsLeft"] = remainingFromExpiresMs(expiresAtMs, millis());
  }
  session["expiresAtMs"] = 0U;
}

void commitAuthorizedClockUnlocked(JsonObject session, uint32_t authorizedAtMs,
                                   uint32_t grantedSeconds) {
  if (authorizedAtMs == 0) authorizedAtMs = millis();
  if (grantedSeconds == 0) {
    grantedSeconds = static_cast<uint32_t>(session["secondsLeft"] | 0L);
  }
  session["authorizedAtMs"] = authorizedAtMs;
  session["grantedSeconds"] = grantedSeconds;
  const uint32_t expiresAtMs = authorizedAtMs + grantedSeconds * 1000UL;
  session["expiresAtMs"] = expiresAtMs;
  const long rem = remainingFromExpiresMs(expiresAtMs, millis());
  session["secondsLeft"] = rem < 0 ? static_cast<long>(grantedSeconds) : rem;
}

bool alreadyAuthorizedThisGeneration(JsonObjectConst session) {
  const char* state = session["sessionState"] | PortalState::Idle;
  return (session["hadRouterAuth"] | false) &&
         (session["connected"] | false) &&
         strcmp(state, PortalState::Active) == 0 &&
         !(session["routerAuthPending"] | false) &&
         !(session["resumePending"] | false) &&
         (session["secondsLeft"] | 0L) > 0;
}

bool isPaidActivatingState(const char* state) {
  return strcmp(state, PortalState::Activating) == 0 ||
         strcmp(state, PortalState::Active) == 0 ||
         strcmp(state, PortalState::ActivationError) == 0 ||
         strcmp(state, PortalState::Paused) == 0;
}
}  // namespace

uint32_t PortalSessionManager::sessionGenerationOf(JsonObjectConst session) {
  if (session.isNull()) return 0;
  return session["sessionGeneration"] | 0U;
}

uint32_t PortalSessionManager::bumpSessionGenerationUnlocked(JsonObject session) {
  uint32_t g = session["sessionGeneration"] | 0U;
  if (g == 0xFFFFFFFFu) g = 0;
  ++g;
  session["sessionGeneration"] = g;
  return g;
}

void PortalSessionManager::clearSupersededCleanupUnlocked(JsonObject session) {
  session["cleanupRetryPending"] = false;
  session["routerCleanupQueued"] = false;
  session["routerCleanupPending"] = false;
  session["routerCleanupComplete"] = false;
  session["cleanupRetryCount"] = 0;
  session.remove("terminationReason");
  session.remove("terminationActor");
}

static void logActivationStack(const char *label) {
  UBaseType_t stackWords = uxTaskGetStackHighWaterMark(NULL);
  Serial.printf("[STACK] %s free=%u bytes\n", label,
                static_cast<unsigned>(stackWords * sizeof(StackType_t)));
}

// ──────────────────────────────────────────────────────────────────────────────
// State serialization — protects _doc, _activeInsertMac, _dirty, work queue
// ──────────────────────────────────────────────────────────────────────────────

void PortalSessionManager::lockState() const {
  if (_stateMutex) {
    xSemaphoreTake(_stateMutex, portMAX_DELAY);
  }
}

void PortalSessionManager::unlockState() const {
  if (_stateMutex) {
    xSemaphoreGive(_stateMutex);
  }
}

// ──────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ──────────────────────────────────────────────────────────────────────────────

void PortalSessionManager::begin(StorageManager* storage, Logger* logger,
                                  EventBus* events, PromoManager* promos,
                                  RouterPlatform* router,
                                  RouterProvisioningWorker* routerWorker,
                                  VoucherManager* vouchers,
                                  SessionManager* sessions) {
  Serial.println("[portal] PortalSessionManager::begin");
  _storage = storage;
  _logger  = logger;
  _events  = events;
  _promos  = promos;
  _router = router;
  _routerWorker = routerWorker;
  _vouchers = vouchers;
  _sessions = sessions;
  _stateMutex = xSemaphoreCreateMutex();

  loadFromSD();
  if (_routerWorker) {
    _routerWorker->setIdleCallback(routerIdleTrampoline, this);
  }
  recoverSessionsAfterReboot();

  lockState();
  JsonArray loadedSessions = _doc["sessions"].as<JsonArray>();
  const size_t count =
      loadedSessions.isNull() ? 0U : loadedSessions.size();
  unlockState();

  Serial.printf("[portal] Loaded %u session record(s)\n", (unsigned)count);
  if (_logger) _logger->info("portal", "PortalSessionManager ready");
}

void PortalSessionManager::setCoinManager(CoinManager* coin) {
  _coin = coin;
}

uint32_t PortalSessionManager::coinInsertTimeoutSecs() const {
  if (_coin) return _coin->insertTimeoutSeconds();
  return RenzFiConfig::COIN_INSERT_TIMEOUT_SEC;
}

void PortalSessionManager::loop() {
  drainHotspotOutcomes();
  if (_routerIdleNotified) {
    _routerIdleNotified = false;
    retryPendingRouterWork();
  }
  processDeferredWork();

  const uint32_t now = millis();

  RouterApiTransportGate::tickHealth(now);
  // Skip a redundant readiness probe when a paid Activate can itself prove
  // RouterOS is usable. Still probe when Activate is blocked (UNAVAILABLE).
  // Connected-only sessions must NOT keep HealthProbe/login looping after a
  // Verify login timeout — recovery waits for real required work.
  if (_routerWorker && !_routerWorker->isBusy() &&
      RouterApiTransportGate::wantsHealthProbe(now) &&
      needsHealthRecoveryProbe()) {
    const bool activateCanProve =
        hasCustomerActivatePending() &&
        RouterApiTransportGate::allowsHotspotActivate();
    if (!activateCanProve && _routerWorker->tryEnqueueHealthProbe()) {
      Serial.println("[ros-health] recovery probe enqueued");
    }
  } else if (_routerWorker && !_routerWorker->isBusy() &&
             RouterApiTransportGate::wantsHealthProbe(now) &&
             needsRouterOsWork() && !needsHealthRecoveryProbe()) {
    static uint32_t s_lastProbeSuppressLogMs = 0;
    if (s_lastProbeSuppressLogMs == 0 ||
        (now - s_lastProbeSuppressLogMs) >= 30000U) {
      s_lastProbeSuppressLogMs = now;
      Serial.println(
          "[router-health] verify-login-timeout connected=1 probe_suppressed=1");
    }
  }

  if (now - _lastTickMs >= 1000) {
    _lastTickMs = now;
    tickSessions();
    maybeEnqueueActiveVerify();
    if (_routerWorker && !_routerWorker->isBusy()) {
      retryPendingRouterWork();
    }

    static bool s_loggedIdleNoRos = false;
    if (!needsRouterOsWork()) {
      if (!s_loggedIdleNoRos) {
        Serial.println("[router-worker] idle no-router-work");
        s_loggedIdleNoRos = true;
      }
    } else {
      s_loggedIdleNoRos = false;
    }
  }

  bool needsSave = false;
  lockState();
  if (_dirty && (now - _lastSaveMs >= RenzFiConfig::PORTAL_SAVE_INTERVAL_MS)) {
    needsSave = true;
  }
  unlockState();

  if (needsSave) {
    enqueueSaveSessions();
  }
}

void PortalSessionManager::routerIdleTrampoline(void* ctx) {
  auto* self = static_cast<PortalSessionManager*>(ctx);
  if (self) self->_routerIdleNotified = true;
}

void PortalSessionManager::retryPendingRouterWork() {
  // Paid Activate outranks leftover cleanup for the same MAC. A newer
  // generation must never wait behind superseded ExpireSession work.
  String activateMac;
  String pauseMac;
  String cleanupMac;
  uint32_t activateGen = 0;
  uint32_t pauseGen = 0;
  uint32_t cleanupGen = 0;
  bool activateWaitingOnEthernet = false;

  lockState();
  JsonArray sessions = _doc["sessions"].as<JsonArray>();
  for (JsonObject session : sessions) {
    const String mac = session["macAddress"] | "";
    if (mac.isEmpty()) continue;
    const char* state = session["sessionState"] | PortalState::Idle;
    const long remaining = session["secondsLeft"] | 0L;
    const bool livePurchase =
        remaining > 0 && isPaidActivatingState(state);

    if ((session["cleanupRetryPending"] | false) && livePurchase) {
      clearSupersededCleanupUnlocked(session);
      continue;
    }
    if (cleanupMac.isEmpty() && (session["cleanupRetryPending"] | false) &&
        !livePurchase) {
      cleanupMac = mac;
      cleanupGen = sessionGenerationOf(session);
    }
    if (activateMac.isEmpty() && (session["activationRetryPending"] | false) &&
        RouterApiTransportGate::allowsHotspotActivate()) {
      if (alreadyAuthorizedThisGeneration(session)) {
        session["activationRetryPending"] = false;
      } else if (_routerWorker && !_routerWorker->ethernetReadyForHotspot()) {
        activateWaitingOnEthernet = true;
      } else {
        activateMac = mac;
        activateGen = sessionGenerationOf(session);
      }
    }
    if (pauseMac.isEmpty() && (session["pauseRetryPending"] | false) &&
        RouterApiTransportGate::allowsHotspotDeauth()) {
      pauseMac = mac;
      pauseGen = sessionGenerationOf(session);
    }
  }
  if (!activateMac.isEmpty()) {
    JsonObject session = findSessionUnlocked(activateMac);
    if (!session.isNull()) session["activationRetryPending"] = false;
  } else if (!cleanupMac.isEmpty()) {
    JsonObject session = findSessionUnlocked(cleanupMac);
    if (!session.isNull()) {
      session["cleanupRetryPending"] = false;
      session["routerCleanupQueued"] = true;
    }
  } else if (!pauseMac.isEmpty()) {
    JsonObject session = findSessionUnlocked(pauseMac);
    if (!session.isNull()) session["pauseRetryPending"] = false;
  }
  unlockState();

  if (activateWaitingOnEthernet && activateMac.isEmpty()) {
    static uint32_t s_lastEthDeferLogMs = 0;
    const uint32_t now = millis();
    if (s_lastEthDeferLogMs == 0 || (now - s_lastEthDeferLogMs) >= 5000U) {
      s_lastEthDeferLogMs = now;
      Serial.printf(
          "[activate] deferred reason=ethernet_not_ready ip=%s\n",
          _routerWorker ? _routerWorker->ethernetIpLabel().c_str() : "0.0.0.0");
    }
  }

  if (!activateMac.isEmpty()) {
    Serial.printf("[ros-health] recovery drain activate mac=%s gen=%u\n",
                  activateMac.c_str(), static_cast<unsigned>(activateGen));
    enqueueWork(PortalWorkType::ActivateSession, activateMac, nullptr, 0,
                activateGen);
  } else if (!cleanupMac.isEmpty()) {
    Serial.printf("[ros-health] recovery drain cleanup mac=%s gen=%u\n",
                  cleanupMac.c_str(), static_cast<unsigned>(cleanupGen));
    enqueueWork(PortalWorkType::ExpireSession, cleanupMac, nullptr, 0,
                cleanupGen);
  } else if (!pauseMac.isEmpty()) {
    Serial.printf("[ros-health] recovery drain pause mac=%s gen=%u\n",
                  pauseMac.c_str(), static_cast<unsigned>(pauseGen));
    enqueueWork(PortalWorkType::PauseSession, pauseMac, nullptr, 0, pauseGen);
  }
}

void PortalSessionManager::markActivationEnqueueFailed(
    const String& mac, bool resumeAttempt) {
  lockState();
  JsonObject session = findSessionUnlocked(mac);
  if (!session.isNull()) {
    session["routerAuthPending"] = false;
    session["activationRetryPending"] = false;
    session["connected"] = false;
    session["activationError"] = true;
    session["activationErrorReason"] =
        "Router worker queue is full — purchased time preserved";
    if (resumeAttempt || (session["resumePending"] | false)) {
      session["resumePending"] = false;
      session["paused"] = true;
      session["sessionState"] = PortalState::Paused;
    } else {
      session["sessionState"] = PortalState::ActivationError;
    }
    _dirty = true;
  }
  unlockState();
  enqueueSaveSessions();
  enqueueWork(PortalWorkType::EmitSessionEvent, mac,
              "portal.session.activation_failed");
  enqueueEmitBus("sessions.changed");
}

// ──────────────────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────────────────

bool PortalSessionManager::getSession(const String& mac, const String& ip,
                                       JsonDocument& out) {
  lockState();
  JsonObject session = findOrCreateUnlocked(mac, ip);
  session["lastSeen"] = (unsigned long)(millis() / 1000);
  _dirty = true;
  out.set(session);
  unlockState();
  enrichSessionPurchasedMinutes(out);
  enrichSessionCapabilities(out);
  return true;
}

void PortalSessionManager::enrichSessionCapabilities(JsonDocument& out) {
  // Derived, read-only view fields. The portal is a renderer: every control it
  // shows is decided here so the UI never has to infer lifecycle rules.
  // ESP32-local only — 0 RouterOS commands.
  const String state = out["sessionState"] | PortalState::Idle;
  const bool isVoucher = String(out["source"] | "portal") == "voucher";
  long secondsLeft = out["secondsLeft"] | 0L;
  const bool paused = out["paused"] | false;
  const bool coinWindow = out["coinWindowActive"] | false;
  bool ended = state == PortalState::Expiring || state == PortalState::Expired;

  // Voucher authority: wall-clock serviceExpiresAt (NTP), not Active presence.
  if (isVoucher) {
    const String serviceExpiresAt = out["serviceExpiresAt"] | "";
    if (!serviceExpiresAt.isEmpty()) {
      const long wallRemaining = secondsUntilRecordedAt(serviceExpiresAt);
      if (wallRemaining == 0) {
        secondsLeft = 0;
        out["secondsLeft"] = 0;
        out["connected"] = false;
        ended = true;
      } else if (wallRemaining > 0) {
        secondsLeft = wallRemaining;
        out["secondsLeft"] = wallRemaining;
      }
    }
  }

  const int pausesUsed = out["pausesUsed"] | 0;
  const int pausesRemaining =
      kMaxCustomerPauses > pausesUsed ? kMaxCustomerPauses - pausesUsed : 0;
  out["pausesUsed"]      = pausesUsed;
  out["pausesRemaining"] = pausesRemaining;
  out["pauseLimit"]      = kMaxCustomerPauses;

  // Buying more time is blocked only while a router cleanup is genuinely in
  // flight. If the cleanup retry budget is spent the session would otherwise be
  // a dead end, so the customer keeps the ability to start a new one.
  const bool cleanupStuck = (out["cleanupRetryCount"] | 0) >= 3 &&
                            !(out["cleanupRetryPending"] | false) &&
                            !(out["routerCleanupQueued"] | false);
  const bool cleanupInFlight = state == PortalState::Expiring && !cleanupStuck;
  out["canInsertCoin"] = !isVoucher && !cleanupInFlight;
  out["canPause"]      = !isVoucher && !ended && !coinWindow && !paused &&
                    secondsLeft > 0 && state == PortalState::Active &&
                    pausesRemaining > 0;
  // resume() already re-queues activation for activation_error with time left.
  out["canResume"]     = !isVoucher && !ended && secondsLeft > 0 &&
                    (paused || state == PortalState::ActivationError);
  out["canTerminate"]  = !isVoucher && !ended && (secondsLeft > 0 || paused);
  out["canReconnect"]  = isVoucher && !ended && secondsLeft > 0;

  // The countdown only advances in Active; the UI must not interpolate in any
  // other state (see the timer-ownership contract in the portal bundle).
  const bool connected = out["connected"] | false;
  out["timerRunning"] = state == PortalState::Active && !paused && secondsLeft > 0 &&
                        (out["connected"] | false);
  out["sessionGeneration"] = out["sessionGeneration"] | 0U;

  const uint32_t nowMs = millis();
  out["serverNowMs"] = nowMs;
  out["authorizedAtMs"] = out["authorizedAtMs"] | 0U;
  out["grantedSeconds"] = out["grantedSeconds"] | 0U;
  const uint32_t expiresAtMs = out["expiresAtMs"] | 0U;
  out["expiresAtMs"] = expiresAtMs;
  if (!isVoucher && expiresAtMs > 0 &&
      state == PortalState::Active && !paused && connected) {
    const long clockRemaining = remainingFromExpiresMs(expiresAtMs, nowMs);
    if (clockRemaining >= 0 &&
        (secondsLeft <= 0 || clockRemaining < secondsLeft)) {
      secondsLeft = clockRemaining;
      out["secondsLeft"] = clockRemaining;
    }
    if (secondsLeft <= 0) {
      out["timerRunning"] = false;
    }
  }
}

void PortalSessionManager::enrichSessionPurchasedMinutes(JsonDocument& out) {
  // ESP32-local only — 0 RouterOS commands.
  // purchasedMinutes is accumulated per physical insertion in onCoinInserted.
  // Never re-resolve the credit total through PromoManager.
  if (out["purchasedMinutes"].is<int>()) return;

  // Ancient unpaid sessions (pre-purchasedMinutes field): arithmetic fallback
  // only — not a promo table lookup on the accumulated amount.
  const int credits = out["credits"] | 0;
  out["purchasedMinutes"] = credits > 0 ? credits * 5 : 0;
}

bool PortalSessionManager::startCoinWindow(const String& mac,
                                            const String& ip) {
  lockState();
  closeOtherCoinWindowsUnlocked(mac);

  JsonObject session = findOrCreateUnlocked(mac, ip);
  if (strcmp(session["source"] | "portal", "voucher") == 0) {
    unlockState();
    return false;
  }
  const int windowSecs = (int)coinInsertTimeoutSecs();
  session["coinWindowActive"]    = true;
  session["coinWindowRemaining"] = windowSecs;
  session["sessionState"]        = PortalState::WaitingCoin;
  session["lastSeen"]            = (unsigned long)(millis() / 1000);

  _activeInsertMac = mac;
  _dirty = true;
  unlockState();

  enqueueSaveSessions();
  enqueueWork(PortalWorkType::EmitSessionEvent, mac, "portal.coin.started");
  enqueueEmitBus("sessions.changed");
  if (_logger) _logger->infoLocal("portal", "Coin window opened for " + mac);
  return true;
}

void PortalSessionManager::onCoinInserted(int pesoAmount) {
  int matchedCoin = 0;
  int coinMinutes = pesoAmount * 5;
  if (_promos) {
    const int configuredMinutes =
        _promos->resolveForAmount(pesoAmount, nullptr, nullptr, &matchedCoin);
    // A denomination earns only its own configured promo time. If that exact
    // denomination has no promo, retain the existing 5-minutes-per-peso fallback.
    if (matchedCoin == pesoAmount && configuredMinutes > 0) {
      coinMinutes = configuredMinutes;
    }
  }
  coinLatencyTrace().markT3PromoDone();
  if (coinMinutes < 1) coinMinutes = 1;

  String creditMac;
  int totalCredits = 0;
  int totalPurchasedMinutes = 0;

  lockState();
  if (_activeInsertMac.isEmpty()) {
    unlockState();
    if (_logger) {
      _logger->warn("portal", "Coin pulse received but no active coin window");
    }
    coinLatencyTrace().reset();
    return;
  }

  JsonObject session = findSessionUnlocked(_activeInsertMac);
  if (session.isNull()) {
    unlockState();
    if (_logger) {
      _logger->warn("portal", "Coin pulse: session not found for " +
                                   _activeInsertMac);
    }
    coinLatencyTrace().reset();
    return;
  }

  if (!(session["coinWindowActive"] | false)) {
    unlockState();
    if (_logger) {
      _logger->warn("portal",
                    "Coin pulse ignored — insert window closed for " +
                        _activeInsertMac);
    }
    coinLatencyTrace().reset();
    return;
  }

  // One-time compatibility for a pending unpaid session created before
  // purchasedMinutes became persistent. Seed with arithmetic fallback only —
  // never re-resolve the accumulated credit total through the promo table.
  // After this, every newly accepted denomination contributes independently.
  if (!session["purchasedMinutes"].is<int>()) {
    const int previousCredits = session["credits"] | 0;
    session["purchasedMinutes"] =
        previousCredits > 0 ? previousCredits * 5 : 0;
  }

  session["credits"]        = (session["credits"] | 0) + pesoAmount;
  session["insertedAmount"] = (session["insertedAmount"] | 0) + pesoAmount;
  session["purchasedMinutes"] =
      (session["purchasedMinutes"] | 0) + coinMinutes;
  session["updatedAt"]      = (unsigned long)(millis() / 1000);
  session["lastSeen"]       = (unsigned long)(millis() / 1000);
  session["coinWindowRemaining"] = (int)coinInsertTimeoutSecs();
  session["sessionState"]        = PortalState::WaitingCoin;

  creditMac = _activeInsertMac;
  totalCredits = session["credits"] | 0;
  totalPurchasedMinutes = session["purchasedMinutes"] | 0;
  const int insertedAmount = session["insertedAmount"] | 0;
  _dirty = true;
  unlockState();
  coinLatencyTrace().markT2CreditApplied(creditMac);

  // Credit is already in portal RAM. SSE is observational — crash recovery
  // still depends on the SaveSessions job queued below, same as today.
  // Emitting here avoids the 9s+ wait behind EmitBusEvent/SaveSessions /
  // loop starvation while RouterWorker holds SPI.
  coinLatencyTrace().markT4Queued();
  emitSessionEvent(creditMac, "portal.coin.credit");
  lockState();
  Serial.printf("[coin-latency] queue depth=%u items=", (unsigned)_workCount);
  for (uint8_t i = 0; i < _workCount; ++i) {
    const uint8_t idx =
        static_cast<uint8_t>((_workTail + i) % kPortalWorkQueueCap);
    const PortalWorkType type = _workQueue[idx].type;
    const char *name = "Unknown";
    switch (type) {
      case PortalWorkType::SaveSessions:
        name = "SaveSessions";
        break;
      case PortalWorkType::ActivateSession:
        name = "ActivateSession";
        break;
      case PortalWorkType::ExpireSession:
        name = "ExpireSession";
        break;
      case PortalWorkType::PauseSession:
        name = "PauseSession";
        break;
      case PortalWorkType::EmitSessionEvent:
        name = "EmitSessionEvent";
        break;
      case PortalWorkType::EmitBusEvent:
        name = "EmitBusEvent";
        break;
      case PortalWorkType::RecordSale:
        name = "RecordSale";
        break;
    }
    Serial.printf("%s%s", i ? "," : "", name);
    if (_workQueue[idx].event[0] != '\0') {
      Serial.printf("(%s)", _workQueue[idx].event);
    }
  }
  Serial.println();
  unlockState();
  enqueueEmitBus("sessions.changed");
  enqueueSaveSessions();
  // CoinManager already logged pulses; mirror final credit attribution here.
  Serial.printf(
      "[coin] mac=%s pulses=n/a peso=%d coinMinutes=%d sessionCredits=%d "
      "purchasedMinutes=%d insertedAmount=%d\n",
      creditMac.c_str(), pesoAmount, coinMinutes, totalCredits,
      totalPurchasedMinutes, insertedAmount);
  if (_logger) {
    _logger->info("portal",
                  "Credit +PHP " + String(pesoAmount) + " → " + creditMac +
                      " (total PHP " + String(totalCredits) + ", " +
                      String(totalPurchasedMinutes) + " min)");
  }
}

bool PortalSessionManager::donePaying(const String& mac, String& errorCode,
                                      const String& ip) {
  errorCode = "";
  activationLatencyTrace().begin(mac);
  Serial.println("[portal] done-paying begin");

  int saleAmount = 0;
  int saleMinutes = 0;
  String sessionId;
  String recordedAt;
  bool activate = false;
  bool ok = false;
  long existingRemaining = 0;
  bool addTime = false;

  lockState();
  JsonObject session = findSessionUnlocked(mac);
  if (session.isNull()) {
    unlockState();
    Serial.println("[portal] done-paying aborted (session not found)");
    errorCode = "SESSION_NOT_FOUND";
    return false;
  }

  const char* stateStr = session["sessionState"] | PortalState::Idle;
  if (strcmp(session["source"] | "portal", "voucher") == 0) {
    unlockState();
    Serial.println("[portal] done-paying rejected for voucher session");
    errorCode = "VOUCHER_SESSION";
    return false;
  }
  const long secondsLeft = session["secondsLeft"] | 0L;
  int credits = session["credits"] | 0;

  // Idempotent — activation already in flight (no new credits to convert).
  if (credits <= 0) {
    if (strcmp(stateStr, PortalState::Activating) == 0 ||
        hasPendingActivationUnlocked(mac) ||
        hasPendingRecordSaleUnlocked(mac, String(session["sessionId"] | ""))) {
      unlockState();
      Serial.println("[portal] done-paying idempotent (activation in progress)");
      return true;
    }
    if (strcmp(stateStr, PortalState::Active) == 0 && secondsLeft > 0) {
      unlockState();
      Serial.println("[portal] done-paying idempotent (already active)");
      return true;
    }
    unlockState();
    Serial.println("[portal] done-paying aborted (no credits)");
    errorCode = "NO_CREDITS";
    return false;
  }

  if (hasPendingActivationUnlocked(mac) ||
      hasPendingRecordSaleUnlocked(mac, String(session["sessionId"] | ""))) {
    unlockState();
    Serial.println("[portal] done-paying idempotent (activation in progress)");
    return true;
  }

  String hotspotProfile;
  int minutes = session["purchasedMinutes"] | 0;
  if (!session["purchasedMinutes"].is<int>()) {
    // Ancient unpaid session without accumulated minutes: arithmetic fallback
    // only. Do not resolvePromo(totalCredits).
    minutes = credits * 5;
    session["purchasedMinutes"] = minutes;
    _dirty = true;
  }
  if (minutes < 1) {
    unlockState();
    Serial.println("[portal] done-paying aborted (no purchased minutes)");
    errorCode = "NO_MINUTES";
    activationLatencyTrace().reset();
    return false;
  }

  const String existingProfileEarly = session["hotspotProfile"] | "";
  const bool addTimeEarly =
      secondsLeft > 0 &&
      (strcmp(stateStr, PortalState::Active) == 0 ||
       strcmp(stateStr, PortalState::Paused) == 0 ||
       strcmp(stateStr, PortalState::Activating) == 0 ||
       strcmp(stateStr, PortalState::ActivationError) == 0);
  // Add Time keeps the stored Hotspot profile — do not block activation on
  // a promo-table read that will be discarded.
  const bool needPromoProfile =
      !(addTimeEarly && existingProfileEarly.length() > 0);

  unlockState();
  activationLatencyTrace().markT1();

  // Sales wall clock is downstream bookkeeping — never abort coin Internet.
  recordedAt = salesRecordedAtNow();
  if (recordedAt.isEmpty()) {
    recordedAt = String("uptime-ms:") + millis();
    Serial.println(
        "[portal] wall clock unavailable — using uptime sale marker");
    if (_logger) {
      _logger->warn("portal",
                    "donePaying: wall clock not ready — sale uses uptime marker");
    }
  }

  saleAmount = credits;
  saleMinutes = minutes;
  hotspotProfile = "";
  // Profile lookup uses the RAM promo cache after the first SD load.
  if (needPromoProfile && _promos) {
    _promos->resolveHighestProfileForAmount(saleAmount, &hotspotProfile);
  }

  lockState();
  session = findSessionUnlocked(mac);
  if (session.isNull()) {
    unlockState();
    errorCode = "SESSION_NOT_FOUND";
    return false;
  }

  // Re-check under lock — another request may have reserved while we fetched time.
  stateStr = session["sessionState"] | PortalState::Idle;
  credits = session["credits"] | 0;
  if (credits <= 0) {
    if (strcmp(stateStr, PortalState::Activating) == 0 ||
        strcmp(stateStr, PortalState::Active) == 0) {
      unlockState();
      Serial.println("[portal] done-paying idempotent (reserved by concurrent request)");
      return true;
    }
    unlockState();
    Serial.println("[portal] done-paying aborted (no credits under lock)");
    errorCode = "NO_CREDITS";
    return false;
  }
  if (hasPendingActivationUnlocked(mac) ||
      hasPendingRecordSaleUnlocked(mac, String(session["sessionId"] | ""))) {
    unlockState();
    Serial.println("[portal] done-paying idempotent (activation in progress)");
    return true;
  }

  saleAmount = credits;
  sessionId = String(session["sessionId"] | "");
  minutes = session["purchasedMinutes"] | 0;
  if (minutes < 1) {
    unlockState();
    Serial.println("[portal] done-paying aborted (no purchased minutes under lock)");
    errorCode = "NO_MINUTES";
    return false;
  }
  saleMinutes = minutes;

  existingRemaining = session["secondsLeft"] | 0L;
  addTime =
      existingRemaining > 0 &&
      (strcmp(stateStr, PortalState::Active) == 0 ||
       strcmp(stateStr, PortalState::Paused) == 0 ||
       strcmp(stateStr, PortalState::Activating) == 0 ||
       strcmp(stateStr, PortalState::ActivationError) == 0);
  const long purchasedSeconds = (long)saleMinutes * 60L;
  const long newRemaining =
      addTime ? (existingRemaining + purchasedSeconds) : purchasedSeconds;

  // Profile policy: preserve active session profile on Add Time; apply promo
  // profile only on first activation (or when no profile is stored yet).
  const String existingProfile = session["hotspotProfile"] | "";
  if (!(addTime && existingProfile.length() > 0)) {
    if (hotspotProfile.length() > 0) {
      session["hotspotProfile"] = hotspotProfile;
    } else {
      session.remove("hotspotProfile");
    }
  }

  const unsigned long nowSec = millis() / 1000;
  if (!ip.isEmpty()) session["ipAddress"] = ip;
  if (!addTime) {
    bumpSessionGenerationUnlocked(session);
  }
  clearSupersededCleanupUnlocked(session);
  session["credits"]             = 0;
  session["insertedAmount"]      = 0;
  session["purchasedMinutes"]    = 0;
  session["secondsLeft"]         = newRemaining;
  session["grantedSeconds"]      = static_cast<uint32_t>(newRemaining);
  session["authorizedAtMs"]      = 0U;
  session["expiresAtMs"]         = 0U;
  session["sessionState"]        = PortalState::Activating;
  session["connected"]           = false;
  session["paused"]              = false;
  session["routerAuthPending"]   = true;
  session["activationError"]     = false;
  session["coinWindowActive"]    = false;
  session["coinWindowRemaining"] = 0;
  session["updatedAt"]           = nowSec;
  session["lastSeen"]            = nowSec;
  session["activationAttempts"] = 0;
  session["activationStartedAt"] =
      static_cast<unsigned long>(millis() / 1000);
  session.remove("activationErrorReason");
  session.remove("activationRetryAt");
  if (!addTime || !session["startedAt"].is<const char*>()) {
    session["startedAt"] = recordedAt;
    session["connectedSeconds"] = 0UL;
    // Pause budget is per purchased session, not per add-time top-up.
    session["pausesUsed"] = 0;
  }
  if (_activeInsertMac == mac) _activeInsertMac = "";
  _dirty = true;
  activate = true;
  ok = true;
  unlockState();
  activationLatencyTrace().markT2();

  if (!ok) {
    errorCode = "SESSION_ERROR";
    activationLatencyTrace().reset();
    return false;
  }

  // Internet first: dispatch RouterWorker before SD save / sale bookkeeping.
  // processDeferredWork() runs ONE item per loop — a SaveSessions job must
  // never sit ahead of ActivateSession.
  bool routerAccepted = false;
  if (activate) {
    activationLatencyTrace().markT3();
    routerAccepted = onSessionActivated(mac);
    if (!routerAccepted && enqueueActivateSession(mac)) {
      // Deferred ActivateSession is the sole retry — do not also arm idle retry.
      lockState();
      JsonObject queued = findSessionUnlocked(mac);
      if (!queued.isNull()) queued["activationRetryPending"] = false;
      unlockState();
      routerAccepted = true;
    }
    if (!routerAccepted) {
      lockState();
      session = findSessionUnlocked(mac);
      if (!session.isNull()) {
        session["credits"] = saleAmount;
        session["insertedAmount"] = saleAmount;
        session["purchasedMinutes"] = saleMinutes;
        session["secondsLeft"] = existingRemaining;
        session["sessionState"] =
            addTime && existingRemaining > 0 ? PortalState::Active
                                             : PortalState::WaitingCoin;
        session["connected"] = addTime && existingRemaining > 0;
        session["routerAuthPending"] = false;
        session["activationError"] = false;
        _dirty = true;
      }
      unlockState();
      enqueueSaveSessions();
      enqueueEmitBus("sessions.changed");
      Serial.println("[portal] done-paying aborted (activation enqueue failed)");
      if (_logger) {
        _logger->errorLocal("portal",
                            "donePaying: activation queue full — credits restored for " +
                                mac);
      }
      errorCode = "ACTIVATION_QUEUE_FULL";
      activationLatencyTrace().reset();
      return false;
    }
  }

  enqueueSaveSessions();

  if (!_storage || !_sessions) {
    Serial.println("[portal] salesSaved=false");
    if (_logger) {
      _logger->warnLocal("portal",
                         "donePaying: storage unavailable — sale not recorded");
    }
  } else if (!enqueueRecordSale(mac, saleAmount, saleMinutes, sessionId,
                                recordedAt)) {
    // Do not roll back Internet activation — sale is best-effort downstream.
    Serial.println("[portal] salesSaved=false");
    if (_logger) {
      _logger->errorLocal("portal",
                          "donePaying: failed to queue sale for " + mac +
                              " (activation continues)");
    }
  }

  enqueueEmitBus("sessions.changed");

  uint32_t doneGen = 0;
  lockState();
  JsonObject genSession = findSessionUnlocked(mac);
  if (!genSession.isNull()) doneGen = sessionGenerationOf(genSession);
  unlockState();
  Serial.printf(
      "[portal-session] mac=%s state=activating credits=0 remaining=%ld "
      "profile=%s addTime=%s saleMin=%d gen=%u connected=0\n",
      mac.c_str(), newRemaining,
      hotspotProfile.c_str(), addTime ? "yes" : "no", saleMinutes,
      static_cast<unsigned>(doneGen));

  if (_logger) {
    _logger->infoLocal("portal",
                       String(addTime ? "Add-time " : "Session activating ") + mac +
                           " — +" + String(saleMinutes) + " min (PHP " +
                           String(saleAmount) + ") remaining=" + String(newRemaining) +
                           "s");
  }
  Serial.println("[portal] done-paying complete");
  return true;
}

bool PortalSessionManager::hasSession(const String& mac) {
  lockState();
  const bool found = !findSessionUnlocked(mac).isNull();
  unlockState();
  return found;
}

bool PortalSessionManager::pause(const String& mac, String* errorCode,
                                 bool enforceLimit) {
  bool ok = false;
  bool already = false;
  int pausesUsed = 0;
  uint32_t pauseGen = 0;
  if (errorCode) *errorCode = "";

  auto reject = [&](const char* code) {
    if (errorCode) *errorCode = code;
    unlockState();
    return false;
  };

  lockState();
  JsonObject session = findSessionUnlocked(mac);
  if (session.isNull()) return reject("SESSION_NOT_FOUND");

  const char* stateStr = session["sessionState"] | PortalState::Idle;
  if (strcmp(session["source"] | "portal", "voucher") == 0) {
    return reject("VOUCHER_SESSION");
  }
  if (strcmp(stateStr, PortalState::Expiring) == 0 ||
      strcmp(stateStr, PortalState::Expired) == 0) {
    return reject("SESSION_ENDED");
  }
  if (strcmp(stateStr, PortalState::WaitingCoin) == 0) {
    return reject("COIN_WINDOW_OPEN");
  }
  if (session["coinWindowActive"] | false) {
    return reject("COIN_WINDOW_OPEN");
  }
  // Idempotent: already paused (or pause RouterWorker still in flight).
  if (strcmp(stateStr, PortalState::Paused) == 0 ||
      (session["routerPausePending"] | false)) {
    already = true;
    ok = true;
  } else if ((strcmp(stateStr, PortalState::Active) == 0 ||
              strcmp(stateStr, PortalState::Activating) == 0) &&
             (session["secondsLeft"] | 0L) > 0) {
    pausesUsed = session["pausesUsed"] | 0;
    if (enforceLimit && pausesUsed >= kMaxCustomerPauses) {
      return reject("PAUSE_LIMIT_REACHED");
    }
    // Freeze ESP32 timer immediately; RouterOS pause is async.
    freezeSessionClockUnlocked(session);
    session["paused"]             = true;
    session["sessionState"]       = PortalState::Paused;
    session["routerPausePending"] = true;
    if (enforceLimit) {
      pausesUsed += 1;
      session["pausesUsed"] = pausesUsed;
    }
    session["updatedAt"]          = (unsigned long)(millis() / 1000);
    session["lastSeen"]           = (unsigned long)(millis() / 1000);
    _dirty = true;
    ok = true;
    pauseGen = sessionGenerationOf(session);
  } else {
    return reject("PAUSE_NOT_ALLOWED");
  }
  unlockState();

  if (!ok) {
    if (errorCode && errorCode->isEmpty()) *errorCode = "PAUSE_NOT_ALLOWED";
    return false;
  }
  if (already) return true;

  enqueueSaveSessions();
  enqueueWork(PortalWorkType::PauseSession, mac, nullptr, 0, pauseGen);
  enqueueEmitBus("sessions.changed");
  enqueueEmitBus("users.active");
  enqueueEmitBus("system.status");
  Serial.printf("[portal-pause] mac=%s queued pausesUsed=%d/%d\n", mac.c_str(),
                pausesUsed, kMaxCustomerPauses);
  return true;
}

bool PortalSessionManager::resume(const String& mac, String* errorCode) {
  bool ok = false;
  bool already = false;
  if (errorCode) *errorCode = "";

  auto reject = [&](const char* code) {
    if (errorCode) *errorCode = code;
    unlockState();
    return false;
  };

  lockState();
  JsonObject session = findSessionUnlocked(mac);
  if (session.isNull()) return reject("SESSION_NOT_FOUND");

  const char* stateStr = session["sessionState"] | PortalState::Idle;
  if (strcmp(session["source"] | "portal", "voucher") == 0) {
    return reject("VOUCHER_SESSION");
  }
  if (strcmp(stateStr, PortalState::Expiring) == 0 ||
      strcmp(stateStr, PortalState::Expired) == 0) {
    return reject("SESSION_ENDED");
  }
  if (strcmp(stateStr, PortalState::WaitingCoin) == 0) {
    unlockState();
    return true;
  }
  if (session["coinWindowActive"] | false) {
    unlockState();
    return true;
  }
  if (strcmp(stateStr, PortalState::Paused) == 0) {
    // Idempotent while resume/authorize is already queued — no duplicate job.
    if ((session["resumePending"] | false) ||
        (session["routerAuthPending"] | false)) {
      already = true;
      ok = true;
    } else {
      // Keep paused=true (timer frozen) until RouterOS authorize succeeds.
      session["routerAuthPending"] = true;
      session["resumePending"]     = true;
      session["activationAttempts"] = 0;
      session["activationStartedAt"] =
          static_cast<unsigned long>(millis() / 1000);
      session.remove("activationRetryAt");
      session["updatedAt"]         = (unsigned long)(millis() / 1000);
      session["lastSeen"]          = (unsigned long)(millis() / 1000);
      _dirty = true;
      ok = true;
    }
  } else if (strcmp(stateStr, PortalState::ActivationError) == 0 &&
             (session["secondsLeft"] | 0L) > 0) {
    session["sessionState"]      = PortalState::Activating;
    session["routerAuthPending"] = true;
    session["activationError"]   = false;
    session["activationAttempts"] = 0;
    session["activationStartedAt"] =
        static_cast<unsigned long>(millis() / 1000);
    session.remove("activationRetryAt");
    session.remove("activationErrorReason");
    session["updatedAt"]         = (unsigned long)(millis() / 1000);
    _dirty = true;
    ok = true;
  } else if (strcmp(stateStr, PortalState::Active) == 0 &&
             !(session["paused"] | false) &&
             (session["connected"] | false)) {
    already = true;
    ok = true;
  } else if ((session["credits"] | 0) > 0) {
    already = true;
    ok = true;
  } else {
    return reject("RESUME_NOT_ALLOWED");
  }
  unlockState();

  if (!ok) {
    if (errorCode && errorCode->isEmpty()) *errorCode = "RESUME_NOT_ALLOWED";
    return false;
  }
  if (already) return true;

  enqueueSaveSessions();
  if (!enqueueActivateSession(mac)) {
    markActivationEnqueueFailed(mac, true);
    if (errorCode) *errorCode = "ACTIVATION_QUEUE_FULL";
    return false;
  }
  enqueueEmitBus("sessions.changed");
  enqueueEmitBus("users.active");
  enqueueEmitBus("system.status");
  Serial.printf("[portal-resume] mac=%s queued\n", mac.c_str());
  return true;
}

bool PortalSessionManager::cancelModal(const String& mac, JsonDocument& out) {
  lockState();
  JsonObject session = findOrCreateUnlocked(mac, "");
  session["coinWindowActive"]    = false;
  session["coinWindowRemaining"] = 0;
  if (strcmp(session["sessionState"] | PortalState::Idle,
             PortalState::WaitingCoin) == 0) {
    session["sessionState"] = PortalState::Idle;
  }
  // Preserve credits / insertedAmount — customer may reopen insert flow.
  session["lastSeen"] = (unsigned long)(millis() / 1000);
  if (_activeInsertMac.equalsIgnoreCase(mac)) _activeInsertMac = "";
  _dirty = true;
  out.set(session);
  unlockState();

  enqueueSaveSessions();
  enqueueWork(PortalWorkType::EmitSessionEvent, mac, "portal.session.updated");
  enqueueEmitBus("sessions.changed");
  return true;
}

bool PortalSessionManager::terminateSession(const String& mac) {
  bool ok = false;

  lockState();
  JsonObject session = findSessionUnlocked(mac);
  if (session.isNull()) {
    unlockState();
    Serial.println("[portal] terminate aborted (session not found)");
    return false;
  }

  const char* stateStr = session["sessionState"] | PortalState::Idle;
  if (strcmp(session["source"] | "portal", "voucher") == 0) {
    unlockState();
    return false;
  }
  const bool isIdle    = strcmp(stateStr, PortalState::Idle) == 0;
  if (isIdle && !(session["coinWindowActive"] | false) &&
      (session["credits"] | 0) == 0 &&
      (session["secondsLeft"] | 0L) == 0) {
    unlockState();
    Serial.println("[portal] terminate aborted (nothing to terminate)");
    return false;
  }

  // Idempotent expire cleanup — do not enqueue a second RouterOS job every call.
  if ((strcmp(stateStr, PortalState::Expiring) == 0 ||
       strcmp(stateStr, PortalState::Expired) == 0) &&
      ((session["routerCleanupQueued"] | false) ||
       (session["routerCleanupComplete"] | false))) {
    unlockState();
    return true;
  }

  const unsigned long nowSec = millis() / 1000;
  const uint32_t terminateGen = sessionGenerationOf(session);
  clearSessionClockUnlocked(session);
  session["secondsLeft"]            = 0;
  session["credits"]                = 0;
  session["insertedAmount"]         = 0;
  session["purchasedMinutes"]       = 0;
  session["paused"]                 = false;
  session["connected"]              = false;
  session["coinWindowActive"]       = false;
  session["coinWindowRemaining"]    = 0;
  session["sessionState"]           = PortalState::Expiring;
  session["routerAuthPending"]      = false;
  session["routerPausePending"]     = false;
  session["resumePending"]          = false;
  session["routerCleanupQueued"]    = true;
  session["routerCleanupComplete"]  = false;
  session["routerCleanupPending"]   = true;
  session["terminationReason"]      = "customer_terminated";
  session["terminationActor"]       = "customer";
  session["updatedAt"]              = nowSec;
  session["lastSeen"]               = nowSec;
  if (_activeInsertMac == mac) _activeInsertMac = "";
  _dirty = true;
  ok = true;
  unlockState();

  if (!ok) return false;

  enqueueSaveSessions();
  enqueueWork(PortalWorkType::ExpireSession, mac, nullptr, 0, terminateGen);
  enqueueWork(PortalWorkType::EmitSessionEvent, mac, "portal.session.terminated");
  enqueueEmitBus("sessions.changed");
  enqueueEmitBus("users.active");
  enqueueEmitBus("system.status");
  Serial.printf("[portal-terminate] mac=%s queued\n", mac.c_str());
  if (_logger) _logger->infoLocal("portal", "Session terminated by user request: " + mac);
  return true;
}

bool PortalSessionManager::reset(const String& mac) {
  String voucherCode;
  lockState();
  JsonObject session = findSessionUnlocked(mac);
  if (session.isNull()) {
    unlockState();
    return false;
  }
  voucherCode = String(session["voucherCode"] | "");
  const uint32_t resetGen = sessionGenerationOf(session);
  clearSessionClockUnlocked(session);
  session["secondsLeft"] = 0;
  session["credits"] = 0;
  session["insertedAmount"] = 0;
  session["purchasedMinutes"] = 0;
  session["paused"] = false;
  session["connected"] = false;
  session["coinWindowActive"] = false;
  session["coinWindowRemaining"] = 0;
  session["sessionState"] = PortalState::Expiring;
  session["routerAuthPending"] = false;
  session["routerPausePending"] = false;
  session["resumePending"] = false;
  session["routerCleanupQueued"] = true;
  session["routerCleanupPending"] = true;
  session["routerCleanupComplete"] = false;
  session["terminationReason"] = "owner_disconnected";
  session["terminationActor"] = "owner";
  if (_activeInsertMac == mac) _activeInsertMac = "";
  _dirty = true;
  unlockState();

  if (!voucherCode.isEmpty() && _vouchers) {
    _vouchers->expire(voucherCode, "owner_disconnected",
                      salesRecordedAtNow());
  }
  enqueueWork(PortalWorkType::ExpireSession, mac, nullptr, 0, resetGen);
  enqueueSaveSessions();
  enqueueEmitBus("sessions.changed");
  enqueueEmitBus("users.active");
  return true;
}

bool PortalSessionManager::heartbeat(const String& mac, const String& ip) {
  lockState();
  JsonObject session = findOrCreateUnlocked(mac, ip);
  const unsigned long nowSec = millis() / 1000;
  session["lastSeen"] = nowSec;
  if (!ip.isEmpty()) session["ipAddress"] = ip;
  _dirty = true;
  unlockState();
  return true;
}

bool PortalSessionManager::getRates(JsonDocument& out) {
  if (!_promos) return false;
  return _promos->list(out);
}

bool PortalSessionManager::redeemVoucher(const String& code, const String& mac,
                                         const String& ip, JsonDocument& out,
                                         String& errorCode) {
  errorCode = "";
  if (!_vouchers || !_sessions || mac.isEmpty() || code.isEmpty()) {
    errorCode = "INVALID_VOUCHER";
    return false;
  }

  const String recordedAt = salesRecordedAtNow();
  if (recordedAt.isEmpty()) {
    errorCode = "CLOCK_NOT_READY";
    return false;
  }

  lockState();
  JsonObject existing = findSessionUnlocked(mac);
  if (!existing.isNull() &&
      strcmp(existing["source"] | "portal", "voucher") != 0 &&
      ((existing["credits"] | 0) > 0 ||
       (existing["secondsLeft"] | 0L) > 0 ||
       (existing["coinWindowActive"] | false))) {
    unlockState();
    errorCode = "COIN_SESSION_ACTIVE";
    return false;
  }
  unlockState();

  const String candidateSessionId = makeSessionId();
  VoucherManager::ReserveResult reserved =
      _vouchers->reserve(code, mac, candidateSessionId, recordedAt);
  if (!reserved.accepted()) {
    using Status = VoucherManager::ReserveStatus;
    if (reserved.result == Status::NotFound) errorCode = "VOUCHER_NOT_FOUND";
    else if (reserved.result == Status::BoundToAnotherDevice)
      errorCode = "VOUCHER_BOUND_TO_ANOTHER_DEVICE";
    else if (reserved.result == Status::StorageError)
      errorCode = "STORAGE_ERROR";
    else
      errorCode = "VOUCHER_UNAVAILABLE";
    return false;
  }

  const String sessionId =
      reserved.sessionId.isEmpty() ? candidateSessionId : reserved.sessionId;
  const String saleId = "sale-" + sessionId;
  // Absolute voucher authority: remaining until redeemedAt+validity (serviceExpiresAt).
  long entitlementSeconds = static_cast<long>(reserved.minutes) * 60L;
  if (!reserved.serviceExpiresAt.isEmpty()) {
    entitlementSeconds = secondsUntilRecordedAt(reserved.serviceExpiresAt);
    if (entitlementSeconds < 0) {
      errorCode = "CLOCK_NOT_READY";
      return false;
    }
    if (entitlementSeconds == 0) {
      _vouchers->expire(reserved.code, "time_expired", recordedAt);
      Serial.printf(
          "[voucher-expiry] mac=%s code=%s redeemedAt= expiresAt=%s now=%s "
          "remaining=0 action=expire\n",
          mac.c_str(), reserved.code.c_str(),
          reserved.serviceExpiresAt.c_str(), recordedAt.c_str());
      errorCode = "VOUCHER_EXPIRED";
      return false;
    }
  } else if (reserved.status == "active") {
    // Legacy active voucher without serviceExpiresAt — refuse extension.
    errorCode = "VOUCHER_UNAVAILABLE";
    return false;
  }
  SaleRecord sale;
  sale.id = saleId;
  sale.timestamp = String("uptime-ms:") + millis();
  sale.recordedAt = recordedAt;
  sale.amount = reserved.amount;
  sale.sessionId = sessionId;
  sale.paymentType = "voucher";
  sale.durationMinutes = reserved.minutes;
  sale.macAddress = mac;
  sale.ipAddress = ip;
  sale.voucherCode = reserved.code;
  sale.credits = reserved.amount;
  sale.profile = reserved.profileName;
  sale.speed = reserved.speed;
  sale.expiresAt = reserved.validUntil;
  sale.operatorName = "owner";
  sale.status = "pending_activation";
  if (reserved.status != "active" && !_sessions->upsertSale(sale)) {
    errorCode = "STORAGE_ERROR";
    return false;
  }

  lockState();
  JsonObject session = findOrCreateUnlocked(mac, ip);
  session["sessionId"] = sessionId;
  session["source"] = "voucher";
  session["voucherCode"] = reserved.code;
  session["voucherStatus"] = reserved.status;
  session["voucherExpiresAt"] = reserved.validUntil;
  session["saleId"] = saleId;
  session["credits"] = 0;
  session["insertedAmount"] = 0;
  session["purchasedMinutes"] = 0;
  session["secondsLeft"] = entitlementSeconds;
  session["paused"] = false;
  session["connected"] = false;
  session["coinWindowActive"] = false;
  session["coinWindowRemaining"] = 0;
  session["sessionState"] = PortalState::Activating;
  session["routerAuthPending"] = true;
  session["activationError"] = false;
  bumpSessionGenerationUnlocked(session);
  clearSupersededCleanupUnlocked(session);
  session["connectedSeconds"] = session["connectedSeconds"] | 0UL;
  session["startedAt"] =
      reserved.activatedAt.isEmpty() ? recordedAt : reserved.activatedAt;
  session["serviceExpiresAt"] = reserved.serviceExpiresAt;
  session["serviceExpiresEpoch"] =
      !reserved.serviceExpiresAt.isEmpty()
          ? static_cast<unsigned long>(time(nullptr) + entitlementSeconds)
          : 0UL;
  if (!reserved.profileName.isEmpty()) {
    session["hotspotProfile"] = reserved.profileName;
  }
  session["updatedAt"] = static_cast<unsigned long>(millis() / 1000);
  session["lastSeen"] = static_cast<unsigned long>(millis() / 1000);
  _dirty = true;
  out.set(session);
  unlockState();

  Serial.printf(
      "[voucher-activate] mac=%s code=%s profile=%s expiresAt=%s remaining=%ld "
      "action=enqueue\n",
      mac.c_str(), reserved.code.c_str(),
      reserved.profileName.c_str(), reserved.serviceExpiresAt.c_str(),
      entitlementSeconds);

  enqueueSaveSessions();
  if (!enqueueActivateSession(mac)) {
    markActivationEnqueueFailed(mac, false);
    errorCode = "ACTIVATION_QUEUE_FULL";
    return false;
  }
  enqueueEmitBus("sessions.changed");
  return true;
}

bool PortalSessionManager::reconnectVoucher(const String& mac, const String& ip,
                                            JsonDocument& out,
                                            String& errorCode) {
  errorCode = "";
  bool mustExpire = false;
  lockState();
  JsonObject session = findSessionUnlocked(mac);
  if (session.isNull() ||
      strcmp(session["source"] | "portal", "voucher") != 0) {
    unlockState();
    errorCode = "VOUCHER_SESSION_NOT_FOUND";
    return false;
  }
  if (!ip.isEmpty()) session["ipAddress"] = ip;
  const String serviceExpiresAt = session["serviceExpiresAt"] | "";
  if (!serviceExpiresAt.isEmpty()) {
    const long remaining = secondsUntilRecordedAt(serviceExpiresAt);
    if (remaining < 0) {
      unlockState();
      errorCode = "CLOCK_NOT_READY";
      return false;
    } else if (remaining == 0) {
      mustExpire = true;
    } else {
      session["secondsLeft"] = remaining;
      session["serviceExpiresEpoch"] =
          static_cast<unsigned long>(time(nullptr) + remaining);
    }
  } else if ((session["secondsLeft"] | 0L) <= 0) {
    mustExpire = true;
  }
  if (!mustExpire) {
    const char* state = session["sessionState"] | PortalState::Idle;
    if (strcmp(state, PortalState::Expired) == 0 ||
        strcmp(state, PortalState::Expiring) == 0 ||
        (session["secondsLeft"] | 0L) <= 0) {
      mustExpire = true;
    } else {
      session["connected"] = false;
      session["paused"] = false;
      session["routerAuthPending"] = true;
      session["sessionState"] = PortalState::Activating;
      session["activationError"] = false;
      session["updatedAt"] = static_cast<unsigned long>(millis() / 1000);
      session["lastSeen"] = static_cast<unsigned long>(millis() / 1000);
      _dirty = true;
      out.set(session);
    }
  }
  unlockState();

  if (mustExpire) {
    String codeValue;
    lockState();
    session = findSessionUnlocked(mac);
    if (!session.isNull()) codeValue = String(session["voucherCode"] | "");
    unlockState();
    Serial.printf(
        "[voucher-expiry] mac=%s code=%s expiresAt=%s remaining=0 "
        "action=expire\n",
        mac.c_str(), codeValue.c_str(), serviceExpiresAt.c_str());
    String actionError;
    administerVoucher(codeValue, "expire", "time_expired", actionError);
    errorCode = "VOUCHER_EXPIRED";
    return false;
  }

  enqueueSaveSessions();
  if (!enqueueActivateSession(mac)) {
    markActivationEnqueueFailed(mac, false);
    errorCode = "ACTIVATION_QUEUE_FULL";
    return false;
  }
  return true;
}

bool PortalSessionManager::administerVoucher(const String& code,
                                             const String& action,
                                             const String& reason,
                                             String& errorCode) {
  errorCode = "";
  if (!_vouchers) {
    errorCode = "NOT_READY";
    return false;
  }
  DynamicJsonDocument voucher(RenzFiConfig::JSON_DOC_SMALL);
  if (!_vouchers->find(code, voucher)) {
    errorCode = "VOUCHER_NOT_FOUND";
    return false;
  }
  String normalizedAction = action;
  normalizedAction.toLowerCase();
  const String mac = voucher["boundMac"] | "";
  const String now = salesRecordedAtNow();

  bool changed = false;
  if (normalizedAction == "terminate" || normalizedAction == "expire") {
    changed = _vouchers->expire(code, reason, now);
  } else if (normalizedAction == "disable") {
    changed = _vouchers->disable(code, reason, now);
  } else if (normalizedAction == "archive") {
    changed = _vouchers->archive(code, reason, now);
  }
  if (!changed) {
    errorCode = "INVALID_VOUCHER_STATE";
    return false;
  }

  if (!mac.isEmpty() && normalizedAction != "archive") {
    lockState();
    JsonObject session = findSessionUnlocked(mac);
    if (!session.isNull() &&
        strcmp(session["source"] | "portal", "voucher") == 0) {
      session["secondsLeft"] = 0;
      session["connected"] = false;
      session["paused"] = false;
      session["sessionState"] = PortalState::Expiring;
      session["routerAuthPending"] = false;
      session["routerCleanupQueued"] = true;
      session["routerCleanupPending"] = true;
      session["routerCleanupComplete"] = false;
      session["terminationReason"] = reason;
      session["terminationActor"] = "owner";
      session["voucherStatus"] =
          normalizedAction == "disable" ? "disabled" : "expired";
      _dirty = true;
    }
    unlockState();
    completeAccounting(mac, reason, "owner");
    enqueueSaveSessions();
    enqueueWork(PortalWorkType::ExpireSession, mac);
  }
  return true;
}

void PortalSessionManager::completeAccounting(const String& mac,
                                              const String& reason,
                                              const String& actor) {
  if (!_sessions) return;
  String sessionId;
  uint32_t connectedSeconds = 0;
  lockState();
  JsonObject session = findSessionUnlocked(mac);
  if (!session.isNull()) {
    sessionId = String(session["sessionId"] | "");
    connectedSeconds = session["connectedSeconds"] | 0UL;
  }
  unlockState();
  if (sessionId.isEmpty()) return;
  String endedAt = salesRecordedAtNow();
  if (endedAt.isEmpty()) endedAt = String("uptime-ms:") + millis();
  _sessions->completeSaleBySessionId(sessionId, endedAt, connectedSeconds,
                                     reason, actor);
}

static bool macAlreadyListed(const JsonArray &seenMacs, const String &mac) {
  if (mac.isEmpty()) return false;
  for (JsonVariantConst seen : seenMacs) {
    if (mac.equalsIgnoreCase(seen.as<const char *>())) return true;
  }
  return false;
}

static void markMacSeen(JsonArray &seenMacs, const String &mac) {
  if (!mac.isEmpty()) seenMacs.add(mac);
}

static const char *portalApiState(JsonObjectConst session) {
  const char *sessionState = session["sessionState"] | PortalState::Idle;
  if (strcmp(sessionState, PortalState::Paused) == 0) return "paused";
  if (session["paused"] | false) return "paused";
  if (strcmp(sessionState, PortalState::WaitingCoin) == 0) return "waiting_coin";
  if (session["coinWindowActive"] | false) return "waiting_coin";
  if (strcmp(sessionState, PortalState::Activating) == 0) return "activating";
  if (strcmp(sessionState, PortalState::ActivationError) == 0) {
    return "activation_error";
  }
  if (strcmp(sessionState, PortalState::Active) == 0) return "active";
  if (strcmp(sessionState, PortalState::Expiring) == 0) return "expiring";
  if (strcmp(sessionState, PortalState::Expired) == 0) return "expired";
  return "idle";
}

/**
 * Active Users entitlement filter (not browser liveness).
 *
 * WaitingCoin / open coin window still require a fresh portal heartbeat — that
 * path is browser presence.
 *
 * Paid Active / Paused / Activating / ActivationError with remaining time (or
 * pause) remain listed even when the captive portal is closed. Expiration and
 * Idle/Expired states remove them. Heartbeat must not equate to Hotspot auth.
 */
static bool isPortalSessionActive(JsonObjectConst session, unsigned long nowSec) {
  const char *state = session["sessionState"] | PortalState::Idle;
  if (strcmp(state, PortalState::Expired) == 0) return false;
  if (strcmp(state, PortalState::Idle) == 0) return false;
  if (strcmp(state, PortalState::Expiring) == 0) {
    // Expiring still has entitlement until Expired transition.
    const long secondsLeft = session["secondsLeft"] | 0L;
    return secondsLeft > 0;
  }

  const bool coinWindow = session["coinWindowActive"] | false;
  const int credits = session["credits"] | 0;
  const long secondsLeft = session["secondsLeft"] | 0L;
  const bool paused = session["paused"] | false;
  const unsigned long lastSeen = session["lastSeen"] | 0UL;
  const bool heartbeatFresh = lastSeen > 0 && nowSec >= lastSeen &&
                              (nowSec - lastSeen) <=
                                  RenzFiConfig::PORTAL_HEARTBEAT_STALE_SEC;

  if (strcmp(state, PortalState::WaitingCoin) == 0) {
    return coinWindow && heartbeatFresh;
  }
  // Open unpaid coin window without WaitingCoin: still browser-presence gated.
  if (coinWindow && credits > 0 &&
      strcmp(state, PortalState::Active) != 0 &&
      strcmp(state, PortalState::Paused) != 0 &&
      strcmp(state, PortalState::Activating) != 0 &&
      strcmp(state, PortalState::ActivationError) != 0) {
    return heartbeatFresh;
  }
  if (credits > 0 && !coinWindow &&
      strcmp(state, PortalState::Active) != 0 &&
      strcmp(state, PortalState::Paused) != 0 &&
      strcmp(state, PortalState::Activating) != 0 &&
      strcmp(state, PortalState::ActivationError) != 0) {
    return false;
  }
  if (strcmp(state, PortalState::Paused) == 0) {
    return secondsLeft > 0;
  }
  if (strcmp(state, PortalState::Activating) == 0 ||
      strcmp(state, PortalState::ActivationError) == 0) {
    return secondsLeft > 0;
  }
  if (strcmp(state, PortalState::Active) == 0) {
    return secondsLeft > 0 || paused;
  }
  return false;
}

bool PortalSessionManager::hasActiveClientSession() const {
  lockState();
  JsonArrayConst arr = _doc["sessions"];
  if (arr.isNull()) {
    unlockState();
    return false;
  }

  const unsigned long nowSec = millis() / 1000;
  for (JsonObjectConst session : arr) {
    if (!isPortalSessionActive(session, nowSec)) continue;
    const char *state = session["sessionState"] | PortalState::Idle;
    if (strcmp(state, PortalState::Active) == 0) {
      unlockState();
      return true;
    }
  }
  unlockState();
  return false;
}

void PortalSessionManager::appendActiveUsers(JsonArray &out, JsonArray &seenMacs) {
  lockState();
  JsonArray arr = _doc["sessions"].as<JsonArray>();
  if (arr.isNull()) {
    unlockState();
    return;
  }

  const unsigned long nowSec = millis() / 1000;
  for (JsonObjectConst session : arr) {
    if (!isPortalSessionActive(session, nowSec)) continue;

    String mac = session["macAddress"] | "";
    if (macAlreadyListed(seenMacs, mac)) continue;

    const char *state = session["sessionState"] | PortalState::Idle;
    const bool paused = session["paused"] | false;
    const bool isPausedState = strcmp(state, PortalState::Paused) == 0;
    const bool isActiveState = strcmp(state, PortalState::Active) == 0;
    const bool isActivating =
        strcmp(state, PortalState::Activating) == 0 ||
        strcmp(state, PortalState::ActivationError) == 0 ||
        strcmp(state, PortalState::Expiring) == 0;
    const long secondsLeft = session["secondsLeft"] | 0L;
    const unsigned long lastSeen = session["lastSeen"] | 0UL;
    const String sourceRaw = session["source"] | "portal";
    const bool isVoucher = sourceRaw == "voucher";

    int remainingMinutes = 0;
    if (isActiveState || isPausedState || isActivating) {
      remainingMinutes = (int)((secondsLeft + 59L) / 60L);
    }

    JsonObject row = out.createNestedObject();
    row["mac"] = mac;
    row["ip"] = session["ipAddress"] | "";
    row["sessionType"] = isVoucher ? "voucher" : "coin";
    row["remainingMinutes"] = remainingMinutes;
    row["credits"] = session["credits"] | 0;
    row["paused"] = paused || isPausedState;
    row["active"] = true;
    row["state"] = portalApiState(session);
    row["source"] = isVoucher ? "voucher" : "portal";
    row["secondsLeft"] = secondsLeft;
    row["portalHeartbeatFresh"] = lastSeen > 0 && nowSec >= lastSeen &&
                                  (nowSec - lastSeen) <=
                                      RenzFiConfig::PORTAL_HEARTBEAT_STALE_SEC;
    markMacSeen(seenMacs, mac);
  }
  unlockState();
}

void PortalSessionManager::cleanupExpired() {
  bool changed = false;

  lockState();
  JsonArray arr = _doc["sessions"].as<JsonArray>();
  if (arr.isNull()) {
    unlockState();
    return;
  }

  const unsigned long nowSec = millis() / 1000;
  constexpr unsigned long EXPIRED_TTL   = 3600UL;
  constexpr unsigned long IDLE_TTL      = RenzFiConfig::PORTAL_IDLE_TTL_SEC;
  constexpr unsigned long HEARTBEAT_TTL = RenzFiConfig::PORTAL_HEARTBEAT_STALE_SEC;

  HeapJsonDocument tmpHeap(RenzFiConfig::JSON_DOC_LARGE);
  DynamicJsonDocument &tmp = tmpHeap.doc();
  JsonArray newArr = tmp["sessions"].to<JsonArray>();

  for (JsonObject s : arr) {
    const unsigned long lastSeen = s["lastSeen"] | 0UL;
    const char* state = s["sessionState"] | PortalState::Idle;
    const bool expired  = strcmp(state, PortalState::Expired) == 0;
    const bool expiring = strcmp(state, PortalState::Expiring) == 0;
    const bool idle = strcmp(state, PortalState::Idle) == 0;
    const bool unpaid = (s["credits"] | 0) > 0 && !(s["coinWindowActive"] | false);
    const bool heartbeatStale = lastSeen > 0 && nowSec > lastSeen &&
                                (nowSec - lastSeen) > HEARTBEAT_TTL;
    const bool waitingLike = strcmp(state, PortalState::WaitingCoin) == 0 ||
                             (s["coinWindowActive"] | false);

    bool staleSince = false;
    if (expired) {
      staleSince = (nowSec > lastSeen) && (nowSec - lastSeen > EXPIRED_TTL);
    } else if (expiring) {
      staleSince = false;
    } else if (idle || unpaid) {
      staleSince = (nowSec > lastSeen) && (nowSec - lastSeen > IDLE_TTL);
    } else if (waitingLike && heartbeatStale) {
      // Browser presence may close an unpaid coin window, but it must never
      // erase an active, activating, or paused entitlement. RouterOS cleanup
      // remains an explicit lifecycle transition.
      staleSince = true;
    }

    if (staleSince) {
      // RC9: never erase expired JSON while RouterOS cleanup is still pending.
      if (expired) {
        const bool cleanupDone = s["routerCleanupComplete"] | false;
        const bool neverHadRouter = !(s["hadRouterAuth"] | false) &&
                                    !(s["routerCleanupPending"] | false) &&
                                    !(s["routerCleanupQueued"] | false);
        if (!cleanupDone && !neverHadRouter) {
          newArr.add(s);
          continue;
        }
      }
      changed = true;
      continue;
    }
    newArr.add(s);
  }

  if (changed) {
    _doc.set(tmp);
    _dirty = true;
  }
  unlockState();

  if (changed) {
    enqueueSaveSessions();
  }
}

// ──────────────────────────────────────────────────────────────────────────────
// Deferred work queue
// ──────────────────────────────────────────────────────────────────────────────

bool PortalSessionManager::enqueueWork(PortalWorkType type, const String& mac,
                                       const char* event, uint32_t firstAttemptMs,
                                       uint32_t generation) {
  lockState();
  if (_workCount >= kPortalWorkQueueCap) {
    unlockState();
    Serial.printf("[portal] deferred queue full (%u) — dropping work type=%u\n",
                  (unsigned)_workCount, (unsigned)type);
    return false;
  }

  PortalWorkItem& item = _workQueue[_workHead];
  _workHead = static_cast<uint8_t>((_workHead + 1) % kPortalWorkQueueCap);
  _workCount++;

  item.type = type;
  item.mac[0]   = '\0';
  item.event[0] = '\0';
  item.saleAmount = 0;
  item.saleMinutes = 0;
  item.saleSessionId[0] = '\0';
  item.saleRecordedAt[0] = '\0';
  item.firstAttemptMs = firstAttemptMs;
  item.generation = generation;

  if (mac.length() > 0) {
    mac.toCharArray(item.mac, sizeof(item.mac));
  }
  if (event != nullptr && event[0] != '\0') {
    strncpy(item.event, event, sizeof(item.event) - 1);
    item.event[sizeof(item.event) - 1] = '\0';
  }
  unlockState();
  return true;
}

bool PortalSessionManager::enqueueSaveSessions() {
  lockState();
  for (uint8_t i = 0; i < _workCount; ++i) {
    const uint8_t idx =
        static_cast<uint8_t>((_workTail + i) % kPortalWorkQueueCap);
    if (_workQueue[idx].type == PortalWorkType::SaveSessions) {
      unlockState();
      return true;
    }
  }
  unlockState();
  return enqueueWork(PortalWorkType::SaveSessions);
}

bool PortalSessionManager::enqueueEmitBus(const char* event) {
  if (event == nullptr || event[0] == '\0') return false;
  lockState();
  for (uint8_t i = 0; i < _workCount; ++i) {
    const uint8_t idx =
        static_cast<uint8_t>((_workTail + i) % kPortalWorkQueueCap);
    if (_workQueue[idx].type == PortalWorkType::EmitBusEvent &&
        strcmp(_workQueue[idx].event, event) == 0) {
      unlockState();
      return true;
    }
  }
  unlockState();
  return enqueueWork(PortalWorkType::EmitBusEvent, String(), event);
}

bool PortalSessionManager::enqueueRecordSale(const String& mac, int amount,
                                             int minutes,
                                             const String& sessionId,
                                             const String& recordedAt) {
  lockState();
  if (hasPendingRecordSaleUnlocked(mac, sessionId)) {
    unlockState();
    return true;
  }
  if (_workCount >= kPortalWorkQueueCap) {
    unlockState();
    Serial.println("[portal] deferred queue full — sale not queued");
    return false;
  }

  PortalWorkItem& item = _workQueue[_workHead];
  _workHead = static_cast<uint8_t>((_workHead + 1) % kPortalWorkQueueCap);
  _workCount++;

  item.type = PortalWorkType::RecordSale;
  item.mac[0] = '\0';
  item.event[0] = '\0';
  item.saleAmount = amount;
  item.saleMinutes = minutes;
  item.saleSessionId[0] = '\0';
  item.saleRecordedAt[0] = '\0';

  if (mac.length() > 0) mac.toCharArray(item.mac, sizeof(item.mac));
  if (sessionId.length() > 0) {
    sessionId.toCharArray(item.saleSessionId, sizeof(item.saleSessionId));
  }
  if (recordedAt.length() > 0) {
    recordedAt.toCharArray(item.saleRecordedAt, sizeof(item.saleRecordedAt));
  }
  unlockState();
  return true;
}

bool PortalSessionManager::enqueueActivateSession(const String& mac) {
  uint32_t gen = 0;
  lockState();
  JsonObject session = findSessionUnlocked(mac);
  if (!session.isNull() && alreadyAuthorizedThisGeneration(session)) {
    session["activationRetryPending"] = false;
    unlockState();
    Serial.printf(
        "[portal-activate] mac=%s already authorized — not queued\n",
        mac.c_str());
    return true;
  }
  if (hasPendingActivationUnlocked(mac)) {
    unlockState();
    return true;
  }
  if (!session.isNull()) gen = sessionGenerationOf(session);
  unlockState();
  return enqueueWork(PortalWorkType::ActivateSession, mac, nullptr, 0, gen);
}

void PortalSessionManager::deferRouterDisconnect(const String& mac) {
  enqueueWork(PortalWorkType::ExpireSession, mac);
}

void PortalSessionManager::emitBusEvent(const char* event) {
  if (_events && event) _events->emit(event);
}

void PortalSessionManager::processDeferredWork() {
  PortalWorkItem item;
  lockState();
  if (_workCount == 0) {
    unlockState();
    return;
  }

  // Customer session SSE must not sit behind owner bus events or SD saves.
  // Router jobs (activate/pause/expire) and RecordSale keep FIFO order.
  uint8_t pickOffset = 0;
  for (uint8_t i = 0; i < _workCount; ++i) {
    const uint8_t idx =
        static_cast<uint8_t>((_workTail + i) % kPortalWorkQueueCap);
    if (_workQueue[idx].type == PortalWorkType::EmitSessionEvent) {
      pickOffset = i;
      break;
    }
  }
  if (pickOffset == 0) {
    item = _workQueue[_workTail];
    _workTail = static_cast<uint8_t>((_workTail + 1) % kPortalWorkQueueCap);
  } else {
    const uint8_t pick =
        static_cast<uint8_t>((_workTail + pickOffset) % kPortalWorkQueueCap);
    item = _workQueue[pick];
    for (uint8_t i = pickOffset; i + 1 < _workCount; ++i) {
      const uint8_t dst =
          static_cast<uint8_t>((_workTail + i) % kPortalWorkQueueCap);
      const uint8_t src =
          static_cast<uint8_t>((_workTail + i + 1) % kPortalWorkQueueCap);
      _workQueue[dst] = _workQueue[src];
    }
    _workHead = static_cast<uint8_t>(
        (_workHead + kPortalWorkQueueCap - 1) % kPortalWorkQueueCap);
  }
  _workCount--;
  const uint8_t remain = _workCount;
  unlockState();
  if (coinLatencyTrace().armed) {
    Serial.printf("[coin-latency] drain type=%u event=%s remain=%u\n",
                  static_cast<unsigned>(item.type),
                  item.event[0] != '\0' ? item.event : "-",
                  static_cast<unsigned>(remain));
  }

  const String mac = item.mac[0] != '\0' ? String(item.mac) : String();

  switch (item.type) {
    case PortalWorkType::SaveSessions:
      saveToSD(true);
      break;

    case PortalWorkType::ActivateSession:
      onSessionActivated(mac, item.firstAttemptMs, item.generation);
      break;

    case PortalWorkType::PauseSession:
      onSessionPaused(mac, item.firstAttemptMs, item.generation);
      break;

    case PortalWorkType::ExpireSession:
      onSessionExpired(mac, item.firstAttemptMs, item.generation);
      break;

    case PortalWorkType::EmitSessionEvent:
      emitSessionEvent(mac, item.event);
      break;

    case PortalWorkType::EmitBusEvent:
      emitBusEvent(item.event);
      break;

    case PortalWorkType::RecordSale: {
      if (!_sessions) break;
      String ip;
      String profile;
      lockState();
      JsonObject portalSession = findSessionUnlocked(mac);
      if (!portalSession.isNull()) {
        ip = String(portalSession["ipAddress"] | "");
        profile = String(portalSession["hotspotProfile"] | "");
      }
      unlockState();
      SaleRecord sale;
      sale.id = String("psale-") + makeSessionId();
      sale.timestamp = String("uptime-ms:") + millis();
      sale.recordedAt = item.saleRecordedAt;
      sale.amount = item.saleAmount;
      sale.sessionId = item.saleSessionId;
      sale.macAddress = item.mac;
      sale.ipAddress = ip;
      sale.paymentType = "coin";
      sale.durationMinutes = item.saleMinutes;
      sale.credits = item.saleAmount;
      sale.profile = profile;
      sale.status = "pending_activation";
      if (!_sessions->upsertSale(sale) && _logger) {
        _logger->error("portal",
                       String("deferred sale failed for ") + item.mac);
      }
      break;
    }
  }
}

// ──────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ──────────────────────────────────────────────────────────────────────────────

void PortalSessionManager::pushTickEffect(PortalWorkType type, const String& mac,
                                          const char* event, uint32_t generation) {
  if (_tickEffectCount >= kMaxTickEffects) return;
  PortalTickEffect& fx = _tickEffects[_tickEffectCount++];
  fx.type = type;
  fx.mac[0] = '\0';
  fx.event[0] = '\0';
  fx.generation = generation;
  if (mac.length() > 0) mac.toCharArray(fx.mac, sizeof(fx.mac));
  if (event != nullptr && event[0] != '\0') {
    strncpy(fx.event, event, sizeof(fx.event) - 1);
    fx.event[sizeof(fx.event) - 1] = '\0';
  }
}

void PortalSessionManager::flushTickEffects() {
  const size_t count = _tickEffectCount;
  PortalTickEffect local[kMaxTickEffects];
  for (size_t i = 0; i < count; ++i) local[i] = _tickEffects[i];
  _tickEffectCount = 0;

  for (size_t i = 0; i < count; ++i) {
    const String mac = local[i].mac[0] != '\0' ? String(local[i].mac) : String();
    switch (local[i].type) {
      case PortalWorkType::ActivateSession:
        enqueueWork(PortalWorkType::ActivateSession, mac, nullptr, 0,
                    local[i].generation);
        break;
      case PortalWorkType::ExpireSession:
        enqueueWork(PortalWorkType::ExpireSession, mac, nullptr, 0,
                    local[i].generation);
        break;
      case PortalWorkType::PauseSession:
        enqueueWork(PortalWorkType::PauseSession, mac, nullptr, 0,
                    local[i].generation);
        break;
      case PortalWorkType::EmitSessionEvent:
        enqueueWork(PortalWorkType::EmitSessionEvent, mac, local[i].event);
        break;
      case PortalWorkType::EmitBusEvent:
        enqueueEmitBus(local[i].event);
        break;
      default:
        break;
    }
  }
}

void PortalSessionManager::tickSessions() {
  _tickEffectCount = 0;

  lockState();
  JsonArray arr = _doc["sessions"].as<JsonArray>();
  if (arr.isNull()) {
    unlockState();
    return;
  }

  bool changed = false;
  const time_t wallNow = time(nullptr);

  for (JsonObject session : arr) {
    if (session["coinWindowActive"] | false) {
      int rem = session["coinWindowRemaining"] | 0;
      if (rem > 0) {
        session["coinWindowRemaining"] = rem - 1;
        changed = true;
      } else {
        session["coinWindowActive"]    = false;
        session["coinWindowRemaining"] = 0;
        const String mac = String(session["macAddress"] | "");
        if (_activeInsertMac == mac) _activeInsertMac = "";

        const int credits = session["credits"] | 0;
        if (credits > 0) {
          session["sessionState"] = PortalState::Idle;
          session["connected"]    = false;
          pushTickEffect(PortalWorkType::EmitSessionEvent, mac,
                         "portal.coin.window_closed");
        } else if (session["hadRouterAuth"] | false) {
          session["sessionState"]          = PortalState::Expiring;
          session["connected"]             = false;
          session["routerCleanupQueued"]   = true;
          session["routerCleanupPending"]  = true;
          session["routerCleanupComplete"] = false;
          pushTickEffect(PortalWorkType::ExpireSession, mac, "coin_window_expired",
                         sessionGenerationOf(session));
          pushTickEffect(PortalWorkType::EmitSessionEvent, mac,
                         "portal.session.expired");
        } else {
          session["sessionState"] = PortalState::Idle;
          session["connected"]    = false;
          pushTickEffect(PortalWorkType::EmitSessionEvent, mac,
                         "portal.coin.window_closed");
        }
        pushTickEffect(PortalWorkType::EmitBusEvent, String(), "sessions.changed");
        pushTickEffect(PortalWorkType::EmitBusEvent, String(), "users.active");
        pushTickEffect(PortalWorkType::EmitBusEvent, String(), "system.status");
        changed = true;
      }
    }

    const char* stateStr = session["sessionState"] | PortalState::Idle;
    const bool isActive = strcmp(stateStr, PortalState::Active) == 0;
    const bool isPaused = session["paused"] | false;

    // Paid session stuck in Activating with no RouterWorker outcome (mailbox
    // drop / hang). Preserve secondsLeft and surface an exact recoverable error.
    if (strcmp(stateStr, PortalState::Activating) == 0 &&
        (session["routerAuthPending"] | false) &&
        (session["secondsLeft"] | 0L) > 0 &&
        !(session["activationRetryPending"] | false)) {
      const unsigned long nowSec = millis() / 1000;
      unsigned long started = session["activationStartedAt"] | 0UL;
      if (started == 0UL) {
        session["activationStartedAt"] = nowSec;
        changed = true;
      } else if (nowSec >= started + kActivationPendingTimeoutSec) {
        const String mac = String(session["macAddress"] | "");
        session["routerAuthPending"] = false;
        session["activationError"] = true;
        session["activationErrorReason"] =
            "Activation timed out — purchased time preserved. Tap RETRY "
            "INTERNET.";
        session["sessionState"] = PortalState::ActivationError;
        session.remove("activationStartedAt");
        // Seed the normal activation_error retry schedule.
        session["activationRetryAt"] = nowSec + kActivationRetryDelaySec;
        Serial.printf(
            "[portal-activate] mac=%s ACTIVATING watchdog timeout (%lus)\n",
            mac.c_str(), (unsigned long)kActivationPendingTimeoutSec);
        pushTickEffect(PortalWorkType::EmitSessionEvent, mac,
                       "portal.session.activation_failed");
        pushTickEffect(PortalWorkType::EmitBusEvent, String(), "sessions.changed");
        pushTickEffect(PortalWorkType::EmitBusEvent, String(), "users.active");
        pushTickEffect(PortalWorkType::EmitBusEvent, String(), "system.status");
        changed = true;
      }
    }

    // A paid session must never park permanently in activation_error. Retry a
    // bounded burst (kActivationRetryLimit), then cool down and retry again so
    // Internet is granted once the underlying problem is fixed. Purchased time
    // (secondsLeft) is never cleared here.
    // Voucher: wall-clock serviceExpiresAt also gates retry (not secondsLeft alone).
    if (strcmp(stateStr, PortalState::ActivationError) == 0 &&
        (session["secondsLeft"] | 0L) > 0 &&
        !(session["routerAuthPending"] | false) &&
        !(session["activationRetryPending"] | false)) {
      bool voucherPastDue = false;
      if (strcmp(session["source"] | "portal", "voucher") == 0) {
        long wallRemaining = -1;
        if (voucherWallRemaining(session, wallRemaining) &&
            wallRemaining == 0) {
          voucherPastDue = true;
        }
      }
      if (voucherPastDue) {
        if (!(session["routerCleanupQueued"] | false) &&
            !(session["routerCleanupComplete"] | false)) {
          session["sessionState"] = PortalState::Expiring;
          session["connected"] = false;
          session["secondsLeft"] = 0;
          session["routerCleanupQueued"] = true;
          session["routerCleanupPending"] = true;
          session["routerCleanupComplete"] = false;
          session["terminationReason"] = "time_expired";
          session["terminationActor"] = "system";
          const String mac = String(session["macAddress"] | "");
          const String code = String(session["voucherCode"] | "");
          const String exp = String(session["serviceExpiresAt"] | "");
          Serial.printf(
              "[voucher-expiry] mac=%s code=%s expiresAt=%s remaining=0 "
              "action=expire\n",
              mac.c_str(), code.c_str(), exp.c_str());
          pushTickEffect(PortalWorkType::ExpireSession, mac, "time_expired");
          pushTickEffect(PortalWorkType::EmitSessionEvent, mac,
                         "portal.session.expired");
          pushTickEffect(PortalWorkType::EmitBusEvent, String(),
                         "sessions.changed");
          pushTickEffect(PortalWorkType::EmitBusEvent, String(),
                         "users.active");
          changed = true;
        }
      } else {
      const int attempts = session["activationAttempts"] | 0;
      const unsigned long nowSec = millis() / 1000;
      const unsigned long retryAt = session["activationRetryAt"] | 0UL;
      if (attempts >= kActivationRetryLimit) {
        if (retryAt == 0UL) {
          session["activationRetryAt"] = nowSec + kActivationRetryCooldownSec;
          changed = true;
        } else if (nowSec >= retryAt) {
          // Start a fresh burst after cooldown — credentials may now be ready.
          session["activationAttempts"] = 0;
          session["activationRetryAt"]  = 0;
          changed = true;
          Serial.printf(
              "[portal-activate] mac=%s cooldown elapsed — retry budget reset\n",
              String(session["macAddress"] | "").c_str());
        }
      } else if (retryAt == 0UL) {
        session["activationRetryAt"] = nowSec + kActivationRetryDelaySec;
        changed = true;
      } else if (nowSec >= retryAt) {
        if (!RouterApiTransportGate::allowsHotspotActivate()) {
          // Preserve purchased time; do not burn retry budget or hammer RouterOS.
          static uint32_t s_lastActDeferLogMs = 0;
          const uint32_t deferNow = millis();
          if (s_lastActDeferLogMs == 0 ||
              (deferNow - s_lastActDeferLogMs) >= 30000U) {
            s_lastActDeferLogMs = deferNow;
            Serial.printf(
                "[router-worker] activate deferred reason=router_unavailable "
                "mac=%s health=%s\n",
                String(session["macAddress"] | "").c_str(),
                RouterApiTransportGate::healthLabel());
          }
        } else {
        session["activationAttempts"] = attempts + 1;
        session["activationRetryAt"]  = 0;
        session["sessionState"]       = PortalState::Activating;
        session["activationError"]    = false;
        session["routerAuthPending"]  = true;
        session["activationStartedAt"] = nowSec;
        const String mac = String(session["macAddress"] | "");
        Serial.printf("[portal-activate] mac=%s auto-retry %d/%d\n", mac.c_str(),
                      attempts + 1, kActivationRetryLimit);
        pushTickEffect(PortalWorkType::ActivateSession, mac, nullptr,
                       sessionGenerationOf(session));
        pushTickEffect(PortalWorkType::EmitBusEvent, String(), "sessions.changed");
        changed = true;
        }
      }
      }
    }

    // Absolute voucher expiry: enqueue ONE ExpireSession even when NOT Active.
    // Do not call RouterOS from tick — worker deauth only.
    if (strcmp(session["source"] | "portal", "voucher") == 0 &&
        strcmp(stateStr, PortalState::Expired) != 0 &&
        strcmp(stateStr, PortalState::Expiring) != 0 &&
        !(session["routerCleanupQueued"] | false) &&
        !(session["routerCleanupComplete"] | false)) {
      long wallRemaining = -1;
      if (voucherWallRemaining(session, wallRemaining) && wallRemaining == 0) {
        session["sessionState"] = PortalState::Expiring;
        session["connected"] = false;
        session["secondsLeft"] = 0;
        session["routerCleanupQueued"] = true;
        session["routerCleanupPending"] = true;
        session["routerCleanupComplete"] = false;
        session["terminationReason"] = "time_expired";
        session["terminationActor"] = "system";
        const String mac = String(session["macAddress"] | "");
        const String code = String(session["voucherCode"] | "");
        const String exp = String(session["serviceExpiresAt"] | "");
        Serial.printf(
            "[voucher-expiry] mac=%s code=%s expiresAt=%s remaining=0 "
            "action=expire\n",
            mac.c_str(), code.c_str(), exp.c_str());
        pushTickEffect(PortalWorkType::ExpireSession, mac, "time_expired");
        pushTickEffect(PortalWorkType::EmitSessionEvent, mac,
                       "portal.session.expired");
        pushTickEffect(PortalWorkType::EmitBusEvent, String(),
                       "sessions.changed");
        pushTickEffect(PortalWorkType::EmitBusEvent, String(), "users.active");
        changed = true;
      } else if (wallRemaining > 0) {
        const long secs = session["secondsLeft"] | 0L;
        if (wallRemaining < secs) {
          session["secondsLeft"] = wallRemaining;
          changed = true;
        }
      }
    }

    if (isActive && !isPaused && (session["connected"] | false)) {
      long secs = session["secondsLeft"] | 0L;
      if (strcmp(session["source"] | "portal", "voucher") == 0) {
        const time_t expiryEpoch =
            static_cast<time_t>(session["serviceExpiresEpoch"] | 0UL);
        if (wallNow >= 1704067200 && expiryEpoch > 0) {
          const long absoluteRemaining =
              expiryEpoch > wallNow
                  ? static_cast<long>(expiryEpoch - wallNow)
                  : 0L;
          if (absoluteRemaining < secs) {
            secs = absoluteRemaining;
            session["secondsLeft"] = secs;
            changed = true;
          }
        }
      } else {
        const uint32_t expiresAtMs = session["expiresAtMs"] | 0U;
        if (expiresAtMs > 0) {
          const long clockRemaining = remainingFromExpiresMs(expiresAtMs, millis());
          if (clockRemaining >= 0) {
            secs = clockRemaining;
            session["secondsLeft"] = secs;
            changed = true;
          }
        }
      }
      if (secs > 0) {
        if ((session["expiresAtMs"] | 0U) == 0 &&
            strcmp(session["source"] | "portal", "voucher") != 0) {
          session["secondsLeft"] = secs - 1;
        }
        session["connectedSeconds"] =
            (session["connectedSeconds"] | 0UL) + 1UL;
        changed = true;
      } else if (!(session["routerCleanupQueued"] | false) &&
                 !(session["routerCleanupComplete"] | false)) {
        // ONE expiration event = ONE router job (idempotency guard).
        session["sessionState"]          = PortalState::Expiring;
        session["connected"]             = false;
        clearSessionClockUnlocked(session);
        session["secondsLeft"]           = 0;
        session["routerCleanupQueued"]   = true;
        session["routerCleanupPending"]  = true;
        session["routerCleanupComplete"] = false;
        session["terminationReason"]     = "time_expired";
        session["terminationActor"]      = "system";
        const String mac = String(session["macAddress"] | "");
        pushTickEffect(PortalWorkType::ExpireSession, mac, "time_expired",
                       sessionGenerationOf(session));
        pushTickEffect(PortalWorkType::EmitSessionEvent, mac,
                       "portal.session.expired");
        pushTickEffect(PortalWorkType::EmitBusEvent, String(), "sessions.changed");
        pushTickEffect(PortalWorkType::EmitBusEvent, String(), "users.active");
        changed = true;
      }
    }
  }

  if (changed) _dirty = true;
  unlockState();

  if (changed) flushTickEffects();
}

void PortalSessionManager::maybeEnqueueActiveVerify() {
  // Non-aggressive: at most one Active print every 60s for one Connected MAC.
  // Never from async_tcp — only loop() → router_worker.
  // Zero Connected sessions ⇒ never verify. Unhealthy RouterOS ⇒ never verify.
  if (!_routerWorker) return;
  if (!RouterApiTransportGate::allowsHotspotVerify()) {
    static uint32_t s_lastVerifySkipLogMs = 0;
    const uint32_t nowSkip = millis();
    if (s_lastVerifySkipLogMs == 0 ||
        (nowSkip - s_lastVerifySkipLogMs) >= 30000U) {
      s_lastVerifySkipLogMs = nowSkip;
      Serial.printf(
          "[router-worker] verify skipped reason=router_unavailable health=%s\n",
          RouterApiTransportGate::healthLabel());
    }
    return;
  }
  constexpr uint32_t kVerifyIntervalMs = 60000;
  const uint32_t now = millis();
  if (_lastActiveVerifyMs != 0 &&
      (now - _lastActiveVerifyMs) < kVerifyIntervalMs) {
    return;
  }

  String mac;
  lockState();
  JsonArray arr = _doc["sessions"].as<JsonArray>();
  if (!arr.isNull()) {
    for (JsonObject session : arr) {
      const char* state = session["sessionState"] | PortalState::Idle;
      if (strcmp(state, PortalState::Active) != 0) continue;
      if (!(session["connected"] | false)) continue;
      if (session["paused"] | false) continue;
      if ((session["secondsLeft"] | 0L) <= 0) continue;
      if (session["routerAuthPending"] | false) continue;
      if (session["routerPausePending"] | false) continue;
      if (session["routerCleanupPending"] | false) continue;
      if (session["routerCleanupQueued"] | false) continue;
      const String candidate = String(session["macAddress"] | "");
      if (candidate.isEmpty()) continue;
      // Post-activation trust window: skip Verify for the MAC just activated.
      if (_lastActivateSuccessMs != 0 &&
          (now - _lastActivateSuccessMs) <
              RenzFiConfig::ROUTER_ACTIVATE_TRUST_WINDOW_MS &&
          strcmp(candidate.c_str(), _lastActivateSuccessMac) == 0) {
        continue;
      }
      mac = candidate;
      break;
    }
  }
  unlockState();

  if (mac.isEmpty()) return;
  uint32_t verifyGen = 0;
  lockState();
  JsonObject verifySession = findSessionUnlocked(mac);
  if (!verifySession.isNull()) verifyGen = sessionGenerationOf(verifySession);
  unlockState();
  if (!_routerWorker->tryEnqueueVerifyHotspotActive(mac, verifyGen)) return;
  _lastActiveVerifyMs = now;
  mac.toCharArray(_pendingVerifyMac, sizeof(_pendingVerifyMac));
  Serial.printf("[portal-verify] mac=%s queued\n", mac.c_str());
}

bool PortalSessionManager::needsRouterOsWork() const {
  lockState();
  JsonArrayConst arr = _doc["sessions"];
  if (arr.isNull()) {
    unlockState();
    return false;
  }
  for (JsonObjectConst session : arr) {
    const char* state = session["sessionState"] | PortalState::Idle;
    if (session["activationRetryPending"] | false) {
      unlockState();
      return true;
    }
    if (session["pauseRetryPending"] | false) {
      unlockState();
      return true;
    }
    if (session["cleanupRetryPending"] | false) {
      unlockState();
      return true;
    }
    if (session["routerAuthPending"] | false) {
      unlockState();
      return true;
    }
    if (session["routerPausePending"] | false) {
      unlockState();
      return true;
    }
    if (session["routerCleanupQueued"] | false) {
      unlockState();
      return true;
    }
    if (session["routerCleanupPending"] | false) {
      unlockState();
      return true;
    }
    if (strcmp(state, PortalState::Activating) == 0) {
      unlockState();
      return true;
    }
    if (strcmp(state, PortalState::ActivationError) == 0 &&
        (session["secondsLeft"] | 0L) > 0) {
      unlockState();
      return true;
    }
    if (strcmp(state, PortalState::Expiring) == 0) {
      unlockState();
      return true;
    }
    if (strcmp(state, PortalState::Active) == 0 &&
        (session["connected"] | false) &&
        !(session["paused"] | false) &&
        (session["secondsLeft"] | 0L) > 0) {
      unlockState();
      return true;
    }
  }
  unlockState();
  return false;
}

bool PortalSessionManager::needsHealthRecoveryProbe() const {
  lockState();
  JsonArrayConst arr = _doc["sessions"];
  if (arr.isNull()) {
    unlockState();
    return false;
  }
  for (JsonObjectConst session : arr) {
    const char* state = session["sessionState"] | PortalState::Idle;
    if (session["activationRetryPending"] | false) {
      unlockState();
      return true;
    }
    if (session["pauseRetryPending"] | false) {
      unlockState();
      return true;
    }
    if (session["cleanupRetryPending"] | false) {
      unlockState();
      return true;
    }
    if (session["routerAuthPending"] | false) {
      unlockState();
      return true;
    }
    if (session["routerPausePending"] | false) {
      unlockState();
      return true;
    }
    if (session["routerCleanupQueued"] | false) {
      unlockState();
      return true;
    }
    if (session["routerCleanupPending"] | false) {
      unlockState();
      return true;
    }
    if (strcmp(state, PortalState::Activating) == 0) {
      unlockState();
      return true;
    }
    if (strcmp(state, PortalState::ActivationError) == 0 &&
        (session["secondsLeft"] | 0L) > 0) {
      unlockState();
      return true;
    }
    if (strcmp(state, PortalState::Expiring) == 0) {
      unlockState();
      return true;
    }
  }
  unlockState();
  return false;
}

bool PortalSessionManager::hasCustomerActivatePending() const {
  lockState();
  JsonArrayConst arr = _doc["sessions"];
  if (arr.isNull()) {
    unlockState();
    return false;
  }
  for (JsonObjectConst session : arr) {
    const char* state = session["sessionState"] | PortalState::Idle;
    if (session["activationRetryPending"] | false) {
      unlockState();
      return true;
    }
    if (session["routerAuthPending"] | false) {
      unlockState();
      return true;
    }
    if (strcmp(state, PortalState::Activating) == 0) {
      unlockState();
      return true;
    }
    if (strcmp(state, PortalState::ActivationError) == 0 &&
        (session["secondsLeft"] | 0L) > 0) {
      unlockState();
      return true;
    }
  }
  unlockState();
  return false;
}

void PortalSessionManager::emitSessionEvent(const String& mac,
                                             const char* event) {
  if (!_events || !event) return;

  const bool coinCredit =
      event && strcmp(event, "portal.coin.credit") == 0;
  if (coinCredit) coinLatencyTrace().markT5Emitted();

  String payload;
  HeapJsonDocument snapshotHeap(RenzFiConfig::JSON_DOC_SMALL);
  DynamicJsonDocument &snapshot = snapshotHeap.doc();
  bool found = false;

  lockState();
  JsonObject session = findSessionUnlocked(mac);
  if (!session.isNull()) {
    snapshot.set(session);
    found = true;
  }
  unlockState();

  if (found) {
    // Same shape the portal receives from GET /api/portal/session so an SSE
    // push can be applied directly — no follow-up HTTP round trip.
    enrichSessionPurchasedMinutes(snapshot);
    enrichSessionCapabilities(snapshot);
    serializeJson(snapshot, payload);
  }

  if (payload.length() > 0) {
    _events->emit(event, payload);
  } else {
    _events->emit(event);
  }
  if (coinCredit) coinLatencyTrace().markT6SseSent();
}

void PortalSessionManager::recoverSessionsAfterReboot() {
  _tickEffectCount = 0;
  lockState();
  _activeInsertMac = "";

  JsonArray arr = _doc["sessions"].as<JsonArray>();
  if (arr.isNull()) {
    unlockState();
    return;
  }

  bool changed = false;
  for (JsonObject session : arr) {
    const bool hadWindow = session["coinWindowActive"] | false;
    const int credits = session["credits"] | 0;
    const String mac = String(session["macAddress"] | "");
    if (hadWindow) {
      session["coinWindowActive"]    = false;
      session["coinWindowRemaining"] = 0;
      session["sessionState"]        = PortalState::Idle;
      session["connected"]           = false;
      changed = true;
      Serial.printf(
          "[portal] Boot recovery: closed coin window for %s (credits=%d "
          "preserved — customer must reopen insert flow)\n",
          mac.c_str(), credits);
    }

    const char* state =
        session["sessionState"] | PortalState::Idle;
    long secondsLeft = session["secondsLeft"] | 0L;

    // Voucher absolute expiry survives reboot: never re-activate past deadline.
    if (strcmp(session["source"] | "portal", "voucher") == 0) {
      long wallRemaining = -1;
      if (voucherWallRemaining(session, wallRemaining)) {
        if (wallRemaining == 0) {
          secondsLeft = 0;
          session["secondsLeft"] = 0;
          if (strcmp(state, PortalState::Expired) != 0) {
            session["sessionState"] = PortalState::Expiring;
            session["connected"] = false;
            session["routerCleanupQueued"] = true;
            session["routerCleanupPending"] = true;
            session["routerCleanupComplete"] = false;
            session["terminationReason"] = "time_expired";
            session["terminationActor"] = "system";
            const String code = String(session["voucherCode"] | "");
            const String exp = String(session["serviceExpiresAt"] | "");
            Serial.printf(
                "[voucher-expiry] mac=%s code=%s expiresAt=%s remaining=0 "
                "action=expire\n",
                mac.c_str(), code.c_str(), exp.c_str());
            pushTickEffect(PortalWorkType::ExpireSession, mac, "time_expired");
            changed = true;
          }
          continue;
        }
        if (wallRemaining > 0 && wallRemaining < secondsLeft) {
          secondsLeft = wallRemaining;
          session["secondsLeft"] = wallRemaining;
          changed = true;
        }
      }
    }

    if (secondsLeft > 0 &&
        (strcmp(state, PortalState::Activating) == 0 ||
         (strcmp(state, PortalState::Paused) == 0 &&
          (session["resumePending"] | false)))) {
      session["routerAuthPending"] = true;
      session["activationRetryPending"] = false;
      pushTickEffect(PortalWorkType::ActivateSession, mac, nullptr,
                     sessionGenerationOf(session));
      changed = true;
    } else if ((strcmp(state, PortalState::Expiring) == 0 ||
                strcmp(state, PortalState::Expired) == 0) &&
               !(session["routerCleanupComplete"] | false) &&
               ((session["routerCleanupPending"] | false) ||
                (session["routerCleanupQueued"] | false) ||
                (session["hadRouterAuth"] | false))) {
      session["routerCleanupPending"] = true;
      session["routerCleanupQueued"] = true;
      session["cleanupRetryPending"] = false;
      pushTickEffect(PortalWorkType::ExpireSession, mac, nullptr);
      changed = true;
    }
  }

  if (changed) _dirty = true;
  unlockState();

  if (changed) {
    flushTickEffects();
    enqueueSaveSessions();
    if (_logger) {
      _logger->info("portal",
                    "Boot recovery: closed stale coin windows; credits preserved");
    }
  }
}

void PortalSessionManager::closeOtherCoinWindowsUnlocked(const String& exceptMac) {
  JsonArray arr = _doc["sessions"].as<JsonArray>();
  if (arr.isNull()) return;

  for (JsonObject session : arr) {
    const String mac = String(session["macAddress"] | "");
    if (mac.isEmpty() || mac == exceptMac) continue;
    if (!(session["coinWindowActive"] | false)) continue;

    session["coinWindowActive"]    = false;
    session["coinWindowRemaining"] = 0;
    if (strcmp(session["sessionState"] | PortalState::Idle,
               PortalState::WaitingCoin) == 0) {
      session["sessionState"] = PortalState::Idle;
    }
    if (_activeInsertMac == mac) _activeInsertMac = "";
    _dirty = true;
  }
}

bool PortalSessionManager::hasPendingActivationUnlocked(const String& mac) const {
  for (uint8_t i = 0; i < _workCount; ++i) {
    const uint8_t idx =
        static_cast<uint8_t>((_workTail + i) % kPortalWorkQueueCap);
    if (_workQueue[idx].type == PortalWorkType::ActivateSession &&
        mac.equalsIgnoreCase(_workQueue[idx].mac)) {
      return true;
    }
  }
  return false;
}

bool PortalSessionManager::hasPendingRecordSaleUnlocked(
    const String& mac, const String& sessionId) const {
  for (uint8_t i = 0; i < _workCount; ++i) {
    const uint8_t idx =
        static_cast<uint8_t>((_workTail + i) % kPortalWorkQueueCap);
    if (_workQueue[idx].type != PortalWorkType::RecordSale) continue;
    if (mac.equalsIgnoreCase(_workQueue[idx].mac)) return true;
    if (sessionId.length() > 0 &&
        sessionId.equalsIgnoreCase(_workQueue[idx].saleSessionId)) {
      return true;
    }
  }
  return false;
}

bool PortalSessionManager::loadFromSD() {
  if (!_storage) return false;
  DmaMemoryMonitor::ScopedProbe dmaProbe("portal-load");
  _doc.clear();
  if (!_storage->readJson(RenzFiConfig::PORTAL_SESSIONS_FILE, _doc)) {
    _doc["sessions"].to<JsonArray>();
    return false;
  }
  if (!_doc["sessions"].is<JsonArray>()) {
    _doc["sessions"].to<JsonArray>();
  }
  return true;
}

bool PortalSessionManager::saveToSD(bool immediate) {
  if (!_storage) return false;
  DmaMemoryMonitor::ScopedProbe dmaProbe("portal-save");

  HeapJsonDocument copyHeap(RenzFiConfig::JSON_DOC_LARGE);
  DynamicJsonDocument &copy = copyHeap.doc();
  bool shouldWrite = false;

  lockState();
  if (!_dirty && !immediate) {
    unlockState();
    return true;
  }
  copy.set(_doc);
  shouldWrite = true;
  unlockState();

  if (!shouldWrite) return true;

  const bool ok =
      _storage->writeJson(RenzFiConfig::PORTAL_SESSIONS_FILE, copy, immediate);
  if (ok) {
    lockState();
    _dirty = false;
    _lastSaveMs = millis();
    unlockState();
  }
  return ok;
}

JsonObject PortalSessionManager::findSessionUnlocked(const String& mac) {
  JsonArray arr = _doc["sessions"].as<JsonArray>();
  if (arr.isNull()) return JsonObject();
  for (JsonObject s : arr) {
    if (String(s["macAddress"] | "") == mac) return s;
  }
  return JsonObject();
}

JsonObject PortalSessionManager::findOrCreateUnlocked(const String& mac,
                                                        const String& ip) {
  JsonObject existing = findSessionUnlocked(mac);
  if (!existing.isNull()) {
    if (!ip.isEmpty() && String(existing["ipAddress"] | "") != ip) {
      existing["ipAddress"] = ip;
      _dirty = true;
    }
    return existing;
  }

  JsonArray arr = _doc["sessions"].as<JsonArray>();
  if (arr.isNull()) arr = _doc["sessions"].to<JsonArray>();

  JsonObject s        = arr.createNestedObject();
  const unsigned long nowSec = millis() / 1000;

  s["sessionId"]           = makeSessionId();
  s["macAddress"]          = mac;
  s["ipAddress"]           = ip;
  s["credits"]             = 0;
  s["insertedAmount"]      = 0;
  s["purchasedMinutes"]    = 0;
  s["secondsLeft"]         = 0;
  s["paused"]              = false;
  s["connected"]           = false;
  s["coinWindowActive"]    = false;
  s["coinWindowRemaining"] = 0;
  s["createdAt"]           = nowSec;
  s["updatedAt"]           = nowSec;
  s["lastSeen"]            = nowSec;
  s["source"]              = "portal";
  s["sessionState"]        = PortalState::Idle;
  s["sessionGeneration"]   = 0;

  _dirty = true;
  return s;
}

String PortalSessionManager::makeSessionId() {
  return String(esp_random(), HEX) + String(millis(), HEX);
}

bool PortalSessionManager::recycleExpiredSessionUnlocked(JsonObject session) {
  if (session.isNull()) return false;
  // Voucher records keep their terminal state so the portal can explain why the
  // code stopped working.
  if (strcmp(session["source"] | "portal", "voucher") == 0) return false;
  // Anything still owed to the customer (unspent credits, an open insert
  // window, leftover time) must survive untouched.
  if ((session["credits"] | 0) > 0) return false;
  if ((session["secondsLeft"] | 0L) > 0) return false;
  if (session["coinWindowActive"] | false) return false;
  if (!(session["routerCleanupComplete"] | false)) return false;

  const unsigned long nowSec = millis() / 1000;
  // Fresh id: the previous one is already closed in sales/history.
  session["sessionId"]             = makeSessionId();
  session["sessionState"]          = PortalState::Idle;
  session["insertedAmount"]        = 0;
  session["purchasedMinutes"]      = 0;
  session["paused"]                = false;
  session["connected"]             = false;
  session["activationError"]       = false;
  session["activationAttempts"]    = 0;
  session["pausesUsed"]            = 0;
  session["coinWindowRemaining"]   = 0;
  session["routerAuthPending"]     = false;
  session["routerPausePending"]    = false;
  session["resumePending"]         = false;
  session["routerCleanupQueued"]   = false;
  session["routerCleanupPending"]  = false;
  session["cleanupRetryPending"]   = false;
  session["cleanupRetryCount"]     = 0;
  session["hadRouterAuth"]         = false;
  session["connectedSeconds"]      = 0;
  clearSessionClockUnlocked(session);
  session["updatedAt"]             = nowSec;
  session.remove("activationErrorReason");
  session.remove("activationRetryAt");
  session.remove("activationStartedAt");
  session.remove("startedAt");
  session.remove("terminationReason");
  session.remove("terminationActor");
  _dirty = true;
  return true;
}

bool PortalSessionManager::onSessionActivated(const String& mac,
                                              uint32_t firstAttemptMs,
                                              uint32_t generation) {
  (void)firstAttemptMs;
  logActivationStack("activation entry");

  HotspotUser user;
  bool mustExpireVoucher = false;
  String voucherCode;
  String voucherExpiresAt;
  lockState();
  JsonObject session = findSessionUnlocked(mac);
  if (session.isNull()) {
    unlockState();
    if (_logger) {
      _logger->warnLocal("portal", "onSessionActivated skipped — session missing");
    }
    return false;
  }
  const uint32_t currentGen = sessionGenerationOf(session);
  if (generation != 0 && currentGen != 0 && generation != currentGen) {
    unlockState();
    Serial.printf(
        "[portal-activate] mac=%s stale job gen=%u current=%u ignored\n",
        mac.c_str(), static_cast<unsigned>(generation),
        static_cast<unsigned>(currentGen));
    return false;
  }
  if (alreadyAuthorizedThisGeneration(session)) {
    session["activationRetryPending"] = false;
    unlockState();
    Serial.printf(
        "[portal-activate] mac=%s gen=%u already authorized — skip duplicate\n",
        mac.c_str(), static_cast<unsigned>(currentGen));
    return true;
  }
  user.mac = mac;
  user.ip = session["ipAddress"] | "";
  user.timeoutSeconds = static_cast<uint32_t>(session["secondsLeft"] | 0);
  user.profile = session["hotspotProfile"] | "";
  user.sessionGeneration = currentGen != 0 ? currentGen : generation;
  const bool resumeAttempt = session["resumePending"] | false;
  const bool isVoucher =
      strcmp(session["source"] | "portal", "voucher") == 0;
  if (isVoucher) {
    voucherCode = String(session["voucherCode"] | "");
    voucherExpiresAt = String(session["serviceExpiresAt"] | "");
    long wallRemaining = -1;
    if (voucherWallRemaining(session, wallRemaining)) {
      if (wallRemaining == 0) {
        mustExpireVoucher = true;
      } else if (wallRemaining > 0) {
        user.timeoutSeconds = static_cast<uint32_t>(wallRemaining);
        session["secondsLeft"] = wallRemaining;
        _dirty = true;
      }
    }
  }
  unlockState();

  if (mustExpireVoucher) {
    lockState();
    session = findSessionUnlocked(mac);
    if (!session.isNull()) {
      session["sessionState"] = PortalState::Expiring;
      session["connected"] = false;
      session["secondsLeft"] = 0;
      session["routerCleanupQueued"] = true;
      session["routerCleanupPending"] = true;
      session["routerCleanupComplete"] = false;
      session["terminationReason"] = "time_expired";
      session["terminationActor"] = "system";
      _dirty = true;
    }
    unlockState();
    Serial.printf(
        "[voucher-expiry] mac=%s code=%s expiresAt=%s remaining=0 "
        "action=expire\n",
        mac.c_str(), voucherCode.c_str(), voucherExpiresAt.c_str());
    enqueueWork(PortalWorkType::ExpireSession, mac, "time_expired");
    return false;
  }

  if (user.timeoutSeconds == 0) {
    if (_logger) {
      _logger->warnLocal("portal",
                         "onSessionActivated skipped — zero remaining entitlement");
    }
    markActivationEnqueueFailed(mac, resumeAttempt);
    return false;
  }

  if (!_routerWorker) {
    if (_logger) {
      _logger->warnLocal("portal", "onSessionActivated skipped — router worker unavailable");
    }
    markActivationEnqueueFailed(mac, resumeAttempt);
    return false;
  }

  // All RouterOS communication is serialized through router_worker (Single
  // Router Worker Rule) — this never calls MikroTikDriver/RouterPlatform
  // directly from loopTask or async_tcp.
  if (_routerWorker->tryEnqueueActivateHotspotUser(user)) {
    lockState();
    session = findSessionUnlocked(mac);
    if (!session.isNull()) session["activationRetryPending"] = false;
    unlockState();
    if (isVoucher) {
      Serial.printf(
          "[voucher-activate] mac=%s code=%s profile=%s expiresAt=%s "
          "remaining=%u action=queued\n",
          mac.c_str(), voucherCode.c_str(), user.profile.c_str(),
          voucherExpiresAt.c_str(), (unsigned)user.timeoutSeconds);
    }
    Serial.printf("[portal-activate] mac=%s job=queued profile=%s remaining=%u\n",
                  mac.c_str(), user.profile.c_str(),
                  (unsigned)user.timeoutSeconds);
    if (_logger) {
      _logger->infoLocal("portal", "RouterOS hotspot user activation queued for " + mac);
    }
    return true;
  }

  lockState();
  session = findSessionUnlocked(mac);
  if (!session.isNull()) {
    session["activationRetryPending"] = true;
    _dirty = true;
  }
  unlockState();
  return false;
}

void PortalSessionManager::onSessionPaused(const String& mac,
                                           uint32_t firstAttemptMs,
                                           uint32_t generation) {
  (void)firstAttemptMs;
  uint32_t jobGen = generation;
  lockState();
  JsonObject session = findSessionUnlocked(mac);
  if (!session.isNull()) {
    const uint32_t currentGen = sessionGenerationOf(session);
    if (generation != 0 && currentGen != 0 && generation != currentGen) {
      unlockState();
      Serial.printf(
          "[portal-pause] mac=%s stale job gen=%u current=%u ignored\n",
          mac.c_str(), static_cast<unsigned>(generation),
          static_cast<unsigned>(currentGen));
      return;
    }
    jobGen = currentGen != 0 ? currentGen : generation;
  }
  unlockState();

  if (!_routerWorker) {
    if (_logger) _logger->warn("portal", "onSessionPaused — router worker unavailable");
    return;
  }

  if (_routerWorker->tryEnqueuePauseHotspotUser(mac, jobGen)) {
    lockState();
    session = findSessionUnlocked(mac);
    if (!session.isNull()) session["pauseRetryPending"] = false;
    unlockState();
    Serial.printf("[portal-pause] mac=%s router=queued gen=%u\n", mac.c_str(),
                  static_cast<unsigned>(jobGen));
    if (_logger) {
      _logger->info("portal", "RouterOS hotspot pause queued for " + mac);
    }
    return;
  }

  lockState();
  session = findSessionUnlocked(mac);
  if (!session.isNull()) {
    session["pauseRetryPending"] = true;
    _dirty = true;
  }
  unlockState();
}

void PortalSessionManager::onSessionExpired(const String& mac,
                                            uint32_t firstAttemptMs,
                                            uint32_t generation) {
  (void)firstAttemptMs;
  uint32_t jobGen = generation;
  lockState();
  JsonObject session = findSessionUnlocked(mac);
  if (!session.isNull()) {
    const uint32_t currentGen = sessionGenerationOf(session);
    const char* state = session["sessionState"] | PortalState::Idle;
    const long remaining = session["secondsLeft"] | 0L;
    if (generation != 0 && currentGen != 0 && generation != currentGen) {
      unlockState();
      Serial.printf(
          "[portal-expire] mac=%s stale job gen=%u current=%u ignored\n",
          mac.c_str(), static_cast<unsigned>(generation),
          static_cast<unsigned>(currentGen));
      return;
    }
    if (remaining > 0 && isPaidActivatingState(state) &&
        strcmp(state, PortalState::Expiring) != 0) {
      unlockState();
      Serial.printf(
          "[portal-expire] mac=%s superseded by live session gen=%u ignored\n",
          mac.c_str(), static_cast<unsigned>(currentGen));
      return;
    }
    jobGen = currentGen != 0 ? currentGen : generation;
  }
  unlockState();

  if (!_routerWorker) {
    lockState();
    session = findSessionUnlocked(mac);
    if (!session.isNull()) {
      session["routerCleanupPending"] = true;
      session["routerCleanupQueued"] = false;
      session["cleanupRetryPending"] = true;
      _dirty = true;
    }
    unlockState();
    return;
  }

  if (_routerWorker->tryEnqueueDeauthorizeHotspotUser(mac, jobGen)) {
    lockState();
    session = findSessionUnlocked(mac);
    if (!session.isNull()) session["cleanupRetryPending"] = false;
    unlockState();
    Serial.printf("[portal-expire] mac=%s router=queued gen=%u\n", mac.c_str(),
                  static_cast<unsigned>(jobGen));
    if (_logger) {
      _logger->info("portal", "RouterOS hotspot user deauthorization queued for " + mac);
    }
    return;
  }

  lockState();
  session = findSessionUnlocked(mac);
  if (!session.isNull()) {
    session["routerCleanupPending"] = true;
    session["routerCleanupQueued"] = false;
    session["cleanupRetryPending"] = true;
    _dirty = true;
  }
  unlockState();
}

void PortalSessionManager::drainHotspotOutcomes() {
  if (!_routerWorker) return;

  RouterProvisioningWorker::HotspotOutcome outcome;
  while (_routerWorker->takeHotspotOutcome(outcome)) {
    const String mac = outcome.mac[0] != '\0' ? String(outcome.mac) : String();
    if (mac.isEmpty()) continue;

    lockState();
    JsonObject genSession = findSessionUnlocked(mac);
    const uint32_t currentGen =
        genSession.isNull() ? 0U : sessionGenerationOf(genSession);
    unlockState();
    if (outcome.generation != 0 && currentGen != 0 &&
        outcome.generation != currentGen) {
      Serial.printf(
          "[portal] stale outcome kind=%u mac=%s gen=%u current=%u ignored\n",
          static_cast<unsigned>(outcome.kind), mac.c_str(),
          static_cast<unsigned>(outcome.generation),
          static_cast<unsigned>(currentGen));
      continue;
    }

    using Kind = RouterProvisioningWorker::HotspotOutcomeKind;
    if (outcome.kind == Kind::Activate) {
      bool emitConnected = false;
      bool emitFailed = false;
      bool emitResumed = false;
      bool voucherSession = false;
      bool voucherWasActive = false;
      bool effectiveOk = outcome.ok;
      bool cleanupVoucher = false;
      String voucherCode;
      String sessionId;
      String saleId;
      String activatedAt;
      String serviceExpiresAt;
      String startedAt;
      uint32_t voucherSeconds = 0;

      lockState();
      JsonObject session = findSessionUnlocked(mac);
      if (!session.isNull()) {
        voucherSession =
            strcmp(session["source"] | "portal", "voucher") == 0;
        voucherCode = String(session["voucherCode"] | "");
        voucherWasActive =
            strcmp(session["voucherStatus"] | "", "active") == 0;
        sessionId = String(session["sessionId"] | "");
        saleId = String(session["saleId"] | "");
        if (outcome.ok) {
          startedAt = String(session["startedAt"] | "");
        }
        if (voucherSession && outcome.ok) {
          voucherSeconds =
              static_cast<uint32_t>(session["secondsLeft"] | 0L);
          serviceExpiresAt = String(session["serviceExpiresAt"] | "");
        }
      }
      unlockState();

      if (outcome.ok) {
        activatedAt = salesRecordedAtNow();
        if (activatedAt.isEmpty()) activatedAt = startedAt;
      }
      if (voucherSession && outcome.ok) {
        // Absolute expiry is stamped at redeem — never extend from activatedAt.
        if (serviceExpiresAt.isEmpty()) {
          serviceExpiresAt =
              addSecondsToRecordedAt(activatedAt, voucherSeconds);
        } else {
          const long remaining = secondsUntilRecordedAt(serviceExpiresAt);
          if (remaining < 0) {
            effectiveOk = false;
          } else if (remaining == 0) {
            cleanupVoucher = true;
            effectiveOk = false;
          } else {
            voucherSeconds = static_cast<uint32_t>(remaining);
          }
        }
        if (!cleanupVoucher && effectiveOk) {
          effectiveOk = _vouchers && !activatedAt.isEmpty() &&
                        !serviceExpiresAt.isEmpty() &&
                        _vouchers->markActivated(voucherCode, mac, sessionId,
                                                 activatedAt,
                                                 serviceExpiresAt);
          if (effectiveOk && !voucherWasActive && _sessions &&
              !saleId.isEmpty()) {
            effectiveOk = _sessions->markSaleActivated(saleId, activatedAt);
          }
        }
        Serial.printf(
            "[voucher-activate] mac=%s code=%s expiresAt=%s remaining=%u "
            "action=%s\n",
            mac.c_str(), voucherCode.c_str(), serviceExpiresAt.c_str(),
            (unsigned)voucherSeconds,
            effectiveOk ? "activated" : (cleanupVoucher ? "expire" : "fail"));
      }

      lockState();
      session = findSessionUnlocked(mac);
      if (!session.isNull()) {
        const char* currentState =
            session["sessionState"] | PortalState::Idle;
        const bool expected =
            (session["routerAuthPending"] | false) &&
            (strcmp(currentState, PortalState::Activating) == 0 ||
             strcmp(currentState, PortalState::Paused) == 0 ||
             strcmp(currentState, PortalState::ActivationError) == 0);
        if (!expected) {
          // A late activation result must never revive an expired or
          // superseded session. Existing cleanup state remains authoritative.
          unlockState();
          continue;
        }
        session["routerAuthPending"] = false;
        if (effectiveOk) {
          session["connected"]       = true;
          session["activationError"] = false;
          session["hadRouterAuth"]   = true;
          session["activationRetryPending"] = false;
          session["activationAttempts"] = 0;
          session.remove("activationErrorReason");
          session.remove("activationRetryAt");
          session.remove("activationStartedAt");
          if (voucherSession) {
            session["voucherStatus"] = "active";
            session["activatedAt"] = activatedAt;
            session["serviceExpiresAt"] = serviceExpiresAt;
            session["secondsLeft"] = static_cast<long>(voucherSeconds);
            session["serviceExpiresEpoch"] =
                static_cast<unsigned long>(time(nullptr) + voucherSeconds);
            session["grantedSeconds"] = voucherSeconds;
          } else {
            uint32_t granted = outcome.grantedSeconds;
            if (granted == 0) {
              granted = static_cast<uint32_t>(session["secondsLeft"] | 0L);
            }
            commitAuthorizedClockUnlocked(session, outcome.authorizedAtMs,
                                          granted);
          }
          if (session["resumePending"] | false) {
            session["paused"]        = false;
            session["resumePending"] = false;
            emitResumed = true;
          }
          session["sessionState"] = PortalState::Active;
          emitConnected = true;
          _lastActivateSuccessMs = millis();
          mac.toCharArray(_lastActivateSuccessMac,
                          sizeof(_lastActivateSuccessMac));
        } else {
          session["connected"] = false;
          session["activationError"] = true;
          if (outcome.reason[0] != '\0') {
            session["activationErrorReason"] = outcome.reason;
          } else if (!effectiveOk && outcome.ok) {
            session["activationErrorReason"] =
                "Voucher accounting failed after authorization";
          }
          if (voucherSession && outcome.ok) {
            // RouterOS granted access but the authoritative voucher/sale commit
            // failed. Revoke access instead of exposing an unaccounted session.
            session["sessionState"] = PortalState::Expiring;
            session["routerCleanupQueued"] = true;
            session["routerCleanupPending"] = true;
            session["routerCleanupComplete"] = false;
            cleanupVoucher = true;
          } else if (session["resumePending"] | false) {
            // Stay paused with purchased time intact.
            session["resumePending"] = false;
            session["sessionState"]  = PortalState::Paused;
            session["paused"]        = true;
          } else {
            session["sessionState"] = PortalState::ActivationError;
          }
          session.remove("activationStartedAt");
          emitFailed = true;
        }
        _dirty = true;
      }
      unlockState();
      if (cleanupVoucher) {
        enqueueWork(PortalWorkType::ExpireSession, mac);
      }
      Serial.printf("[portal-activate] mac=%s ok=%s%s%s\n", mac.c_str(),
                    effectiveOk ? "yes" : "no",
                    effectiveOk ? "" : " reason=",
                    effectiveOk ? "" : outcome.reason);
      if (effectiveOk) {
        const uint32_t commitMs = millis();
        const uint32_t authMs = outcome.authorizedAtMs;
        Serial.printf(
            "[session-clock] mac=%s gen=%u granted=%u authorizedAtMs=%u "
            "existingUserUptime=%u existingUserLimit=%u newUserLimit=%u "
            "activeUptime=%u activeSessionTimeLeft=%u usedActiveSet=%s "
            "activeLogin=%s activeVerify=%s "
            "routerAuthorizationToPortalCommitMs=%d\n",
            mac.c_str(), static_cast<unsigned>(outcome.generation),
            (unsigned)outcome.grantedSeconds, (unsigned)authMs,
            (unsigned)outcome.existingUserUptime,
            (unsigned)outcome.existingUserLimit,
            (unsigned)outcome.newUserLimit, (unsigned)outcome.activeUptime,
            (unsigned)outcome.activeSessionTimeLeft,
            outcome.usedActiveSet ? "yes" : "no",
            outcome.activeLoginSuccess ? "yes" : "no",
            outcome.activeVerifySuccess ? "yes" : "no",
            authMs ? (int)(commitMs - authMs) : -1);
      }
      if (!effectiveOk && _logger) {
        _logger->error("portal",
                       "Activation failed for " + mac + ": " +
                           (outcome.reason[0] != '\0'
                                ? String(outcome.reason)
                                : String("no reason reported")));
      }
      // Publish CONNECTED before SD save / sale bookkeeping so the UI
      // is not queued behind processDeferredWork(SaveSessions).
      if (emitConnected) {
        emitSessionEvent(mac, "portal.session.connected");
        activationLatencyTrace().finishT10();
      }
      if (emitConnected && !voucherSession && _sessions &&
          !sessionId.isEmpty()) {
        _sessions->markSalesActivatedBySessionId(sessionId, activatedAt);
      }
      enqueueSaveSessions();
      if (emitResumed) {
        if (_sessions) {
          _sessions->recordSessionEvent(sessionId, "resumed",
                                        salesRecordedAtNow());
        }
        enqueueWork(PortalWorkType::EmitSessionEvent, mac, "portal.session.resumed");
      }
      if (emitFailed) {
        enqueueWork(PortalWorkType::EmitSessionEvent, mac,
                    "portal.session.activation_failed");
      }
      enqueueEmitBus("sessions.changed");
      enqueueEmitBus("users.active");
      enqueueEmitBus("system.status");
    } else if (outcome.kind == Kind::Pause) {
      String pausedSessionId;
      lockState();
      JsonObject session = findSessionUnlocked(mac);
      if (!session.isNull() &&
          (session["routerPausePending"] | false) &&
          strcmp(session["sessionState"] | PortalState::Idle,
                 PortalState::Expired) != 0) {
        session["routerPausePending"] = false;
        if (outcome.ok) {
          pausedSessionId = String(session["sessionId"] | "");
          session["connected"]    = false;
          session["paused"]       = true;
          session["sessionState"] = PortalState::Paused;
          freezeSessionClockUnlocked(session);
        } else {
          // Do not claim Internet is paused.
          session["paused"]       = false;
          session["sessionState"] = PortalState::Active;
          commitAuthorizedClockUnlocked(
              session, millis(),
              static_cast<uint32_t>(session["secondsLeft"] | 0L));
        }
        _dirty = true;
      }
      unlockState();
      if (outcome.ok && _sessions && !pausedSessionId.isEmpty()) {
        _sessions->recordSessionEvent(pausedSessionId, "paused",
                                      salesRecordedAtNow());
      }
      enqueueSaveSessions();
      Serial.printf("[portal-pause] mac=%s ok=%s\n", mac.c_str(),
                    outcome.ok ? "yes" : "no");
      enqueueWork(PortalWorkType::EmitSessionEvent, mac,
                  outcome.ok ? "portal.session.paused" : "portal.session.pause_failed");
      enqueueEmitBus("sessions.changed");
      enqueueEmitBus("users.active");
      enqueueEmitBus("system.status");
    } else if (outcome.kind == Kind::VerifyActive) {
      // Transport/query failure (outcome.ok=false): leave Connected alone.
      // Explicit not_active: ESP32 claimed Connected but RouterOS has no Active.
      if (!outcome.ok) {
        Serial.println(
            "[router-health] verify-login-timeout connected=1 probe_suppressed=1");
        continue;
      }
      if (strcmp(outcome.reason, "not_active") != 0) continue;

      bool cleared = false;
      lockState();
      JsonObject session = findSessionUnlocked(mac);
      if (!session.isNull()) {
        const char* state = session["sessionState"] | PortalState::Idle;
        const bool stillClaimsAuth =
            strcmp(state, PortalState::Active) == 0 &&
            (session["connected"] | false) &&
            !(session["paused"] | false) &&
            (session["secondsLeft"] | 0L) > 0 &&
            !(session["routerAuthPending"] | false) &&
            !(session["routerPausePending"] | false) &&
            !(session["routerCleanupQueued"] | false);
        if (stillClaimsAuth) {
          freezeSessionClockUnlocked(session);
          session["connected"] = false;
          session["activationError"] = true;
          session["activationErrorReason"] =
              "Internet authorization lost — purchased time preserved. "
              "Tap RETRY INTERNET.";
          session["sessionState"] = PortalState::ActivationError;
          session["routerAuthPending"] = false;
          session.remove("activationStartedAt");
          _dirty = true;
          cleared = true;
        }
      }
      unlockState();
      if (cleared) {
        Serial.printf(
            "[portal-verify] mac=%s active=missing -> activation_error "
            "(secondsLeft preserved)\n",
            mac.c_str());
        enqueueSaveSessions();
        enqueueWork(PortalWorkType::EmitSessionEvent, mac,
                    "portal.session.activation_failed");
        enqueueEmitBus("sessions.changed");
        enqueueEmitBus("users.active");
        enqueueEmitBus("system.status");
      }
    } else if (outcome.kind == Kind::Deauthorize) {
      String voucherCode;
      String terminationReason;
      String terminationActor;
      lockState();
      JsonObject session = findSessionUnlocked(mac);
      if (!session.isNull()) {
        const char* liveState = session["sessionState"] | PortalState::Idle;
        const long liveRemaining = session["secondsLeft"] | 0L;
        if (liveRemaining > 0 && isPaidActivatingState(liveState) &&
            strcmp(liveState, PortalState::Expiring) != 0) {
          unlockState();
          Serial.printf(
              "[portal-expire] mac=%s deauth outcome ignored live gen=%u\n",
              mac.c_str(), static_cast<unsigned>(currentGen));
          continue;
        }
        session["routerCleanupQueued"]   = false;
        session["routerCleanupComplete"] = outcome.ok;
        session["routerCleanupPending"]  = !outcome.ok;
        const uint8_t cleanupRetryCount =
            outcome.ok
                ? 0
                : static_cast<uint8_t>(
                      (session["cleanupRetryCount"] | 0U) + 1U);
        session["cleanupRetryCount"] = cleanupRetryCount;
        session["cleanupRetryPending"] =
            !outcome.ok && cleanupRetryCount < 3U;
        session["connected"]             = false;
        session["hadRouterAuth"]         = false;
        session["sessionState"] =
            outcome.ok ? PortalState::Expired : PortalState::Expiring;
        voucherCode = String(session["voucherCode"] | "");
        terminationReason =
            String(session["terminationReason"] | "expired");
        terminationActor =
            String(session["terminationActor"] | "system");
        if (outcome.ok && !voucherCode.isEmpty()) {
          session["voucherStatus"] = "expired";
        }
        _dirty = true;
      }
      unlockState();
      bool recycled = false;
      if (outcome.ok) {
        const String endedAt = salesRecordedAtNow();
        if (!voucherCode.isEmpty() && _vouchers &&
            terminationReason != "owner_disable") {
          _vouchers->expire(voucherCode, terminationReason, endedAt);
        }
        completeAccounting(mac, terminationReason, terminationActor);

        // Close the lifecycle loop: the router no longer authorizes this MAC
        // and the sale is booked, so the device goes back to Waiting Payment.
        // Without this the record stays in `expired` forever and the customer
        // is left on a dead screen with no way to buy more time.
        lockState();
        JsonObject done = findSessionUnlocked(mac);
        if (!done.isNull()) recycled = recycleExpiredSessionUnlocked(done);
        unlockState();
      }
      enqueueSaveSessions();
      Serial.printf("[portal-expire] mac=%s ok=%s%s\n", mac.c_str(),
                    outcome.ok ? "yes" : "no",
                    recycled ? " -> waiting_payment" : "");
      // Push the cleared session so the portal flips to Waiting Payment
      // immediately instead of waiting for its next poll.
      enqueueWork(PortalWorkType::EmitSessionEvent, mac,
                  "portal.session.expired");
      enqueueEmitBus("sessions.changed");
      enqueueEmitBus("users.active");
      enqueueEmitBus("system.status");
    }
  }
}
