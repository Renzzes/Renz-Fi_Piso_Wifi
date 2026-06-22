#include "PortalSessionManager.h"

#include "CoinManager.h"
#include "Config.h"
#include "Models.h"
#include "SalesTime.h"

// ──────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ──────────────────────────────────────────────────────────────────────────────

void PortalSessionManager::begin(StorageManager* storage, Logger* logger,
                                  EventBus* events, PromoManager* promos,
                                  MikroTikManager* mikrotik) {
  Serial.println("[portal] PortalSessionManager::begin");
  _storage = storage;
  _logger  = logger;
  _events  = events;
  _promos  = promos;
  _mikrotik = mikrotik;
  loadFromSD();
  JsonArray sessions = _doc["sessions"].as<JsonArray>();
  Serial.printf("[portal] Loaded %u session record(s)\n",
                sessions.isNull() ? 0U : sessions.size());
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
  uint32_t now = millis();

  // 1-second session timer tick
  if (now - _lastTickMs >= 1000) {
    _lastTickMs = now;
    tickSessions();
  }

  // Periodic SD persist for in-flight timer state
  if (_dirty && (now - _lastSaveMs >= RenzFiConfig::PORTAL_SAVE_INTERVAL_MS)) {
    saveToSD(true);
  }
}

// ──────────────────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────────────────

bool PortalSessionManager::getSession(const String& mac, const String& ip,
                                       JsonDocument& out) {
  JsonObject session = findOrCreate(mac, ip);
  session["lastSeen"] = (unsigned long)(millis() / 1000);
  _dirty = true;
  out.set(session);
  return true;
}

bool PortalSessionManager::startCoinWindow(const String& mac,
                                            const String& ip) {
  JsonObject session = findOrCreate(mac, ip);

  const int windowSecs = (int)coinInsertTimeoutSecs();
  session["coinWindowActive"]    = true;
  session["coinWindowRemaining"] = windowSecs;
  session["sessionState"]        = PortalState::WaitingCoin;
  session["lastSeen"]            = (unsigned long)(millis() / 1000);

  _activeInsertMac = mac;
  _dirty = true;
  saveToSD(true);

  emitSessionEvent(mac, "portal.coin.started");
  if (_events) _events->emit("sessions.changed");
  if (_logger)
    _logger->info("portal", "Coin window opened for " + mac);
  return true;
}

void PortalSessionManager::onCoinInserted(int pesoAmount) {
  if (_activeInsertMac.isEmpty()) {
    if (_logger)
      _logger->warn("portal", "Coin pulse received but no active coin window");
    return;
  }
  JsonObject session = findSession(_activeInsertMac);
  if (session.isNull()) {
    if (_logger)
      _logger->warn("portal", "Coin pulse: session not found for " +
                                   _activeInsertMac);
    return;
  }

  session["credits"]        = (session["credits"] | 0) + pesoAmount;
  session["insertedAmount"] = (session["insertedAmount"] | 0) + pesoAmount;
  session["updatedAt"]      = (unsigned long)(millis() / 1000);
  session["lastSeen"]       = (unsigned long)(millis() / 1000);

  if (session["coinWindowActive"] | false) {
    session["coinWindowRemaining"] = (int)coinInsertTimeoutSecs();
    session["sessionState"]        = PortalState::WaitingCoin;
  }

  _dirty = true;
  saveToSD(true);
  emitSessionEvent(_activeInsertMac, "portal.coin.credit");
  if (_logger)
    _logger->info("portal",
                  "Credit +PHP " + String(pesoAmount) + " → " +
                      _activeInsertMac + " (total PHP " +
                      String(session["credits"] | 0) + ")");
}

