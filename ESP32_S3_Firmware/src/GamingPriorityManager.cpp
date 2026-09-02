#include "GamingPriorityManager.h"

#include "Config.h"
#include "JsonHeap.h"
#include "StorageManager.h"
#include "StoragePaths.h"

namespace {

bool validPortList(const char *ports) {
  if (!ports || !ports[0] || strlen(ports) > 64) return false;
  for (const char *p = ports; *p; ++p) {
    if (!isdigit(static_cast<unsigned char>(*p)) && *p != '-' && *p != ',') {
      return false;
    }
  }
  return true;
}

bool validClassData(JsonObjectConst data, String &errorOut) {
  const char *protocol = data["protocol"] | "";
  const char *ports = data["ports"] | "";
  if (strcmp(protocol, "udp") != 0 && strcmp(protocol, "tcp") != 0) {
    errorOut = "classificationData.protocol must be udp or tcp";
    return false;
  }
  if (!validPortList(ports)) {
    errorOut = "classificationData.ports is invalid";
    return false;
  }
  errorOut = "";
  return true;
}

bool serializeClassData(JsonObjectConst data, char *out, size_t cap) {
  if (!out || cap == 0) return false;
  const char *protocol = data["protocol"] | "";
  const char *ports = data["ports"] | "";
  const int n = snprintf(out, cap, "{\"protocol\":\"%s\",\"ports\":\"%s\"}",
                         protocol, ports);
  return n > 0 && static_cast<size_t>(n) < cap;
}

}  // namespace

void GamingPriorityManager::lock() const {
  if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
}

void GamingPriorityManager::unlock() const {
  if (_mutex) xSemaphoreGive(_mutex);
}

void GamingPriorityManager::begin(StorageManager *storage) {
  _storage = storage;
  if (!_mutex) _mutex = xSemaphoreCreateMutex();
  _profileCount = 0;
  loadFromStorage();
}

void GamingPriorityManager::seedDefaultsLocked() {
  _enabled = false;
  GamingPriority::copyField(_priority, sizeof(_priority), "normal");
  _minimumGamingMbps = 5;
  _maximumGamingMbps = 20;
  _perUserGamingMbps = 5;
  _profileCount = 2;
  GamingPriority::copyField(_profiles[0].id, sizeof(_profiles[0].id), "mlbb");
  GamingPriority::copyField(_profiles[0].name, sizeof(_profiles[0].name),
                            "Mobile Legends");
  GamingPriority::copyField(_profiles[0].slug, sizeof(_profiles[0].slug),
                            "mobile-legends");
  GamingPriority::copyField(_profiles[0].classData, sizeof(_profiles[0].classData),
                            "{\"protocol\":\"udp\",\"ports\":\"5001-5010\"}");
  GamingPriority::copyField(_profiles[0].priority, sizeof(_profiles[0].priority),
                            "high");
  _profiles[0].enabled = true;
  GamingPriority::copyField(_profiles[1].id, sizeof(_profiles[1].id), "codm");
  GamingPriority::copyField(_profiles[1].name, sizeof(_profiles[1].name),
                            "Call of Duty Mobile");
  GamingPriority::copyField(_profiles[1].slug, sizeof(_profiles[1].slug),
                            "call-of-duty-mobile");
  GamingPriority::copyField(_profiles[1].classData, sizeof(_profiles[1].classData),
                            "{\"protocol\":\"udp\",\"ports\":\"10010-10019\"}");
  GamingPriority::copyField(_profiles[1].priority, sizeof(_profiles[1].priority),
                            "high");
  _profiles[1].enabled = true;
}

