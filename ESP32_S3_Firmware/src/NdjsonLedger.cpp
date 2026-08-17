#include "NdjsonLedger.h"

#include <SD.h>
#include <SPIFFS.h>

#include "Config.h"
#include "StoragePaths.h"

namespace {

constexpr NdjsonLedger::Kind kKinds[] = {
    NdjsonLedger::Kind::Sales, NdjsonLedger::Kind::Sessions,
    NdjsonLedger::Kind::Vouchers, NdjsonLedger::Kind::Logs};

String g_recentIds[4][24];
size_t g_recentHeads[4] = {};

size_t kindIndex(NdjsonLedger::Kind kind) {
  return static_cast<size_t>(kind);
}

bool validBucket(const String &bucket) {
  if (bucket == "undated") return true;
  if (bucket.length() != 7 || bucket[4] != '-') return false;
  for (size_t i = 0; i < bucket.length(); ++i) {
    if (i == 4) continue;
    if (!isDigit(bucket[i])) return false;
  }
  const int month = bucket.substring(5).toInt();
  return month >= 1 && month <= 12;
}

bool lineHasEventId(const String &line, const String &eventId) {
  if (line.isEmpty() || eventId.isEmpty()) return false;
  DynamicJsonDocument doc(line.length() + 256);
  if (deserializeJson(doc, line)) return false;  // torn/invalid lines are ignored
  return eventId == String(doc["eventId"] | "");
}

}  // namespace

const char *NdjsonLedger::kindName(Kind kind) {
  switch (kind) {
    case Kind::Sales: return "sales";
    case Kind::Sessions: return "sessions";
    case Kind::Vouchers: return "vouchers";
    case Kind::Logs: return "logs";
  }
  return "";
}

const char *NdjsonLedger::directoryFor(Kind kind) {
  switch (kind) {
    case Kind::Sales: return StoragePaths::HistorySales;
    case Kind::Sessions: return StoragePaths::HistorySessions;
    case Kind::Vouchers: return StoragePaths::HistoryVouchers;
    case Kind::Logs: return StoragePaths::HistoryLogs;
  }
  return nullptr;
}

const char *NdjsonLedger::spoolFor(Kind kind) {
  switch (kind) {
    case Kind::Sales: return StoragePaths::Spiffs::SalesHistorySpool;
    case Kind::Sessions: return StoragePaths::Spiffs::SessionsHistorySpool;
    case Kind::Vouchers: return StoragePaths::Spiffs::VouchersHistorySpool;
    case Kind::Logs: return nullptr;
  }
  return nullptr;
}

String NdjsonLedger::bucketFor(const String &eventAt) {
  if (eventAt.length() >= 7 && eventAt[4] == '-' &&
      isDigit(eventAt[0]) && isDigit(eventAt[1]) &&
      isDigit(eventAt[2]) && isDigit(eventAt[3]) &&
      isDigit(eventAt[5]) && isDigit(eventAt[6])) {
    const String bucket = eventAt.substring(0, 7);
    if (validBucket(bucket)) return bucket;
  }
  return "undated";
}

bool NdjsonLedger::pathFor(Kind kind, const String &bucket, String &path) {
  const char *dir = directoryFor(kind);
  if (!dir || !validBucket(bucket)) return false;
  path = String(dir) + "/" + bucket + ".ndjson";
  return StoragePaths::isValidSdPath(path.c_str());
}

bool NdjsonLedger::recentIdContains(Kind kind, const String &eventId) {
  const size_t index = kindIndex(kind);
  for (size_t i = 0; i < kRecentIdCount; ++i) {
    if (g_recentIds[index][i] == eventId) return true;
  }
  return false;
}

void NdjsonLedger::rememberRecentId(Kind kind, const String &eventId) {
  if (eventId.isEmpty()) return;
  const size_t index = kindIndex(kind);
  g_recentIds[index][g_recentHeads[index]] = eventId;
  g_recentHeads[index] = (g_recentHeads[index] + 1) % kRecentIdCount;
}

