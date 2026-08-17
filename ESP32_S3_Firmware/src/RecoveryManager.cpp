#include "RecoveryManager.h"

#include "AuthCredentials.h"
#include "Config.h"
#include "NetworkSettingsManager.h"

namespace {

bool _monitoring = false;
uint32_t _holdStartMs = 0;
uint32_t _lastPollMs = 0;

void setRgbLed(bool red, bool green, bool blue) {
  digitalWrite(RenzFiConfig::PIN_RGB_LED_RED, red ? HIGH : LOW);
  digitalWrite(RenzFiConfig::PIN_RGB_LED_GREEN, green ? HIGH : LOW);
  digitalWrite(RenzFiConfig::PIN_RGB_LED_BLUE, blue ? HIGH : LOW);
}

void initRecoveryPins() {
  pinMode(RenzFiConfig::PIN_RECOVERY, INPUT_PULLUP);
  pinMode(RenzFiConfig::PIN_RGB_LED_RED, OUTPUT);
  pinMode(RenzFiConfig::PIN_RGB_LED_GREEN, OUTPUT);
  pinMode(RenzFiConfig::PIN_RGB_LED_BLUE, OUTPUT);
  setRgbLed(false, false, false);
}

void flashPattern(bool red, bool green, bool blue) {
  for (uint8_t i = 0; i < RenzFiConfig::RECOVERY_RGB_FLASH_COUNT; i++) {
    setRgbLed(red, green, blue);
    delay(RenzFiConfig::RECOVERY_RGB_FLASH_MS);
    setRgbLed(false, false, false);
    delay(RenzFiConfig::RECOVERY_RGB_FLASH_MS);
  }
}

void applyLevel1() {
  Serial.println("[recovery] Level 1 — password reset to default");
  AuthCredentials::applyRecoveryReset();
}

void applyLevel2() {
  Serial.println("[recovery] Level 2 — password + network reset to defaults");
  AuthCredentials::applyRecoveryReset();
  NetworkSettingsManager::applyRecoveryResetNvs();
}

void executeRecovery(RecoveryLevel level) {
  _monitoring = false;
  setRgbLed(false, false, false);

  if (level == RecoveryLevel::Level1) {
    applyLevel1();
    flashPattern(false, false, true);
    Serial.println("[recovery] L1 complete — rebooting");
  } else if (level == RecoveryLevel::Level2) {
    applyLevel2();
    flashPattern(true, true, false);
    Serial.println("[recovery] L2 complete — rebooting");
  } else {
    return;
  }

  delay(500);
  ESP.restart();
}

uint32_t currentHoldMs() {
  if (_holdStartMs == 0) return 0;
  return millis() - _holdStartMs;
}

void updateHoldFeedback(uint32_t holdMs) {
  if (holdMs >= RenzFiConfig::RECOVERY_LEVEL1_HOLD_MS &&
      holdMs < RenzFiConfig::RECOVERY_LEVEL2_HOLD_MS) {
    setRgbLed(false, false, (millis() / RenzFiConfig::RECOVERY_RGB_FLASH_MS) % 2);
  } else if (holdMs >= RenzFiConfig::RECOVERY_LEVEL2_HOLD_MS) {
    setRgbLed(true, true, false);
  }
}

// Returns true when recovery triggered (caller should not continue normal work).
RecoveryLevel pollRecoveryHold() {
  const bool pressed = digitalRead(RenzFiConfig::PIN_RECOVERY) == LOW;
  const uint32_t holdMs = currentHoldMs();

  if (pressed) {
    if (_holdStartMs == 0) {
      _holdStartMs = millis();
      _monitoring = true;
    }
    const uint32_t elapsed = currentHoldMs();

    if (elapsed >= RenzFiConfig::RECOVERY_LEVEL2_HOLD_MS) {
      return RecoveryLevel::Level2;
    }

    updateHoldFeedback(elapsed);
    return RecoveryLevel::None;
  }

  if (holdMs >= RenzFiConfig::RECOVERY_LEVEL1_HOLD_MS &&
      holdMs < RenzFiConfig::RECOVERY_LEVEL2_HOLD_MS) {
    return RecoveryLevel::Level1;
  }

  _holdStartMs = 0;
  _monitoring = false;
  setRgbLed(false, false, false);
  return RecoveryLevel::None;
}

}  // namespace

void RecoveryManager::runBootCheck() {
  initRecoveryPins();
  _holdStartMs = 0;
  _monitoring = false;

  Serial.println("[recovery] Boot check — hold GPIO2 for recovery");
  Serial.printf("[recovery] detection window %lus, L1 >= %lus, L2 >= %lus\n",
                RenzFiConfig::RECOVERY_BOOT_WINDOW_MS / 1000UL,
                RenzFiConfig::RECOVERY_LEVEL1_HOLD_MS / 1000UL,
                RenzFiConfig::RECOVERY_LEVEL2_HOLD_MS / 1000UL);

  if (digitalRead(RenzFiConfig::PIN_RECOVERY) == LOW) {
    _holdStartMs = millis();
    _monitoring = true;
    Serial.println("[recovery] Button held — timer started at boot");
  } else {
    Serial.println("[recovery] No button — continuing boot");
    return;
  }

  const uint32_t windowStart = millis();
  while (_monitoring && millis() - windowStart < RenzFiConfig::RECOVERY_BOOT_WINDOW_MS) {
    const RecoveryLevel level = pollRecoveryHold();
    if (level != RecoveryLevel::None) {
      executeRecovery(level);
      return;
    }
    delay(50);
  }

  if (_monitoring) {
    Serial.println("[recovery] Still held after boot window — monitoring continues");
  } else {
    Serial.println("[recovery] No recovery action");
  }
}

void RecoveryManager::loop() {
  if (!_monitoring) return;

  const uint32_t now = millis();
  if (now - _lastPollMs < 50) return;
  _lastPollMs = now;

  const RecoveryLevel level = pollRecoveryHold();
  if (level != RecoveryLevel::None) {
    executeRecovery(level);
  }
}

bool RecoveryManager::isMonitoring() {
  return _monitoring;
}
