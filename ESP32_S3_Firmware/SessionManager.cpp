#include "SessionManager.h"

#include "Config.h"

static String durationText(uint32_t seconds) {
  uint32_t minutes = seconds / 60;
  if (minutes >= 60) return String(minutes / 60) + "h " + String(minutes % 60) + "m";
  return String(minutes) + "m";
}

void SessionManager::begin(StorageManager *storage, Logger *logger, EventBus *events) {
  _storage = storage;
  _logger = logger;
  _events = events;
}

bool SessionManager::listUsers(JsonDocument &doc) {
  if (!_storage || !_storage->readJson(RenzFiConfig::USERS_FILE, doc)) return false;
  uint32_t now = millis() / 1000;
  for (JsonObject user : doc.as<JsonArray>()) {
    uint32_t expiresAt = user["expiresAt"] | now;
    user["remaining"] = expiresAt > now ? durationText(expiresAt - now) : "expired";
  }
  return true;
}

bool SessionManager::disconnect(const String &mac) {
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  if (!listUsers(doc)) return false;
  DynamicJsonDocument next(RenzFiConfig::JSON_DOC_MEDIUM);
  JsonArray out = next.to<JsonArray>();
  bool removed = false;
  for (JsonObject user : doc.as<JsonArray>()) {
    if (mac.equals(user["mac"] | "")) {
      removed = true;
      continue;
    }
    JsonObject copy = out.createNestedObject();
    copy.set(user);
  }
  bool ok = _storage->writeJson(RenzFiConfig::USERS_FILE, next);
  if (ok && removed && _events) _events->emit("users.active");
  return ok && removed;
}

String SessionManager::grantCoinSession(int amount, int minutes) {
  String sessionId = makeSessionId();
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  if (!_storage->readJson(RenzFiConfig::USERS_FILE, doc) || !doc.is<JsonArray>()) {
    doc.clear();
    doc.to<JsonArray>();
  }

  JsonObject user = doc.as<JsonArray>().createNestedObject();
  user["mac"] = "coin-pending";
  user["ip"] = "0.0.0.0";
  user["device"] = "Coin session";
  user["source"] = "coin";
  user["sessionId"] = sessionId;
  user["startTime"] = millis() / 1000;
  user["expiresAt"] = (millis() / 1000) + (minutes * 60UL);
  user["amount"] = amount;

  _storage->writeJson(RenzFiConfig::USERS_FILE, doc);

  SaleRecord sale;
  sale.id = String("sale-") + sessionId;
  sale.timestamp = String("uptime-ms:") + millis();
  sale.amount = amount;
  sale.sessionId = sessionId;
  sale.paymentType = "coin";
  sale.durationMinutes = minutes;
  recordSale(sale);

  if (_events) {
    _events->emit("users.active");
    _events->emit("sales.changed");
  }
  if (_logger) _logger->info("coin", "Coin session granted");
  return sessionId;
}

bool SessionManager::recordSale(const SaleRecord &sale) {
  DynamicJsonDocument item(512);
  item["id"] = sale.id;
  item["timestamp"] = sale.timestamp;
  item["amount"] = sale.amount;
  item["sessionId"] = sale.sessionId;
  item["paymentType"] = sale.paymentType;
  item["durationMinutes"] = sale.durationMinutes;
  bool ok = _storage && _storage->appendJsonArrayItem(RenzFiConfig::SALES_FILE, item.as<JsonObject>(), RenzFiConfig::JSON_DOC_LARGE);
  if (ok && _events) _events->emit("sales.changed");
  return ok;
}

bool SessionManager::salesToday(JsonDocument &doc) {
  DynamicJsonDocument sales(RenzFiConfig::JSON_DOC_LARGE);
  if (!_storage || !_storage->readJson(RenzFiConfig::SALES_FILE, sales)) return false;
  int amount = 0;
  int sessions = 0;
  for (JsonObject sale : sales.as<JsonArray>()) {
    amount += sale["amount"] | 0;
    sessions++;
  }
  doc["amount"] = amount;
  doc["sessions"] = sessions;
  return true;
}

bool SessionManager::salesHistory(JsonDocument &doc) {
  DynamicJsonDocument today(256);
  salesToday(today);
  JsonArray arr = doc.to<JsonArray>();
  JsonObject row = arr.createNestedObject();
  row["date"] = "today";
  row["sessions"] = today["sessions"] | 0;
  row["revenue"] = today["amount"] | 0;
  return true;
}

bool SessionManager::salesPeriod(JsonDocument &doc, uint8_t days) {
  DynamicJsonDocument today(256);
  salesToday(today);
  doc["amount"] = today["amount"] | 0;
  doc["sessions"] = today["sessions"] | 0;
  doc["days"] = days;
  return true;
}

void SessionManager::cleanupExpired() {
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  if (!_storage || !_storage->readJson(RenzFiConfig::USERS_FILE, doc)) return;
  DynamicJsonDocument next(RenzFiConfig::JSON_DOC_MEDIUM);
  JsonArray out = next.to<JsonArray>();
  uint32_t now = millis() / 1000;
  bool changed = false;
  for (JsonObject user : doc.as<JsonArray>()) {
    if ((user["expiresAt"] | 0UL) <= now) {
      changed = true;
      continue;
    }
    JsonObject copy = out.createNestedObject();
    copy.set(user);
  }
  if (changed) {
    _storage->writeJson(RenzFiConfig::USERS_FILE, next);
    if (_events) _events->emit("users.active");
  }
}

int SessionManager::activeCount() {
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  if (!listUsers(doc)) return 0;
  return doc.as<JsonArray>().size();
}

bool SessionManager::hasActiveUsers() {
  return activeCount() > 0;
}

String SessionManager::makeSessionId() {
  return String(esp_random(), HEX) + String(millis(), HEX);
}
