#include "RgbController.h"

#include "CoinManager.h"
#include "Config.h"
#include "EthernetManager.h"
#include "RecoveryManager.h"

namespace {

RgbMode parseRgbMode(const char *value, RgbMode fallback) {
  if (!value) return fallback;
  if (strcmp(value, "OFF") == 0) return RgbMode::Off;
  if (strcmp(value, "SOLID") == 0) return RgbMode::Solid;
  if (strcmp(value, "BREATHING") == 0) return RgbMode::Breathing;
  if (strcmp(value, "RAINBOW") == 0) return RgbMode::Rainbow;
  if (strcmp(value, "SYSTEM_STATUS") == 0) return RgbMode::SystemStatus;
  return fallback;
}

uint8_t scaleChannel(uint8_t value, uint8_t brightness) {
  return static_cast<uint8_t>((static_cast<uint16_t>(value) * brightness) / 100);
}

}  // namespace

void RgbController::eventThunk(const char *event, const String &json, void *ctx) {
  if (ctx && event) static_cast<RgbController *>(ctx)->handleEvent(event, json);
}

void RgbController::handleEvent(const char *event, const String &json) {
  if (!event) return;
  if (strcmp(event, "coin.accepted") == 0) {
    showCoinAccepted();
    return;
  }
  if (strcmp(event, "coin.fault") == 0) {
    emitRgbChanged();
    return;
  }
  if (strcmp(event, "storage.changed") == 0 ||
      strcmp(event, "storage.state.changed") == 0) {
    emitRgbChanged();
    return;
  }
  if (strcmp(event, "system.recovery.active") == 0) {
    emitRgbChanged();
    return;
  }
  if (strcmp(event, "firmware.update.active") == 0) {
    _otaActive = true;
    emitRgbChanged();
    return;
  }
  if (strcmp(event, "firmware.progress") == 0) {
    if (json.indexOf("\"phase\":\"complete\"") >= 0 ||
        json.indexOf("\"phase\":\"verify\"") >= 0) {
      _otaActive = json.indexOf("\"phase\":\"complete\"") < 0;
    } else {
      _otaActive = true;
    }
    emitRgbChanged();
    return;
  }
}

void RgbController::showCoinAccepted() {
  _coinAcceptedUntilMs = millis() + RenzFiConfig::RGB_LED_ACCEPTED_MS;
  emitRgbChanged();
}

void RgbController::emitRgbChanged() const {
  if (_events) _events->emit("rgb.changed");
}

void RgbController::begin(StorageManager *storage, EventBus *events,
                          EthernetManager *eth, StorageManager *storageHealth,
                          CoinManager *coin) {
  (void)storageHealth;
  if (_beginComplete) return;

  _storage = storage;
  _events = events;
  _eth = eth;
  _coin = coin;
  _bootStartedMs = millis();

  pinMode(RenzFiConfig::PIN_RGB_LED_RED, OUTPUT);
  pinMode(RenzFiConfig::PIN_RGB_LED_GREEN, OUTPUT);
  pinMode(RenzFiConfig::PIN_RGB_LED_BLUE, OUTPUT);
  setRgbLed(false, false, false);

  loadSettings();
  if (_events) {
    _events->setInternalListener(&RgbController::eventThunk, this);
  }

  _beginComplete = true;
  Serial.println("[rgb] RgbController initialized");
}

void RgbController::markBootComplete() {
  _bootComplete = true;
  emitRgbChanged();
}

void RgbController::loop() {
  if (!_beginComplete) return;
  if (!_bootComplete &&
      millis() - _bootStartedMs >= RenzFiConfig::RGB_BOOT_YELLOW_MS) {
    _bootComplete = true;
    emitRgbChanged();
  }
  applyOutput();
}

RgbMode RgbController::mode() const {
  return _settings.mode;
}

RgbSettings RgbController::settings() const {
  return _settings;
}

RgbSignal RgbController::activeSignal() const {
  return resolveSignal();
}

const char *RgbController::modeLabel(RgbMode mode) {
  switch (mode) {
    case RgbMode::Off:
      return "OFF";
    case RgbMode::Solid:
      return "SOLID";
    case RgbMode::Breathing:
      return "BREATHING";
    case RgbMode::Rainbow:
      return "RAINBOW";
    case RgbMode::SystemStatus:
    default:
      return "SYSTEM_STATUS";
  }
}

const char *RgbController::signalLabel(RgbSignal signal) {
  switch (signal) {
    case RgbSignal::Booting:
      return "BOOTING";
    case RgbSignal::Recovery:
      return "RECOVERY";
    case RgbSignal::OtaUpdate:
      return "OTA_UPDATE";
    case RgbSignal::Error:
      return "ERROR";
    case RgbSignal::Warning:
      return "WARNING";
    case RgbSignal::CoinAccepted:
      return "COIN_ACCEPTED";
    case RgbSignal::Idle:
      return "IDLE";
    case RgbSignal::Off:
    default:
      return "OFF";
  }
}