bool PortalSessionManager::donePaying(const String& mac) {
  JsonObject session = findSession(mac);
  if (session.isNull()) return false;

  int credits = session["credits"] | 0;
  if (credits <= 0) return false;

  int minutes = _promos ? _promos->minutesForAmount(credits) : credits * 5;
  if (minutes < 1) minutes = 1;

  if (!_storage) {
    if (_logger)
      _logger->error("portal", "donePaying: storage unavailable — sale not recorded");
    return false;
  }

  const String recordedAt = salesRecordedAtNow();
  if (recordedAt.isEmpty()) {
    if (_logger)
      _logger->error("portal",
                       "donePaying: wall clock not ready — sale not recorded");
    return false;
  }

  DynamicJsonDocument sale(512);
  sale["id"]              = String("psale-") + makeSessionId();
  sale["timestamp"]       = String("uptime-ms:") + millis();
  sale["recorded_at"]     = recordedAt;
  sale["amount"]          = credits;
  sale["sessionId"]       = session["sessionId"] | "";
  sale["macAddress"]      = mac;
  sale["paymentType"]     = "coin";
  sale["durationMinutes"] = minutes;
  if (!_storage->appendJsonArrayItem(RenzFiConfig::SALES_FILE,
                                     sale.as<JsonObject>(),
                                     RenzFiConfig::JSON_DOC_LARGE)) {
    if (_logger) {
      _logger->error("portal",
                     "donePaying: failed to persist sale for " + mac + " — " +
                         _storage->lastError());
    }
    return false;
  }
  if (_events) _events->emit("sales.changed");

  unsigned long nowSec = millis() / 1000;

  session["secondsLeft"]      = (long)(minutes * 60);
  session["sessionState"]     = PortalState::Active;
  session["connected"]        = true;
  session["paused"]           = false;
  session["coinWindowActive"] = false;
  session["credits"]          = 0;
  session["updatedAt"]        = nowSec;
  session["lastSeen"]         = nowSec;

  _dirty = true;
  saveToSD(true);

  onSessionActivated(mac);
  emitSessionEvent(mac, "portal.session.connected");
  if (_events) _events->emit("sessions.changed");
  if (_logger)
    _logger->info("portal", "Session activated " + mac + " — " +
                                 String(minutes) + " min (PHP " +
                                 String(credits) + " inserted)");
  return true;
}

bool PortalSessionManager::hasSession(const String& mac) {
  return !findSession(mac).isNull();
}

bool PortalSessionManager::pause(const String& mac) {
  JsonObject session = findSession(mac);
  if (session.isNull()) return false;

  const char* stateStr = session["sessionState"] | PortalState::Idle;
  if (strcmp(stateStr, PortalState::Expired) == 0) return false;
  if (strcmp(stateStr, PortalState::WaitingCoin) == 0) return false;
  if (session["coinWindowActive"] | false) return false;
  if (strcmp(stateStr, PortalState::Paused) == 0) return true;
  if (strcmp(stateStr, PortalState::Active) != 0) return false;
  if ((session["secondsLeft"] | 0L) <= 0) return false;

  session["paused"]       = true;
  session["sessionState"] = PortalState::Paused;
  session["updatedAt"]    = (unsigned long)(millis() / 1000);
  session["lastSeen"]     = (unsigned long)(millis() / 1000);

  _dirty = true;
  saveToSD(true);
  onSessionPaused(mac);
  emitSessionEvent(mac, "portal.session.paused");
  if (_events) {
    _events->emit("sessions.changed");
    _events->emit("users.active");
    _events->emit("system.status");
  }
  return true;
}

bool PortalSessionManager::resume(const String& mac) {
  JsonObject session = findSession(mac);
  if (session.isNull()) return false;

  const char* stateStr = session["sessionState"] | PortalState::Idle;
  if (strcmp(stateStr, PortalState::Expired) == 0) return false;
  if (strcmp(stateStr, PortalState::WaitingCoin) == 0) return true;
  if (session["coinWindowActive"] | false) return true;
  if (strcmp(stateStr, PortalState::Paused) == 0) {
    // fall through to resume
  } else if (strcmp(stateStr, PortalState::Active) == 0 && !(session["paused"] | false)) {
    return true;
  } else if ((session["credits"] | 0) > 0) {
    return true;
  } else {
    return false;
  }

  session["paused"]       = false;
  session["sessionState"] = PortalState::Active;
  session["updatedAt"]    = (unsigned long)(millis() / 1000);
  session["lastSeen"]     = (unsigned long)(millis() / 1000);

  _dirty = true;
  saveToSD(true);
  emitSessionEvent(mac, "portal.session.resumed");
  if (_events) {
    _events->emit("sessions.changed");
    _events->emit("users.active");
    _events->emit("system.status");
  }
  return true;
}

