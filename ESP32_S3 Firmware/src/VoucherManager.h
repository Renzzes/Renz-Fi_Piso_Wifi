#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "EventBus.h"
#include "Logger.h"
#include "StorageManager.h"

class VoucherManager {
 public:
  void begin(StorageManager *storage, Logger *logger, EventBus *events);
  bool list(JsonDocument &doc);
  bool find(const String &code, JsonDocument &doc);
  bool generate(int count, int amount, int minutes, const String &expires, JsonDocument &response);
  bool remove(const String &code);
  bool markActive(const String &code);

 private:
  StorageManager *_storage = nullptr;
  Logger *_logger = nullptr;
  EventBus *_events = nullptr;

  String makeCode();
};
