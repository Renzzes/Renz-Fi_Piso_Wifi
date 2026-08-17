#include "CoinManager.h"

#include "CoinLatencyTrace.h"
#include "Config.h"
#include "GpioIsrService.h"
#include "SalesTime.h"

#include <algorithm>
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

int resolveCoinInt(JsonObjectConst obj, const char *legacyKey, const char *firmwareKey,
                   int current) {
  if (legacyKey && obj.containsKey(legacyKey))
    return jsonIntOr(obj, legacyKey, current);
  if (firmwareKey && obj.containsKey(firmwareKey))
    return jsonIntOr(obj, firmwareKey, current);
  return current;
}

void applyDefaultDenominationMap(CoinSettings &out) {
  static const CoinDenomination kProductionDefaults[] = {
      {1, 1},
      {5, 5},
      {10, 10},
      {20, 20},
  };
  out.denominationCount = sizeof(kProductionDefaults) / sizeof(kProductionDefaults[0]);
  for (uint8_t i = 0; i < out.denominationCount; ++i) {
    out.denominations[i] = kProductionDefaults[i];
  }
}

bool validateDenominationMap(const CoinSettings &settings) {
  if (settings.denominationCount == 0 ||
      settings.denominationCount > CoinSettings::kMaxDenominations) {
    return false;
  }
  for (uint8_t i = 0; i < settings.denominationCount; ++i) {
    const CoinDenomination &entry = settings.denominations[i];
    if (entry.pulses == 0 || entry.pulses > CoinSettings::kPulseGroupHardMax) {
      return false;
    }
    if (entry.pesos == 0) return false;
    for (uint8_t j = i + 1; j < settings.denominationCount; ++j) {
      if (settings.denominations[j].pulses == entry.pulses) return false;
    }
  }
  return true;
}

// Returns PHP amount for an exact pulse count, or -1 when unsupported.
int resolvePulseGroupToPesos(const CoinSettings &settings, uint32_t pulseCount) {
  for (uint8_t i = 0; i < settings.denominationCount; ++i) {
    if (settings.denominations[i].pulses == pulseCount) {
      return settings.denominations[i].pesos;
    }
  }
  return -1;
}

String formatSupportedPulseCounts(const CoinSettings &settings) {
  uint8_t pulses[CoinSettings::kMaxDenominations];
  uint8_t count = settings.denominationCount;
  for (uint8_t i = 0; i < count; ++i) {
    pulses[i] = settings.denominations[i].pulses;
  }
  for (uint8_t i = 0; i < count; ++i) {
    for (uint8_t j = i + 1; j < count; ++j) {
      if (pulses[j] < pulses[i]) {
        const uint8_t tmp = pulses[i];
        pulses[i] = pulses[j];
        pulses[j] = tmp;
      }
    }
  }
  String out;
  for (uint8_t i = 0; i < count; ++i) {
    if (i > 0) out += ", ";
    out += String(pulses[i]);
  }
  return out;
}

void applyDenominationMap(JsonObjectConst coin, CoinSettings &out) {
  applyDefaultDenominationMap(out);
  if (!coin.containsKey("denominationMap")) return;

  JsonArrayConst arr = coin["denominationMap"];
  if (arr.isNull() || arr.size() == 0) return;

  CoinSettings trial = out;
  trial.denominationCount = 0;
  for (JsonObjectConst entry : arr) {
    if (trial.denominationCount >= CoinSettings::kMaxDenominations) break;
    const int pulseCount = jsonIntOr(entry, "pulses", 0);
    const int pesoValue = jsonIntOr(entry, "pesos", 0);
    if (pulseCount <= 0 || pulseCount > CoinSettings::kPulseGroupHardMax) continue;
    if (pesoValue <= 0) continue;
    trial.denominations[trial.denominationCount++] = {
        static_cast<uint8_t>(pulseCount),
        static_cast<uint8_t>(pesoValue),
    };
  }
  if (trial.denominationCount > 0 && validateDenominationMap(trial)) {
    out.denominationCount = trial.denominationCount;
    for (uint8_t i = 0; i < trial.denominationCount; ++i) {
      out.denominations[i] = trial.denominations[i];
    }
  }
}

