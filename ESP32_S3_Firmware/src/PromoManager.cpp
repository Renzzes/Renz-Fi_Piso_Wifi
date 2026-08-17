#include "PromoManager.h"

#include "Config.h"
#include "JsonHeap.h"

void PromoManager::begin(StorageManager *storage, Logger *logger, EventBus *events) {
  _storage = storage;
  _logger = logger;
  _events = events;
  _cacheLoaded = false;
  ensureCacheLoaded();
}

bool PromoManager::ensureCacheLoaded() {
  return loadCache();
}

void PromoManager::rememberCache(JsonDocument &doc) {
  if (!_cache) {
    _cache = new DynamicJsonDocument(RenzFiConfig::JSON_DOC_MEDIUM);
  }
  if (!_cache) {
    _cacheLoaded = false;
    return;
  }
  _cache->clear();
  _cache->set(doc);
  _cacheLoaded = true;
}

bool PromoManager::loadCache() {
  if (_cacheLoaded && _cache) return true;
  if (!_storage) return false;
  if (!_cache) {
    _cache = new DynamicJsonDocument(RenzFiConfig::JSON_DOC_MEDIUM);
  }
  if (!_cache) return false;
  Serial.println("[rates] promo cache miss — reading /config/promos.json");
  _cache->clear();
  if (!_storage->readJson(RenzFiConfig::PROMOS_FILE, *_cache)) {
    _cacheLoaded = false;
    return false;
  }
  _cacheLoaded = true;
  return true;
}

bool PromoManager::list(JsonDocument &doc) {
  if (!_cacheLoaded || !_cache) return false;
  doc.clear();
  doc.set(*_cache);
  return true;
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
  // Optional speed association (backward compatible — omitted on legacy promos).
  if (promo["speedMode"].is<const char *>()) {
    item["speedMode"] = promo["speedMode"].as<const char *>();
  }
  if (promo["profileName"].is<const char *>()) {
    item["profileName"] = promo["profileName"].as<const char *>();
  }
  if (!promo["customDownloadMbps"].isNull()) {
    item["customDownloadMbps"] = promo["customDownloadMbps"];
  }
  if (!promo["customUploadMbps"].isNull()) {
    item["customUploadMbps"] = promo["customUploadMbps"];
  }
  if (promo["managedProfileName"].is<const char *>()) {
    item["managedProfileName"] = promo["managedProfileName"].as<const char *>();
  }
  if (!_storage->writeJson(RenzFiConfig::PROMOS_FILE, doc)) return -1;
  rememberCache(doc);
  if (_logger) _logger->infoLocal("promos", "Promo created");
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
      if (promo["speedMode"].is<const char *>()) {
        item["speedMode"] = promo["speedMode"].as<const char *>();
      }
      if (promo["profileName"].is<const char *>()) {
        item["profileName"] = promo["profileName"].as<const char *>();
      } else if (promo.containsKey("profileName") && promo["profileName"].isNull()) {
        item.remove("profileName");
      }
      if (!promo["customDownloadMbps"].isNull()) {
        item["customDownloadMbps"] = promo["customDownloadMbps"];
      }
      if (!promo["customUploadMbps"].isNull()) {
        item["customUploadMbps"] = promo["customUploadMbps"];
      }
      if (promo["managedProfileName"].is<const char *>()) {
        item["managedProfileName"] = promo["managedProfileName"].as<const char *>();
      } else if (promo.containsKey("managedProfileName") &&
                 promo["managedProfileName"].isNull()) {
        item.remove("managedProfileName");
      }
      bool ok = _storage->writeJson(RenzFiConfig::PROMOS_FILE, doc);
      if (ok) rememberCache(doc);
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
  if (ok) rememberCache(next);
  if (ok && _events) _events->emit("promos.changed");
  return ok;
}

int PromoManager::minutesForAmount(int amount, int *matchedCoinOut) {
  return resolveForAmount(amount, nullptr, nullptr, matchedCoinOut);
}

int PromoManager::resolveForAmount(int amount, String *profileOut, int *promoIdOut,
                                   int *matchedCoinOut) {
  if (matchedCoinOut) *matchedCoinOut = 0;
  if (promoIdOut) *promoIdOut = 0;
  if (profileOut) profileOut->clear();
  if (amount <= 0) return 0;

  // Single physical insertion contract:
  //   Resolve the exact denomination (coin == amount) against the promo table.
  //   Do NOT decompose or re-price accumulated totals — callers accumulate
  //   purchasedMinutes per insertion in PortalSessionManager::onCoinInserted.
  // Fallback when no exact promo exists: amount * 5 (legacy per-peso rate).
  // Heap allocation — may run on async_tcp; avoid large stack docs.
  HeapJsonDocument heapDoc(RenzFiConfig::JSON_DOC_MEDIUM);
  DynamicJsonDocument &doc = heapDoc.doc();
  if (!list(doc)) return amount * 5;

  for (JsonObject promo : doc.as<JsonArray>()) {
    if (promo["enabled"] == false) continue;
    const int coin = promo["coin"] | 0;
    const int minutes = promo["minutes"] | 0;
    if (coin != amount || minutes <= 0) continue;

    if (matchedCoinOut) *matchedCoinOut = coin;
    if (promoIdOut) *promoIdOut = promo["id"] | 0;
    if (profileOut) {
      const String managed = promo["managedProfileName"] | "";
      const String named = promo["profileName"] | "";
      if (managed.length() > 0) {
        *profileOut = managed;
      } else if (named.length() > 0) {
        *profileOut = named;
      }
    }
    return minutes;
  }
  return amount * 5;
}

bool PromoManager::resolveHighestProfileForAmount(int amount, String *profileOut,
                                                  int *promoIdOut) {
  if (profileOut) profileOut->clear();
  if (promoIdOut) *promoIdOut = 0;
  if (amount <= 0) return false;

  // Profile-only helper for Done Paying / first activation. Does not define
  // purchased time — entitlement minutes come from per-insertion accumulation.
  HeapJsonDocument heapDoc(RenzFiConfig::JSON_DOC_MEDIUM);
  DynamicJsonDocument &doc = heapDoc.doc();
  if (!list(doc)) return false;

  int bestCoin = 0;
  JsonObject bestPromo;
  for (JsonObject promo : doc.as<JsonArray>()) {
    if (promo["enabled"] == false) continue;
    const int coin = promo["coin"] | 0;
    const int minutes = promo["minutes"] | 0;
    if (coin <= 0 || minutes <= 0 || coin > amount) continue;
    if (coin > bestCoin) {
      bestCoin = coin;
      bestPromo = promo;
    }
  }
  if (bestPromo.isNull()) return false;

  if (promoIdOut) *promoIdOut = bestPromo["id"] | 0;
  if (profileOut) {
    const String managed = bestPromo["managedProfileName"] | "";
    const String named = bestPromo["profileName"] | "";
    if (managed.length() > 0) {
      *profileOut = managed;
    } else if (named.length() > 0) {
      *profileOut = named;
    }
  }
  return profileOut && profileOut->length() > 0;
}

int PromoManager::nextId(JsonArray arr) {
  int id = 1;
  for (JsonObject item : arr) id = max(id, (item["id"] | 0) + 1);
  return id;
}
