#include "CoinManager.h"

#include "Config.h"

static CoinManager *coinInstance = nullptr;

void CoinManager::begin(StorageManager *storage, Logger *logger, EventBus *events, PromoManager *promos, SessionManager *sessions) {
  _storage = storage;
  _logger = logger;
  _events = events;
  _promos = promos;
  _sessions = sessions;
  loadSettings();

  pinMode(RenzFiConfig::PIN_COIN, INPUT_PULLUP);
  pinMode(RenzFiConfig::PIN_RGB_LED_RED, OUTPUT);
  pinMode(RenzFiConfig::PIN_RGB_LED_GREEN, OUTPUT);
  pinMode(RenzFiConfig::PIN_RGB_LED_BLUE, OUTPUT);
  setRgbLed(false, false, false);
  coinInstance = this;
  attachInterrupt(digitalPinToInterrupt(RenzFiConfig::PIN_COIN), CoinManager::isrThunk, FALLING);
}

void CoinManager::loop() {
  if (!_settings.enabled) {
    _ledMode = LedMode::Off;
    updateLed();
    return;
  }

  uint32_t pulsesSnapshot;
  uint32_t lastPulseSnapshot;
  noInterrupts();
  pulsesSnapshot = _pulses;
  lastPulseSnapshot = _lastPulseMs;
  interrupts();

  if (pulsesSnapshot > 0 && millis() - lastPulseSnapshot > _settings.settleMs && lastPulseSnapshot != _lastProcessedPulseMs) {
    noInterrupts();
    uint32_t pulses = _pulses;
    _pulses = 0;
    _lastProcessedPulseMs = _lastPulseMs;
    interrupts();
    processCoin(pulses);
  }

  _ledMode = _lastAcceptedCoinMs > 0 && millis() - _lastAcceptedCoinMs < RenzFiConfig::RGB_LED_ACCEPTED_MS ? LedMode::Active : LedMode::Waiting;
  updateLed();
}

bool CoinManager::settings(JsonDocument &doc) {
  doc["pesoPerPulse"] = String(_settings.pesoPerPulse);
  doc["defaultMinutesPerPeso"] = String(_settings.defaultMinutesPerPeso);
  doc["debounceMs"] = String(_settings.debounceMs);
  doc["settleMs"] = String(_settings.settleMs);
  doc["enabled"] = _settings.enabled ? "true" : "false";
  return true;
}

bool CoinManager::saveSettings(JsonObjectConst settings) {
  _settings.pesoPerPulse = String(settings["pesoPerPulse"] | _settings.pesoPerPulse).toInt();
  _settings.defaultMinutesPerPeso = String(settings["defaultMinutesPerPeso"] | _settings.defaultMinutesPerPeso).toInt();
  _settings.debounceMs = String(settings["debounceMs"] | _settings.debounceMs).toInt();
  _settings.settleMs = String(settings["settleMs"] | _settings.settleMs).toInt();
  if (settings["enabled"].is<bool>()) _settings.enabled = settings["enabled"];
  if (settings["enabled"].is<const char *>()) _settings.enabled = String(settings["enabled"].as<const char *>()) == "true";

  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  if (!_storage->readJson(RenzFiConfig::SETTINGS_FILE, doc)) return false;
  JsonObject coin = doc["coin"].to<JsonObject>();
  coin["pesoPerPulse"] = _settings.pesoPerPulse;
  coin["defaultMinutesPerPeso"] = _settings.defaultMinutesPerPeso;
  coin["debounceMs"] = _settings.debounceMs;
  coin["settleMs"] = _settings.settleMs;
  coin["enabled"] = _settings.enabled;
  bool ok = _storage->writeJson(RenzFiConfig::SETTINGS_FILE, doc);
  if (ok && _events) _events->emit("coin.diagnostics");
  return ok;
}

bool CoinManager::diagnostics(JsonDocument &doc) {
  JsonObject stats = doc["stats"].to<JsonObject>();
  stats["enabled"] = _settings.enabled ? "true" : "false";
  stats["pendingPulses"] = String(_pulses);
  stats["pulsesToday"] = String(_pulsesToday);
  stats["debounceMs"] = String(_settings.debounceMs);
  stats["settleMs"] = String(_settings.settleMs);
  stats["pin"] = String(RenzFiConfig::PIN_COIN);

  JsonArray logs = doc["logs"].to<JsonArray>();
  JsonObject row = logs.createNestedObject();
  row["t"] = String("uptime-ms:") + millis();
  row["lvl"] = "INFO";
  row["msg"] = "Coin diagnostics sampled";
  return true;
}

LedMode CoinManager::ledMode() const {
  return _ledMode;
}

void IRAM_ATTR CoinManager::isrThunk() {
  if (coinInstance) coinInstance->handlePulse();
}

void IRAM_ATTR CoinManager::handlePulse() {
  uint32_t now = millis();
  if (now - _lastPulseMs < _settings.debounceMs) return;
  _lastPulseMs = now;
  _pulses++;
}

void CoinManager::loadSettings() {
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  if (_storage && _storage->readJson(RenzFiConfig::SETTINGS_FILE, doc)) {
    JsonObject coin = doc["coin"];
    _settings.pesoPerPulse = coin["pesoPerPulse"] | 1;
    _settings.defaultMinutesPerPeso = coin["defaultMinutesPerPeso"] | 5;
    _settings.debounceMs = coin["debounceMs"] | RenzFiConfig::COIN_DEBOUNCE_MS;
    _settings.settleMs = coin["settleMs"] | RenzFiConfig::COIN_SETTLE_MS;
    _settings.enabled = coin["enabled"] | true;
  }
}

void CoinManager::processCoin(uint32_t pulses) {
  if (pulses == 0) return;
  _pulsesToday += pulses;
  int amount = pulses * max(1, _settings.pesoPerPulse);
  int minutes = _promos ? _promos->minutesForAmount(amount) : amount * _settings.defaultMinutesPerPeso;
  if (_sessions) _sessions->grantCoinSession(amount, minutes);
  _lastAcceptedCoinMs = millis();
  if (_logger) _logger->info("coin", String("Accepted ") + pulses + " pulse(s), amount PHP " + amount);
  if (_events) _events->emit("coin.diagnostics");
}

void CoinManager::updateLed() {
  static uint32_t lastTick = 0;
  static bool ledState = false;
  if (millis() - lastTick < RenzFiConfig::LED_TICK_MS) return;
  lastTick = millis();

  switch (_ledMode) {
    case LedMode::Off:
      setRgbLed(false, false, false);
      break;
    case LedMode::Active:
      setRgbLed(false, true, false);
      break;
    case LedMode::Error:
      ledState = !ledState;
      setRgbLed(ledState, false, false);
      break;
    case LedMode::Waiting:
    default:
      setRgbLed(false, false, (millis() / 500) % 2 == 0);
      break;
  }
}

void CoinManager::setRgbLed(bool red, bool green, bool blue) {
  digitalWrite(RenzFiConfig::PIN_RGB_LED_RED, red ? HIGH : LOW);
  digitalWrite(RenzFiConfig::PIN_RGB_LED_GREEN, green ? HIGH : LOW);
  digitalWrite(RenzFiConfig::PIN_RGB_LED_BLUE, blue ? HIGH : LOW);
}
