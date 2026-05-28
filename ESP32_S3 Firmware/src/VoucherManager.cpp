#include "VoucherManager.h"

#include "config.h"

void VoucherManager::begin(StorageManager *storage, Logger *logger, EventBus *events) {
  _storage = storage;
  _logger = logger;
  _events = events;
}

bool VoucherManager::list(JsonDocument &doc) {
  return _storage && _storage->readJson(RenzFiConfig::VOUCHERS_FILE, doc);
}

bool VoucherManager::find(const String &code, JsonDocument &doc) {
  DynamicJsonDocument all(RenzFiConfig::JSON_DOC_LARGE);
  if (!list(all)) return false;
  for (JsonObject item : all.as<JsonArray>()) {
    if ((item["code"] | "") == code) {
      doc.set(item);
      return true;
    }
  }
  return false;
}

bool VoucherManager::generate(int count, int amount, int minutes, const String &expires, JsonDocument &response) {
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_LARGE);
  if (!list(doc)) return false;
  JsonArray vouchers = doc.as<JsonArray>();
  JsonArray created = response["created"].to<JsonArray>();

  for (int i = 0; i < count; i++) {
    String code = makeCode();
    JsonObject item = vouchers.createNestedObject();
    item["code"] = code;
    item["amount"] = amount;
    item["minutes"] = minutes;
    item["status"] = "unused";
    item["expires"] = expires.isEmpty() ? "never" : expires;
    created.add(code);
  }

  bool ok = _storage->writeJson(RenzFiConfig::VOUCHERS_FILE, doc);
  if (ok && _events) _events->emit("vouchers.changed");
  if (ok && _logger) _logger->info("vouchers", "Vouchers generated");
  return ok;
}

bool VoucherManager::remove(const String &code) {
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_LARGE);
  if (!list(doc)) return false;
  DynamicJsonDocument next(RenzFiConfig::JSON_DOC_LARGE);
  JsonArray out = next.to<JsonArray>();
  bool removed = false;
  for (JsonObject item : doc.as<JsonArray>()) {
    if ((item["code"] | "") == code) {
      removed = true;
      continue;
    }
    JsonObject copy = out.createNestedObject();
    copy.set(item);
  }
  if (!removed) return false;
  bool ok = _storage->writeJson(RenzFiConfig::VOUCHERS_FILE, next);
  if (ok && _events) _events->emit("vouchers.changed");
  return ok;
}

bool VoucherManager::markActive(const String &code) {
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_LARGE);
  if (!list(doc)) return false;
  for (JsonObject item : doc.as<JsonArray>()) {
    if ((item["code"] | "") == code) {
      item["status"] = "active";
      bool ok = _storage->writeJson(RenzFiConfig::VOUCHERS_FILE, doc);
      if (ok && _events) _events->emit("vouchers.changed");
      return ok;
    }
  }
  return false;
}

String VoucherManager::makeCode() {
  const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  String code;
  for (int i = 0; i < 10; i++) {
    code += alphabet[esp_random() % (sizeof(alphabet) - 1)];
    if (i == 4) code += "-";
  }
  return code;
}
