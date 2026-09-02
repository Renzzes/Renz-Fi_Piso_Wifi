#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include <freertos/semphr.h>

#include "EventBus.h"
#include "JsonHeap.h"
#include "Logger.h"
#include "router/RouterPlatform.h"
#include "PromoManager.h"
#include "StorageManager.h"

class CoinManager;
class RouterProvisioningWorker;
class SessionManager;
class VoucherManager;

namespace PortalState {
static constexpr const char* Idle             = "idle";
static constexpr const char* WaitingCoin      = "waiting_coin";
static constexpr const char* Activating       = "activating";
static constexpr const char* Active           = "active";
static constexpr const char* Paused           = "paused";
static constexpr const char* ActivationError  = "activation_error";
static constexpr const char* Expiring         = "expiring";
static constexpr const char* Expired          = "expired";
}  // namespace PortalState

// PortalSessionManager owns the authoritative session truth for the
// MikroTik captive portal.  All reads/writes of _doc and _activeInsertMac
// are serialized via _stateMutex.  SD I/O, RouterOS, HTTP, and SSE run
// only after releasing the mutex (deferred work queue).
class PortalSessionManager {
 public:
  void begin(StorageManager* storage, Logger* logger, EventBus* events,
             PromoManager* promos, RouterPlatform* router = nullptr,
             RouterProvisioningWorker* routerWorker = nullptr,
             VoucherManager* vouchers = nullptr,
             SessionManager* sessions = nullptr);
  void loop();
  /** Reload portal_sessions.json from storage after SD/SPIFFS reconcile. */
  bool reloadFromStorage();

  bool getSession(const String& mac, const String& ip, JsonDocument& out);
  bool startCoinWindow(const String& mac, const String& ip);
  void setCoinManager(CoinManager* coin);
  bool donePaying(const String& mac, String& errorCode,
                  const String& ip = String());
  // Customer pause is capped at kMaxCustomerPauses per purchased session.
  // Owner disconnect/suspend uses enforceLimit=false; admin Pause uses the
  // same customer budget so the portal button stays accurate.
  bool pause(const String& mac, String* errorCode = nullptr,
             bool enforceLimit = true);
  bool resume(const String& mac, String* errorCode = nullptr);
  static constexpr int kMaxCustomerPauses = 3;
  // Automatic recovery budget for a paid session stuck in activation_error.
  static constexpr int      kActivationRetryLimit    = 3;
  static constexpr uint32_t kActivationRetryDelaySec = 20;
  // If RouterWorker never returns an outcome (queue drop / hang), do not leave
  // the customer permanently in Activating — preserve purchased time.
  static constexpr uint32_t kActivationPendingTimeoutSec = 45;
  // After the fast-retry budget is spent, keep trying slowly so Internet is
  // granted once credentials/setup are repaired (no credit loss).
  static constexpr uint32_t kActivationRetryCooldownSec = 60;
  bool cancelModal(const String& mac, JsonDocument& out);
  bool reset(const String& mac);
  bool terminateSession(const String& mac);
  bool heartbeat(const String& mac, const String& ip);
  bool getRates(JsonDocument& out);
  bool redeemVoucher(const String& code, const String& mac, const String& ip,
                     JsonDocument& out, String& errorCode);
  bool reconnectVoucher(const String& mac, const String& ip,
                        JsonDocument& out, String& errorCode);
  bool administerVoucher(const String& code, const String& action,
                         const String& reason, String& errorCode);
  /** Owner/admin: drop HotSpot auth and freeze remaining time (coin + voucher). */
  bool suspendInternet(const String& mac, String* errorCode = nullptr);
  /** Owner/admin: restore HotSpot auth without adding time. */
  bool reconnectInternet(const String& mac, String* errorCode = nullptr);
  /** Owner/admin: end session, zero time, show owner notice on portal reload. */
  bool ownerTerminateSession(const String& mac, String* errorCode = nullptr);

  void onCoinInserted(int pesoAmount);
  void cleanupExpired();
  void appendActiveUsers(JsonArray &out, JsonArray &seenMacs);
  bool hasActiveClientSession() const;
  /** Any non-idle customer portal work (coin window, activating, active, …). */
  bool hasOperationalPortalLoad() const;
  bool hasSession(const String &mac);
  void deferRouterDisconnect(const String &mac);

 private:
  StorageManager* _storage = nullptr;
  Logger*         _logger  = nullptr;
  EventBus*       _events  = nullptr;
  PromoManager*   _promos  = nullptr;
  CoinManager*    _coin    = nullptr;
  RouterPlatform* _router = nullptr;
  RouterProvisioningWorker* _routerWorker = nullptr;
  VoucherManager* _vouchers = nullptr;
  SessionManager* _sessions = nullptr;
  volatile bool _routerIdleNotified = false;

