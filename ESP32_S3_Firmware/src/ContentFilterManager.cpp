#include "ContentFilterManager.h"

#include <ctype.h>

#include "Config.h"
#include "JsonHeap.h"
#include "Logger.h"
#include "StorageManager.h"
#include "StoragePaths.h"

namespace {

String trimCopy(const String &value) {
  String out = value;
  out.trim();
  return out;
}

bool isHostnameChar(char c) {
  return isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.';
}

ContentFilterManager::DomainStatus parseStatusLabel(const char *label) {
  if (!label || !label[0]) return ContentFilterManager::DomainStatus::Pending;
  if (strcmp(label, "active") == 0) return ContentFilterManager::DomainStatus::Active;
  if (strcmp(label, "failed") == 0) return ContentFilterManager::DomainStatus::Failed;
  if (strcmp(label, "disabled") == 0) return ContentFilterManager::DomainStatus::Disabled;
  return ContentFilterManager::DomainStatus::Pending;
}

const char *statusToJson(ContentFilterManager::DomainStatus status) {
  switch (status) {
    case ContentFilterManager::DomainStatus::Active:
      return "active";
    case ContentFilterManager::DomainStatus::Failed:
      return "failed";
    case ContentFilterManager::DomainStatus::Disabled:
      return "disabled";
    case ContentFilterManager::DomainStatus::Pending:
    default:
      return "pending";
  }
}

}  // namespace

void ContentFilterManager::lock() const {
  if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
}

void ContentFilterManager::unlock() const {
  if (_mutex) xSemaphoreGive(_mutex);
}

void ContentFilterManager::begin(StorageManager *storage, Logger *logger) {
  _storage = storage;
  _logger = logger;
  if (!_mutex) _mutex = xSemaphoreCreateMutex();
  _count = 0;
  _enabled = false;
  _lastSyncAt = 0;
  _lastSyncError = "";
  loadFromStorage();
  Serial.printf("[content-filter] loaded enabled=%s domains=%u\n",
                _enabled ? "true" : "false", static_cast<unsigned>(_count));
}

bool ContentFilterManager::enabled() const {
  lock();
  const bool value = _enabled;
  unlock();
  return value;
}

uint8_t ContentFilterManager::domainCount() const {
  lock();
  const uint8_t value = _count;
  unlock();
  return value;
}

const char *ContentFilterManager::lastSyncError() const {
  lock();
  const char *value = _lastSyncError.length() > 0 ? _lastSyncError.c_str() : nullptr;
  unlock();
  return value;
}

uint32_t ContentFilterManager::lastSyncAt() const {
  lock();
  const uint32_t value = _lastSyncAt;
  unlock();
  return value;
}

const char *ContentFilterManager::syncEnqueueMessage(SyncEnqueueStatus status) {
  switch (status) {
    case SyncEnqueueStatus::Ok:
      return "Domain updated";
    case SyncEnqueueStatus::Busy:
      return "Router worker busy — try again shortly";
    case SyncEnqueueStatus::EmptyDomain:
      return "Domain is required";
    case SyncEnqueueStatus::InvalidDomain:
      return "Enter a valid domain name (example.com)";
    case SyncEnqueueStatus::DuplicateDomain:
      return "Domain is already blocked";
    case SyncEnqueueStatus::LimitReached:
      return "Blocked domain limit reached";
    case SyncEnqueueStatus::PersistFailed:
      return "Unable to save content filter configuration";
    case SyncEnqueueStatus::StorageUnavailable:
      return "Storage unavailable";
    default:
      return "Request failed";
  }
}

int ContentFilterManager::syncEnqueueHttpStatus(SyncEnqueueStatus status) {
  switch (status) {
    case SyncEnqueueStatus::Ok:
      return 200;
    case SyncEnqueueStatus::Busy:
      return 503;
    case SyncEnqueueStatus::EmptyDomain:
    case SyncEnqueueStatus::InvalidDomain:
    case SyncEnqueueStatus::DuplicateDomain:
    case SyncEnqueueStatus::LimitReached:
      return 400;
    case SyncEnqueueStatus::PersistFailed:
    case SyncEnqueueStatus::StorageUnavailable:
      return 503;
    default:
      return 500;
  }
}

const char *ContentFilterManager::syncEnqueueCode(SyncEnqueueStatus status) {
  switch (status) {
    case SyncEnqueueStatus::Ok:
      return "OK";
    case SyncEnqueueStatus::Busy:
      return "WORKER_BUSY";
    case SyncEnqueueStatus::EmptyDomain:
      return "EMPTY_DOMAIN";
    case SyncEnqueueStatus::InvalidDomain:
      return "INVALID_DOMAIN";
    case SyncEnqueueStatus::DuplicateDomain:
      return "DUPLICATE_DOMAIN";
    case SyncEnqueueStatus::LimitReached:
      return "LIMIT_REACHED";
    case SyncEnqueueStatus::PersistFailed:
      return "PERSIST_FAILED";
    case SyncEnqueueStatus::StorageUnavailable:
      return "STORAGE_UNAVAILABLE";
    default:
      return "INTERNAL_ERROR";
  }
}

