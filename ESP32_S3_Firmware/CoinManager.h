#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "EventBus.h"
#include "Logger.h"
#include "Models.h"
#include "PromoManager.h"
#include "SessionManager.h"
#include "StorageManager.h"

class CoinManager {
 public:
  void begin(StorageManager *storage, Logger *logger, EventBus *events, PromoManager *promos, SessionManager *sessions);
  void loop();
  bool settings(JsonDocument &doc);
  bool saveSettings(JsonObjectConst settings);
  bool diagnostics(JsonDocument &doc);
  LedMode ledMode() const;
  static void IRAM_ATTR isrThunk();

 private:
  StorageManager *_storage = nullptr;
  Logger *_logger = nullptr;
  EventBus *_events = nullptr;
  PromoManager *_promos = nullptr;
  SessionManager *_sessions = nullptr;

  volatile uint32_t _pulses = 0;
  volatile uint32_t _lastPulseMs = 0;
  uint32_t _lastProcessedPulseMs = 0;
  uint32_t _pulsesToday = 0;
  uint32_t _lastAcceptedCoinMs = 0;
  CoinSettings _settings;
  LedMode _ledMode = LedMode::Waiting;

  void IRAM_ATTR handlePulse();
  void loadSettings();
  void processCoin(uint32_t pulses);
  void updateLed();
  void setRgbLed(bool red, bool green, bool blue);
};
