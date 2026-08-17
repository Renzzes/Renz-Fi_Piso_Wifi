#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "EventBus.h"
#include "Logger.h"
#include "StorageManager.h"

class PromoManager {
 public:
  void begin(StorageManager *storage, Logger *logger, EventBus *events);
  /** loopTask / boot only — may read promos.json. No-op if already cached. */
  bool ensureCacheLoaded();
  bool list(JsonDocument &doc);
  int create(JsonObjectConst promo);
  bool update(int id, JsonObjectConst promo);
  bool remove(int id);
  int minutesForAmount(int amount, int *matchedCoinOut = nullptr);
  /** Exact denomination lookup for one physical coin insertion. */
  int resolveForAmount(int amount, String *profileOut = nullptr,
                       int *promoIdOut = nullptr, int *matchedCoinOut = nullptr);
  /** Highest matching promo profile for a sale total (minutes not used). */
  bool resolveHighestProfileForAmount(int amount, String *profileOut,
                                      int *promoIdOut = nullptr);

 private:
  StorageManager *_storage = nullptr;
  Logger *_logger = nullptr;
  EventBus *_events = nullptr;
  DynamicJsonDocument *_cache = nullptr;
  bool _cacheLoaded = false;

  bool loadCache();
  void rememberCache(JsonDocument &doc);
  int nextId(JsonArray arr);
};