void writeDenominationMap(JsonObject coin, const CoinSettings &settings) {
  JsonArray map = coin["denominationMap"].to<JsonArray>();
  map.clear();
  for (uint8_t i = 0; i < settings.denominationCount; ++i) {
    JsonObject entry = map.createNestedObject();
    entry["pulses"] = settings.denominations[i].pulses;
    entry["pesos"] = settings.denominations[i].pesos;
  }
}

void applyCoinObject(JsonObjectConst coin, CoinSettings &out) {
  // pulsesPerPeso is the canonical field (new).  Fall back to legacy pesoPerPulse/calibration
  // only when pulsesPerPeso is absent so old settings files are not silently broken.
  if (coin.containsKey("pulsesPerPeso")) {
    out.pulsesPerPeso = jsonIntOr(coin, "pulsesPerPeso", out.pulsesPerPeso);
  } else {
    // Legacy: old field was pesoPerPulse (amount = pulses * pesoPerPulse).
    // Treat legacy value=1 as pulsesPerPeso=1 (1-for-1 mapping).
    int legacyPpp = resolveCoinInt(coin, "calibration", "pesoPerPulse", out.pulsesPerPeso);
    out.pulsesPerPeso = max(1, legacyPpp);
  }
  out.pulsesPerPeso = max(1, out.pulsesPerPeso);

  out.debounceMs = static_cast<uint32_t>(
      resolveCoinInt(coin, "pulse_width_ms", "debounceMs", static_cast<int>(out.debounceMs)));
  out.debounceMs = std::max<uint32_t>(1UL, out.debounceMs);
  out.settleMs = static_cast<uint32_t>(
      resolveCoinInt(coin, nullptr, "settleMs", static_cast<int>(out.settleMs)));
  out.maxPulsesPerGroup = static_cast<uint32_t>(resolveCoinInt(
      coin, nullptr, "maxPulsesPerGroup", static_cast<int>(out.maxPulsesPerGroup)));
  out.maxPulsesPerGroup = std::min<uint32_t>(
      std::max<uint32_t>(1UL, out.maxPulsesPerGroup), CoinSettings::kPulseGroupHardMax);
  out.timeoutSeconds = static_cast<uint32_t>(resolveCoinInt(
      coin, "timeout_seconds", "timeoutSeconds", static_cast<int>(out.timeoutSeconds)));
  out.defaultMinutesPerPeso =
      resolveCoinInt(coin, nullptr, "defaultMinutesPerPeso", out.defaultMinutesPerPeso);
  if (coin.containsKey("noActivityTimeoutSec")) {
    out.noActivityTimeoutSec = static_cast<uint32_t>(
        jsonIntOr(coin, "noActivityTimeoutSec",
                  static_cast<int>(out.noActivityTimeoutSec)));
  }
  if (coin["enabled"].is<bool>()) out.enabled = coin["enabled"].as<bool>();
  else if (coin["enabled"].is<const char *>())
    out.enabled = String(coin["enabled"].as<const char *>()) == "true";

  applyDenominationMap(coin, out);
}

void writeCoinObject(JsonObject coin, const CoinSettings &settings) {
  coin["pulsesPerPeso"]        = settings.pulsesPerPeso;
  coin["pesoPerPulse"]         = settings.pulsesPerPeso;  // legacy alias kept for compatibility
  coin["defaultMinutesPerPeso"] = settings.defaultMinutesPerPeso;
  coin["debounceMs"]           = settings.debounceMs;
  coin["settleMs"]             = settings.settleMs;
  coin["maxPulsesPerGroup"]    = settings.maxPulsesPerGroup;
  coin["timeoutSeconds"]       = settings.timeoutSeconds;
  coin["noActivityTimeoutSec"] = settings.noActivityTimeoutSec;
  coin["enabled"]              = settings.enabled;
  coin["pulse_width_ms"]       = String(settings.debounceMs);
  coin["calibration"]          = String(settings.pulsesPerPeso);
  coin["timeout_seconds"]      = String(settings.timeoutSeconds);
  writeDenominationMap(coin, settings);
}