bool ContentFilterManager::normalizeDomain(const String &raw, String &out,
                                           String &errorOut) {
  out = trimCopy(raw);
  if (out.length() == 0) {
    errorOut = "Domain is required";
    return false;
  }

  out.toLowerCase();
  if (out.startsWith("http://")) out.remove(0, 7);
  if (out.startsWith("https://")) out.remove(0, 8);
  const int slash = out.indexOf('/');
  if (slash >= 0) out.remove(slash);
  const int qmark = out.indexOf('?');
  if (qmark >= 0) out.remove(qmark);
  const int hash = out.indexOf('#');
  if (hash >= 0) out.remove(hash);
  const int colon = out.indexOf(':');
  if (colon >= 0) out.remove(colon);
  if (out.startsWith("www.")) out.remove(0, 4);
  out.trim();
  if (out.endsWith(".")) out.remove(out.length() - 1);

  if (out.length() < 3 || out.length() > 253) {
    errorOut = "Domain length is invalid";
    return false;
  }
  if (out.indexOf(' ') >= 0 || out.indexOf("..") >= 0) {
    errorOut = "Domain contains invalid characters";
    return false;
  }
  if (out.indexOf('.') < 0) {
    errorOut = "Enter a domain such as example.com";
    return false;
  }
  for (size_t i = 0; i < out.length(); ++i) {
    if (!isHostnameChar(out.charAt(i))) {
      errorOut = "Domain contains invalid characters";
      return false;
    }
  }
  if (out.startsWith("-") || out.endsWith("-") || out.startsWith(".") ||
      out.endsWith(".")) {
    errorOut = "Domain format is invalid";
    return false;
  }
  errorOut = "";
  return true;
}

bool ContentFilterManager::loadFromStorage() {
  if (!_storage) return false;
  lock();
  _count = 0;
  _enabled = false;
  _lastSyncAt = 0;
  _lastSyncError = "";
  unlock();

  if (!_storage->exists(StoragePaths::ContentFilterFile)) return true;

  HeapJsonDocument heapDoc(RenzFiConfig::JSON_DOC_MEDIUM);
  DynamicJsonDocument &doc = heapDoc.doc();
  if (!_storage->readJson(StoragePaths::ContentFilterFile, doc) ||
      !doc.is<JsonObject>()) {
    return false;
  }

  lock();
  _enabled = doc["enabled"] | false;
  _lastSyncAt = doc["lastSyncAt"] | 0;
  _lastSyncError = doc["lastSyncError"] | "";
  JsonArrayConst domains = doc["domains"].as<JsonArrayConst>();
  if (!domains.isNull()) {
    for (JsonObjectConst row : domains) {
      if (_count >= kMaxDomains) break;
      String domain = trimCopy(row["domain"] | "");
      if (domain.isEmpty()) continue;
      _domains[_count].domain = domain;
      _domains[_count].status = parseStatusLabel(row["status"] | "pending");
      _domains[_count].addedAt = row["addedAt"] | 0;
      _domains[_count].lastError = row["lastError"] | "";
      _count++;
    }
  }
  unlock();
  return true;
}

bool ContentFilterManager::persistLocked() {
  if (!_storage) return false;
  HeapJsonDocument heapDoc(RenzFiConfig::JSON_DOC_MEDIUM);
  DynamicJsonDocument &doc = heapDoc.doc();
  doc["schemaVersion"] = 1;
  doc["enabled"] = _enabled;
  doc["lastSyncAt"] = _lastSyncAt;
  if (_lastSyncError.length() > 0) doc["lastSyncError"] = _lastSyncError;
  JsonArray domains = doc["domains"].to<JsonArray>();
  for (uint8_t i = 0; i < _count; ++i) {
    JsonObject row = domains.add<JsonObject>();
    row["domain"] = _domains[i].domain;
    row["status"] = statusToJson(_domains[i].status);
    if (_domains[i].addedAt > 0) row["addedAt"] = _domains[i].addedAt;
    if (_domains[i].lastError.length() > 0) row["lastError"] = _domains[i].lastError;
  }
  return _storage->writeJson(StoragePaths::ContentFilterFile, doc);
}

int ContentFilterManager::findDomainIndex(const String &domain) const {
  for (uint8_t i = 0; i < _count; ++i) {
    if (_domains[i].domain.equalsIgnoreCase(domain)) return static_cast<int>(i);
  }
  return -1;
}

void ContentFilterManager::setAllStatusLocked(DomainStatus status) {
  for (uint8_t i = 0; i < _count; ++i) {
    _domains[i].status = status;
  }
}