bool NdjsonLedger::containsRecentEventId(Kind kind, const String &path,
                                         const String &eventId) {
  if (recentIdContains(kind, eventId)) return true;
  if (!SD.exists(path)) return false;
  File file = SD.open(path, FILE_READ);
  if (!file) return false;
  const size_t size = file.size();
  const size_t start = size > kTailDedupeBytes ? size - kTailDedupeBytes : 0;
  if (start > 0) {
    file.seek(start);
    file.readStringUntil('\n');  // discard a partial first line
  }
  String line;
  line.reserve(512);
  while (file.available()) {
    const char ch = static_cast<char>(file.read());
    if (ch == '\n') {
      if (lineHasEventId(line, eventId)) {
        file.close();
        rememberRecentId(kind, eventId);
        return true;
      }
      line = "";
    } else if (ch != '\r' && line.length() < kMaxLineBytes) {
      line += ch;
    }
  }
  const bool found = lineHasEventId(line, eventId);
  file.close();
  if (found) rememberRecentId(kind, eventId);
  return found;
}

bool NdjsonLedger::appendLineSd(const String &path, const String &line) {
  File file = SD.open(path, FILE_APPEND);
  if (!file) return false;
  if (file.size() > 0) {
    file.seek(file.size() - 1);
    if (file.read() != '\n') file.print('\n');
    file.seek(file.size());
  }
  const size_t written = file.print(line);
  const size_t newline = file.print('\n');
  file.flush();
  file.close();
  return written == line.length() && newline == 1;
}

bool NdjsonLedger::appendSd(Kind kind, const String &eventId,
                            const String &eventAt, JsonObjectConst event) {
  if (eventId.isEmpty()) return false;
  String path;
  if (!pathFor(kind, bucketFor(eventAt), path)) return false;
  // Log IDs contain a boot-instance and monotonic sequence and are unique by
  // construction. Other domains use a 24-ID RAM cache plus a 32 KiB tail scan.
  if (kind != Kind::Logs && containsRecentEventId(kind, path, eventId)) {
    return true;
  }

  DynamicJsonDocument doc(measureJson(event) + eventId.length() + 256);
  doc.set(event);
  doc["eventId"] = eventId;
  if (!doc["eventAt"].is<const char *>()) doc["eventAt"] = eventAt;
  String line;
  serializeJson(doc, line);
  if (line.isEmpty() || line.length() > kMaxLineBytes) return false;
  const bool appended = appendLineSd(path, line);
  if (appended && kind != Kind::Logs) rememberRecentId(kind, eventId);
  return appended;
}

bool NdjsonLedger::appendSdPreparedLines(Kind kind, const String &eventAt,
                                         const String *eventIds,
                                         const String *lines, size_t count) {
  if (!eventIds || !lines || count == 0) return false;
  String path;
  if (!pathFor(kind, bucketFor(eventAt), path)) return false;

  // Ensure parent directory exists via first open failure path handled below.
  File file = SD.open(path, FILE_APPEND);
  if (!file) {
    // Create empty file if missing (appendSd historically relied on open).
    file = SD.open(path, FILE_WRITE);
    if (!file) return false;
    file.close();
    file = SD.open(path, FILE_APPEND);
    if (!file) return false;
  }
  if (file.size() > 0) {
    file.seek(file.size() - 1);
    if (file.read() != '\n') file.print('\n');
    file.seek(file.size());
  }

  size_t writtenCount = 0;
  for (size_t i = 0; i < count; ++i) {
    if (eventIds[i].isEmpty() || lines[i].isEmpty()) continue;
    if (lines[i].length() > kMaxLineBytes) {
      file.close();
      return false;
    }
    // Batch path: RAM recent-id only (no 32 KiB tail scan per line). Callers
    // must use unique eventIds (e.g. voucher:CODE:created for new codes).
    if (kind != Kind::Logs && recentIdContains(kind, eventIds[i])) continue;
    const size_t written = file.print(lines[i]);
    const size_t newline = file.print('\n');
    if (written != lines[i].length() || newline != 1) {
      file.close();
      return false;
    }
    if (kind != Kind::Logs) rememberRecentId(kind, eventIds[i]);
    (void)writtenCount;
    ++writtenCount;
  }
  file.flush();
  file.close();
  return true;
}