void writeCoinStats(JsonObject stats, const CoinStats &values) {
  stats["totalPulseCount"] = values.totalPulseCount;
  stats["totalCoinCount"] = values.totalCoinCount;
  stats["lastPulseMs"] = values.lastPulseMs;
  stats["lastCoinMs"] = values.lastCoinMs;
  stats["uptimePulseCount"] = values.uptimePulseCount;
  stats["uptimeCoinCount"] = values.uptimeCoinCount;
}

void applyCoinStats(JsonObjectConst stats, CoinStats &out) {
  if (stats.isNull()) return;
  if (stats["totalPulseCount"].is<uint32_t>()) {
    out.totalPulseCount = stats["totalPulseCount"];
  } else if (stats["totalPulseCount"].is<int>()) {
    out.totalPulseCount = stats["totalPulseCount"];
  }
  if (stats["totalCoinCount"].is<uint32_t>()) {
    out.totalCoinCount = stats["totalCoinCount"];
  } else if (stats["totalCoinCount"].is<int>()) {
    out.totalCoinCount = stats["totalCoinCount"];
  }
  if (stats["lastPulseMs"].is<uint32_t>()) {
    out.lastPulseMs = stats["lastPulseMs"];
  } else if (stats["lastPulseMs"].is<int>()) {
    out.lastPulseMs = stats["lastPulseMs"];
  }
  if (stats["lastCoinMs"].is<uint32_t>()) {
    out.lastCoinMs = stats["lastCoinMs"];
  } else if (stats["lastCoinMs"].is<int>()) {
    out.lastCoinMs = stats["lastCoinMs"];
  }
  if (stats["uptimePulseCount"].is<uint32_t>()) {
    out.uptimePulseCount = stats["uptimePulseCount"];
  } else if (stats["uptimePulseCount"].is<int>()) {
    out.uptimePulseCount = stats["uptimePulseCount"];
  }
  if (stats["uptimeCoinCount"].is<uint32_t>()) {
    out.uptimeCoinCount = stats["uptimeCoinCount"];
  } else if (stats["uptimeCoinCount"].is<int>()) {
    out.uptimeCoinCount = stats["uptimeCoinCount"];
  }
}

const char *faultReasonLabel(CoinFaultReason reason) {
  switch (reason) {
    case CoinFaultReason::IsrAttachFailed:
      return "ISR_ATTACH_FAILED";
    case CoinFaultReason::IsrServiceMissing:
      return "ISR_SERVICE_MISSING";
    case CoinFaultReason::InvalidGpio:
      return "INVALID_GPIO";
    case CoinFaultReason::None:
    default:
      return "NONE";
  }
}

}  // namespace

const char *CoinManager::stateLabel(CoinState state) {
  switch (state) {
    case CoinState::Disabled:
      return "DISABLED";
    case CoinState::WaitingForActivity:
      return "WAITING_FOR_ACTIVITY";
    case CoinState::Responding:
      return "RESPONDING";
    case CoinState::NoRecentActivity:
      return "NO_RECENT_ACTIVITY";
    case CoinState::Fault:
      return "FAULT";
    default:
      return "WAITING_FOR_ACTIVITY";
  }
}

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

bool CoinManager::attachCoinIsr() {
  if (_coinIsrAttached) return true;

  if (!GpioIsrService::isInstalled()) {
    enterFault(CoinFaultReason::IsrServiceMissing, "GPIO ISR service unavailable");
    return false;
  }

  if (RenzFiConfig::PIN_COIN >= SOC_GPIO_PIN_COUNT) {
    enterFault(CoinFaultReason::InvalidGpio, "Invalid coin GPIO pin");
    return false;
  }

  coinInstance = this;
  const gpio_num_t gpio = static_cast<gpio_num_t>(RenzFiConfig::PIN_COIN);
  // Universal coin slot NO output floats when idle; internal pull-up (set in
  // applyEnableState) holds the pin HIGH at rest. A valid coin pulse closes
  // the NO contact to common GND, so the accepted edge is FALLING (NEGEDGE).
  gpio_set_intr_type(gpio, GPIO_INTR_NEGEDGE);
  const esp_err_t err = gpio_isr_handler_add(gpio, coinIsrDispatch, nullptr);
  if (err != ESP_OK) {
    _errors++;
    enterFault(CoinFaultReason::IsrAttachFailed, "Coin ISR handler add failed");
    Serial.printf("[GPIO_ISR] Coin handler add failed on GPIO%d: %s\n",
                  RenzFiConfig::PIN_COIN, esp_err_to_name(err));
    return false;
  }

  _coinIsrAttached = true;
  _faultReason = CoinFaultReason::None;
  Serial.printf("[GPIO_ISR] Coin interrupt attached (GPIO%d)\n",
                RenzFiConfig::PIN_COIN);
  return true;
}