bool GamingPriorityManager::ingestProfiles(JsonArrayConst profiles, bool strict,
                                           String &errorOut) {
  _profileCount = 0;
  for (JsonObjectConst row : profiles) {
    if (_profileCount >= GamingPriority::kMaxProfiles) break;
    GamingPriority::GameProfile &p = _profiles[_profileCount];
    memset(&p, 0, sizeof(p));
    GamingPriority::copyField(p.id, sizeof(p.id), row["id"] | "");
    GamingPriority::copyField(p.name, sizeof(p.name), row["name"] | "");
    GamingPriority::copyField(p.slug, sizeof(p.slug), row["slug"] | "");
    GamingPriority::copyField(p.priority, sizeof(p.priority),
                              row["priority"] | "normal");
    p.enabled = row["enabled"] | false;
    JsonObjectConst data = row["classificationData"].as<JsonObjectConst>();
    if (!data.isNull()) {
      if (strict && strcmp(row["classificationMethod"] | "", GamingPriority::kClassMethod) != 0) {
        errorOut = "Unsupported classification method";
        return false;
      }
      if (strict && !validClassData(data, errorOut)) return false;
      if (!serializeClassData(data, p.classData, sizeof(p.classData))) {
        errorOut = "Malformed classification data";
        return false;
      }
    } else {
      GamingPriority::copyField(p.classData, sizeof(p.classData),
                                row["classificationData"] | "");
    }
    if (!p.id[0] || !p.name[0] || !GamingPriority::isValidSlug(p.slug)) {
      errorOut = strict ? "Invalid game profile" : "Invalid stored profile";
      return false;
    }
    if (strict && !GamingPriority::isValidPriorityLabel(p.priority)) {
      errorOut = "Invalid game profile priority";
      return false;
    }
    for (uint8_t i = 0; i < _profileCount; ++i) {
      if (strcmp(_profiles[i].id, p.id) == 0) {
        errorOut = "Duplicate game profile id";
        return false;
      }
      if (strcmp(_profiles[i].slug, p.slug) == 0) {
        errorOut = "Duplicate game profile slug";
        return false;
      }
    }
    _profileCount++;
  }
  if (_profileCount == 0) {
    errorOut = "At least one game profile is required";
    return false;
  }
  errorOut = "";
  return true;
}

bool GamingPriorityManager::loadFromStorage() {
  if (!_storage) return false;
  lock();
  _profileCount = 0;
  _enabled = false;
  GamingPriority::copyField(_priority, sizeof(_priority), "normal");
  _minimumGamingMbps = 5;
  _maximumGamingMbps = 20;
  _perUserGamingMbps = 5;
  _updatedAt = 0;
  _configRevision = 0;
  _appliedRevision = 0;
  _lastApplyAt = 0;
  _lastApplyOk = true;
  _lastSyncError = "";
  unlock();

  if (!_storage->exists(StoragePaths::GamingPriorityFile)) {
    lock();
    seedDefaultsLocked();
    _configRevision = 1;
    unlock();
    return persistLocked();
  }

  HeapJsonDocument heapDoc(RenzFiConfig::JSON_DOC_SMALL);
  DynamicJsonDocument &doc = heapDoc.doc();
  if (!_storage->readJson(StoragePaths::GamingPriorityFile, doc) ||
      !doc.is<JsonObject>()) {
    return false;
  }

  lock();
  _enabled = doc["enabled"] | false;
  GamingPriority::copyField(_priority, sizeof(_priority), doc["priority"] | "normal");
  _minimumGamingMbps = doc["minimumGamingMbps"] | 5;
  _maximumGamingMbps = doc["maximumGamingMbps"] | 20;
  _perUserGamingMbps = doc["perUserGamingMbps"] | 5;
  _updatedAt = doc["updatedAt"] | 0;
  _configRevision = doc["configRevision"] | 1;
  _appliedRevision = doc["appliedRevision"] | 0;
  _lastApplyAt = doc["lastApplyAt"] | 0;
  _lastApplyOk = doc["lastApplyOk"] | true;
  _lastSyncError = doc["lastSyncError"] | doc["lastApplyError"] | "";
  JsonArrayConst profiles = doc["gameProfiles"].as<JsonArrayConst>();
  if (profiles.isNull() || profiles.size() == 0) {
    seedDefaultsLocked();
  } else {
    String parseError;
    if (!ingestProfiles(profiles, false, parseError)) {
      unlock();
      return false;
    }
  }
  unlock();
  return true;
}

void GamingPriorityManager::writeMetaJson(JsonObject obj) const {
  obj["schemaVersion"] = GamingPriority::kSchemaVersion;
  obj["enabled"] = _enabled;
  obj["priority"] = _priority;
  obj["minimumGamingMbps"] = _minimumGamingMbps;
  obj["maximumGamingMbps"] = _maximumGamingMbps;
  obj["perUserGamingMbps"] = _perUserGamingMbps;
  obj["updatedAt"] = _updatedAt;
  obj["configRevision"] = _configRevision;
  obj["appliedRevision"] = _appliedRevision;
  obj["lastApplyAt"] = _lastApplyAt;
  obj["lastApplyOk"] = _lastApplyOk;
  if (_lastSyncError.length() > 0) {
    obj["lastApplyError"] = _lastSyncError;
  }
  uint8_t code = 0;
  if (_configRevision != _appliedRevision) code = 1;
  else if (!_lastApplyOk) code = 3;
  else if (_enabled && _appliedRevision > 0) code = 2;
  obj["applyStatus"] = GamingPriority::applyStatusLabel(code);
}

