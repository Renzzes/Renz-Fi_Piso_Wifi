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
  void appendActiveUsers(JsonArray &out, JsonArray &seenMacs);
  bool disconnect(const String &mac);
  bool pause(const String &mac);
  bool resume(const String &mac);
  String grantCoinSession(int amount, int minutes);
  bool recordSale(const SaleRecord &sale);
  bool salesToday(JsonDocument &doc);
  bool salesWeek(JsonDocument &doc);
  bool salesMonth(JsonDocument &doc);
  bool salesHistory(JsonDocument &doc);
  bool salesChart(JsonDocument &doc, int days);
  bool buildSalesCsv(String &csvOut, String &filenameOut);
  void cleanupExpired();
  int activeCount();
  bool hasActiveUsers();

 private:
  StorageManager *_storage = nullptr;
  Logger *_logger = nullptr;
  EventBus *_events = nullptr;

  String makeSessionId();
};