void CoinManager::enterFault(CoinFaultReason reason, const char *logMsg) {
  detachCoinIsr();
  appendDiagLog(logMsg);
  if (_logger) _logger->error("coin", logMsg);
  transitionState(CoinState::Fault, reason);
  if (_events) {
    DynamicJsonDocument payload(128);
    payload["reason"] = faultReasonLabel(reason);
    String json;
    serializeJson(payload, json);
    _events->emit("coin.fault", json);
  }
}

void CoinManager::transitionState(CoinState next, CoinFaultReason fault) {
  const CoinState from = _state;
  if (from == next && (next != CoinState::Fault || _faultReason == fault)) return;

  _state = next;
  if (next == CoinState::Fault) {
    _faultReason = fault;
  } else if (next != CoinState::Disabled) {
    _faultReason = CoinFaultReason::None;
  }

  emitStateChanged(from);
}

void CoinManager::emitStateChanged(CoinState from) const {
  if (!_events) return;
  DynamicJsonDocument payload(192);
  payload["from"] = stateLabel(from);
  payload["to"] = stateLabel(_state);
  payload["state"] = stateLabel(_state);
  if (_state == CoinState::Fault) {
    payload["faultReason"] = faultReasonLabel(_faultReason);
  }
  String json;
  serializeJson(payload, json);
  _events->emit("coin.state.changed", json);
  _events->emit("system.status");
}

void CoinManager::applyEnableState(bool enabled) {
  if (!enabled) {
    detachCoinIsr();
    transitionState(CoinState::Disabled);
    return;
  }

  // Idle coin signal floats (~1.95-3.0V observed) with nothing driving it;
  // ESP32 internal pull-up gives GPIO4 a stable idle HIGH so only an actual
  // NO-contact closure to GND (a real coin pulse) is seen as a LOW edge.
  pinMode(RenzFiConfig::PIN_COIN, INPUT_PULLUP);
  if (attachCoinIsr()) {
    updateState();
  }
}

void CoinManager::begin(StorageManager *storage, Logger *logger, EventBus *events,
                        PromoManager *promos, PortalSessionManager *portalSessions) {
  if (_beginComplete) {
    if (_logger) {
      _logger->warn("coin", "CoinManager::begin skipped — already initialized");
    }
    Serial.println("[coin] WARN: CoinManager::begin skipped — already initialized");
    return;
  }

  Serial.println("[coin] CoinManager::begin (first run)");

  _storage = storage;
  _logger = logger;
  _events = events;
  _promos = promos;
  _portalSessions = portalSessions;
  loadSettings();
  loadStats();

  if (_settings.enabled) {
    applyEnableState(true);
  } else {
    transitionState(CoinState::Disabled);
  }

  if (_portalSessions) _portalSessions->setCoinManager(this);
  appendDiagLog("Coin slot initialized");
  _beginComplete = true;
  Serial.println("[coin] CoinManager::begin complete");
}