  // Persistent session store — PSRAM-first pool (see JsonHeap.h / PsramAllocator).
  // mutable: const query helpers read via JsonDocument& implicit conversion.
  mutable PsramJsonDocument _doc;
  bool         _dirty       = false;
  uint32_t     _lastTickMs  = 0;
  uint32_t     _lastSaveMs  = 0;
  // Coalesced Active presence check — at most one MAC every ~60s while
  // Connected, via router_worker (never from async_tcp / heartbeat).
  uint32_t     _lastActiveVerifyMs = 0;
  char         _pendingVerifyMac[18] = {};
  // Post-activation trust: skip Verify for this MAC briefly after success.
  uint32_t     _lastActivateSuccessMs = 0;
  char         _lastActivateSuccessMac[18] = {};

  String _activeInsertMac;

  mutable SemaphoreHandle_t _stateMutex = nullptr;

  enum class PortalWorkType : uint8_t {
    SaveSessions,
    ActivateSession,
    ExpireSession,
    PauseSession,
    EmitSessionEvent,
    EmitBusEvent,
    RecordSale,
  };

  struct PortalWorkItem {
    PortalWorkType type = PortalWorkType::SaveSessions;
    char mac[18]          = {};
    char event[48]        = {};
    int  saleAmount       = 0;
    int  saleMinutes      = 0;
    char saleSessionId[40] = {};
    char saleRecordedAt[32] = {};
    // Wall-clock timestamp of the first (non-retry) enqueue of an
    // ActivateSession/ExpireSession item — carried through re-enqueues when
    // the router_worker is busy so onSessionActivated/onSessionExpired can
    // bound total retry time instead of retrying forever. 0 == not tracked.
    uint32_t firstAttemptMs = 0;
    uint32_t generation     = 0;
  };

  static constexpr size_t kPortalWorkQueueCap = 16;
  PortalWorkItem _workQueue[kPortalWorkQueueCap];
  uint8_t        _workHead  = 0;
  uint8_t        _workTail  = 0;
  uint8_t        _workCount = 0;

  struct PortalTickEffect {
    PortalWorkType type = PortalWorkType::EmitBusEvent;
    char mac[18] = {};
    char event[48] = {};
    uint32_t generation = 0;
  };

  static constexpr size_t kMaxTickEffects = 12;
  PortalTickEffect _tickEffects[kMaxTickEffects];
  size_t           _tickEffectCount = 0;

  void lockState() const;
  void unlockState() const;

  bool enqueueWork(PortalWorkType type, const String& mac = String(),
                   const char* event = nullptr, uint32_t firstAttemptMs = 0,
                   uint32_t generation = 0);
  bool enqueueSaveSessions();
  bool enqueueEmitBus(const char* event);
  bool enqueueRecordSale(const String& mac, int amount, int minutes,
                         const String& sessionId, const String& recordedAt);
  bool enqueueActivateSession(const String& mac);
  void processDeferredWork();

  bool       loadFromSD();
  bool       saveToSD(bool immediate = false);
  void       recoverSessionsAfterReboot();
  void       retryPendingRouterWork();
  void       markActivationEnqueueFailed(const String& mac, bool resumeAttempt);
  void       completeAccounting(const String& mac, const String& reason,
                                const String& actor);
  static void routerIdleTrampoline(void* ctx);
  void       closeOtherCoinWindowsUnlocked(const String& exceptMac);
  bool       hasPendingActivationUnlocked(const String& mac) const;
  bool       hasPendingRecordSaleUnlocked(const String& mac,
                                          const String& sessionId) const;
  void       pushTickEffect(PortalWorkType type, const String& mac,
                            const char* event, uint32_t generation = 0);
  static uint32_t sessionGenerationOf(JsonObjectConst session);
  static uint32_t bumpSessionGenerationUnlocked(JsonObject session);
  void       clearSupersededCleanupUnlocked(JsonObject session);
  bool       hasCustomerActivatePending() const;
  void       flushTickEffects();

  JsonObject findSessionUnlocked(const String& mac);
  JsonObject findOrCreateUnlocked(const String& mac, const String& ip);
  String     makeSessionId();
  void       tickSessions();
  void       drainHotspotOutcomes();
  void       maybeEnqueueActiveVerify();
  /** True when any session still needs RouterOS (activate/pause/cleanup/Connected). */
  bool       needsRouterOsWork() const;
  /** True when a critical job (Activate/Pause/Deauth/cleanup) needs recovery probe. */
  bool       needsHealthRecoveryProbe() const;
  void       emitSessionEvent(const String& mac, const char* event);
  void       emitBusEvent(const char* event);
  uint32_t   coinInsertTimeoutSecs() const;
  void       enrichSessionPurchasedMinutes(JsonDocument& out);
  void       enrichSessionCapabilities(JsonDocument& out);
  // Returns a fully cleaned-up session to Waiting Payment. Caller holds the
  // state lock. False when the record must keep its terminal state.
  bool       recycleExpiredSessionUnlocked(JsonObject session);

  bool onSessionActivated(const String& mac, uint32_t firstAttemptMs = 0,
                          uint32_t generation = 0);
  void onSessionPaused(const String& mac, uint32_t firstAttemptMs = 0,
                       uint32_t generation = 0);
  void onSessionExpired(const String& mac, uint32_t firstAttemptMs = 0,
                        uint32_t generation = 0);
};
