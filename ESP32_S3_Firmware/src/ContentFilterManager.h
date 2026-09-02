#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/semphr.h>

class Logger;
class StorageManager;

// Owner-managed guest-network domain blocking. Config persists on SD;
// RouterOS apply runs on RouterProvisioningWorker only (never async_tcp).
class ContentFilterManager {
 public:
  static constexpr uint8_t kMaxDomains = 32;
  static constexpr const char *kListName = "renzfi-blocked";
  static constexpr const char *kRuleComment = "renzfi-content-filter-guest";

  enum class DomainStatus : uint8_t {
    Pending,
    Active,
    Failed,
    Disabled,
  };

  enum class SyncEnqueueStatus : uint8_t {
    Ok,
    Busy,
    EmptyDomain,
    InvalidDomain,
    DuplicateDomain,
    LimitReached,
    PersistFailed,
    StorageUnavailable,
  };

  struct DomainRecord {
    String domain;
    DomainStatus status = DomainStatus::Pending;
    uint32_t addedAt = 0;
    String lastError;
  };

  void begin(StorageManager *storage, Logger *logger);

  bool enabled() const;
  uint8_t domainCount() const;
  const char *lastSyncError() const;
  uint32_t lastSyncAt() const;

  void fillList(JsonDocument &doc) const;

  SyncEnqueueStatus setEnabled(bool enabled);
  SyncEnqueueStatus addDomain(const String &rawDomain, String &normalizedOut);
  SyncEnqueueStatus removeDomain(const String &rawDomain);

  /** Build JSON payload for Router Worker ContentFilterSync job. */
  bool buildSyncPayload(JsonDocument &doc) const;

  /** Apply worker sync result — updates domain statuses and persist. */
  bool applySyncResult(JsonObjectConst result, bool routerOk, const String &message);

  static const char *syncEnqueueMessage(SyncEnqueueStatus status);
  static int syncEnqueueHttpStatus(SyncEnqueueStatus status);
  static const char *syncEnqueueCode(SyncEnqueueStatus status);

  /** Normalize user input to a bare registrable domain (no scheme/path). */
  static bool normalizeDomain(const String &raw, String &out, String &errorOut);

 private:
  StorageManager *_storage = nullptr;
  Logger *_logger = nullptr;
  mutable SemaphoreHandle_t _mutex = nullptr;

  bool _enabled = false;
  DomainRecord _domains[kMaxDomains];
  uint8_t _count = 0;
  uint32_t _lastSyncAt = 0;
  String _lastSyncError;

  void lock() const;
  void unlock() const;

  bool loadFromStorage();
  bool persistLocked();
  int findDomainIndex(const String &domain) const;
  void setAllStatusLocked(DomainStatus status);
};
