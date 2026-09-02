#include "SessionManager.h"

#include <cstring>
#include <esp_heap_caps.h>
#include <time.h>

#include "Config.h"
#include "DmaMemoryMonitor.h"
#include "JsonHeap.h"
#include "PortalSessionManager.h"
#include "SalesTime.h"

static String durationText(uint32_t seconds) {
  uint32_t minutes = seconds / 60;
  if (minutes >= 60) return String(minutes / 60) + "h " + String(minutes % 60) + "m";
  return String(minutes) + "m";
}

namespace {

struct SalesTotals {
  int amount          = 0;
  int sessions        = 0;
  int undatedAmount   = 0;
  int undatedSessions = 0;
};

enum class SalesPeriod { Today, Week, Month };

bool saleIsUptimeUndated(JsonObjectConst sale) {
  const char *recordedAt = sale["recorded_at"] | "";
  const char *timestamp  = sale["timestamp"] | "";
  return salesIsUptimeMarker(recordedAt) ||
         (recordedAt[0] == '\0' && salesIsUptimeMarker(timestamp));
}

String resolveSaleIsoDate(JsonObjectConst sale) {
  const char *recordedAt  = sale["recorded_at"] | "";
  const char *reportingAt = sale["reporting_at"] | "";
  const char *connectedAt = sale["connectedAt"] | "";
  const char *endedAt     = sale["endedAt"] | "";

  String iso = salesEffectiveIsoStamp(recordedAt, reportingAt);
  if (!iso.isEmpty()) return iso;

  int y = 0, m = 0, d = 0;
  if (connectedAt[0] != '\0' && salesParseRecordedAt(connectedAt, y, m, d)) {
    return String(connectedAt);
  }
  if (endedAt[0] != '\0' && salesParseRecordedAt(endedAt, y, m, d)) {
    return String(endedAt);
  }

  iso = salesIsoFromUptimeMarker(recordedAt);
  if (!iso.isEmpty()) return iso;

  const char *timestamp = sale["timestamp"] | "";
  return salesIsoFromUptimeMarker(timestamp);
}

struct SalesPeriodBundle {
  SalesTotals today;
  SalesTotals week;
  SalesTotals month;
};

void accumulatePeriod(SalesTotals &totals, int amount, bool match) {
  if (!match) return;
  totals.amount += amount;
  totals.sessions++;
}

void accumulateUndated(SalesTotals &totals, int amount) {
  totals.undatedAmount += amount;
  totals.undatedSessions++;
}

/**
 * Calendar aggregation with offline uptime-marker policy (forensic-proven):
 * - ISO recorded_at / reporting_at: filter normally.
 * Uptime markers: resolve via connectedAt/endedAt when present, else same-boot
 * conversion when wall clock is ready. Never attribute cross-boot undated sales to Today.
 * Does not rewrite sales.json.
 * One SD read computes all three periods (same semantics as three filters).
 */
SalesPeriodBundle aggregateAllSales(StorageManager *storage) {
  SalesPeriodBundle bundle;
  if (!storage) return bundle;

  PsramJsonDocument salesHeap;
  JsonDocument &sales = salesHeap.doc();
  if (!storage->readJson(RenzFiConfig::SALES_FILE, sales) || !sales.is<JsonArray>()) {
    return bundle;
  }

  for (JsonObjectConst sale : sales.as<JsonArrayConst>()) {
    const int amount = sale["amount"] | 0;
    const String iso = resolveSaleIsoDate(sale);

    if (!iso.isEmpty()) {
      accumulatePeriod(bundle.today, amount, salesIsToday(iso.c_str()));
      accumulatePeriod(bundle.week, amount, salesIsThisWeek(iso.c_str()));
      accumulatePeriod(bundle.month, amount, salesIsThisMonth(iso.c_str()));
      continue;
    }

    if (!saleIsUptimeUndated(sale)) continue;

    accumulateUndated(bundle.today, amount);
    accumulateUndated(bundle.week, amount);
    accumulateUndated(bundle.month, amount);
  }
  return bundle;
}

struct SalesSummarySnapshot {
  SalesTotals today;
  SalesTotals week;
  SalesTotals month;
  uint32_t refreshedAtMs = 0;
  bool valid = false;
};

SalesSummarySnapshot g_salesSummary;
volatile bool g_salesSummaryDirty = true;
portMUX_TYPE g_salesSummaryMux = portMUX_INITIALIZER_UNLOCKED;

void markSalesSummaryDirty() {
  portENTER_CRITICAL(&g_salesSummaryMux);
  g_salesSummaryDirty = true;
  portEXIT_CRITICAL(&g_salesSummaryMux);
}

void copySalesSummarySnapshot(SalesPeriod period, SalesTotals &out) {
  portENTER_CRITICAL(&g_salesSummaryMux);
  switch (period) {
    case SalesPeriod::Today:
      out = g_salesSummary.today;
      break;
    case SalesPeriod::Week:
      out = g_salesSummary.week;
      break;
    case SalesPeriod::Month:
      out = g_salesSummary.month;
      break;
  }
  portEXIT_CRITICAL(&g_salesSummaryMux);
}

void fillTotals(JsonDocument &doc, const SalesTotals &totals) {
  doc["amount"]          = totals.amount;
  doc["sessions"]        = totals.sessions;
  doc["undatedAmount"]   = totals.undatedAmount;
  doc["undatedSessions"] = totals.undatedSessions;
  doc["clockReady"]      = salesTimeReady();
}

constexpr size_t kSalesChartCacheSlots = 3;
constexpr int kSalesChartMaxDays = 180;

uint32_t g_salesChartRevision = 0;

// Compact CPU-side cache. Lives in PSRAM: never a W5500/SPI DMA source.
struct SalesChartCachePayload {
  int32_t amounts[kSalesChartMaxDays];
  char dateKeys[kSalesChartMaxDays][11];
};

struct SalesChartCacheSlot {
  int days = 0;
  uint32_t revision = 0;
  uint32_t cachedAtMs = 0;
  SalesChartCachePayload *payload = nullptr;

