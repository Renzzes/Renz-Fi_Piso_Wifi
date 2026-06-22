#include "CoinManager.h"

#include "Config.h"
#include "GpioIsrService.h"

#include <driver/gpio.h>
#include <soc/soc_caps.h>

static CoinManager *coinInstance = nullptr;

namespace {

int jsonIntOr(JsonObjectConst obj, const char *key, int fallback) {
  if (!obj.containsKey(key)) return fallback;
  if (obj[key].is<int>()) return obj[key].as<int>();
  if (obj[key].is<long>()) return static_cast<int>(obj[key].as<long>());
  if (obj[key].is<const char *>()) return String(obj[key].as<const char *>()).toInt();
  return fallback;
}

// Prefer legacy React/admin keys when present, else firmware keys, else keep current.
int resolveCoinInt(JsonObjectConst obj, const char *legacyKey, const char *firmwareKey,
                   int current) {
  if (legacyKey && obj.containsKey(legacyKey))
    return jsonIntOr(obj, legacyKey, current);
  if (firmwareKey && obj.containsKey(firmwareKey))
    return jsonIntOr(obj, firmwareKey, current);
  return current;
}

void applyCoinObject(JsonObjectConst coin, CoinSettings &out) {
  out.pesoPerPulse = resolveCoinInt(coin, "calibration", "pesoPerPulse", out.pesoPerPulse);
  out.debounceMs = static_cast<uint32_t>(
      resolveCoinInt(coin, "pulse_width_ms", "debounceMs", static_cast<int>(out.debounceMs)));
  out.settleMs = static_cast<uint32_t>(
      resolveCoinInt(coin, nullptr, "settleMs", static_cast<int>(out.settleMs)));
  out.timeoutSeconds = static_cast<uint32_t>(resolveCoinInt(
      coin, "timeout_seconds", "timeoutSeconds", static_cast<int>(out.timeoutSeconds)));
  out.defaultMinutesPerPeso =
      resolveCoinInt(coin, nullptr, "defaultMinutesPerPeso", out.defaultMinutesPerPeso);
  if (coin["enabled"].is<bool>()) out.enabled = coin["enabled"].as<bool>();
  else if (coin["enabled"].is<const char *>())
    out.enabled = String(coin["enabled"].as<const char *>()) == "true";
}

void writeCoinObject(JsonObject coin, const CoinSettings &settings) {
  coin["pesoPerPulse"] = settings.pesoPerPulse;
  coin["defaultMinutesPerPeso"] = settings.defaultMinutesPerPeso;
  coin["debounceMs"] = settings.debounceMs;
  coin["settleMs"] = settings.settleMs;
  coin["timeoutSeconds"] = settings.timeoutSeconds;
  coin["enabled"] = settings.enabled;
  // Legacy React/admin keys — kept in sync for migration and external tools.
  coin["pulse_width_ms"] = String(settings.debounceMs);
  coin["calibration"] = String(settings.pesoPerPulse);
  coin["timeout_seconds"] = String(settings.timeoutSeconds);
}

}  // namespace

static void IRAM_ATTR coinIsrDispatch(void *arg) {
  (void)arg;
  CoinManager::isrThunk();
}

void CoinManager::detachCoinIsr() {
  if (!_coinIsrAttached) return;

  const gpio_num_t gpio = static_cast<gpio_num_t>(RenzFiConfig::PIN_COIN);
  gpio_isr_handler_remove(gpio);
  gpio_intr_disable(gpio);
  gpio_set_intr_type(gpio, GPIO_INTR_DISABLE);
  Serial.printf("[GPIO_ISR] Coin interrupt detached (GPIO%d)\n",
                RenzFiConfig::PIN_COIN);
  _coinIsrAttached = false;
}