bool PortalSessionManager::cancelModal(const String& mac, JsonDocument& out) {
  // Modal cancel: credits and session state are intentionally preserved.
  // The user can re-open the modal and continue from where they left off.
  JsonObject session = findOrCreate(mac, "");
  session["lastSeen"] = (unsigned long)(millis() / 1000);
  out.set(session);
  emitSessionEvent(mac, "portal.session.updated");
  return true;
}

bool PortalSessionManager::reset(const String& mac) {
  JsonArray oldArr = _doc["sessions"].as<JsonArray>();
  if (oldArr.isNull()) return false;

  // Rebuild the array without the target session (ArduinoJson has no erase).
  DynamicJsonDocument tmp(RenzFiConfig::JSON_DOC_LARGE);
  JsonArray newArr = tmp["sessions"].to<JsonArray>();
  bool found = false;

  for (JsonObject s : oldArr) {
    if (String(s["macAddress"] | "") == mac) {
      found = true;
      onSessionExpired(mac);
      continue;
    }
    newArr.add(s);
  }

  if (found) {
    if (_activeInsertMac == mac) _activeInsertMac = "";
    _doc.set(tmp);
    _dirty = true;
    saveToSD(true);
    if (_events) {
      _events->emit("sessions.changed");
      _events->emit("users.active");
    }
  }
  return found;
}

bool PortalSessionManager::heartbeat(const String& mac, const String& ip) {
  JsonObject session = findOrCreate(mac, ip);
  unsigned long nowSec = millis() / 1000;
  session["lastSeen"] = nowSec;
  if (!ip.isEmpty()) session["ipAddress"] = ip;
  _dirty = true;
  return true;
}

bool PortalSessionManager::getRates(JsonDocument& out) {
  Serial.println("[rates] PortalSessionManager::getRates entered");

  if (!_promos) {
    Serial.println("[rates] FAILED: _promos is NULL");
    return false;
  }
  Serial.println("[rates] _promos ptr ok, calling PromoManager::list()");

  bool ok = _promos->list(out);
  Serial.printf("[rates] PromoManager::list() returned: %s\n",
                ok ? "true" : "false");
  return ok;
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
  if (strcmp(sessionState, PortalState::Active) == 0) return "active";
  if (strcmp(sessionState, PortalState::Expired) == 0) return "expired";
  return "idle";
}

static bool isPortalSessionActive(JsonObjectConst session, unsigned long nowSec) {
  const char *state = session["sessionState"] | PortalState::Idle;
  if (strcmp(state, PortalState::Expired) == 0) return false;
  if (strcmp(state, PortalState::Idle) == 0) return false;

  bool coinWindow = session["coinWindowActive"] | false;
  int credits = session["credits"] | 0;
  long secondsLeft = session["secondsLeft"] | 0L;
  bool paused = session["paused"] | false;
  bool connected = session["connected"] | false;
  unsigned long lastSeen = session["lastSeen"] | 0UL;
  bool heartbeatFresh = lastSeen > 0 && nowSec >= lastSeen &&
                        (nowSec - lastSeen) <= RenzFiConfig::PORTAL_HEARTBEAT_STALE_SEC;

  if (strcmp(state, PortalState::WaitingCoin) == 0) {
    return coinWindow && heartbeatFresh;
  }
  if (coinWindow) {
    return heartbeatFresh;
  }
  if (credits > 0) {
    // Unpaid credits after the coin window closed are not active internet users.
    return false;
  }
  if (strcmp(state, PortalState::Paused) == 0) {
    return connected && secondsLeft > 0 && heartbeatFresh;
  }
  if (strcmp(state, PortalState::Active) == 0) {
    return connected && (secondsLeft > 0 || paused) && heartbeatFresh;
  }
  return false;
}