  bool matches(int requestedDays, uint32_t nowMs, uint32_t revisionNow) const {
    if (!payload || days != requestedDays) return false;
    if (revision != revisionNow) return false;
    return (nowMs - cachedAtMs) < RenzFiConfig::SALES_CHART_CACHE_MS;
  }
};

SalesChartCachePayload *ensureChartPayload(SalesChartCacheSlot *slot) {
  if (!slot) return nullptr;
  if (slot->payload) return slot->payload;
  slot->payload = static_cast<SalesChartCachePayload *>(heap_caps_calloc(
      1, sizeof(SalesChartCachePayload),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!slot->payload) {
    slot->payload = static_cast<SalesChartCachePayload *>(heap_caps_calloc(
        1, sizeof(SalesChartCachePayload), MALLOC_CAP_8BIT));
  }
  return slot->payload;
}

int chartBucketIndex(const char dateKeys[][11], int days, const char *dateKey) {
  if (!dateKey || !dateKey[0]) return -1;
  for (int i = 0; i < days; i++) {
    if (strcmp(dateKeys[i], dateKey) == 0) return i;
  }
  return -1;
}

void fillChartDoc(JsonDocument &doc, const char dateKeys[][11],
                  const int32_t *amounts, int days) {
  doc.clear();
  JsonArray labels = doc["labels"].to<JsonArray>();
  JsonArray values = doc["data"].to<JsonArray>();
  for (int i = 0; i < days; i++) {
    labels.add(dateKeys[i]);
    values.add(amounts[i]);
  }
}

SalesChartCacheSlot g_salesChartCache[kSalesChartCacheSlots];
portMUX_TYPE g_salesChartCacheMux = portMUX_INITIALIZER_UNLOCKED;

// Fixed slot per chart period — lookup never evicts or mutates cache state.
SalesChartCacheSlot *chartCacheSlotForDays(int days) {
  if (days <= 7) return &g_salesChartCache[0];
  if (days <= 28) return &g_salesChartCache[1];
  return &g_salesChartCache[2];
}

void writeSaleRecord(JsonObject item, const SaleRecord &sale) {
  item["schemaVersion"] = sale.schemaVersion == 0 ? 2 : sale.schemaVersion;
  item["id"] = sale.id;
  item["timestamp"] = sale.timestamp;
  if (!sale.recordedAt.isEmpty()) item["recorded_at"] = sale.recordedAt;
  item["amount"] = sale.amount;
  item["sessionId"] = sale.sessionId;
  item["paymentType"] = sale.paymentType;
  item["durationMinutes"] = sale.durationMinutes;
  if (!sale.macAddress.isEmpty()) item["macAddress"] = sale.macAddress;
  if (!sale.ipAddress.isEmpty()) item["ipAddress"] = sale.ipAddress;
  if (!sale.voucherCode.isEmpty()) item["voucherCode"] = sale.voucherCode;
  if (sale.promoId != 0) item["promoId"] = sale.promoId;
  if (!sale.promoName.isEmpty()) item["promoName"] = sale.promoName;
  item["credits"] = sale.credits;
  if (!sale.profile.isEmpty()) item["profile"] = sale.profile;
  if (!sale.speed.isEmpty()) item["speed"] = sale.speed;
  if (!sale.expiresAt.isEmpty()) item["expiresAt"] = sale.expiresAt;
  if (!sale.operatorName.isEmpty()) item["operatorName"] = sale.operatorName;
  if (!sale.connectedAt.isEmpty()) item["connectedAt"] = sale.connectedAt;
  if (!sale.endedAt.isEmpty()) item["endedAt"] = sale.endedAt;
  item["actualConnectedSeconds"] = sale.actualConnectedSeconds;
  if (!sale.status.isEmpty()) item["status"] = sale.status;
  if (!sale.terminationReason.isEmpty()) {
    item["terminationReason"] = sale.terminationReason;
  }
}

}  // namespace

class SessionManager::SalesLock {
 public:
  explicit SalesLock(SemaphoreHandle_t mutex) : _mutex(mutex) {
    _locked = _mutex && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE;
  }

  ~SalesLock() {
    if (_locked) xSemaphoreGive(_mutex);
  }

  explicit operator bool() const { return _locked; }