bool NdjsonLedger::appendSpool(Kind kind, const String &eventId,
                               const String &eventAt, JsonObjectConst event,
                               size_t aggregateFallbackBytes) {
  const char *path = spoolFor(kind);
  if (!path || eventId.isEmpty()) return false;
  if (spoolContainsEventId(path, eventId)) return true;

  DynamicJsonDocument envelope(measureJson(event) + eventId.length() + 384);
  envelope["kind"] = kindName(kind);
  envelope["eventId"] = eventId;
  envelope["eventAt"] = eventAt;
  envelope["event"].set(event);
  String line;
  serializeJson(envelope, line);
  if (line.isEmpty() || line.length() > kMaxLineBytes) return false;

  size_t currentSize = 0;
  if (SPIFFS.exists(path)) {
    File current = SPIFFS.open(path, "r");
    if (!current) return false;
    currentSize = current.size();
    current.close();
  }
  const size_t addedBytes = line.length() + 1;
  if (currentSize + addedBytes > kMaxSpoolBytesPerKind) return false;
  if (aggregateFallbackBytes + addedBytes >
      RenzFiConfig::FB_HARD_LIMIT_BYTES) {
    return false;
  }
  const size_t total = SPIFFS.totalBytes();
  const size_t used = SPIFFS.usedBytes();
  const size_t freeBytes = total > used ? total - used : 0;
  if (freeBytes < RenzFiConfig::SPIFFS_MIN_FREE_BYTES + addedBytes) {
    return false;
  }

  File spool = SPIFFS.open(path, "a");
  if (!spool) return false;
  const size_t written = spool.print(line);
  const size_t newline = spool.print('\n');
  spool.flush();
  spool.close();
  return written == line.length() && newline == 1;
}

bool NdjsonLedger::spoolContainsEventId(const char *path,
                                        const String &eventId) {
  if (!path || !SPIFFS.exists(path)) return false;
  File spool = SPIFFS.open(path, "r");
  if (!spool) return false;
  while (spool.available()) {
    String line = spool.readStringUntil('\n');
    if (line.length() > kMaxLineBytes) continue;
    DynamicJsonDocument envelope(line.length() + 128);
    if (!deserializeJson(envelope, line) &&
        eventId == String(envelope["eventId"] | "")) {
      spool.close();
      return true;
    }
  }
  spool.close();
  return false;
}

bool NdjsonLedger::replaySpools() {
  bool allOk = true;
  for (Kind kind : kKinds) {
    const char *path = spoolFor(kind);
    if (!path || !SPIFFS.exists(path)) continue;
    File spool = SPIFFS.open(path, "r");
    if (!spool) {
      allOk = false;
      continue;
    }

    bool spoolOk = true;
    String quarantine;
    quarantine.reserve(kMaxLineBytes);
    String line;
    line.reserve(512);
    while (spool.available()) {
      const char ch = static_cast<char>(spool.read());
      if (ch != '\n') {
        if (line.length() < kMaxLineBytes) line += ch;
        continue;
      }
      line.trim();
      if (!line.isEmpty()) {
        DynamicJsonDocument envelope(line.length() + 256);
        if (deserializeJson(envelope, line) ||
            String(envelope["kind"] | "") != kindName(kind) ||
            !envelope["event"].is<JsonObject>()) {
          if (quarantine.length() + line.length() + 1 <= kMaxLineBytes) {
            quarantine += line;
            quarantine += '\n';
          }
        } else {
          const String eventId = envelope["eventId"] | "";
          const String eventAt = envelope["eventAt"] | "";
          if (!appendSd(kind, eventId, eventAt,
                        envelope["event"].as<JsonObjectConst>())) {
            spoolOk = false;
            break;
          }
        }
      }
      line = "";
    }
    spool.close();
    // A non-newline-terminated final fragment is torn. It is quarantined and
    // never blocks replay of the complete records that preceded it.
    line.trim();
    if (spoolOk && !line.isEmpty() &&
        quarantine.length() + line.length() <= kMaxLineBytes) {
      quarantine += line;
    }
    if (spoolOk) {
      const String quarantinePath = String(path) + ".q";
      // Valid complete events are already verified on SD. Clear the active
      // spool first so a low-space quarantine write can never block replay.
      SPIFFS.remove(path);
      if (!quarantine.isEmpty()) {
        File out = SPIFFS.open(quarantinePath, "w");
        if (out) {
          out.print(quarantine);
          out.flush();
          out.close();
        }
      }
    } else {
      allOk = false;  // active spool retained for bounded-tail idempotent retry
    }
  }
  return allOk;
}

void NdjsonLedger::clearSpools() {
  for (Kind kind : kKinds) {
    const char *path = spoolFor(kind);
    if (path && SPIFFS.exists(path)) SPIFFS.remove(path);
    const String quarantinePath = path ? String(path) + ".q" : String();
    if (!quarantinePath.isEmpty() && SPIFFS.exists(quarantinePath)) {
      SPIFFS.remove(quarantinePath);
    }
  }
}
