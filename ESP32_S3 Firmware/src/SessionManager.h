#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "EventBus.h"
#include "Logger.h"
#include "Models.h"
#include "StorageManager.h"

class SessionManager {
 public:
  void begin(StorageManager *storage, Logger *logger, EventBus *events);
  bool listUsers(JsonDocument &doc);
  bool disconnect(const String &mac);
  String grantCoinSession(int amount, int minutes);
  bool recordSale(const SaleRecord &sale);
  bool salesToday(JsonDocument &doc);
  bool salesHistory(JsonDocument &doc);
  bool salesPeriod(JsonDocument &doc, uint8_t days);
  void cleanupExpired();
  int activeCount();
  bool hasActiveUsers();

 private:
  StorageManager *_storage = nullptr;
  Logger *_logger = nullptr;
  EventBus *_events = nullptr;

  String makeSessionId();
};
