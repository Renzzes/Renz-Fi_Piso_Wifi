#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <freertos/semphr.h>

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
  /** Compatibility alias for upsertSale(). */
  bool recordSale(const SaleRecord &sale);
  /** Insert or replace one idempotent sale record, then enforce retention. */
  bool upsertSale(const SaleRecord &sale);
  /** Mark one idempotent sale active by immutable sale id. */
  bool markSaleActivated(const String &saleId, const String &connectedAt);
  bool markSalesActivatedBySessionId(const String &sessionId,
                                     const String &connectedAt);
  bool recordSessionEvent(const String &sessionId, const String &event,
                          const String &recordedAt);
  /** Complete every retained sale for a session. */
  bool completeSaleBySessionId(const String &sessionId, const String &endedAt,
                               uint32_t actualConnectedSeconds,
                               const String &terminationReason,
                               const String &operatorName = "");
  /** Return raw retained records newest-first without changing their schema. */
  bool listSalesRecords(JsonDocument &doc, size_t limit = 20);
  /** Owner audit cycle: clear retained sales collection (empty JSON array). */
  bool resetSales();
  bool salesToday(JsonDocument &doc);
  bool salesWeek(JsonDocument &doc);
  bool salesMonth(JsonDocument &doc);
  bool salesHistory(JsonDocument &doc);
  bool salesChart(JsonDocument &doc, int days);
  bool buildSalesCsv(String &csvOut, String &filenameOut);
  void cleanupExpired();
  int activeCount();
  bool hasActiveUsers();

  /** Drop cached chart payloads after sales.json changes. */
  static void invalidateSalesChartCache();

  /**
   * Recalculate today/week/month from sales.json on loopTask only.
   * HTTP handlers must copy the RAM snapshot via salesToday/Week/Month.
   */
  void refreshSalesSummarySnapshot();

  /**
   * Merge portal RAM sessions with users.json on loopTask only.
   * GET /api/status must copy cachedActiveUserStats() — never appendActiveUsers.
   */
  void refreshMergedActiveUserSnapshot(class PortalSessionManager *portal);
  void cachedActiveUserStats(int &count, int &paused) const;

 private:
  class SalesLock;

  // Live sales.json is a bounded ring used for Today/Week/Month aggregates.
  // Cap of 20 was proven to silently drop the oldest row on every new sale
  // once full (e.g. +₱5 and evict ₱1 → display +₱4). Keep enough headroom
  // for a busy site-day; NDJSON history remains the long-term ledger.
  static constexpr size_t kSalesMaxRecords = 200;
  static constexpr size_t kSalesMaxSerializedBytes = 96U * 1024U;

  StorageManager *_storage = nullptr;
  Logger *_logger = nullptr;
  EventBus *_events = nullptr;
  mutable SemaphoreHandle_t _salesMutex = nullptr;
  mutable portMUX_TYPE _activeUserSnapMux = portMUX_INITIALIZER_UNLOCKED;
  int _snapActiveCount = 0;
  int _snapPausedCount = 0;

  String makeSessionId();
  bool loadSalesLocked(JsonDocument &doc) const;
  bool saveSalesBoundedLocked(JsonDocument &doc);
  bool finishSalesMutationLocked(JsonDocument &doc, bool changed);
  void appendSaleHistory(JsonObjectConst sale);
  void appendCompletedSessionHistory(JsonObjectConst sale);
};