void GamingPriorityManager::writeProfilesJson(JsonArray arr, bool syncOnly) const {
  for (uint8_t i = 0; i < _profileCount; ++i) {
    const GamingPriority::GameProfile &p = _profiles[i];
    JsonObject row = arr.add<JsonObject>();
    if (!syncOnly) {
      row["id"] = p.id;
      row["name"] = p.name;
    }
    row["slug"] = p.slug;
    row["enabled"] = p.enabled;
    row["classificationMethod"] = GamingPriority::kClassMethod;
    row["priority"] = p.priority;
    row["classificationData"] = serialized(p.classData);
  }
}

bool GamingPriorityManager::persistLocked() {
  if (!_storage) return false;
  HeapJsonDocument heapDoc(RenzFiConfig::JSON_DOC_SMALL);
  DynamicJsonDocument &doc = heapDoc.doc();
  writeMetaJson(doc.to<JsonObject>());
  writeProfilesJson(doc["gameProfiles"].to<JsonArray>(), false);
  return _storage->writeJson(StoragePaths::GamingPriorityFile, doc);
}

void GamingPriorityManager::fillJson(JsonDocument &doc) const {
  lock();
  writeMetaJson(doc.to<JsonObject>());
  writeProfilesJson(doc["gameProfiles"].to<JsonArray>(), false);
  unlock();
}

bool GamingPriorityManager::updateFromJson(JsonObjectConst body,
                                           String &errorOut) {
  if (body["enabled"].isNull()) {
    errorOut = "enabled is required";
    return false;
  }
  const bool enabled = body["enabled"].as<bool>();
  const char *priority = body["priority"] | "normal";
  if (!GamingPriority::isValidPriorityLabel(priority)) {
    errorOut = "Invalid priority";
    return false;
  }
  const uint16_t minimum = body["minimumGamingMbps"] | 0;
  const uint16_t maximum = body["maximumGamingMbps"] | 0;
  const uint16_t perUser = body["perUserGamingMbps"] | 0;
  if (minimum == 0 || maximum == 0 || minimum > GamingPriority::kMaxMbps ||
      maximum > GamingPriority::kMaxMbps) {
    errorOut = "Invalid Mbps value";
    return false;
  }
  if (enabled && (perUser == 0 || perUser > GamingPriority::kMaxMbps)) {
    errorOut = "Invalid per-user Mbps";
    return false;
  }
  if (maximum < minimum) {
    errorOut = "maximum must be >= minimum";
    return false;
  }
  if (enabled && perUser > maximum) {
    errorOut = "per-user exceeds maximum";
    return false;
  }
  JsonArrayConst profiles = body["gameProfiles"].as<JsonArrayConst>();
  if (profiles.isNull() || profiles.size() == 0) {
    errorOut = "At least one game profile is required";
    return false;
  }

  lock();
  if (!ingestProfiles(profiles, true, errorOut)) {
    unlock();
    return false;
  }
  _enabled = enabled;
  GamingPriority::copyField(_priority, sizeof(_priority), priority);
  _minimumGamingMbps = minimum;
  _maximumGamingMbps = maximum;
  if (enabled) _perUserGamingMbps = perUser;
  _updatedAt = millis();
  if (_configRevision == 0) _configRevision = 1;
  else _configRevision++;
  const bool ok = persistLocked();
  unlock();
  if (!ok) {
    loadFromStorage();
    errorOut = "Unable to save GP config";
    return false;
  }
  errorOut = "";
  return true;
}

bool GamingPriorityManager::buildSyncPayload(JsonDocument &doc) const {
  lock();
  doc["enabled"] = _enabled;
  doc["priority"] = _priority;
  doc["maximumGamingMbps"] = _maximumGamingMbps;
  doc["perUserGamingMbps"] = _perUserGamingMbps;
  writeProfilesJson(doc["gameProfiles"].to<JsonArray>(), true);
  unlock();
  return true;
}

bool GamingPriorityManager::applySyncResult(bool routerOk, const String &message) {
  lock();
  _lastApplyAt = millis();
  _lastApplyOk = routerOk;
  _lastSyncError = routerOk ? "" : message;
  if (routerOk) _appliedRevision = _configRevision;
  const bool ok = persistLocked();
  unlock();
  return ok;
}
