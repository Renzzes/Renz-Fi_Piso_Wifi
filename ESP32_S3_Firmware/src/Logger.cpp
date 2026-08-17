#include "Logger.h"

#include "Config.h"
#include "SalesTime.h"

#include <esp_system.h>

static String logTimestamp() {
  return String("uptime-ms:") + millis();
}

void Logger::begin(StorageManager *storage, EventBus *events) {
  _storage = storage;
  _events = events;
  const uint64_t chipId = ESP.getEfuseMac();
  char bootId[33];
  snprintf(bootId, sizeof(bootId), "%08lx%08lx%08lx",
           static_cast<unsigned long>(chipId >> 32),
           static_cast<unsigned long>(chipId),
           static_cast<unsigned long>(esp_random()));
  _bootInstance = bootId;
  _historySequence = 0;
}

void Logger::info(const String &type, const String &message) {
  write(LogLevel::Info, type, message, true);
}

void Logger::infoLocal(const String &type, const String &message) {
  write(LogLevel::Info, type, message, false);
}

void Logger::warn(const String &type, const String &message) {
  write(LogLevel::Warn, type, message, true);
}

void Logger::warnLocal(const String &type, const String &message) {
  write(LogLevel::Warn, type, message, false);
}

void Logger::error(const String &type, const String &message) {
  write(LogLevel::Error, type, message, true);
}

void Logger::errorLocal(const String &type, const String &message) {
  write(LogLevel::Error, type, message, false);
}

size_t Logger::ramCount() const { return _ramCount; }

void Logger::pushRam(uint32_t id, const String &t, const String &lvl,
                     const String &type, const String &msg) {
  RamEntry &slot = _ram[_ramHead];
  slot.id = id;
  slot.t = t;
  slot.lvl = lvl;
  slot.type = type;
  slot.msg = msg;
  slot.used = true;

  _ramHead = (_ramHead + 1) % RenzFiConfig::LOG_RAM_BUFFER_SIZE;
  if (_ramCount < RenzFiConfig::LOG_RAM_BUFFER_SIZE) _ramCount++;
}

void Logger::emitEntry(uint32_t id, const String &t, const String &lvl,
                       const String &type, const String &msg) {
  if (!_events) return;

  DynamicJsonDocument payload(768);
  payload["id"] = id;
  payload["t"] = t;
  payload["lvl"] = lvl;
  payload["type"] = type;
  payload["msg"] = msg;

  String json;
  serializeJson(payload, json);
  _events->emit("log.entry", json);
  _events->emit("logs.changed", "{}");
}

bool Logger::list(JsonDocument &doc, const String &query) {
  JsonArray out = doc.to<JsonArray>();

  const size_t cap = RenzFiConfig::LOG_RAM_BUFFER_SIZE;
  for (size_t i = 0; i < _ramCount; i++) {
    const size_t idx =
        (_ramHead + cap - _ramCount + i) % cap;
    const RamEntry &entry = _ram[idx];
    if (!entry.used) continue;

    if (!query.isEmpty()) {
      if (entry.msg.indexOf(query) < 0 && entry.type.indexOf(query) < 0 &&
          entry.lvl.indexOf(query) < 0) {
        continue;
      }
    }

    JsonObject row = out.createNestedObject();
    row["id"] = entry.id;
    row["t"] = entry.t;
    row["lvl"] = entry.lvl;
    row["type"] = entry.type;
    row["msg"] = entry.msg;
  }

  return true;
}

bool Logger::exportRam(JsonDocument &doc) const {
  JsonArray out = doc.to<JsonArray>();
  const size_t cap = RenzFiConfig::LOG_RAM_BUFFER_SIZE;
  for (size_t i = 0; i < _ramCount; i++) {
    const size_t idx = (_ramHead + cap - _ramCount + i) % cap;
    const RamEntry &entry = _ram[idx];
    if (!entry.used) continue;
    JsonObject row = out.createNestedObject();
    row["id"] = entry.id;
    row["t"] = entry.t;
    row["lvl"] = entry.lvl;
    row["type"] = entry.type;
    row["msg"] = entry.msg;
  }
  return true;
}

bool Logger::clear() {
  for (size_t i = 0; i < RenzFiConfig::LOG_RAM_BUFFER_SIZE; i++) {
    _ram[i].used = false;
    _ram[i].msg = "";
    _ram[i].type = "";
  }
  _ramHead = 0;
  _ramCount = 0;
  if (_events) _events->emit("logs.changed", "{}");
  return true;
}

void Logger::write(LogLevel level, const String &type, const String &message,
                   bool durableHistory) {
  const uint32_t id = millis();
  const String t = logTimestamp();
  const String lvl = levelName(level);

  Serial.printf("[%s] %s: %s\n", lvl.c_str(), type.c_str(), message.c_str());

  pushRam(id, t, lvl, type, message);
  emitEntry(id, t, lvl, type, message);

  // Durable NDJSON append + flush is storage I/O. Callers on async_tcp must
  // pass durableHistory=false when this work would risk the task WDT.
  if (!durableHistory) return;
  if (!_storage || !_storage->healthy()) return;

  DynamicJsonDocument item(512);
  item["id"] = id;
  item["t"] = t;
  item["lvl"] = lvl;
  item["type"] = type;
  item["msg"] = message;

  const String eventId =
      String("log:") + _bootInstance + ":" + (++_historySequence);
  _storage->appendHistory(NdjsonLedger::Kind::Logs, eventId,
                          salesRecordedAtNow(),
                          item.as<JsonObjectConst>(), false);
}

const char *Logger::levelName(LogLevel level) const {
  switch (level) {
    case LogLevel::Warn:
      return "WARN";
    case LogLevel::Error:
      return "ERROR";
    case LogLevel::Info:
    default:
      return "INFO";
  }
}
