#include "SessionManager.h"

#include <time.h>

#include "Config.h"
#include "SalesTime.h"

static String durationText(uint32_t seconds) {
  uint32_t minutes = seconds / 60;
  if (minutes >= 60) return String(minutes / 60) + "h " + String(minutes % 60) + "m";
  return String(minutes) + "m";
}

namespace {

struct SalesTotals {
  int amount   = 0;
  int sessions = 0;
};

using SalesFilter = bool (*)(const char *recordedAt);

SalesTotals aggregateSales(StorageManager *storage, SalesFilter filter) {
  SalesTotals totals;
  if (!storage) return totals;

  DynamicJsonDocument sales(RenzFiConfig::JSON_DOC_LARGE);
  if (!storage->readJson(RenzFiConfig::SALES_FILE, sales) || !sales.is<JsonArray>()) {
    return totals;
  }

  for (JsonObject sale : sales.as<JsonArray>()) {
    const char *recordedAt = sale["recorded_at"] | "";
    if (!recordedAt || recordedAt[0] == '\0') continue;
    if (!filter(recordedAt)) continue;
    totals.amount += sale["amount"] | 0;
    totals.sessions++;
  }
  return totals;
}

void fillTotals(JsonDocument &doc, const SalesTotals &totals) {
  doc["amount"]   = totals.amount;
  doc["sessions"] = totals.sessions;
}

}  // namespace

void SessionManager::begin(StorageManager *storage, Logger *logger, EventBus *events) {
  _storage = storage;
  _logger  = logger;
  _events  = events;
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
  recordSale(sale);

  if (_events) {
    _events->emit("sessions.changed");
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
  if (!sale.recordedAt.isEmpty()) item["recorded_at"] = sale.recordedAt;
  item["amount"] = sale.amount;
  item["sessionId"] = sale.sessionId;
  item["paymentType"] = sale.paymentType;
  item["durationMinutes"] = sale.durationMinutes;
  bool ok = _storage && _storage->appendJsonArrayItem(RenzFiConfig::SALES_FILE, item.as<JsonObject>(), RenzFiConfig::JSON_DOC_LARGE);
  if (ok && _events) _events->emit("sales.changed");
  return ok;
}

bool SessionManager::salesToday(JsonDocument &doc) {
  fillTotals(doc, aggregateSales(_storage, salesIsToday));
  return true;
}

bool SessionManager::salesWeek(JsonDocument &doc) {
  fillTotals(doc, aggregateSales(_storage, salesIsThisWeek));
  return true;
}

bool SessionManager::salesMonth(JsonDocument &doc) {
  fillTotals(doc, aggregateSales(_storage, salesIsThisMonth));
  return true;
}

bool SessionManager::salesHistory(JsonDocument &doc) {
  DynamicJsonDocument sales(RenzFiConfig::JSON_DOC_LARGE);
  if (!_storage || !_storage->readJson(RenzFiConfig::SALES_FILE, sales) ||
      !sales.is<JsonArray>()) {
    doc.to<JsonArray>();
    return true;
  }

  DynamicJsonDocument buckets(RenzFiConfig::JSON_DOC_MEDIUM);
  for (JsonObject sale : sales.as<JsonArray>()) {
    const char *recordedAt = sale["recorded_at"] | "";
    int year = 0, month = 0, day = 0;
    if (!salesParseRecordedAt(recordedAt, year, month, day)) continue;

    char dateKey[11];
    snprintf(dateKey, sizeof(dateKey), "%04d-%02d-%02d", year, month, day);

    if (!buckets[dateKey].is<JsonObject>()) {
      buckets[dateKey]["date"]     = dateKey;
      buckets[dateKey]["sessions"] = 0;
      buckets[dateKey]["revenue"]  = 0;
    }
    buckets[dateKey]["sessions"] = (buckets[dateKey]["sessions"] | 0) + 1;
    buckets[dateKey]["revenue"]  = (buckets[dateKey]["revenue"] | 0) + (sale["amount"] | 0);
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

  DynamicJsonDocument sales(RenzFiConfig::JSON_DOC_LARGE);
  if (!_storage || !_storage->readJson(RenzFiConfig::SALES_FILE, sales) ||
      !sales.is<JsonArray>()) {
    return false;
  }

  struct tm nowInfo;
  if (!getLocalTime(&nowInfo)) return false;

  JsonArray labels = doc["labels"].to<JsonArray>();
  JsonArray values = doc["data"].to<JsonArray>();

  for (int offset = days - 1; offset >= 0; offset--) {
    struct tm day = nowInfo;
    day.tm_mday -= offset;
    mktime(&day);

    char dateKey[11];
    strftime(dateKey, sizeof(dateKey), "%Y-%m-%d", &day);

    int revenue = 0;
    for (JsonObject sale : sales.as<JsonArray>()) {
      const char *recordedAt = sale["recorded_at"] | "";
      if (!recordedAt || recordedAt[0] == '\0') continue;
      if (strncmp(recordedAt, dateKey, 10) != 0) continue;
      revenue += sale["amount"] | 0;
    }

    labels.add(dateKey);
    values.add(revenue);
  }
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
  csvOut =
      "Date,Time,Amount,Minutes,Voucher,MAC Address,IP Address,Profile,Status\n";

  DynamicJsonDocument sales(RenzFiConfig::JSON_DOC_LARGE);
  if (_storage && _storage->readJson(RenzFiConfig::SALES_FILE, sales) &&
      sales.is<JsonArray>()) {
    for (JsonObject sale : sales.as<JsonArray>()) {
      String datePart;
      String timePart;
      splitRecordedAt(sale["recorded_at"] | "", datePart, timePart);

      const String paymentType = saleField(sale, "paymentType");
      String voucher = "";
      if (paymentType.equalsIgnoreCase("voucher")) {
        voucher = saleField(sale, "voucherCode");
        if (voucher.isEmpty()) voucher = saleField(sale, "sessionId");
      }

      String mac = saleField(sale, "macAddress");
      if (mac.isEmpty()) mac = saleField(sale, "mac");

      csvOut += csvEscape(datePart) + ",";
      csvOut += csvEscape(timePart) + ",";
      csvOut += csvEscape(String(sale["amount"] | 0)) + ",";
      csvOut += csvEscape(String(sale["durationMinutes"] | 0)) + ",";
      csvOut += csvEscape(voucher) + ",";
      csvOut += csvEscape(mac) + ",";
      csvOut += csvEscape(saleField(sale, "ipAddress")) + ",";
      csvOut += csvEscape(saleField(sale, "profile")) + ",";
      {
        String status = saleField(sale, "status");
        if (status.isEmpty()) status = "completed";
        csvOut += csvEscape(status);
      }
      csvOut += "\n";
    }
  }

  struct tm timeinfo;
  char dateStamp[11] = "unknown";
  if (getLocalTime(&timeinfo)) {
    strftime(dateStamp, sizeof(dateStamp), "%Y-%m-%d", &timeinfo);
  }
  filenameOut = String("sales-report-") + dateStamp + ".csv";

  if (_logger) _logger->info("sales", "export generated");
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
