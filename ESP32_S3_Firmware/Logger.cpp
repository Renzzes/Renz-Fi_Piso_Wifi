#include "Logger.h"

#include "Config.h"

static String logTimestamp() {
  return String("uptime-ms:") + millis();
}

void Logger::begin(StorageManager *storage, EventBus *events) {
  _storage = storage;
  _events = events;
}

void Logger::info(const String &type, const String &message) {
  write(LogLevel::Info, type, message);
}

void Logger::warn(const String &type, const String &message) {
  write(LogLevel::Warn, type, message);
}

void Logger::error(const String &type, const String &message) {
  write(LogLevel::Error, type, message);
}

bool Logger::list(JsonDocument &doc, const String &query) {
  if (!_storage || !_storage->readJson(RenzFiConfig::LOGS_FILE, doc)) return false;
  if (query.isEmpty()) return true;

  DynamicJsonDocument filtered(RenzFiConfig::JSON_DOC_MEDIUM);
  JsonArray out = filtered.to<JsonArray>();
  for (JsonObject item : doc.as<JsonArray>()) {
    String msg = item["msg"] | "";
    String type = item["type"] | "";
    if (msg.indexOf(query) >= 0 || type.indexOf(query) >= 0) {
      JsonObject copy = out.createNestedObject();
      copy.set(item);
    }
  }
  doc.clear();
  doc.set(filtered.as<JsonArray>());
  return true;
}

bool Logger::clear() {
  bool ok = _storage && _storage->clearJsonArray(RenzFiConfig::LOGS_FILE);
  if (ok && _events) _events->emit("logs.changed");
  return ok;
}

void Logger::write(LogLevel level, const String &type, const String &message) {
  Serial.printf("[%s] %s: %s\n", levelName(level), type.c_str(), message.c_str());
  if (!_storage) return;

  DynamicJsonDocument item(512);
  item["id"] = millis();
  item["t"] = logTimestamp();
  item["lvl"] = levelName(level);
  item["type"] = type;
  item["msg"] = message;

  _storage->appendJsonArrayItem(RenzFiConfig::LOGS_FILE, item.as<JsonObject>(), RenzFiConfig::JSON_DOC_LARGE);
  if (_events) _events->emit("logs.changed");
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