void CoinManager::loop() {
  if (!_settings.enabled) {
    if (_state != CoinState::Disabled) {
      applyEnableState(false);
    }
    return;
  }

  if (_state == CoinState::Fault) {
    return;
  }

  uint32_t pulsesSnapshot;
  uint32_t lastPulseSnapshot;
  uint32_t isrPulseTotalSnapshot;
  noInterrupts();
  pulsesSnapshot = _pulses;
  lastPulseSnapshot = _lastPulseMs;
  isrPulseTotalSnapshot = _isrPulseTotal;
  interrupts();

  if (isrPulseTotalSnapshot != _stats.totalPulseCount) {
    const uint32_t delta = isrPulseTotalSnapshot - _stats.totalPulseCount;
    _stats.totalPulseCount = isrPulseTotalSnapshot;
    _stats.uptimePulseCount += delta;
    _stats.lastPulseMs = lastPulseSnapshot;
    _statsDirty = true;
    _pulseEventPending = true;
    updateState();
  }

  if (_pulseEventPending && _events) {
    _pulseEventPending = false;
    DynamicJsonDocument payload(96);
    payload["totalPulseCount"] = _stats.totalPulseCount;
    payload["uptimePulseCount"] = _stats.uptimePulseCount;
    String json;
    serializeJson(payload, json);
    _events->emit("coin.pulse", json);
    _events->emit("coin.diagnostics");
    _events->emit("system.status");
  }

  if (pulsesSnapshot > 0 && millis() - lastPulseSnapshot > _settings.settleMs &&
      lastPulseSnapshot != _lastProcessedPulseMs) {
    noInterrupts();
    uint32_t pulses = _pulses;
    _pulses = 0;
    _lastProcessedPulseMs = _lastPulseMs;
    // TEMP calibration: arm the post-group guard so ringing right after this
    // finalized group cannot be mistaken for the start of a new one.
    _postGroupGuardUntilMs = millis() + RenzFiConfig::COIN_POST_GROUP_GUARD_MS;
    interrupts();
    processCoin(pulses);
  }

  updateState();
  saveStatsIfNeeded();
}

CoinState CoinManager::state() const {
  return _state;
}

bool CoinManager::isFault() const {
  return _state == CoinState::Fault;
}

void CoinManager::updateState() {
  if (!_settings.enabled) {
    if (_state != CoinState::Disabled) transitionState(CoinState::Disabled);
    return;
  }

  if (_state == CoinState::Fault) return;

  if (_stats.totalPulseCount == 0) {
    if (_state != CoinState::WaitingForActivity) {
      transitionState(CoinState::WaitingForActivity);
    }
    return;
  }

  const uint32_t timeoutMs =
      (_settings.noActivityTimeoutSec > 0
           ? _settings.noActivityTimeoutSec
           : RenzFiConfig::COIN_NO_ACTIVITY_TIMEOUT_SEC) *
      1000UL;
  const uint32_t lastActivityMs = max(_stats.lastPulseMs, _stats.lastCoinMs);

  if (lastActivityMs > 0 && millis() - lastActivityMs >= timeoutMs) {
    if (_state != CoinState::NoRecentActivity) {
      transitionState(CoinState::NoRecentActivity);
    }
    return;
  }

  if (_state != CoinState::Responding) {
    transitionState(CoinState::Responding);
  }
}

bool CoinManager::settings(JsonDocument &doc) {
  doc["pulsesPerPeso"]         = String(_settings.pulsesPerPeso);
  doc["pesoPerPulse"]          = String(_settings.pulsesPerPeso);  // legacy alias
  doc["pulse_width_ms"]        = String(_settings.debounceMs);
  doc["calibration"]           = String(_settings.pulsesPerPeso);
  doc["timeout_seconds"]       = String(_settings.timeoutSeconds);
  doc["defaultMinutesPerPeso"] = String(_settings.defaultMinutesPerPeso);
  doc["debounceMs"]            = String(_settings.debounceMs);
  doc["settleMs"]              = String(_settings.settleMs);
  doc["maxPulsesPerGroup"]     = String(_settings.maxPulsesPerGroup);
  doc["timeoutSeconds"]        = String(_settings.timeoutSeconds);
  doc["noActivityTimeoutSec"]  = String(_settings.noActivityTimeoutSec);
  doc["enabled"]               = _settings.enabled ? "true" : "false";
  doc["state"]                 = stateLabel(_state);
  doc["hardwareState"]         = stateLabel(_state);
  JsonArray map = doc["denominationMap"].to<JsonArray>();
  for (uint8_t i = 0; i < _settings.denominationCount; ++i) {
    JsonObject entry = map.createNestedObject();
    entry["pulses"] = _settings.denominations[i].pulses;
    entry["pesos"] = _settings.denominations[i].pesos;
  }
  return true;
}

