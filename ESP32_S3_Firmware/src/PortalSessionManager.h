#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "EventBus.h"
#include "Logger.h"
#include "MikroTikManager.h"
#include "PromoManager.h"
#include "StorageManager.h"

class CoinManager;

// Session state string constants stored in portal_sessions.json.
// Using a namespace of const char* lets the JSON keys stay readable on SD
// without a separate serialisation step.
namespace PortalState {
static constexpr const char* Idle        = "idle";
static constexpr const char* WaitingCoin = "waiting_coin";
static constexpr const char* Active      = "active";
static constexpr const char* Paused      = "paused";
static constexpr const char* Expired     = "expired";
}  // namespace PortalState

// PortalSessionManager owns the authoritative session truth for the
// MikroTik captive portal.  It tracks per-device credits, countdown timers,
// pause/resume and insert-coin windows entirely on the ESP32 side.
//
// The portal JS (served by MikroTik) is a dumb renderer that polls or
// subscribes to SSE and reflects the state returned by these APIs.
//
// Session model stored in /sessions/portal_sessions.json:
//   sessionId, macAddress, ipAddress, credits, insertedAmount, secondsLeft,
//   paused, connected, coinWindowActive, coinWindowRemaining,
//   createdAt, updatedAt, lastSeen, source, sessionState
class PortalSessionManager {
 public:
  void begin(StorageManager* storage, Logger* logger, EventBus* events,
             PromoManager* promos, MikroTikManager* mikrotik = nullptr);
  void loop();

  // ── API surface called by portal JS via HTTP endpoints ────────────────────

  // Restore or initialise the session for this device.
  // Creates an idle session record on first contact.
  bool getSession(const String& mac, const String& ip, JsonDocument& out);

  // Open an insert-coin window (duration from CoinManager timeout_seconds).
  bool startCoinWindow(const String& mac, const String& ip);

  // Supplies coin insert timeout from saved coin settings.
  void setCoinManager(CoinManager* coin);

  // Convert accumulated credits → active internet minutes.
  // Records a sale entry and fires the MikroTik activation hook.
  bool donePaying(const String& mac);

  // Pause the active countdown.  MikroTik hook fires later.
  bool pause(const String& mac);

  // Resume a paused session.
  bool resume(const String& mac);

  // Close the modal in the browser without destroying credits/session.
  // Returns current session state so the caller can re-render.
  bool cancelModal(const String& mac, JsonDocument& out);

  // Wipe the session record for this device.
  bool reset(const String& mac);

  // Keep-alive from the portal page; updates lastSeen and ipAddress.
  bool heartbeat(const String& mac, const String& ip);

  // Return promo rate table (delegates to PromoManager::list).
  bool getRates(JsonDocument& out);

  // ── Called by CoinManager when pulses are processed ──────────────────────
  // Adds pesoAmount to the currently-active insert session's credits.
  void onCoinInserted(int pesoAmount);

  // Periodic stale-session cleanup (call from FirmwareApp cleanup interval).
  void cleanupExpired();

  // Append active portal sessions to out (skips MACs already in seenMacs).
  void appendActiveUsers(JsonArray &out, JsonArray &seenMacs);

  // True when a portal session record exists for this MAC.
  bool hasSession(const String &mac);

 private:
  StorageManager* _storage = nullptr;
  Logger*         _logger  = nullptr;
  EventBus*       _events  = nullptr;
  PromoManager*   _promos  = nullptr;
  CoinManager*    _coin    = nullptr;
  MikroTikManager* _mikrotik = nullptr;

  // In-memory session cache; root is {"sessions":[...]}
  // Loaded from SD at begin(), saved on every state transition and
  // every PORTAL_SAVE_INTERVAL_MS when dirty (timer decrements).
  JsonDocument _doc;
  bool         _dirty       = false;
  uint32_t     _lastTickMs  = 0;
  uint32_t     _lastSaveMs  = 0;

  // MAC address of the device currently inside a coin window.
  // CoinManager pulses are credited to this session.
  String _activeInsertMac;

  bool       loadFromSD();
  bool       saveToSD(bool immediate = false);
  JsonObject findSession(const String& mac);
  JsonObject findOrCreate(const String& mac, const String& ip);
  String     makeSessionId();
  void       tickSessions();
  void       emitSessionEvent(const String& mac, const char* event);
  uint32_t   coinInsertTimeoutSecs() const;

  // ── MikroTik integration boundary (stubs — implementation TBD) ───────────
  void onSessionActivated(const String& mac);
  void onSessionPaused(const String& mac);
  void onSessionExpired(const String& mac);
};
