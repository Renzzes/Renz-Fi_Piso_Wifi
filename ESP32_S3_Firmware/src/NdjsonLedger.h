#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

class NdjsonLedger {
 public:
  enum class Kind : uint8_t { Sales, Sessions, Vouchers, Logs };

  static const char *kindName(Kind kind);
  static bool pathFor(Kind kind, const String &bucket, String &path);
  static String bucketFor(const String &eventAt);

  // Callers serialize access through StorageManager's recursive mutex.
  static bool appendSd(Kind kind, const String &eventId,
                       const String &eventAt, JsonObjectConst event);
  /** One SD open/flush for N pre-serialized NDJSON lines (same month bucket). */
  static bool appendSdPreparedLines(Kind kind, const String &eventAt,
                                    const String *eventIds, const String *lines,
                                    size_t count);
  static bool appendSpool(Kind kind, const String &eventId,
                          const String &eventAt, JsonObjectConst event,
                          size_t aggregateFallbackBytes);
  static bool replaySpools();
  static void clearSpools();

 private:
  static constexpr size_t kMaxSpoolBytesPerKind = 16U * 1024U;
  static constexpr size_t kMaxLineBytes = 4096U;
  static constexpr size_t kTailDedupeBytes = 32U * 1024U;
  static constexpr size_t kRecentIdCount = 24U;

  static const char *directoryFor(Kind kind);
  static const char *spoolFor(Kind kind);
  static bool containsRecentEventId(Kind kind, const String &path,
                                    const String &eventId);
  static bool spoolContainsEventId(const char *path, const String &eventId);
  static bool recentIdContains(Kind kind, const String &eventId);
  static void rememberRecentId(Kind kind, const String &eventId);
  static bool appendLineSd(const String &path, const String &line);
};
