#include "StorageManager.h"

#include <SPI.h>

#include "config.h"

bool StorageManager::begin() {
  SPI.begin(RenzFiConfig::PIN_SD_SCK, RenzFiConfig::PIN_SD_MISO, RenzFiConfig::PIN_SD_MOSI, RenzFiConfig::PIN_SD_CS);
  _healthy = SD.begin(RenzFiConfig::PIN_SD_CS);
  if (!_healthy) {
    setError("SD card initialization failed");
    return false;
  }
  return ensureLayout();
}

bool StorageManager::healthy() const {
  return _healthy;
}

String StorageManager::lastError() const {
  return _lastError;
}

bool StorageManager::ensureLayout() {
  if (!_healthy) return false;

  bool ok = true;
  ok = ensureDir("/config") && ok;
  ok = ensureDir("/sales") && ok;
  ok = ensureDir("/logs") && ok;
  ok = ensureDir("/vouchers") && ok;
  ok = ensureDir("/sessions") && ok;
  ok = ensureDir(RenzFiConfig::WWW_ROOT) && ok;

  ok = ensureJsonFile(RenzFiConfig::SETTINGS_FILE,
                      "{\"admin\":{\"username\":\"admin\",\"passwordHash\":\"\",\"mustChangePassword\":true},\"coin\":{\"pesoPerPulse\":1,\"defaultMinutesPerPeso\":5,\"debounceMs\":35,\"settleMs\":450,\"enabled\":true},\"device\":{\"name\":\"Renz-Fi\",\"timezone\":\"Asia/Manila\"}}") &&
       ok;
  ok = ensureJsonFile(RenzFiConfig::PROMOS_FILE,
                      "[{\"id\":1,\"name\":\"Peso WiFi 5 minutes\",\"coin\":1,\"minutes\":5,\"speed\":0,\"devices\":1,\"data_cap_mb\":0}]") &&
       ok;
  ok = ensureJsonFile(RenzFiConfig::ROUTER_FILE,
                      "{\"host\":\"10.10.10.2\",\"username\":\"\",\"password\":\"\",\"profile\":\"default\",\"ssid\":\"Renz-Fi\"}") &&
       ok;
  ok = ensureJsonFile(RenzFiConfig::VOUCHERS_FILE, "[]") && ok;
  ok = ensureJsonFile(RenzFiConfig::SALES_FILE, "[]") && ok;
  ok = ensureJsonFile(RenzFiConfig::LOGS_FILE, "[]") && ok;
  ok = ensureJsonFile(RenzFiConfig::USERS_FILE, "[]") && ok;
  ok = ensureJsonFile(RenzFiConfig::ADMIN_SESSIONS_FILE, "[]") && ok;

  return ok;
}

bool StorageManager::readJson(const char *path, JsonDocument &doc) {
  if (!_healthy) return false;
  File file = SD.open(path, FILE_READ);
  if (!file) {
    setError(String("Unable to open ") + path);
    return false;
  }
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    setError(String("JSON parse failed for ") + path + ": " + err.c_str());
    String badPath = String(path) + ".bad";
    SD.remove(badPath);
    SD.rename(path, badPath);
    const char *emptyJson = (String(path).indexOf("/config/") >= 0) ? "{}" : "[]";
    ensureJsonFile(path, emptyJson);
    doc.clear();
    deserializeJson(doc, emptyJson);
    return true;
  }
  return true;
}

bool StorageManager::writeJson(const char *path, JsonDocument &doc) {
  if (!_healthy) return false;
  String tmp = String(path) + ".tmp";
  File file = SD.open(tmp, FILE_WRITE);
  if (!file) {
    setError(String("Unable to write ") + tmp);
    return false;
  }
  if (serializeJson(doc, file) == 0) {
    file.close();
    SD.remove(tmp);
    setError(String("JSON serialize failed for ") + path);
    return false;
  }
  file.flush();
  file.close();
  SD.remove(path);
  if (!SD.rename(tmp, path)) {
    setError(String("Unable to rename temp file for ") + path);
    return false;
  }
  return true;
}

bool StorageManager::appendJsonArrayItem(const char *path, JsonObject item, size_t capacity) {
  DynamicJsonDocument doc(capacity);
  if (!readJson(path, doc) || !doc.is<JsonArray>()) {
    doc.clear();
    doc.to<JsonArray>();
  }
  JsonArray arr = doc.as<JsonArray>();
  JsonObject target = arr.createNestedObject();
  for (JsonPair kv : item) {
    target[kv.key()] = kv.value();
  }
  return writeJson(path, doc);
}

bool StorageManager::clearJsonArray(const char *path) {
  DynamicJsonDocument doc(128);
  doc.to<JsonArray>();
  return writeJson(path, doc);
}

bool StorageManager::exists(const char *path) const {
  return SD.exists(path);
}

File StorageManager::openStatic(const String &path) {
  String full = String(RenzFiConfig::WWW_ROOT) + path;
  if (full.endsWith("/")) full += "index.html";
  if (!SD.exists(full) && !path.startsWith("/api/")) {
    full = String(RenzFiConfig::WWW_ROOT) + "/index.html";
  }
  return SD.open(full, FILE_READ);
}

String StorageManager::contentType(const String &path) const {
  if (path.endsWith(".html")) return "text/html; charset=utf-8";
  if (path.endsWith(".css")) return "text/css; charset=utf-8";
  if (path.endsWith(".js")) return "application/javascript; charset=utf-8";
  if (path.endsWith(".json") || path.endsWith(".webmanifest")) return "application/json; charset=utf-8";
  if (path.endsWith(".svg")) return "image/svg+xml";
  if (path.endsWith(".png")) return "image/png";
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
  if (path.endsWith(".ico")) return "image/x-icon";
  if (path.endsWith(".gz")) return "application/gzip";
  return "application/octet-stream";
}

uint64_t StorageManager::totalBytes() const {
  return _healthy ? SD.totalBytes() : 0;
}

uint64_t StorageManager::usedBytes() const {
  return _healthy ? SD.usedBytes() : 0;
}

void StorageManager::setError(const String &message) {
  _lastError = message;
  Serial.println("[storage] " + message);
}

bool StorageManager::ensureDir(const char *path) {
  if (SD.exists(path)) return true;
  if (!SD.mkdir(path)) {
    setError(String("Unable to create directory ") + path);
    return false;
  }
  return true;
}

bool StorageManager::ensureJsonFile(const char *path, const char *contents) {
  if (SD.exists(path)) return true;
  File file = SD.open(path, FILE_WRITE);
  if (!file) {
    setError(String("Unable to create ") + path);
    return false;
  }
  file.print(contents);
  file.close();
  return true;
}
