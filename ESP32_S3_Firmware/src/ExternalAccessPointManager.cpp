#include "ExternalAccessPointManager.h"

#include <esp_system.h>

#include "CredentialProtector.h"
#include "EthernetManager.h"
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

String jsonString(JsonObjectConst input, const char *key) {
  if (input[key].isNull()) return String();
  return trimCopy(input[key].as<String>());
}

bool jsonHasKey(JsonObjectConst input, const char *key) {
  return !input[key].isNull();
}

}  // namespace

void ExternalAccessPointManager::begin(StorageManager *storage,
                                       EthernetManager *eth, Logger *logger) {
  _storage = storage;
  _eth = eth;
  _logger = logger;
  _count = 0;
  _registryError = "";
  loadFromStorage();
  Serial.printf("[access-points] loaded count=%u registry_error=%s\n",
                static_cast<unsigned>(_count),
                _registryError.length() > 0 ? _registryError.c_str() : "none");
}

void ExternalAccessPointManager::loadFromStorage() {
  _count = 0;
  _registryError = "";
  if (!_storage) return;
  if (_storage->sdRecoveryInProgress()) {
    _registryError = "STORAGE_UNAVAILABLE";
    return;
  }
  if (!_storage->exists(StoragePaths::AccessPointsFile)) return;

  PsramJsonDocument heap;
  JsonDocument &doc = heap.doc();
  if (!_storage->readJson(StoragePaths::AccessPointsFile, doc) ||
      doc.isNull() || !doc["accessPoints"].is<JsonArray>()) {
    _registryError = "CORRUPT_FILE";
    Serial.println("[access-points] registry file corrupt — RAM empty, file kept");
    return;
  }

  const uint8_t schema = static_cast<uint8_t>(doc["schemaVersion"] | 1);
  if (schema != ExternalAccessPoint::kSchemaVersion) {
    _registryError = "UNSUPPORTED_SCHEMA";
    Serial.printf("[access-points] unsupported schemaVersion=%u — RAM empty, file kept\n",
                  static_cast<unsigned>(schema));
    return;
  }

  JsonArrayConst rows = doc["accessPoints"].as<JsonArrayConst>();
  uint8_t loaded = 0;
  for (JsonObjectConst row : rows) {
    if (loaded >= kMaxAccessPoints) {
      Serial.println("[access-points] extra records ignored (max 8)");
      break;
    }
    Record record;
    record.id = jsonString(row, "id");
    record.name = jsonString(row, "name");
    record.enabled = row["enabled"].isNull() ? true : row["enabled"].as<bool>();
    record.vendor = ExternalAccessPoint::parseVendor(row["vendor"] | "generic");
    record.model = jsonString(row, "model");
    record.managementIp = jsonString(row, "managementIp");
    record.username = jsonString(row, "username");
    record.passwordProtected = jsonString(row, "passwordProtected");
    record.ssid = jsonString(row, "ssid");
    record.location = jsonString(row, "location");
    record.notes = jsonString(row, "notes");
    if (record.id.length() == 0 || record.name.length() == 0 ||
        record.managementIp.length() == 0) {
      continue;
    }
    _records[loaded++] = record;
  }
  _count = loaded;
}

ExternalAccessPoint::CrudStatus ExternalAccessPointManager::persist() {
  if (!_storage) return ExternalAccessPoint::CrudStatus::StorageError;
  if (_storage->sdRecoveryInProgress()) {
    return ExternalAccessPoint::CrudStatus::StorageRecovery;
  }
  if (!_storage->sdIoAllowed()) {
    return ExternalAccessPoint::CrudStatus::StorageError;
  }

  PsramJsonDocument heap;
  JsonDocument &doc = heap.doc();
  doc["schemaVersion"] = ExternalAccessPoint::kSchemaVersion;
  JsonArray rows = doc["accessPoints"].to<JsonArray>();
  for (uint8_t i = 0; i < _count; ++i) {
    JsonObject row = rows.add<JsonObject>();
    const Record &record = _records[i];
    row["id"] = record.id;
    row["name"] = record.name;
    row["enabled"] = record.enabled;
    row["vendor"] = ExternalAccessPoint::vendorLabel(record.vendor);
    row["model"] = record.model;
    row["managementIp"] = record.managementIp;
    row["username"] = record.username;
    row["passwordProtected"] = record.passwordProtected;
    row["ssid"] = record.ssid;
    row["location"] = record.location;
    row["notes"] = record.notes;
  }
  if (!_storage->writeJson(StoragePaths::AccessPointsFile, doc)) {
    if (_storage->sdRecoveryInProgress()) {
      return ExternalAccessPoint::CrudStatus::StorageRecovery;
    }
    return ExternalAccessPoint::CrudStatus::StorageError;
  }
  _registryError = "";
  return ExternalAccessPoint::CrudStatus::Ok;
}