bool CoinManager::saveSettings(JsonObjectConst settings) {
  const bool wasEnabled = _settings.enabled;
  applyCoinObject(settings, _settings);
  _debounceMsCached = std::max<uint32_t>(1UL, _settings.debounceMs);

  if (_settings.enabled && !wasEnabled) {
    applyEnableState(true);
  } else if (!_settings.enabled && wasEnabled) {
    applyEnableState(false);
  } else if (_settings.enabled) {
    updateState();
  }

  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  if (!_storage->readJson(RenzFiConfig::SETTINGS_FILE, doc)) return false;
  JsonObject coin = doc["coin"].to<JsonObject>();
  writeCoinObject(coin, _settings);
  writeCoinStats(coin["stats"].to<JsonObject>(), _stats);
  bool ok = _storage->writeJson(RenzFiConfig::SETTINGS_FILE, doc);
  if (ok && _events) {
    _events->emit("coin.diagnostics");
    _events->emit("system.status");
  }
  return ok;
}

CoinStatus CoinManager::statusSnapshot() const {
  CoinStatus out;
  out.enabled = _settings.enabled;
  out.state = _state;
  out.faultReason = _faultReason;
  out.totalPulseCount = _stats.totalPulseCount;
  out.totalCoinCount = _stats.totalCoinCount;
  out.uptimePulseCount = _stats.uptimePulseCount;
  out.uptimeCoinCount = _stats.uptimeCoinCount;
  out.lastPulseTimestamp =
      _stats.lastPulseMs > 0 ? formatTimestamp(_stats.lastPulseMs) : String();
  out.lastCoinTimestamp =
      _stats.lastCoinMs > 0 ? formatTimestamp(_stats.lastCoinMs) : String();
  return out;
}

void CoinManager::fillCoinStatus(JsonObject out) const {
  const CoinStatus snap = statusSnapshot();
  out["enabled"] = snap.enabled;
  out["state"] = stateLabel(snap.state);
  out["totalPulseCount"] = snap.totalPulseCount;
  out["totalCoinCount"] = snap.totalCoinCount;
  out["uptimePulseCount"] = snap.uptimePulseCount;
  out["uptimeCoinCount"] = snap.uptimeCoinCount;
  if (snap.lastPulseTimestamp.length() > 0) {
    out["lastPulseTimestamp"] = snap.lastPulseTimestamp;
  } else {
    out["lastPulseTimestamp"] = nullptr;
  }
  if (snap.lastCoinTimestamp.length() > 0) {
    out["lastCoinTimestamp"] = snap.lastCoinTimestamp;
  } else {
    out["lastCoinTimestamp"] = nullptr;
  }
  if (snap.state == CoinState::Fault) {
    out["faultReason"] = faultReasonLabel(snap.faultReason);
  }
}

void CoinManager::fillStatus(JsonObject coinSlot) const {
  fillCoinStatus(coinSlot);
  coinSlot["hardwareState"] = stateLabel(_state);
  coinSlot["pulsesToday"] = _pulsesToday;
  coinSlot["ok"] = _settings.enabled && _state != CoinState::Fault;
  coinSlot["stateLabel"] = uiStateLabel();
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
  _stats = CoinStats{};
  noInterrupts();
  _isrPulseTotal = 0;
  interrupts();
  _statsDirty = true;
  _errors = 0;
  _diagLogCount = 0;

  if (_settings.enabled) {
    if (!_coinIsrAttached) {
      applyEnableState(true);
    } else {
      updateState();
    }
  } else {
    transitionState(CoinState::Disabled);
  }

  saveStatsIfNeeded(true);
  appendDiagLog("Coin diagnostics counters reset");
  if (_logger) _logger->infoLocal("coin", "Coin diagnostics counters reset");
  if (_events) {
    _events->emit("coin.diagnostics");
    _events->emit("system.status");
  }
}

