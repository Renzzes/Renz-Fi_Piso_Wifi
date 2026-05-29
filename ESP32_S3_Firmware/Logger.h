#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "EventBus.h"
#include "Models.h"
#include "StorageManager.h"

class Logger {
 public:
  void begin(StorageManager *storage, EventBus *events);
  void info(const String &type, const String &message);
  void warn(const String &type, const String &message);
  void error(const String &type, const String &message);
  bool list(JsonDocument &doc, const String &query = "");
  bool clear();

 private:
  StorageManager *_storage = nullptr;
  EventBus *_events = nullptr;

  void write(LogLevel level, const String &type, const String &message);
  const char *levelName(LogLevel level) const;
};