void CoinManager::begin(StorageManager *storage, Logger *logger,
                         EventBus *events, PromoManager *promos,
                         PortalSessionManager *portalSessions) {
  if (_beginComplete) {
    if (_logger) {
      _logger->warn("coin", "CoinManager::begin skipped — already initialized");
    }
    Serial.println("[coin] WARN: CoinManager::begin skipped — already initialized");
    return;
  }

  Serial.println("[coin] CoinManager::begin (first run)");

  _storage        = storage;
  _logger         = logger;
  _events         = events;
  _promos         = promos;
  _portalSessions = portalSessions;
  loadSettings();

  pinMode(RenzFiConfig::PIN_COIN, INPUT_PULLUP);
  pinMode(RenzFiConfig::PIN_RGB_LED_RED, OUTPUT);
  pinMode(RenzFiConfig::PIN_RGB_LED_GREEN, OUTPUT);
  pinMode(RenzFiConfig::PIN_RGB_LED_BLUE, OUTPUT);
  setRgbLed(false, false, false);

  if (!GpioIsrService::isInstalled()) {
    appendDiagLog("GPIO ISR service unavailable");
    if (_logger) _logger->error("coin", "GPIO ISR service unavailable");
    Serial.println("[coin] ERROR: GPIO ISR service not installed at boot");
  }

  coinInstance = this;
  if (RenzFiConfig::PIN_COIN < SOC_GPIO_PIN_COUNT) {
    if (_coinIsrAttached) {
      if (_logger) _logger->warn("coin", "Coin ISR already attached");
      Serial.println("[coin] WARN: Coin ISR already attached");
      return;
    }

    const gpio_num_t gpio = static_cast<gpio_num_t>(RenzFiConfig::PIN_COIN);
    gpio_set_intr_type(gpio, GPIO_INTR_NEGEDGE);
    const esp_err_t err =
        gpio_isr_handler_add(gpio, coinIsrDispatch, nullptr);
    if (err != ESP_OK) {
      _errors++;
      appendDiagLog("Coin ISR handler add failed");
      if (_logger) _logger->error("coin", "Coin ISR handler add failed");
      Serial.printf("[GPIO_ISR] Coin handler add failed on GPIO%d: %s\n",
                    RenzFiConfig::PIN_COIN, esp_err_to_name(err));
    } else {
      _coinIsrAttached = true;
      Serial.printf("[GPIO_ISR] Coin interrupt attached (GPIO%d)\n",
                    RenzFiConfig::PIN_COIN);
    }
  } else {
    _errors++;
    appendDiagLog("Invalid coin GPIO pin");
    if (_logger) _logger->error("coin", "Invalid coin GPIO pin");
  }

  if (_portalSessions) _portalSessions->setCoinManager(this);
  appendDiagLog("Coin slot initialized");
  _beginComplete = true;
  Serial.println("[coin] CoinManager::begin complete");
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
  // Primary contract for React admin (legacy key names).
  doc["pulse_width_ms"] = String(_settings.debounceMs);
  doc["calibration"] = String(_settings.pesoPerPulse);
  doc["timeout_seconds"] = String(_settings.timeoutSeconds);
  // Firmware-native keys for direct API consumers and migration tooling.
  doc["pesoPerPulse"] = String(_settings.pesoPerPulse);
  doc["defaultMinutesPerPeso"] = String(_settings.defaultMinutesPerPeso);
  doc["debounceMs"] = String(_settings.debounceMs);
  doc["settleMs"] = String(_settings.settleMs);
  doc["timeoutSeconds"] = String(_settings.timeoutSeconds);
  doc["enabled"] = _settings.enabled ? "true" : "false";
  return true;
}

bool CoinManager::saveSettings(JsonObjectConst settings) {
  applyCoinObject(settings, _settings);

  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  if (!_storage->readJson(RenzFiConfig::SETTINGS_FILE, doc)) return false;
  JsonObject coin = doc["coin"].to<JsonObject>();
  writeCoinObject(coin, _settings);
  bool ok = _storage->writeJson(RenzFiConfig::SETTINGS_FILE, doc);
  if (ok && _events) _events->emit("coin.diagnostics");
  return ok;
}

void CoinManager::fillStatus(JsonObject coinSlot) const {
  coinSlot["pulsesToday"] = _pulsesToday;
  coinSlot["state"] = stateLabel();
  coinSlot["ok"] = _settings.enabled;
}

uint32_t CoinManager::insertTimeoutSeconds() const {
  return _settings.timeoutSeconds > 0 ? _settings.timeoutSeconds
                                      : RenzFiConfig::COIN_INSERT_TIMEOUT_SEC;
}

