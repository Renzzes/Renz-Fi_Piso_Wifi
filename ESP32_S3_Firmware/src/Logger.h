#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "Config.h"
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
  bool exportRam(JsonDocument &doc) const;
  size_t ramCount() const;

 private:
  StorageManager *_storage = nullptr;
  EventBus *_events = nullptr;

  struct RamEntry {
    uint32_t id = 0;
    String t;
    String lvl;
    String type;
    String msg;
    bool used = false;
  };

  RamEntry _ram[RenzFiConfig::LOG_RAM_BUFFER_SIZE];
  size_t _ramHead = 0;
  size_t _ramCount = 0;

  void write(LogLevel level, const String &type, const String &message);
  void pushRam(uint32_t id, const String &t, const String &lvl,
               const String &type, const String &msg);
  void emitEntry(uint32_t id, const String &t, const String &lvl,
                 const String &type, const String &msg);
  const char *levelName(LogLevel level) const;
};
