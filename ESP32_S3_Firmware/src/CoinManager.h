#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "EventBus.h"
#include "Logger.h"
#include "Models.h"
#include "PortalSessionManager.h"
#include "PromoManager.h"
#include "StorageManager.h"

class CoinManager {
 public:
  void begin(StorageManager *storage, Logger *logger, EventBus *events,
             PromoManager *promos, PortalSessionManager *portalSessions);
  void loop();
  bool settings(JsonDocument &doc);
  bool saveSettings(JsonObjectConst settings);
  bool diagnostics(JsonDocument &doc);
  void resetCounters();
  void fillStatus(JsonObject coinSlot) const;
  uint32_t insertTimeoutSeconds() const;
  LedMode ledMode() const;
  static void IRAM_ATTR isrThunk();

 private:
  StorageManager *_storage = nullptr;
  Logger *_logger = nullptr;
  EventBus *_events = nullptr;
  PromoManager          *_promos         = nullptr;
  PortalSessionManager  *_portalSessions = nullptr;

  volatile uint32_t _pulses = 0;
  volatile uint32_t _lastPulseMs = 0;
  uint32_t _lastProcessedPulseMs = 0;
  uint32_t _pulsesToday = 0;
  uint32_t _lastAcceptedPulses = 0;
  uint32_t _lastAcceptedCoinMs = 0;
  uint32_t _errors = 0;
  CoinSettings _settings;
  LedMode _ledMode = LedMode::Waiting;
  bool _coinIsrAttached = false;
  bool _beginComplete = false;

  static constexpr size_t kDiagLogMax = 12;
  String _diagLogMsg[kDiagLogMax];
  uint32_t _diagLogAtMs[kDiagLogMax];
  size_t _diagLogCount = 0;

  void IRAM_ATTR handlePulse();
  void loadSettings();
  void processCoin(uint32_t pulses);
  void updateLed();
  void setRgbLed(bool red, bool green, bool blue);
  void appendDiagLog(const String &msg);
  const char *stateLabel() const;
  void detachCoinIsr();
};
