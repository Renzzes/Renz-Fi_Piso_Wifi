#include "PromoManager.h"

#include "config.h"

void PromoManager::begin(StorageManager *storage, Logger *logger, EventBus *events) {
  _storage = storage;
  _logger = logger;
  _events = events;
}

bool PromoManager::list(JsonDocument &doc) {
  return _storage && _storage->readJson(RenzFiConfig::PROMOS_FILE, doc);
}

int PromoManager::create(JsonObjectConst promo) {
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  if (!list(doc)) return -1;
  JsonArray arr = doc.as<JsonArray>();
  int id = nextId(arr);
  JsonObject item = arr.createNestedObject();
  item["id"] = id;
  item["name"] = promo["name"] | "Promo";
  item["coin"] = promo["coin"] | 1;
  item["minutes"] = promo["minutes"] | 5;
  item["speed"] = promo["speed"] | 0;
  item["devices"] = promo["devices"] | 1;
  item["data_cap_mb"] = promo["data_cap_mb"] | 0;
  if (!_storage->writeJson(RenzFiConfig::PROMOS_FILE, doc)) return -1;
  if (_logger) _logger->info("promos", "Promo created");
  if (_events) _events->emit("promos.changed");
  return id;
}

bool PromoManager::update(int id, JsonObjectConst promo) {
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  if (!list(doc)) return false;
  for (JsonObject item : doc.as<JsonArray>()) {
    if ((item["id"] | -1) == id) {
      item["name"] = promo["name"] | item["name"];
      item["coin"] = promo["coin"] | item["coin"];
      item["minutes"] = promo["minutes"] | item["minutes"];
      item["speed"] = promo["speed"] | item["speed"];
      item["devices"] = promo["devices"] | item["devices"];
      item["data_cap_mb"] = promo["data_cap_mb"] | item["data_cap_mb"];
      bool ok = _storage->writeJson(RenzFiConfig::PROMOS_FILE, doc);
      if (ok && _events) _events->emit("promos.changed");
      return ok;
    }
  }
  return false;
}

bool PromoManager::remove(int id) {
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  if (!list(doc)) return false;
  DynamicJsonDocument next(RenzFiConfig::JSON_DOC_MEDIUM);
  JsonArray out = next.to<JsonArray>();
  bool removed = false;
  for (JsonObject item : doc.as<JsonArray>()) {
    if ((item["id"] | -1) == id) {
      removed = true;
      continue;
    }
    JsonObject copy = out.createNestedObject();
    copy.set(item);
  }
  if (!removed) return false;
  bool ok = _storage->writeJson(RenzFiConfig::PROMOS_FILE, next);
  if (ok && _events) _events->emit("promos.changed");
  return ok;
}

int PromoManager::minutesForAmount(int amount) {
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  if (!list(doc)) return amount * 5;
  int bestCoin = 0;
  int bestMinutes = amount * 5;
  for (JsonObject promo : doc.as<JsonArray>()) {
    int coin = promo["coin"] | 0;
    int minutes = promo["minutes"] | 0;
    if (coin <= amount && coin > bestCoin && minutes > 0) {
      bestCoin = coin;
      bestMinutes = minutes;
    }
  }
  return bestMinutes;
}

int PromoManager::nextId(JsonArray arr) {
  int id = 1;
  for (JsonObject item : arr) id = max(id, (item["id"] | 0) + 1);
  return id;
}
