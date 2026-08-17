#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "EventBus.h"
#include "Models.h"
#include "StorageManager.h"

class EthernetManager;
class CoinManager;

class RgbController {
 public:
  void begin(StorageManager *storage, EventBus *events, EthernetManager *eth,
             StorageManager *storageHealth, CoinManager *coin);
  void loop();
  void markBootComplete();

  bool fillStatus(JsonObject out) const;
  void fillRgbStatus(JsonObject out) const;
  RgbStatus statusSnapshot() const;

  bool applySettings(bool enabled, uint8_t brightness);
  bool setMode(RgbMode mode);
  bool setColor(uint8_t red, uint8_t green, uint8_t blue);
  bool setBrightness(uint8_t brightness);

  RgbMode mode() const;
  RgbSettings settings() const;
  RgbSignal activeSignal() const;
  SystemHealthLevel systemStatusLevel() const;

  static void eventThunk(const char *event, const String &json, void *ctx);

 private:
  StorageManager *_storage = nullptr;
  EventBus *_events = nullptr;
  EthernetManager *_eth = nullptr;
  CoinManager *_coin = nullptr;

  RgbSettings _settings;
  bool _beginComplete = false;
  bool _bootComplete = false;
  uint32_t _bootStartedMs = 0;
  uint32_t _coinAcceptedUntilMs = 0;
  bool _otaActive = false;

  void loadSettings();
  bool saveSettings();
  void applyOutput();
  RgbSignal resolveSignal() const;
  void resolveColor(RgbSignal signal, uint8_t &red, uint8_t &green,
                    uint8_t &blue) const;
  static const char *signalLabel(RgbSignal signal);
  static const char *colorNameForSignal(RgbSignal signal);
  void setRgbLed(bool red, bool green, bool blue);
  void setRgbScaled(uint8_t red, uint8_t green, uint8_t blue);
  static const char *modeLabel(RgbMode mode);
  void handleEvent(const char *event, const String &json);
  void showCoinAccepted();
  void emitRgbChanged() const;
};