void ExternalAccessPointManager::fillPublicRecord(JsonObject obj,
                                                  const Record &record) const {
  obj["id"] = record.id;
  obj["name"] = record.name;
  obj["enabled"] = record.enabled;
  obj["vendor"] = ExternalAccessPoint::vendorLabel(record.vendor);
  obj["model"] = record.model;
  obj["managementIp"] = record.managementIp;
  obj["hasCredentials"] = record.passwordProtected.length() > 0;
  obj["ssid"] = record.ssid;
  obj["location"] = record.location;
  obj["notes"] = record.notes;
}

void ExternalAccessPointManager::fillList(JsonDocument &doc) const {
  doc.clear();
  doc["schemaVersion"] = ExternalAccessPoint::kSchemaVersion;
  JsonArray rows = doc["accessPoints"].to<JsonArray>();
  for (uint8_t i = 0; i < _count; ++i) {
    fillPublicRecord(rows.add<JsonObject>(), _records[i]);
  }
  if (_registryError.length() > 0) {
    doc["registryError"] = _registryError;
  }
}

ExternalAccessPoint::CrudStatus ExternalAccessPointManager::getById(
    const String &id, JsonDocument &doc) const {
  const int index = findIndex(id);
  if (index < 0) return ExternalAccessPoint::CrudStatus::NotFound;
  doc.clear();
  fillPublicRecord(doc.to<JsonObject>(), _records[index]);
  return ExternalAccessPoint::CrudStatus::Ok;
}

int ExternalAccessPointManager::findIndex(const String &id) const {
  if (id.length() == 0) return -1;
  for (uint8_t i = 0; i < _count; ++i) {
    if (_records[i].id == id) return static_cast<int>(i);
  }
  return -1;
}

int ExternalAccessPointManager::findIndexByIp(const String &ip,
                                              int exceptIndex) const {
  if (ip.length() == 0) return -1;
  for (uint8_t i = 0; i < _count; ++i) {
    if (static_cast<int>(i) == exceptIndex) continue;
    if (_records[i].managementIp == ip) return static_cast<int>(i);
  }
  return -1;
}

String ExternalAccessPointManager::generateId() const {
  for (uint8_t attempt = 0; attempt < 12; ++attempt) {
    char buf[16];
    snprintf(buf, sizeof(buf), "ap_%08x",
             static_cast<unsigned>(esp_random()));
    const String id = buf;
    if (findIndex(id) < 0) return id;
  }
  return String("ap_") + String(millis(), HEX);
}

ExternalAccessPoint::CrudStatus ExternalAccessPointManager::validateName(
    const String &name) const {
  if (name.length() < ExternalAccessPoint::kNameMinLen ||
      name.length() > ExternalAccessPoint::kNameMaxLen) {
    return ExternalAccessPoint::CrudStatus::InvalidRequest;
  }
  return ExternalAccessPoint::CrudStatus::Ok;
}

ExternalAccessPoint::CrudStatus ExternalAccessPointManager::validateIp(
    const String &ip, int exceptIndex) const {
  const String liveIp = (_eth && _eth->hasIp()) ? _eth->ip() : String();
  const String liveGw = (_eth && _eth->hasIp()) ? _eth->gateway() : String();
  const String liveMask = (_eth && _eth->hasIp()) ? _eth->subnet() : String();
  const ExternalAccessPoint::IpCheckResult check =
      ExternalAccessPoint::validateManagementIp(ip.c_str(), liveIp.c_str(),
                                                liveGw.c_str(), liveMask.c_str());
  switch (check) {
    case ExternalAccessPoint::IpCheckResult::Ok:
      break;
    case ExternalAccessPoint::IpCheckResult::InvalidIp:
      return ExternalAccessPoint::CrudStatus::InvalidIp;
    case ExternalAccessPoint::IpCheckResult::EthernetNotReady:
      return ExternalAccessPoint::CrudStatus::EthernetNotReady;
    case ExternalAccessPoint::IpCheckResult::Reserved:
      return ExternalAccessPoint::CrudStatus::IpReserved;
    case ExternalAccessPoint::IpCheckResult::NotOnLan:
      return ExternalAccessPoint::CrudStatus::IpNotOnLan;
  }
  if (findIndexByIp(ip, exceptIndex) >= 0) {
    return ExternalAccessPoint::CrudStatus::DuplicateIp;
  }
  return ExternalAccessPoint::CrudStatus::Ok;
}