 private:
  SemaphoreHandle_t _mutex = nullptr;
  bool _locked = false;
};

void SessionManager::invalidateSalesChartCache() {
  portENTER_CRITICAL(&g_salesChartCacheMux);
  g_salesChartRevision++;
  for (size_t i = 0; i < kSalesChartCacheSlots; i++) {
    g_salesChartCache[i].revision = 0;
  }
  portEXIT_CRITICAL(&g_salesChartCacheMux);
  markSalesSummaryDirty();
}

void SessionManager::refreshSalesSummarySnapshot() {
  bool dirty = false;
  bool valid = false;
  portENTER_CRITICAL(&g_salesSummaryMux);
  dirty = g_salesSummaryDirty;
  valid = g_salesSummary.valid;
  portEXIT_CRITICAL(&g_salesSummaryMux);

  // Clean + valid: reuse RAM snapshot. Dirty invalidation remains authoritative.
  if (!dirty && valid) {
    static uint32_t s_lastSkipLogMs = 0;
    const uint32_t now = millis();
    if (s_lastSkipLogMs == 0 || (now - s_lastSkipLogMs) >= 30000U) {
      s_lastSkipLogMs = now;
      Serial.println("[sales-cache] action=skip-fresh source=RAM");
    }
    return;
  }

  const uint32_t t0 = millis();
  Serial.println("[sales-cache] action=start source=SD");

  SalesLock lock(_salesMutex);
  if (!lock) {
    Serial.println("[sales-cache] refresh skipped lock_busy serving_last_snapshot");
    return;
  }

  PsramJsonDocument salesProbe;
  JsonDocument &salesDoc = salesProbe.doc();
  if (!_storage || !_storage->readJson(RenzFiConfig::SALES_FILE, salesDoc) ||
      !salesDoc.is<JsonArray>()) {
    Serial.println("[sales-cache] refresh aborted sd_read_failed");
    return;
  }

  const SalesPeriodBundle bundle = aggregateAllSales(_storage);
  const uint32_t elapsed = millis() - t0;

  portENTER_CRITICAL(&g_salesSummaryMux);
  g_salesSummary.today = bundle.today;
  g_salesSummary.week = bundle.week;
  g_salesSummary.month = bundle.month;
  g_salesSummary.refreshedAtMs = millis();
  g_salesSummary.valid = true;
  g_salesSummaryDirty = false;
  portEXIT_CRITICAL(&g_salesSummaryMux);

  salesLogDiagnostics(bundle.today.amount, bundle.week.amount,
                      bundle.month.amount);
  if (bundle.today.amount > bundle.week.amount ||
      bundle.week.amount > bundle.month.amount) {
    Serial.printf(
        "[sales] invariant warn today=%d week=%d month=%d (expected today<=week<=month "
        "for same-day attributed set; leftover prior-month week days can differ)\n",
        bundle.today.amount, bundle.week.amount, bundle.month.amount);
  }

  Serial.printf("[sales-cache] action=complete source=SD elapsed=%u\n",
                static_cast<unsigned>(elapsed));
}

void SessionManager::refreshMergedActiveUserSnapshot(PortalSessionManager *portal) {
  DynamicJsonDocument data(RenzFiConfig::JSON_DOC_MEDIUM);
  DynamicJsonDocument seen(RenzFiConfig::JSON_DOC_SMALL);
  JsonArray seenMacs = seen.to<JsonArray>();
  JsonArray out = data.to<JsonArray>();
  if (portal) portal->appendActiveUsers(out, seenMacs);
  appendActiveUsers(out, seenMacs);

  int count = 0;
  int paused = 0;
  for (JsonObjectConst row : data.as<JsonArray>()) {
    count++;
    const char *state = row["state"] | "";
    if (strcmp(state, "paused") == 0) paused++;
  }

  portENTER_CRITICAL(&_activeUserSnapMux);
  _snapActiveCount = count;
  _snapPausedCount = paused;
  portEXIT_CRITICAL(&_activeUserSnapMux);
}

void SessionManager::cachedActiveUserStats(int &count, int &paused) const {
  portENTER_CRITICAL(&_activeUserSnapMux);
  count = _snapActiveCount;
  paused = _snapPausedCount;
  portEXIT_CRITICAL(&_activeUserSnapMux);
}

void SessionManager::begin(StorageManager *storage, Logger *logger, EventBus *events) {
  _storage = storage;
  _logger  = logger;
  _events  = events;
  if (!_salesMutex) _salesMutex = xSemaphoreCreateMutex();
}

static bool macAlreadyListed(const JsonArray &seenMacs, const String &mac) {
  if (mac.isEmpty()) return false;
  for (JsonVariantConst seen : seenMacs) {
    if (mac.equalsIgnoreCase(seen.as<const char *>())) return true;
  }
  return false;
}

static void markMacSeen(JsonArray &seenMacs, const String &mac) {
  if (!mac.isEmpty()) seenMacs.add(mac);
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

void SessionManager::appendActiveUsers(JsonArray &out, JsonArray &seenMacs) {
  if (!_storage) return;

  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  if (!_storage->readJson(RenzFiConfig::USERS_FILE, doc) || !doc.is<JsonArray>()) return;

  uint32_t now = millis() / 1000;
  for (JsonObjectConst user : doc.as<JsonArray>()) {
    uint32_t expiresAt = user["expiresAt"] | 0UL;
    if (expiresAt <= now) continue;

    String mac = user["mac"] | "";
    if (mac.isEmpty() || mac.equalsIgnoreCase("coin-pending")) continue;
    if (macAlreadyListed(seenMacs, mac)) continue;

    String sourceRaw = user["source"] | "coin";
    bool isVoucher = sourceRaw == "voucher";
    uint32_t remainingSec = expiresAt - now;
    int remainingMinutes = (int)((remainingSec + 59UL) / 60UL);

    JsonObject row = out.createNestedObject();
    row["mac"] = mac;
    row["ip"] = user["ip"] | "";
    row["sessionType"] = isVoucher ? "voucher" : "coin";
    row["remainingMinutes"] = remainingMinutes;
    row["credits"] = user["credits"] | user["amount"] | 0;
    bool paused = user["paused"] | false;
    row["paused"] = paused;
    row["active"] = true;
    row["state"] = paused ? "paused" : "active";
    row["source"] = isVoucher ? "voucher" : "portal";
    markMacSeen(seenMacs, mac);
  }
}

static bool setUserPaused(StorageManager *storage, EventBus *events,
                          const String &mac, bool paused) {
  if (!storage) return false;

  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  if (!storage->readJson(RenzFiConfig::USERS_FILE, doc) || !doc.is<JsonArray>()) {
    return false;
  }

  uint32_t now = millis() / 1000;
  bool found = false;
  for (JsonObject user : doc.as<JsonArray>()) {
    if (!mac.equalsIgnoreCase(user["mac"] | "")) continue;
    if ((user["expiresAt"] | 0UL) <= now) return false;
    if ((user["paused"] | false) == paused) return true;
    user["paused"] = paused;
    found = true;
    break;
  }
  if (!found) return false;

  bool ok = storage->writeJson(RenzFiConfig::USERS_FILE, doc);
  if (ok && events) {
    events->emit("sessions.changed");
    events->emit("users.active");
    events->emit("system.status");
  }
  return ok;
}

bool SessionManager::pause(const String &mac) {
  return setUserPaused(_storage, _events, mac, true);
}

bool SessionManager::resume(const String &mac) {
  return setUserPaused(_storage, _events, mac, false);
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
  if (ok && removed && _events) {
    _events->emit("sessions.changed");
    _events->emit("users.active");
  }
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
  sale.recordedAt = salesRecordedAtNow();
  sale.amount = amount;
  sale.sessionId = sessionId;
  sale.paymentType = "coin";
  sale.durationMinutes = minutes;
  sale.credits = amount;
  sale.status = "completed";
  recordSale(sale);

  if (_events) {
    _events->emit("sessions.changed");
    _events->emit("users.active");
    _events->emit("sales.changed");
  }
  // Admin coin/test hits this on async_tcp — no durable history flush tip.
  if (_logger) _logger->infoLocal("coin", "Coin session granted");
  return sessionId;
}

bool SessionManager::recordSale(const SaleRecord &sale) {
  return upsertSale(sale);
}

bool SessionManager::loadSalesLocked(JsonDocument &doc) const {
  if (!_storage) return false;
  if (!_storage->readJson(RenzFiConfig::SALES_FILE, doc) ||
      !doc.is<JsonArray>()) {
    doc.clear();
    doc.to<JsonArray>();
  }
  return true;
}

bool SessionManager::saveSalesBoundedLocked(JsonDocument &doc) {
  if (!_storage || !doc.is<JsonArray>() || doc.overflowed()) return false;

  JsonArray sales = doc.as<JsonArray>();
  int evictedAmount = 0;
  int evictedCount = 0;
  auto evictOldest = [&]() {
    if (sales.size() == 0) return;
    JsonObject oldest = sales[0].as<JsonObject>();
    evictedAmount += oldest["amount"] | 0;
    evictedCount++;
    sales.remove(0);
  };
  while (sales.size() > kSalesMaxRecords) evictOldest();
  while (measureJson(doc) > kSalesMaxSerializedBytes && sales.size() > 1) {
    evictOldest();
  }
  if (evictedCount > 0) {
    Serial.printf(
        "[sales] ring-evict count=%d amount=%d remaining=%u "
        "(Today/Week/Month lose evicted rows)\n",
        evictedCount, evictedAmount, (unsigned)sales.size());
  }
  if (measureJson(doc) > kSalesMaxSerializedBytes) return false;
  return _storage->writeJson(RenzFiConfig::SALES_FILE, doc);
}

bool SessionManager::finishSalesMutationLocked(JsonDocument &doc, bool changed) {
  return !changed || saveSalesBoundedLocked(doc);
}

void SessionManager::appendSaleHistory(JsonObjectConst sale) {
  if (!_storage) return;
  const String saleId = sale["id"] | "";
  if (saleId.isEmpty()) return;
  DynamicJsonDocument event(measureJson(sale) + 192);
  event.set(sale);
  event["event"] = "sale_recorded";
  const String eventAt = sale["recorded_at"] | "";
  _storage->appendHistory(NdjsonLedger::Kind::Sales,
                          String("sale:") + saleId, eventAt,
                          event.as<JsonObjectConst>());
}

void SessionManager::appendCompletedSessionHistory(JsonObjectConst sale) {
  if (!_storage) return;
  const String sessionId = sale["sessionId"] | "";
  if (sessionId.isEmpty()) return;
  DynamicJsonDocument event(measureJson(sale) + 224);
  event["event"] = "session_completed";
  event["sessionId"] = sessionId;
  event["sale"].set(sale);
  String eventAt = sale["endedAt"] | "";
  if (eventAt.isEmpty()) eventAt = sale["recorded_at"] | "";
  _storage->appendHistory(NdjsonLedger::Kind::Sessions,
                          String("session-completed:") + sessionId, eventAt,
                          event.as<JsonObjectConst>());
}

bool SessionManager::upsertSale(const SaleRecord &sale) {
  if (sale.id.isEmpty() || !_salesMutex) return false;

  bool ok = false;
  {
    SalesLock lock(_salesMutex);
    if (!lock) return false;

    PsramJsonDocument salesHeap;
    JsonDocument &salesDoc = salesHeap.doc();
    if (!loadSalesLocked(salesDoc)) return false;

    JsonArray sales = salesDoc.as<JsonArray>();
    for (size_t i = sales.size(); i > 0; --i) {
      JsonObject existing = sales[i - 1].as<JsonObject>();
      const char *existingId = existing["id"] | "";
      if (sale.id == existingId) {
        sales.remove(i - 1);
      }
    }

    JsonObject item = sales.createNestedObject();
    writeSaleRecord(item, sale);
    ok = saveSalesBoundedLocked(salesDoc);
    if (ok) {
      appendSaleHistory(item);
      if (sale.status == "completed") appendCompletedSessionHistory(item);
    }
  }

  if (ok) {
    invalidateSalesChartCache();
    if (_events) {
      _events->emit("sales.changed");
      // Observational Admin event — only after persist succeeded.
      String payload = "{\"event\":\"sale.created\",\"id\":\"";
      payload += sale.id;
      payload += "\",\"amount\":";
      payload += String(sale.amount);
      payload += ",\"type\":\"";
      payload += sale.paymentType;
      payload += "\"}";
      _events->emit("sale.created", payload);
    }
  }
  return ok;
}

bool SessionManager::markSaleActivated(const String &saleId,
                                       const String &connectedAt) {
  if (saleId.isEmpty() || !_salesMutex) return false;

  bool changed = false;
  bool ok = false;
  {
    SalesLock lock(_salesMutex);
    if (!lock) return false;

    PsramJsonDocument salesHeap;
    JsonDocument &salesDoc = salesHeap.doc();
    if (!loadSalesLocked(salesDoc)) return false;

    for (JsonObject sale : salesDoc.as<JsonArray>()) {
      if (saleId != (sale["id"] | "")) continue;
      sale["schemaVersion"] = 2;
      sale["status"] = "active";
      if (!connectedAt.isEmpty()) sale["connectedAt"] = connectedAt;
      changed = true;
      break;
    }
    ok = finishSalesMutationLocked(salesDoc, changed);
  }

  if (ok && changed) {
    invalidateSalesChartCache();
    if (_events) _events->emit("sales.changed");
  }
  return ok && changed;
}

bool SessionManager::markSalesActivatedBySessionId(
    const String &sessionId, const String &connectedAt) {
  if (sessionId.isEmpty() || !_salesMutex) return false;
  bool changed = false;
  bool ok = false;
  {
    SalesLock lock(_salesMutex);
    if (!lock) return false;
    PsramJsonDocument salesHeap;
    JsonDocument &salesDoc = salesHeap.doc();
    if (!loadSalesLocked(salesDoc)) return false;
    for (JsonObject sale : salesDoc.as<JsonArray>()) {
      if (sessionId != (sale["sessionId"] | "")) continue;
      sale["schemaVersion"] = 2;
      sale["status"] = "active";
      if (!connectedAt.isEmpty()) sale["connectedAt"] = connectedAt;
      changed = true;
    }
    ok = finishSalesMutationLocked(salesDoc, changed);
  }
  if (ok && changed) {
    invalidateSalesChartCache();
    if (_events) _events->emit("sales.changed");
  }
  return ok && changed;
}

bool SessionManager::recordSessionEvent(const String &sessionId,
                                        const String &event,
                                        const String &recordedAt) {
  if (sessionId.isEmpty() || recordedAt.isEmpty() || !_salesMutex) return false;
  const char *field = nullptr;
  if (event == "paused") field = "pausedAt";
  else if (event == "resumed") field = "resumedAt";
  else if (event == "expired") field = "expiredAt";
  else if (event == "terminated") field = "terminatedAt";
  if (!field) return false;

  bool changed = false;
  bool ok = false;
  {
    SalesLock lock(_salesMutex);
    if (!lock) return false;
    PsramJsonDocument salesHeap;
    JsonDocument &salesDoc = salesHeap.doc();
    if (!loadSalesLocked(salesDoc)) return false;
    for (JsonObject sale : salesDoc.as<JsonArray>()) {
      if (sessionId != (sale["sessionId"] | "")) continue;
      sale[field] = recordedAt;
      changed = true;
    }
    ok = finishSalesMutationLocked(salesDoc, changed);
  }
  if (ok && changed) {
    invalidateSalesChartCache();
    if (_events) _events->emit("sales.changed");
  }
  return ok && changed;
}

bool SessionManager::completeSaleBySessionId(
    const String &sessionId, const String &endedAt,
    uint32_t actualConnectedSeconds, const String &terminationReason,
    const String &operatorName) {
  if (sessionId.isEmpty() || !_salesMutex) return false;

  bool changed = false;
  bool ok = false;
  DynamicJsonDocument completedEvent(RenzFiConfig::JSON_DOC_SMALL);
  {
    SalesLock lock(_salesMutex);
    if (!lock) return false;

    PsramJsonDocument salesHeap;
    JsonDocument &salesDoc = salesHeap.doc();
    if (!loadSalesLocked(salesDoc)) return false;

    for (JsonObject sale : salesDoc.as<JsonArray>()) {
      if (sessionId != (sale["sessionId"] | "")) continue;
      sale["schemaVersion"] = 2;
      sale["status"] = "completed";
      if (!endedAt.isEmpty()) sale["endedAt"] = endedAt;
      sale["actualConnectedSeconds"] = actualConnectedSeconds;
      if (!terminationReason.isEmpty()) {
        sale["terminationReason"] = terminationReason;
      }
      if (!endedAt.isEmpty()) {
        if (terminationReason == "time_expired") sale["expiredAt"] = endedAt;
        else sale["terminatedAt"] = endedAt;
      }
      if (!operatorName.isEmpty()) sale["operatorName"] = operatorName;
      changed = true;
      if (completedEvent.isNull()) completedEvent.set(sale);
    }
    ok = finishSalesMutationLocked(salesDoc, changed);
    if (ok && changed && completedEvent.is<JsonObject>()) {
      appendCompletedSessionHistory(completedEvent.as<JsonObjectConst>());
    }
  }

  if (ok && changed) {
    invalidateSalesChartCache();
    if (_events) _events->emit("sales.changed");
  }
  return ok && changed;
}

bool SessionManager::listSalesRecords(JsonDocument &doc, size_t limit) {
  if (!_salesMutex) return false;
  SalesLock lock(_salesMutex);
  if (!lock) return false;

  PsramJsonDocument salesHeap;
  JsonDocument &salesDoc = salesHeap.doc();
  if (!loadSalesLocked(salesDoc)) return false;

  JsonArray source = salesDoc.as<JsonArray>();
  JsonArray out = doc.to<JsonArray>();
  if (limit > kSalesMaxRecords) limit = kSalesMaxRecords;
  for (size_t i = source.size(); i > 0 && out.size() < limit; --i) {
    JsonObject row = out.createNestedObject();
    row.set(source[i - 1]);
    if (doc.overflowed()) return false;
  }
  return true;
}

bool SessionManager::resetSales() {
  if (!_salesMutex) return false;
  SalesLock lock(_salesMutex);
  if (!lock) return false;
  DynamicJsonDocument doc(128);
  doc.to<JsonArray>();
  if (!saveSalesBoundedLocked(doc)) return false;
  if (_events) {
    _events->emit("sales.changed");
  }
  if (_logger) _logger->infoLocal("sales", "Sales collection reset by owner");
  return true;
}

bool SessionManager::salesToday(JsonDocument &doc) {
  SalesTotals totals;
  copySalesSummarySnapshot(SalesPeriod::Today, totals);
  fillTotals(doc, totals);
  return true;
}

bool SessionManager::salesWeek(JsonDocument &doc) {
  SalesTotals totals;
  copySalesSummarySnapshot(SalesPeriod::Week, totals);
  fillTotals(doc, totals);
  return true;
}

bool SessionManager::salesMonth(JsonDocument &doc) {
  SalesTotals totals;
  copySalesSummarySnapshot(SalesPeriod::Month, totals);
  fillTotals(doc, totals);
  return true;
}

bool SessionManager::salesHistory(JsonDocument &doc) {
  SalesLock lock(_salesMutex);
  if (!lock) return false;
  PsramJsonDocument salesHeap;
  JsonDocument &sales = salesHeap.doc();
  if (!_storage || !_storage->readJson(RenzFiConfig::SALES_FILE, sales) ||
      !sales.is<JsonArray>()) {
    doc.to<JsonArray>();
    return true;
  }

  PsramJsonDocument bucketsHeap;
  JsonDocument &buckets = bucketsHeap.doc();
  int undatedRevenue = 0;
  int undatedSessions = 0;
  const bool clockReady = salesTimeReady();
  String todayKey;
  if (clockReady) {
    const String nowIso = salesRecordedAtNow();
    int y = 0, m = 0, d = 0;
    if (salesParseRecordedAt(nowIso.c_str(), y, m, d)) {
      char buf[11];
      snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
      todayKey = buf;
    }
  }

  for (JsonObjectConst sale : sales.as<JsonArrayConst>()) {
    const int amount = sale["amount"] | 0;
    const String iso = resolveSaleIsoDate(sale);

    char dateKey[11];
    dateKey[0] = '\0';
    if (!iso.isEmpty() && iso.length() >= 10) {
      memcpy(dateKey, iso.c_str(), 10);
      dateKey[10] = '\0';
    } else if (saleIsUptimeUndated(sale)) {
      undatedRevenue += amount;
      undatedSessions++;
      continue;
    } else {
      continue;
    }

    if (!buckets[dateKey].is<JsonObject>()) {
      buckets[dateKey]["date"]     = dateKey;
      buckets[dateKey]["sessions"] = 0;
      buckets[dateKey]["revenue"]  = 0;
    }
    buckets[dateKey]["sessions"] = (buckets[dateKey]["sessions"] | 0) + 1;
    buckets[dateKey]["revenue"]  = (buckets[dateKey]["revenue"] | 0) + amount;
  }

  if (undatedSessions > 0 && todayKey.isEmpty()) {
    buckets["UNCLOCKED"]["date"]     = "UNCLOCKED";
    buckets["UNCLOCKED"]["sessions"] = undatedSessions;
    buckets["UNCLOCKED"]["revenue"]  = undatedRevenue;
    buckets["UNCLOCKED"]["undated"]  = true;
  }

  String dates[32];
  int dateCount = 0;
  for (JsonPair kv : buckets.as<JsonObject>()) {
    if (dateCount < 32) dates[dateCount++] = kv.key().c_str();
  }

  for (int i = 0; i < dateCount - 1; i++) {
    for (int j = i + 1; j < dateCount; j++) {
      if (dates[j] > dates[i]) {
        String tmp = dates[i];
        dates[i]   = dates[j];
        dates[j]   = tmp;
      }
    }
  }

  JsonArray out = doc.to<JsonArray>();
  const int limit = dateCount < 30 ? dateCount : 30;
  for (int i = 0; i < limit; i++) {
    JsonObject row = out.createNestedObject();
    row.set(buckets[dates[i]]);
  }
  return true;
}

bool SessionManager::salesChart(JsonDocument &doc, int days) {
  if (days < 1) days = 1;
  if (days > kSalesChartMaxDays) days = kSalesChartMaxDays;

  DmaMemoryMonitor::ScopedProbe dmaProbe("sales-chart");

  const uint32_t enterMs = millis();
  SalesChartCacheSlot *cacheSlot = chartCacheSlotForDays(days);
  char dateKeys[kSalesChartMaxDays][11];
  int32_t amounts[kSalesChartMaxDays];
  int cachedDays = 0;
  uint32_t cachedAtMs = 0;
  bool cacheHit = false;

  portENTER_CRITICAL(&g_salesChartCacheMux);
  const uint32_t revisionNow = g_salesChartRevision;
  cacheHit =
      cacheSlot->matches(days, enterMs, revisionNow) && cacheSlot->payload;
  if (cacheHit) {
    cachedDays = cacheSlot->days;
    cachedAtMs = cacheSlot->cachedAtMs;
    memcpy(dateKeys, cacheSlot->payload->dateKeys, sizeof(dateKeys));
    memcpy(amounts, cacheSlot->payload->amounts, sizeof(amounts));
  }
  portEXIT_CRITICAL(&g_salesChartCacheMux);

  if (cacheHit) {
    fillChartDoc(doc, dateKeys, amounts, cachedDays);
    Serial.printf(
        "[sales] chart cache hit days=%d ageMs=%u elapsedMs=%u\n", days,
        (unsigned)(enterMs - cachedAtMs),
        (unsigned)(millis() - enterMs));
    return true;
  }

  // Chart rebuild does SD I/O then replies over W5500 SPI. When DMA-capable
  // internal SRAM is already below ETH TX headroom, continuing can trigger
  // setup_dma_priv_buffer abort on the concurrent ETH path — soft-fail instead.
  if (!DmaMemoryMonitor::hasEthTransmitHeadroom()) {
    DmaMemoryMonitor::logSnapshot("sales-chart-dma-low");
    Serial.printf(
        "[sales] chart deferred days=%d reason=SPI_DMA_LOW largest=%u need=%u\n",
        days,
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
        (unsigned)DmaMemoryMonitor::kMinLargestDmaBlockForEthTx);
    return false;
  }

  Serial.printf("[sales] chart enter days=%d\n", days);

  struct tm nowInfo;
  // Never block async_tcp waiting for SNTP — time must already be valid.
  if (!getLocalTime(&nowInfo, 0)) {
    Serial.println("[sales] chart exit time-not-ready");
    return false;
  }

  // Fixed-size buckets. A JSON object of 180 date keys previously allocated
  // ~9 KB of INTERNAL SRAM and fragmented the DMA pool (largest → 16 B).
  memset(amounts, 0, sizeof(amounts));
  for (int i = 0; i < days; i++) {
    struct tm day = nowInfo;
    day.tm_mday -= (days - 1 - i);
    mktime(&day);
    strftime(dateKeys[i], sizeof(dateKeys[i]), "%Y-%m-%d", &day);
  }

  PsramJsonDocument salesHeap;
  JsonDocument &sales = salesHeap.doc();
  {
    SalesLock lock(_salesMutex);
    if (!lock || !_storage ||
        !_storage->readJson(RenzFiConfig::SALES_FILE, sales) ||
        !sales.is<JsonArray>()) {
      Serial.printf("[sales] chart exit read-failed elapsedMs=%u\n",
                    (unsigned)(millis() - enterMs));
      return false;
    }
  }

  size_t matchedSales = 0;
  for (JsonObjectConst sale : sales.as<JsonArrayConst>()) {
    const int amount = sale["amount"] | 0;
    const String iso = resolveSaleIsoDate(sale);

    char dateKey[11];
    dateKey[0] = '\0';
    if (!iso.isEmpty() && iso.length() >= 10) {
      memcpy(dateKey, iso.c_str(), 10);
      dateKey[10] = '\0';
    } else {
      continue;
    }

    const int idx = chartBucketIndex(dateKeys, days, dateKey);
    if (idx < 0) continue;
    amounts[idx] += amount;
    matchedSales++;
  }

  fillChartDoc(doc, dateKeys, amounts, days);

  SalesChartCachePayload *built = ensureChartPayload(cacheSlot);
  if (built) {
    memcpy(built->dateKeys, dateKeys, sizeof(dateKeys));
    memcpy(built->amounts, amounts, sizeof(amounts));
    portENTER_CRITICAL(&g_salesChartCacheMux);
    cacheSlot->days = days;
    cacheSlot->revision = g_salesChartRevision;
    cacheSlot->cachedAtMs = enterMs;
    portEXIT_CRITICAL(&g_salesChartCacheMux);
  }

  const DmaMemoryMonitor::Snapshot after = DmaMemoryMonitor::readSnapshot();
  Serial.printf(
      "[sales] chart exit ok days=%d sales=%u matched=%u elapsedMs=%u "
      "dma_free=%u dma_largest=%u\n",
      days, (unsigned)sales.as<JsonArray>().size(), (unsigned)matchedSales,
      (unsigned)(millis() - enterMs), (unsigned)after.freeDma,
      (unsigned)after.largestDma);
  return true;
}

namespace {

String csvEscape(const String &value) {
  if (value.indexOf(',') >= 0 || value.indexOf('"') >= 0 || value.indexOf('\n') >= 0) {
    String escaped = value;
    escaped.replace("\"", "\"\"");
    return String("\"") + escaped + "\"";
  }
  return value;
}

void splitRecordedAt(const char *recordedAt, String &dateOut, String &timeOut) {
  dateOut = "";
  timeOut = "";
  if (!recordedAt || recordedAt[0] == '\0') return;
  const char *tPos = strchr(recordedAt, 'T');
  if (tPos) {
    dateOut = String(recordedAt).substring(0, tPos - recordedAt);
    timeOut = String(tPos + 1);
    if (timeOut.endsWith("Z")) timeOut.remove(timeOut.length() - 1);
  } else {
    dateOut = recordedAt;
  }
}

String saleField(JsonObject sale, const char *key) {
  if (sale[key].is<const char *>()) return sale[key].as<const char *>();
  if (sale[key].is<int>()) return String(sale[key].as<int>());
  return "";
}

}  // namespace

bool SessionManager::buildSalesCsv(String &csvOut, String &filenameOut) {
  SalesLock lock(_salesMutex);
  if (!lock) return false;
  csvOut =
      "Date,Time,Amount,Minutes,Voucher,MAC Address,IP Address,Profile,Status,"
      "Payment Type,Session ID,Sale ID,Promo ID,Promo Name,Credits,Speed,"
      "Expires At,Operator,Connected At,Ended At,Connected Seconds,"
      "Termination Reason\n";

  PsramJsonDocument salesHeap;
  JsonDocument &sales = salesHeap.doc();
  if (_storage && _storage->readJson(RenzFiConfig::SALES_FILE, sales) &&
      sales.is<JsonArray>()) {
    for (JsonObject sale : sales.as<JsonArray>()) {
      String datePart;
      String timePart;
      const char *recordedAt = sale["recorded_at"] | "";
      const char *reportingAt = sale["reporting_at"] | "";
      const String iso = salesEffectiveIsoStamp(recordedAt, reportingAt);
      if (!iso.isEmpty()) {
        splitRecordedAt(iso.c_str(), datePart, timePart);
      } else if (salesIsUptimeMarker(recordedAt) ||
                 salesIsUptimeMarker(sale["timestamp"] | "")) {
        datePart = "UNCLOCKED";
        timePart = recordedAt[0] ? recordedAt : (sale["timestamp"] | "");
      } else {
        splitRecordedAt(recordedAt, datePart, timePart);
      }

      const String paymentType = saleField(sale, "paymentType");
      String voucher = "";
      if (paymentType.equalsIgnoreCase("voucher")) {
        voucher = saleField(sale, "voucherCode");
        if (voucher.isEmpty()) voucher = saleField(sale, "sessionId");
      }

      String mac = saleField(sale, "macAddress");
      if (mac.isEmpty()) mac = saleField(sale, "mac");
      String ip = saleField(sale, "ipAddress");
      if (ip.isEmpty()) ip = saleField(sale, "ip");

      csvOut += csvEscape(datePart) + ",";
      csvOut += csvEscape(timePart) + ",";
      csvOut += csvEscape(String(sale["amount"] | 0)) + ",";
      csvOut += csvEscape(String(sale["durationMinutes"] | 0)) + ",";
      csvOut += csvEscape(voucher) + ",";
      csvOut += csvEscape(mac) + ",";
      csvOut += csvEscape(ip) + ",";
      csvOut += csvEscape(saleField(sale, "profile")) + ",";
      {
        String status = saleField(sale, "status");
        if (status.isEmpty()) status = "completed";
        csvOut += csvEscape(status);
      }
      csvOut += ",";
      csvOut += csvEscape(paymentType) + ",";
      csvOut += csvEscape(saleField(sale, "sessionId")) + ",";
      csvOut += csvEscape(saleField(sale, "id")) + ",";
      csvOut += csvEscape(String(sale["promoId"] | 0)) + ",";
      csvOut += csvEscape(saleField(sale, "promoName")) + ",";
      csvOut += csvEscape(String(sale["credits"] | sale["amount"] | 0)) + ",";
      csvOut += csvEscape(saleField(sale, "speed")) + ",";
      csvOut += csvEscape(saleField(sale, "expiresAt")) + ",";
      csvOut += csvEscape(saleField(sale, "operatorName")) + ",";
      csvOut += csvEscape(saleField(sale, "connectedAt")) + ",";
      csvOut += csvEscape(saleField(sale, "endedAt")) + ",";
      csvOut +=
          csvEscape(String(sale["actualConnectedSeconds"] | 0UL)) + ",";
      csvOut += csvEscape(saleField(sale, "terminationReason"));
      csvOut += "\n";
    }
  }

  struct tm timeinfo;
  char dateStamp[11] = "unknown";
  if (getLocalTime(&timeinfo)) {
    strftime(dateStamp, sizeof(dateStamp), "%Y-%m-%d", &timeinfo);
  }
  filenameOut = String("sales-report-") + dateStamp + ".csv";

  if (_logger) _logger->infoLocal("sales", "export generated");
  Serial.println("[sales] export generated");
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
    if (_events) {
      _events->emit("sessions.changed");
      _events->emit("users.active");
    }
  }
}

int SessionManager::activeCount() {
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  DynamicJsonDocument seen(512);
  JsonArray seenMacs = seen.to<JsonArray>();
  JsonArray out = doc.to<JsonArray>();
  appendActiveUsers(out, seenMacs);
  return out.size();
}

bool SessionManager::hasActiveUsers() {
  return activeCount() > 0;
}

String SessionManager::makeSessionId() {
  return String(esp_random(), HEX) + String(millis(), HEX);
}