bool CoinManager::diagnostics(JsonDocument &doc) {
  uint32_t pendingPulses = 0;
  noInterrupts();
  pendingPulses = _pulses;
  interrupts();

  JsonObject stats = doc["stats"].to<JsonObject>();
  stats["last_pulse"]           = String(_lastAcceptedPulses);
  stats["total_today"]          = String(_pulsesToday);
  stats["errors"]               = String(_errors);
  stats["state"]                = uiStateLabel();
  stats["hardwareState"]        = stateLabel(_state);
  stats["coinState"]            = stateLabel(_state);
  stats["enabled"]              = _settings.enabled ? "true" : "false";
  stats["pendingPulses"]        = String(pendingPulses);
  stats["pulsesToday"]          = String(_pulsesToday);
  stats["totalPulseCount"]      = String(_stats.totalPulseCount);
  stats["totalCoinCount"]       = String(_stats.totalCoinCount);
  stats["uptimePulseCount"]     = String(_stats.uptimePulseCount);
  stats["uptimeCoinCount"]      = String(_stats.uptimeCoinCount);
  stats["debounceMs"]           = String(_settings.debounceMs);
  stats["settleMs"]             = String(_settings.settleMs);
  stats["maxPulsesPerGroup"]    = String(_settings.maxPulsesPerGroup);
  stats["pin"]                  = String(RenzFiConfig::PIN_COIN);
  stats["pulsesPerPeso"]        = String(max(1, _settings.pulsesPerPeso));
  stats["supportedPulseCounts"] = formatSupportedPulseCounts(_settings);
  stats["lastPulseCount"]       = String(_settings.lastPulseCount);
  stats["lastCoinValue"]        = String(_settings.lastCoinValue);
  stats["lastEffectivePesos"]   = String(_settings.lastEffectivePesos);
  if (_state == CoinState::Fault) {
    stats["faultReason"] = faultReasonLabel(_faultReason);
  }

  JsonArray logs = doc["logs"].to<JsonArray>();
  for (size_t i = 0; i < _diagLogCount; i++) {
    size_t idx = _diagLogCount - 1 - i;
    JsonObject row = logs.createNestedObject();
    row["t"]   = String("uptime-ms:") + _diagLogAtMs[idx];
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

const char *CoinManager::uiStateLabel() const {
  if (!_settings.enabled) return "Disabled";
  switch (_state) {
    case CoinState::WaitingForActivity:
      return "Ready";
    case CoinState::Responding: {
      uint32_t pending = 0;
      noInterrupts();
      pending = _pulses;
      interrupts();
      return pending > 0 ? "Pending" : "Active";
    }
    case CoinState::NoRecentActivity:
      return "Idle";
    case CoinState::Fault:
      return "Fault";
    case CoinState::Disabled:
    default:
      return "Disabled";
  }
}

String CoinManager::formatTimestamp(uint32_t eventMs) const {
  (void)eventMs;
  if (salesTimeReady()) return salesRecordedAtNow();
  return String("uptime-ms:") + eventMs;
}

void IRAM_ATTR CoinManager::isrThunk() {
  if (coinInstance) coinInstance->handlePulse();
}

void IRAM_ATTR CoinManager::handlePulse() {
  uint32_t now = millis();
  // TEMP calibration: ignore edges during the post-group guard window so
  // electrical ringing right after a finalized group cannot start a phantom
  // second group. Guard is only ever set from loop(), never here.
  if ((int32_t)(now - _postGroupGuardUntilMs) < 0) return;
  if (now - _lastPulseMs < _debounceMsCached) return;
  _lastPulseMs = now;
  _pulses++;
  if (_pulses == 1) _groupFirstPulseMs = now;
  _isrPulseTotal++;
}

void CoinManager::loadSettings() {
  applyDefaultDenominationMap(_settings);
  _settings.debounceMs = RenzFiConfig::COIN_DEBOUNCE_MS;
  _settings.settleMs = RenzFiConfig::COIN_SETTLE_MS;
  _settings.maxPulsesPerGroup = RenzFiConfig::COIN_MAX_PULSES_PER_GROUP;
  _settings.timeoutSeconds = RenzFiConfig::COIN_INSERT_TIMEOUT_SEC;
  _settings.noActivityTimeoutSec = RenzFiConfig::COIN_NO_ACTIVITY_TIMEOUT_SEC;

  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  if (_storage && _storage->readJson(RenzFiConfig::SETTINGS_FILE, doc)) {
    JsonObject coin = doc["coin"];
    if (!coin.isNull()) applyCoinObject(coin, _settings);
  }
  _debounceMsCached = std::max<uint32_t>(1UL, _settings.debounceMs);
}

void CoinManager::loadStats() {
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  if (_storage && _storage->readJson(RenzFiConfig::SETTINGS_FILE, doc)) {
    JsonObject coin = doc["coin"];
    if (!coin.isNull()) applyCoinStats(coin["stats"], _stats);
  }
  noInterrupts();
  _isrPulseTotal = _stats.totalPulseCount;
  interrupts();
}

void CoinManager::saveStatsIfNeeded(bool force) {
  if (!_storage || !_statsDirty) return;
  if (!force && millis() - _lastStatsSaveMs < 5000) return;

  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  if (!_storage->readJson(RenzFiConfig::SETTINGS_FILE, doc)) return;

  JsonObject coin = doc["coin"].to<JsonObject>();
  writeCoinStats(coin["stats"].to<JsonObject>(), _stats);
  if (_storage->writeJson(RenzFiConfig::SETTINGS_FILE, doc)) {
    _statsDirty = false;
    _lastStatsSaveMs = millis();
  }
}

void CoinManager::processCoin(uint32_t pulses) {
  if (pulses == 0) return;

  if (pulses > CoinSettings::kPulseGroupHardMax) {
    _errors++;
    String warnLine = String("Rejected pulse group exceeding physical limit (") +
                      pulses + " pulses; max " +
                      String(CoinSettings::kPulseGroupHardMax) + ")";
    appendDiagLog(warnLine);
    if (_logger) _logger->warn("coin", warnLine);
    if (_events) {
      _events->emit("coin.diagnostics");
      _events->emit("system.status");
    }
    return;
  }

  const int pesoAmount = resolvePulseGroupToPesos(_settings, pulses);
  if (pesoAmount < 0) {
    _errors++;
    String warnLine = String("Rejected unsupported pulse group (") + pulses +
                      " pulses; supported: " + formatSupportedPulseCounts(_settings) +
                      ")";
    appendDiagLog(warnLine);
    if (_logger) _logger->warn("coin", warnLine);
    if (_events) {
      _events->emit("coin.diagnostics");
      _events->emit("system.status");
    }
    return;
  }

  _lastAcceptedPulses = pulses;
  _pulsesToday += pulses;
  _stats.totalCoinCount++;
  _stats.uptimeCoinCount++;
  _stats.lastCoinMs = millis();
  _statsDirty = true;

  _settings.lastPulseCount = pulses;
  _settings.lastCoinValue = pesoAmount;
  _settings.lastEffectivePesos = pesoAmount;

  Serial.printf("[coin] pulses=%u peso=%d\n", (unsigned)pulses, pesoAmount);

  {
    CoinLatencyTrace &lat = coinLatencyTrace();
    lat.reset();
    lat.t0Ms = _groupFirstPulseMs ? _groupFirstPulseMs : millis();
    lat.markT1Finalized(pesoAmount);
    _groupFirstPulseMs = 0;
  }

  if (_portalSessions) {
    _portalSessions->onCoinInserted(pesoAmount);
  } else if (_logger) {
    _logger->warn("coin", "Coin pulse ignored — portal session manager unavailable");
  }

  updateState();

  String logLine = String("Accepted denomination: ") + pulses + " pulse(s) -> PHP " +
                   pesoAmount + "; forwarding to portal session";
  appendDiagLog(logLine);
  if (_logger) _logger->info("coin", logLine);
  if (_events) {
    DynamicJsonDocument payload(192);
    payload["pulses"] = pulses;
    payload["amount"] = pesoAmount;
    payload["pulsesPerPeso"] = max(1, _settings.pulsesPerPeso);
    payload["totalCoinCount"] = _stats.totalCoinCount;
    String json;
    serializeJson(payload, json);
    _events->emit("coin.accepted", json);
    _events->emit("coin.diagnostics");
    _events->emit("system.status");
  }
}