void ContentFilterManager::fillList(JsonDocument &doc) const {
  lock();
  doc["schemaVersion"] = 1;
  doc["enabled"] = _enabled;
  doc["lastSyncAt"] = _lastSyncAt;
  if (_lastSyncError.length() > 0) doc["lastSyncError"] = _lastSyncError;
  JsonArray domains = doc["domains"].to<JsonArray>();
  for (uint8_t i = 0; i < _count; ++i) {
    JsonObject row = domains.add<JsonObject>();
    row["domain"] = _domains[i].domain;
    row["status"] = statusToJson(_domains[i].status);
    if (_domains[i].addedAt > 0) row["addedAt"] = _domains[i].addedAt;
    if (_domains[i].lastError.length() > 0) row["lastError"] = _domains[i].lastError;
  }
  unlock();
}

ContentFilterManager::SyncEnqueueStatus ContentFilterManager::setEnabled(bool enabled) {
  if (!_storage) return SyncEnqueueStatus::StorageUnavailable;
  lock();
  _enabled = enabled;
  if (!enabled) setAllStatusLocked(DomainStatus::Disabled);
  const bool ok = persistLocked();
  unlock();
  return ok ? SyncEnqueueStatus::Ok : SyncEnqueueStatus::PersistFailed;
}

ContentFilterManager::SyncEnqueueStatus ContentFilterManager::addDomain(
    const String &rawDomain, String &normalizedOut) {
  if (!_storage) return SyncEnqueueStatus::StorageUnavailable;
  String error;
  if (!normalizeDomain(rawDomain, normalizedOut, error)) {
    return error.startsWith("Domain is required") ? SyncEnqueueStatus::EmptyDomain
                                                  : SyncEnqueueStatus::InvalidDomain;
  }

  lock();
  SyncEnqueueStatus status = SyncEnqueueStatus::Ok;
  if (findDomainIndex(normalizedOut) >= 0) {
    status = SyncEnqueueStatus::DuplicateDomain;
  } else if (_count >= kMaxDomains) {
    status = SyncEnqueueStatus::LimitReached;
  } else {
    _domains[_count].domain = normalizedOut;
    _domains[_count].status = DomainStatus::Pending;
    _domains[_count].addedAt = millis();
    _domains[_count].lastError = "";
    _count++;
    if (!persistLocked()) status = SyncEnqueueStatus::PersistFailed;
  }
  unlock();
  return status;
}

ContentFilterManager::SyncEnqueueStatus ContentFilterManager::removeDomain(
    const String &rawDomain) {
  if (!_storage) return SyncEnqueueStatus::StorageUnavailable;
  String normalized;
  String error;
  if (!normalizeDomain(rawDomain, normalized, error)) {
    return SyncEnqueueStatus::InvalidDomain;
  }

  lock();
  const int index = findDomainIndex(normalized);
  if (index < 0) {
    unlock();
    return SyncEnqueueStatus::InvalidDomain;
  }
  for (uint8_t i = static_cast<uint8_t>(index); i + 1 < _count; ++i) {
    _domains[i] = _domains[i + 1];
  }
  _count--;
  const bool ok = persistLocked();
  unlock();
  return ok ? SyncEnqueueStatus::Ok : SyncEnqueueStatus::PersistFailed;
}

bool ContentFilterManager::buildSyncPayload(JsonDocument &doc) const {
  lock();
  doc["enabled"] = _enabled;
  JsonArray domains = doc["domains"].to<JsonArray>();
  for (uint8_t i = 0; i < _count; ++i) {
    domains.add(_domains[i].domain);
  }
  unlock();
  return true;
}

bool ContentFilterManager::applySyncResult(JsonObjectConst result, bool routerOk,
                                         const String &message) {
  lock();
  _lastSyncAt = millis();
  _lastSyncError = routerOk ? "" : message;

  JsonArrayConst domainResults = result["domains"].as<JsonArrayConst>();
  if (!domainResults.isNull()) {
    for (JsonObjectConst row : domainResults) {
      const char *domain = row["domain"] | "";
      const char *statusLabel = row["status"] | "failed";
      const char *error = row["error"] | "";
      const int index = findDomainIndex(String(domain));
      if (index < 0) continue;
      _domains[index].status = parseStatusLabel(statusLabel);
      _domains[index].lastError = error;
    }
  } else if (!routerOk) {
    for (uint8_t i = 0; i < _count; ++i) {
      if (_domains[i].status == DomainStatus::Pending) {
        _domains[i].status = DomainStatus::Failed;
        _domains[i].lastError = message;
      }
    }
  } else if (!_enabled) {
    setAllStatusLocked(DomainStatus::Disabled);
  } else {
    for (uint8_t i = 0; i < _count; ++i) {
      _domains[i].status = DomainStatus::Active;
      _domains[i].lastError = "";
    }
  }

  const bool ok = persistLocked();
  unlock();
  return ok;
}