const char *RgbController::colorNameForSignal(RgbSignal signal) {
  switch (signal) {
    case RgbSignal::Recovery:
      return "WHITE";
    case RgbSignal::OtaUpdate:
      return "PURPLE";
    case RgbSignal::Error:
      return "RED";
    case RgbSignal::Warning:
      return "YELLOW";
    case RgbSignal::CoinAccepted:
      return "GREEN";
    case RgbSignal::Booting:
      return "YELLOW";
    case RgbSignal::Idle:
      return "BLUE";
    case RgbSignal::Off:
    default:
      return "OFF";
  }
}

SystemHealthLevel RgbController::systemStatusLevel() const {
  switch (resolveSignal()) {
    case RgbSignal::Error:
      return SystemHealthLevel::Error;
    case RgbSignal::Warning:
      return SystemHealthLevel::Warning;
    case RgbSignal::CoinAccepted:
    case RgbSignal::OtaUpdate:
    case RgbSignal::Recovery:
    case RgbSignal::Booting:
    case RgbSignal::Idle:
      return SystemHealthLevel::Healthy;
    case RgbSignal::Off:
    default:
      return SystemHealthLevel::Healthy;
  }
}

RgbSignal RgbController::resolveSignal() const {
  if (!_settings.enabled) return RgbSignal::Off;

  if (RecoveryManager::isMonitoring()) return RgbSignal::Recovery;
  if (_otaActive) return RgbSignal::OtaUpdate;

  if (_eth && (!_eth->driverReady() || !_eth->linkUp())) {
    return RgbSignal::Error;
  }
  if (_storage && (!_storage->healthy() || _storage->usingFallback())) {
    return RgbSignal::Warning;
  }
  if (_coin && _coin->isFault()) return RgbSignal::Error;

  if (_coinAcceptedUntilMs > 0 && millis() < _coinAcceptedUntilMs) {
    return RgbSignal::CoinAccepted;
  }

  if (!_bootComplete &&
      millis() - _bootStartedMs < RenzFiConfig::RGB_BOOT_YELLOW_MS) {
    return RgbSignal::Booting;
  }

  return RgbSignal::Idle;
}

void RgbController::resolveColor(RgbSignal signal, uint8_t &red, uint8_t &green,
                                 uint8_t &blue) const {
  switch (signal) {
    case RgbSignal::Booting:
      red = 255;
      green = 255;
      blue = 0;
      break;
    case RgbSignal::OtaUpdate:
      red = 255;
      green = 0;
      blue = 255;
      break;
    case RgbSignal::Error:
      red = 255;
      green = 0;
      blue = 0;
      break;
    case RgbSignal::Warning:
      red = 255;
      green = 255;
      blue = 0;
      break;
    case RgbSignal::CoinAccepted:
      red = 0;
      green = 255;
      blue = 0;
      break;
    case RgbSignal::Idle:
      red = 0;
      green = 0;
      blue = 255;
      break;
    case RgbSignal::Off:
    default:
      red = 0;
      green = 0;
      blue = 0;
      break;
  }
}

RgbStatus RgbController::statusSnapshot() const {
  RgbStatus out;
  out.enabled = _settings.enabled;
  out.brightness = _settings.brightness;
  const RgbSignal signal = resolveSignal();
  out.signal = signal;
  out.colorName = colorNameForSignal(signal);
  resolveColor(signal, out.red, out.green, out.blue);
  return out;
}

void RgbController::fillRgbStatus(JsonObject out) const {
  const RgbStatus snap = statusSnapshot();
  out["enabled"] = snap.enabled;
  out["brightness"] = snap.brightness;
  out["state"] = signalLabel(snap.signal);
  out["color"] = snap.colorName;
}

bool RgbController::fillStatus(JsonObject out) const {
  const RgbStatus snap = statusSnapshot();
  out["enabled"] = snap.enabled;
  out["brightness"] = snap.brightness;
  out["mode"] = modeLabel(_settings.mode);
  out["state"] = signalLabel(snap.signal);
  out["colorName"] = snap.colorName;
  JsonObject color = out["color"].to<JsonObject>();
  color["red"] = snap.red;
  color["green"] = snap.green;
  color["blue"] = snap.blue;
  out["systemStatus"] = signalLabel(snap.signal);
  return true;
}

bool RgbController::applySettings(bool enabled, uint8_t brightness) {
  _settings.enabled = enabled;
  _settings.brightness = min(brightness, static_cast<uint8_t>(100));
  const bool ok = saveSettings();
  if (ok) emitRgbChanged();
  return ok;
}

bool RgbController::setMode(RgbMode mode) {
  _settings.mode = mode;
  const bool ok = saveSettings();
  if (ok) emitRgbChanged();
  return ok;
}

bool RgbController::setColor(uint8_t red, uint8_t green, uint8_t blue) {
  _settings.red = red;
  _settings.green = green;
  _settings.blue = blue;
  const bool ok = saveSettings();
  if (ok) emitRgbChanged();
  return ok;
}

bool RgbController::setBrightness(uint8_t brightness) {
  _settings.brightness = min(brightness, static_cast<uint8_t>(100));
  const bool ok = saveSettings();
  if (ok) emitRgbChanged();
  return ok;
}