bool ExternalAccessPointManager::applyInput(
    Record &record, JsonObjectConst input, bool isCreate, String &passwordPlain,
    bool &passwordProvided, ExternalAccessPoint::CrudStatus &status) {
  passwordProvided = false;
  passwordPlain = "";

  if (isCreate || jsonHasKey(input, "name")) {
    const String name = jsonString(input, "name");
    status = validateName(name);
    if (status != ExternalAccessPoint::CrudStatus::Ok) return false;
    record.name = name;
  }
  if (isCreate && record.name.length() == 0) {
    status = ExternalAccessPoint::CrudStatus::InvalidRequest;
    return false;
  }

  if (isCreate || jsonHasKey(input, "managementIp")) {
    const String ip = jsonString(input, "managementIp");
    if (ip.length() == 0) {
      status = ExternalAccessPoint::CrudStatus::InvalidRequest;
      return false;
    }
    record.managementIp = ip;
  }

  if (isCreate || jsonHasKey(input, "enabled")) {
    if (jsonHasKey(input, "enabled")) record.enabled = input["enabled"].as<bool>();
    else if (isCreate) record.enabled = true;
  }
  if (isCreate || jsonHasKey(input, "vendor")) {
    record.vendor = ExternalAccessPoint::parseVendor(input["vendor"] | "generic");
  }
  if (isCreate || jsonHasKey(input, "model")) {
    record.model = jsonString(input, "model");
  }
  if (isCreate || jsonHasKey(input, "username")) {
    record.username = jsonString(input, "username");
  }
  if (isCreate || jsonHasKey(input, "ssid")) {
    record.ssid = jsonString(input, "ssid");
  }
  if (isCreate || jsonHasKey(input, "location")) {
    record.location = jsonString(input, "location");
  }
  if (isCreate || jsonHasKey(input, "notes")) {
    record.notes = jsonString(input, "notes");
  }

  if (jsonHasKey(input, "password")) {
    passwordPlain = jsonString(input, "password");
    passwordProvided = passwordPlain.length() > 0;
  }
  return true;
}

void ExternalAccessPointManager::logSafe(const char *action,
                                         const Record &record) const {
  Serial.printf(
      "[access-points] %s id=%s name=%s vendor=%s ip=%s enabled=%s\n",
      action, record.id.c_str(), record.name.c_str(),
      ExternalAccessPoint::vendorLabel(record.vendor),
      record.managementIp.c_str(), record.enabled ? "true" : "false");
  if (_logger) {
    _logger->infoLocal(
        "access-point",
        String(action) + " id=" + record.id + " name=" + record.name +
            " ip=" + record.managementIp);
  }
}

ExternalAccessPoint::CrudStatus ExternalAccessPointManager::create(
    JsonObjectConst input, JsonDocument &out) {
  if (_count >= kMaxAccessPoints) {
    return ExternalAccessPoint::CrudStatus::LimitReached;
  }
  Record record;
  String passwordPlain;
  bool passwordProvided = false;
  ExternalAccessPoint::CrudStatus status = ExternalAccessPoint::CrudStatus::Ok;
  if (!applyInput(record, input, true, passwordPlain, passwordProvided, status)) {
    return status;
  }
  status = validateIp(record.managementIp, -1);
  if (status != ExternalAccessPoint::CrudStatus::Ok) return status;

  if (passwordProvided) {
    if (!CredentialProtector::protectSecret(passwordPlain, record.passwordProtected) ||
        record.passwordProtected.isEmpty()) {
      return ExternalAccessPoint::CrudStatus::CredentialError;
    }
  }

  record.id = generateId();
  _records[_count] = record;
  ++_count;
  status = persist();
  if (status != ExternalAccessPoint::CrudStatus::Ok) {
    --_count;
    return status;
  }
  logSafe("created", record);
  out.clear();
  fillPublicRecord(out.to<JsonObject>(), record);
  return ExternalAccessPoint::CrudStatus::Ok;
}

ExternalAccessPoint::CrudStatus ExternalAccessPointManager::update(
    const String &id, JsonObjectConst input, JsonDocument &out) {
  const int index = findIndex(id);
  if (index < 0) return ExternalAccessPoint::CrudStatus::NotFound;

  Record next = _records[index];
  String passwordPlain;
  bool passwordProvided = false;
  ExternalAccessPoint::CrudStatus status = ExternalAccessPoint::CrudStatus::Ok;
  if (!applyInput(next, input, false, passwordPlain, passwordProvided, status)) {
    return status;
  }
  status = validateIp(next.managementIp, index);
  if (status != ExternalAccessPoint::CrudStatus::Ok) return status;

  if (passwordProvided) {
    if (!CredentialProtector::protectSecret(passwordPlain, next.passwordProtected) ||
        next.passwordProtected.isEmpty()) {
      return ExternalAccessPoint::CrudStatus::CredentialError;
    }
  }

  const Record previous = _records[index];
  _records[index] = next;
  status = persist();
  if (status != ExternalAccessPoint::CrudStatus::Ok) {
    _records[index] = previous;
    return status;
  }
  logSafe("updated", next);
  out.clear();
  fillPublicRecord(out.to<JsonObject>(), next);
  return ExternalAccessPoint::CrudStatus::Ok;
}

ExternalAccessPoint::CrudStatus ExternalAccessPointManager::remove(
    const String &id) {
  const int index = findIndex(id);
  if (index < 0) return ExternalAccessPoint::CrudStatus::NotFound;
  const Record removed = _records[index];
  for (uint8_t i = static_cast<uint8_t>(index); i + 1 < _count; ++i) {
    _records[i] = _records[i + 1];
  }
  --_count;
  const ExternalAccessPoint::CrudStatus status = persist();
  if (status != ExternalAccessPoint::CrudStatus::Ok) {
    for (uint8_t i = _count; i > static_cast<uint8_t>(index); --i) {
      _records[i] = _records[i - 1];
    }
    _records[index] = removed;
    ++_count;
    return status;
  }
  logSafe("deleted", removed);
  return ExternalAccessPoint::CrudStatus::Ok;
}