void CoinManager::resetCounters() {
  noInterrupts();
  _pulses = 0;
  interrupts();
  _lastProcessedPulseMs = 0;
  _pulsesToday = 0;
  _lastAcceptedPulses = 0;
  _lastAcceptedCoinMs = 0;
  _errors = 0;
  _diagLogCount = 0;
  appendDiagLog("Coin diagnostics counters reset");
  if (_logger) _logger->info("coin", "Coin diagnostics counters reset");
  if (_events) _events->emit("coin.diagnostics");
}

bool CoinManager::diagnostics(JsonDocument &doc) {
  uint32_t pendingPulses = 0;
  noInterrupts();
  pendingPulses = _pulses;
  interrupts();

  JsonObject stats = doc["stats"].to<JsonObject>();
  // Primary contract for React admin diagnostics cards.
  stats["last_pulse"] = String(_lastAcceptedPulses);
  stats["total_today"] = String(_pulsesToday);
  stats["errors"] = String(_errors);
  stats["state"] = stateLabel();
  // Firmware-native keys retained for tooling and migration.
  stats["enabled"] = _settings.enabled ? "true" : "false";
  stats["pendingPulses"] = String(pendingPulses);
  stats["pulsesToday"] = String(_pulsesToday);
  stats["debounceMs"] = String(_settings.debounceMs);
  stats["settleMs"] = String(_settings.settleMs);
  stats["pin"] = String(RenzFiConfig::PIN_COIN);

  JsonArray logs = doc["logs"].to<JsonArray>();
  for (size_t i = 0; i < _diagLogCount; i++) {
    size_t idx = _diagLogCount - 1 - i;
    JsonObject row = logs.createNestedObject();
    row["t"] = String("uptime-ms:") + _diagLogAtMs[idx];
    row["lvl"] = "INFO";
    row["msg"] = _diagLogMsg[idx];
  }
  return true;
}

void CoinManager::appendDiagLog(const String &msg) {
  if (_diagLogCount < kDiagLogMax) {
    _diagLogMsg[_diagLogCount] = msg;
    _diagLogAtMs[_diagLogCount] = millis();
    _diagLogCount++;
    return;
  }
  for (size_t i = 1; i < kDiagLogMax; i++) {
    _diagLogMsg[i - 1] = _diagLogMsg[i];
    _diagLogAtMs[i - 1] = _diagLogAtMs[i];
  }
  _diagLogMsg[kDiagLogMax - 1] = msg;
  _diagLogAtMs[kDiagLogMax - 1] = millis();
}

const char *CoinManager::stateLabel() const {
  if (!_settings.enabled) return "Disabled";
  switch (_ledMode) {
    case LedMode::Active:
      return "Active";
    case LedMode::Error:
      return "Error";
    case LedMode::Off:
      return "Off";
    case LedMode::Waiting:
    default: {
      uint32_t pending = 0;
      noInterrupts();
      pending = _pulses;
      interrupts();
      return pending > 0 ? "Pending" : "Ready";
    }
  }
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
  _settings.debounceMs = RenzFiConfig::COIN_DEBOUNCE_MS;
  _settings.settleMs = RenzFiConfig::COIN_SETTLE_MS;
  _settings.timeoutSeconds = RenzFiConfig::COIN_INSERT_TIMEOUT_SEC;

  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  if (_storage && _storage->readJson(RenzFiConfig::SETTINGS_FILE, doc)) {
    JsonObject coin = doc["coin"];
    if (!coin.isNull()) applyCoinObject(coin, _settings);
  }
}

void CoinManager::processCoin(uint32_t pulses) {
  if (pulses == 0) return;
  _lastAcceptedPulses = pulses;
  _pulsesToday += pulses;
  int amount = pulses * max(1, _settings.pesoPerPulse);
  if (_portalSessions) {
    _portalSessions->onCoinInserted(amount);
  } else if (_logger) {
    _logger->warn("coin", "Coin pulse ignored — portal session manager unavailable");
  }
  _lastAcceptedCoinMs = millis();
  String logLine = String("Accepted ") + pulses + " pulse(s), PHP " + amount +
                   " credited to portal session";
  appendDiagLog(logLine);
  if (_logger) _logger->info("coin", logLine);
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