void RgbController::loadSettings() {
  _settings.enabled = true;
  _settings.mode = RgbMode::SystemStatus;
  _settings.red = 0;
  _settings.green = 0;
  _settings.blue = 255;
  _settings.brightness = 80;

  if (!_storage) return;

  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  if (!_storage->readJson(RenzFiConfig::SETTINGS_FILE, doc)) return;

  JsonObject rgb = doc["rgb"];
  if (rgb.isNull()) return;

  if (rgb["enabled"].is<bool>()) {
    _settings.enabled = rgb["enabled"].as<bool>();
  } else if (rgb["enabled"].is<const char *>()) {
    _settings.enabled = String(rgb["enabled"].as<const char *>()) == "true";
  }
  if (rgb["mode"].is<const char *>()) {
    _settings.mode = parseRgbMode(rgb["mode"], _settings.mode);
  }
  if (rgb["red"].is<int>()) _settings.red = rgb["red"].as<uint8_t>();
  if (rgb["green"].is<int>()) _settings.green = rgb["green"].as<uint8_t>();
  if (rgb["blue"].is<int>()) _settings.blue = rgb["blue"].as<uint8_t>();
  if (rgb["brightness"].is<int>()) {
    _settings.brightness = rgb["brightness"].as<uint8_t>();
  }
}

bool RgbController::saveSettings() {
  if (!_storage) return false;

  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  if (!_storage->readJson(RenzFiConfig::SETTINGS_FILE, doc)) return false;

  JsonObject rgb = doc["rgb"].to<JsonObject>();
  rgb["enabled"] = _settings.enabled;
  rgb["mode"] = modeLabel(_settings.mode);
  rgb["red"] = _settings.red;
  rgb["green"] = _settings.green;
  rgb["blue"] = _settings.blue;
  rgb["brightness"] = _settings.brightness;
  return _storage->writeJson(RenzFiConfig::SETTINGS_FILE, doc);
}

void RgbController::setRgbLed(bool red, bool green, bool blue) {
  digitalWrite(RenzFiConfig::PIN_RGB_LED_RED, red ? HIGH : LOW);
  digitalWrite(RenzFiConfig::PIN_RGB_LED_GREEN, green ? HIGH : LOW);
  digitalWrite(RenzFiConfig::PIN_RGB_LED_BLUE, blue ? HIGH : LOW);
}

void RgbController::setRgbScaled(uint8_t red, uint8_t green, uint8_t blue) {
  const uint8_t threshold = 127;
  setRgbLed(scaleChannel(red, _settings.brightness) >= threshold,
            scaleChannel(green, _settings.brightness) >= threshold,
            scaleChannel(blue, _settings.brightness) >= threshold);
}

void RgbController::applyOutput() {
  static uint32_t lastTick = 0;
  if (millis() - lastTick < RenzFiConfig::LED_TICK_MS) return;
  lastTick = millis();

  if (_settings.mode != RgbMode::SystemStatus) {
    switch (_settings.mode) {
      case RgbMode::Off:
        setRgbLed(false, false, false);
        break;
      case RgbMode::Solid:
        setRgbScaled(_settings.red, _settings.green, _settings.blue);
        break;
      case RgbMode::Breathing: {
        const uint32_t phase = millis() / 20;
        const uint8_t wave =
            static_cast<uint8_t>((sin(phase * 0.05f) + 1.0f) * 50.0f);
        setRgbScaled(static_cast<uint8_t>((_settings.red * wave) / 100),
                     static_cast<uint8_t>((_settings.green * wave) / 100),
                     static_cast<uint8_t>((_settings.blue * wave) / 100));
        break;
      }
      case RgbMode::Rainbow: {
        const uint32_t hue = (millis() / 20) % 360;
        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        const uint8_t sector = hue / 60;
        const uint8_t remainder = (hue % 60) * 255 / 60;
        switch (sector) {
          case 0:
            r = 255;
            g = remainder;
            break;
          case 1:
            r = 255 - remainder;
            g = 255;
            break;
          case 2:
            g = 255;
            b = remainder;
            break;
          case 3:
            g = 255 - remainder;
            b = 255;
            break;
          case 4:
            r = remainder;
            b = 255;
            break;
          default:
            r = 255;
            b = 255 - remainder;
            break;
        }
        setRgbScaled(r, g, b);
        break;
      }
      default:
        break;
    }
    return;
  }

  const RgbSignal signal = resolveSignal();
  uint8_t red = 0;
  uint8_t green = 0;
  uint8_t blue = 0;
  resolveColor(signal, red, green, blue);

  if (signal == RgbSignal::Recovery && RecoveryManager::isMonitoring()) {
    const bool on = (millis() / RenzFiConfig::RECOVERY_RGB_FLASH_MS) % 2;
    setRgbScaled(on ? 255 : 0, on ? 255 : 0, on ? 255 : 0);
    return;
  }

  if (signal == RgbSignal::Booting) {
    setRgbScaled(255, 255, 0);
    return;
  }

  setRgbScaled(red, green, blue);
}