void PortalSessionManager::appendActiveUsers(JsonArray &out, JsonArray &seenMacs) {
  JsonArray arr = _doc["sessions"].as<JsonArray>();
  if (arr.isNull()) return;

  const unsigned long nowSec = millis() / 1000;
  for (JsonObjectConst session : arr) {
    if (!isPortalSessionActive(session, nowSec)) continue;

    String mac = session["macAddress"] | "";
    if (macAlreadyListed(seenMacs, mac)) continue;

    const char *state = session["sessionState"] | PortalState::Idle;
    bool paused = session["paused"] | false;
    bool isPausedState = strcmp(state, PortalState::Paused) == 0;
    bool isActiveState = strcmp(state, PortalState::Active) == 0;
    bool isWaitingCoin = strcmp(state, PortalState::WaitingCoin) == 0;
    long secondsLeft = session["secondsLeft"] | 0L;

    String sourceRaw = session["source"] | "portal";
    bool isVoucher = sourceRaw == "voucher";

    int remainingMinutes = 0;
    if (isActiveState || isPausedState) {
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
    markMacSeen(seenMacs, mac);
  }
}

void PortalSessionManager::cleanupExpired() {
  JsonArray arr = _doc["sessions"].as<JsonArray>();
  if (arr.isNull()) return;

  // Remove sessions that have been in the Expired state for over 1 hour,
  // idle/unpaid records after PORTAL_IDLE_TTL_SEC, or stale heartbeats.
  unsigned long nowSec = millis() / 1000;
  constexpr unsigned long EXPIRED_TTL  = 3600UL;   // 1 h post-expiry
  constexpr unsigned long IDLE_TTL     = RenzFiConfig::PORTAL_IDLE_TTL_SEC;
  constexpr unsigned long HEARTBEAT_TTL = RenzFiConfig::PORTAL_HEARTBEAT_STALE_SEC;

  DynamicJsonDocument tmp(RenzFiConfig::JSON_DOC_LARGE);
  JsonArray newArr = tmp["sessions"].to<JsonArray>();
  bool changed = false;

  for (JsonObject s : arr) {
    unsigned long lastSeen = s["lastSeen"] | 0UL;
    const char* state = s["sessionState"] | PortalState::Idle;
    bool expired  = strcmp(state, PortalState::Expired) == 0;
    bool idle = strcmp(state, PortalState::Idle) == 0;
    bool unpaid = (s["credits"] | 0) > 0 && !(s["coinWindowActive"] | false);
    bool heartbeatStale = lastSeen > 0 && nowSec > lastSeen &&
                          (nowSec - lastSeen) > HEARTBEAT_TTL;
    bool activeLike = strcmp(state, PortalState::Active) == 0 ||
                      strcmp(state, PortalState::Paused) == 0 ||
                      strcmp(state, PortalState::WaitingCoin) == 0 ||
                      (s["coinWindowActive"] | false);

    bool staleSince = false;
    if (expired) {
      staleSince = (nowSec > lastSeen) && (nowSec - lastSeen > EXPIRED_TTL);
    } else if (idle || unpaid) {
      staleSince = (nowSec > lastSeen) && (nowSec - lastSeen > IDLE_TTL);
    } else if (activeLike && heartbeatStale) {
      staleSince = true;
    }

    if (staleSince) {
      changed = true;
      continue;
    }
    newArr.add(s);
  }

  if (changed) {
    _doc.set(tmp);
    _dirty = true;
    saveToSD(true);
  }
}

// ──────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ──────────────────────────────────────────────────────────────────────────────

void PortalSessionManager::tickSessions() {
  JsonArray arr = _doc["sessions"].as<JsonArray>();
  if (arr.isNull()) return;

  bool changed = false;

  for (JsonObject session : arr) {
    // ── Coin-window countdown ──────────────────────────────────────────────
    if (session["coinWindowActive"] | false) {
      int rem = session["coinWindowRemaining"] | 0;
      if (rem > 0) {
        session["coinWindowRemaining"] = rem - 1;
        changed = true;
      } else {
        session["coinWindowActive"] = false;
        String mac = String(session["macAddress"] | "");
        if (_activeInsertMac == mac) _activeInsertMac = "";
        int credits = session["credits"] | 0;
        if (credits <= 0) {
          session["sessionState"] = PortalState::Expired;
          session["connected"]    = false;
          onSessionExpired(mac);
          emitSessionEvent(mac, "portal.session.expired");
          if (_events) {
            _events->emit("sessions.changed");
            _events->emit("users.active");
            _events->emit("system.status");
          }
        } else {
          session["credits"]          = 0;
          session["insertedAmount"]   = 0;
          session["sessionState"]     = PortalState::Expired;
          session["connected"]        = false;
          onSessionExpired(mac);
          emitSessionEvent(mac, "portal.coin.timeout");
          if (_events) {
            _events->emit("sessions.changed");
            _events->emit("users.active");
            _events->emit("system.status");
          }
        }
        changed = true;
      }
    }

    // ── Active session countdown ───────────────────────────────────────────
    const char* stateStr = session["sessionState"] | PortalState::Idle;
    bool isActive = strcmp(stateStr, PortalState::Active) == 0;
    bool isPaused = session["paused"] | false;

    if (isActive && !isPaused) {
      long secs = session["secondsLeft"] | 0L;
      if (secs > 0) {
        session["secondsLeft"] = secs - 1;
        changed = true;
      } else {
        // Session time exhausted
        session["sessionState"] = PortalState::Expired;
        session["connected"]    = false;
        session["secondsLeft"]  = 0;
        String mac = String(session["macAddress"] | "");
        onSessionExpired(mac);
        emitSessionEvent(mac, "portal.session.expired");
        if (_events) {
          _events->emit("sessions.changed");
          _events->emit("users.active");
        }
        changed = true;
      }
    }
  }

  if (changed) _dirty = true;
}

void PortalSessionManager::emitSessionEvent(const String& mac,
                                             const char* event) {
  if (!_events || !event) return;
  JsonObject session = findSession(mac);
  if (session.isNull()) {
    _events->emit(event);
    return;
  }
  String payload;
  serializeJson(session, payload);
  _events->emit(event, payload);
}

bool PortalSessionManager::loadFromSD() {
  if (!_storage) return false;
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
  if (!_dirty && !immediate) return true;
  bool ok =
      _storage->writeJson(RenzFiConfig::PORTAL_SESSIONS_FILE, _doc, immediate);
  if (ok) {
    _dirty      = false;
    _lastSaveMs = millis();
  }
  return ok;
}

JsonObject PortalSessionManager::findSession(const String& mac) {
  JsonArray arr = _doc["sessions"].as<JsonArray>();
  if (arr.isNull()) return JsonObject();
  for (JsonObject s : arr) {
    if (String(s["macAddress"] | "") == mac) return s;
  }
  return JsonObject();
}

JsonObject PortalSessionManager::findOrCreate(const String& mac,
                                               const String& ip) {
  JsonObject existing = findSession(mac);
  if (!existing.isNull()) {
    if (!ip.isEmpty() && String(existing["ipAddress"] | "") != ip) {
      existing["ipAddress"] = ip;
      _dirty = true;
    }
    return existing;
  }

  // No existing session — create a fresh idle record
  JsonArray arr = _doc["sessions"].as<JsonArray>();
  if (arr.isNull()) arr = _doc["sessions"].to<JsonArray>();

  JsonObject s        = arr.createNestedObject();
  unsigned long nowSec = millis() / 1000;

  s["sessionId"]           = makeSessionId();
  s["macAddress"]          = mac;
  s["ipAddress"]           = ip;
  s["credits"]             = 0;
  s["insertedAmount"]      = 0;
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

  _dirty = true;
  return s;
}

String PortalSessionManager::makeSessionId() {
  return String(esp_random(), HEX) + String(millis(), HEX);
}

// ──────────────────────────────────────────────────────────────────────────────
// MikroTik integration stubs
// These are the extension points for future RouterOS API calls.
// ──────────────────────────────────────────────────────────────────────────────

void PortalSessionManager::onSessionActivated(const String& mac) {
  if (!_mikrotik) {
    if (_logger)
      _logger->warn("portal", "onSessionActivated skipped — MikroTikManager unavailable");
    return;
  }

  JsonObject session = findSession(mac);
  if (session.isNull()) return;

  HotspotUser user;
  user.mac = mac;
  user.ip = session["ipAddress"] | "";
  user.timeoutSeconds = static_cast<uint32_t>(session["secondsLeft"] | 0);

  if (_mikrotik->provisionHotspotUser(user)) {
    if (_logger) _logger->info("portal", "RouterOS hotspot user provisioned for " + mac);
  } else if (_logger) {
    _logger->error("portal", "RouterOS hotspot user provisioning failed for " + mac);
  }
}

void PortalSessionManager::onSessionPaused(const String& mac) {
  if (_logger) _logger->info("portal", "Session paused locally: " + mac);
}

void PortalSessionManager::onSessionExpired(const String& mac) {
  if (_mikrotik) {
    if (_mikrotik->disconnectHotspotUser(mac)) {
      if (_logger) _logger->info("portal", "RouterOS hotspot user removed for " + mac);
    } else if (_logger) {
      _logger->warn("portal", "RouterOS hotspot disconnect failed for " + mac);
    }
  }
}
