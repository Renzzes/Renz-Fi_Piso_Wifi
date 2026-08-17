#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "Models.h"

class CoinManager;
class EthernetManager;
class RgbController;
class StorageManager;

class SystemHealthService {
 public:
  void begin(EthernetManager *eth, StorageManager *storage,
             CoinManager *coin, RgbController *rgb);

  void fillHealth(JsonObject out) const;
  SystemHealthLevel overallLevel() const;

 private:
  EthernetManager *_eth = nullptr;
  StorageManager *_storage = nullptr;
  CoinManager *_coin = nullptr;
  RgbController *_rgb = nullptr;
};
