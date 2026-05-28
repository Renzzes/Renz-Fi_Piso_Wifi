#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "EventBus.h"
#include "Logger.h"
#include "StorageManager.h"

class PromoManager {
 public:
  void begin(StorageManager *storage, Logger *logger, EventBus *events);
  bool list(JsonDocument &doc);
  int create(JsonObjectConst promo);
  bool update(int id, JsonObjectConst promo);
  bool remove(int id);
  int minutesForAmount(int amount);

 private:
  StorageManager *_storage = nullptr;
  Logger *_logger = nullptr;
  EventBus *_events = nullptr;

  int nextId(JsonArray arr);
};
