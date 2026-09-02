#include "ExternalAccessPointManager.h"

#include <string.h>
#include <esp_system.h>

#include "CredentialProtector.h"
#include "DmaMemoryMonitor.h"
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

class ExternalAccessPointManager::ScopedRegistryLock {
 public:
  explicit ScopedRegistryLock(const ExternalAccessPointManager *manager)
      : _manager(manager) {
    _manager->lockRegistry();
  }
  ~ScopedRegistryLock() { _manager->unlockRegistry(); }

 private:
  const ExternalAccessPointManager *_manager;
};

class ExternalAccessPointManager::ScopedJobLock {
 public:
  explicit ScopedJobLock(const ExternalAccessPointManager *manager)
      : _manager(manager) {
    _manager->lockJob();
  }
  ~ScopedJobLock() { _manager->unlockJob(); }

 private:
  const ExternalAccessPointManager *_manager;
};

void ExternalAccessPointManager::lockRegistry() const {
  if (_registryMutex) xSemaphoreTake(_registryMutex, portMAX_DELAY);
}

void ExternalAccessPointManager::unlockRegistry() const {
  if (_registryMutex) xSemaphoreGive(_registryMutex);
}

void ExternalAccessPointManager::lockJob() const {
  if (_jobMutex) xSemaphoreTake(_jobMutex, portMAX_DELAY);
}

void ExternalAccessPointManager::unlockJob() const {
  if (_jobMutex) xSemaphoreGive(_jobMutex);
}

void ExternalAccessPointManager::copyId(char *dest, size_t destSize,
                                        const String &id) {
  if (dest == nullptr || destSize == 0) return;
  dest[0] = '\0';
  if (id.length() == 0) return;
  strncpy(dest, id.c_str(), destSize - 1);
  dest[destSize - 1] = '\0';
}

void ExternalAccessPointManager::begin(StorageManager *storage,
                                       EthernetManager *eth, Logger *logger) {
  _storage = storage;
  _eth = eth;
  _logger = logger;
  _count = 0;
  _registryError = "";
  if (!_registryMutex) _registryMutex = xSemaphoreCreateMutex();
  if (!_jobMutex) _jobMutex = xSemaphoreCreateMutex();
  loadFromStorage();
  Serial.printf("[access-points] loaded count=%u registry_error=%s\n",
                static_cast<unsigned>(_count),
                _registryError.length() > 0 ? _registryError.c_str() : "none");

  if (_workerTask) return;
  const BaseType_t ok = xTaskCreatePinnedToCore(
      workerTaskEntry, "ap_check_worker", kWorkerStackWords, this,
      kWorkerPriority, &_workerTask, kWorkerCore);
  if (ok != pdPASS) {
    _workerTask = nullptr;
    Serial.println("[ap-check] worker create failed");
    return;
  }
  Serial.printf("[ap-check] worker started stackWords=%u core=%d\n",
                static_cast<unsigned>(kWorkerStackWords),
                static_cast<int>(kWorkerCore));
}

void ExternalAccessPointManager::loop() {
  if (checkBusy()) notifyWorker();
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
  obj["status"] = ExternalAccessPoint::reachabilityLabel(record.status);
  if (record.latencyValid) {
    obj["latencyMs"] = record.latencyMs;
  } else {
    obj["latencyMs"] = nullptr;
  }
  if (record.lastCheckMs > 0) {
    obj["lastCheck"] = record.lastCheckMs;
  } else {
    obj["lastCheck"] = nullptr;
  }
  if (record.lastSuccessfulCheckMs > 0) {
    obj["lastSuccessfulCheck"] = record.lastSuccessfulCheckMs;
  } else {
    obj["lastSuccessfulCheck"] = nullptr;
  }
  if (record.lastError != nullptr) {
    obj["lastError"] = record.lastError;
  }
  JsonObject caps = obj["capabilities"].to<JsonObject>();
  caps["icmp"] = record.capIcmp;
  caps["http"] = record.capHttp;
  caps["https"] = record.capHttps;
}

void ExternalAccessPointManager::fillList(JsonDocument &doc) const {
  ScopedRegistryLock guard(this);
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
  ScopedRegistryLock guard(this);
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
  if (check != ExternalAccessPoint::IpCheckResult::Ok) {
    Serial.printf(
        "[access-points] ip rejected candidate=%s esp32=%s gw=%s mask=%s code=%d\n",
        ip.c_str(), liveIp.c_str(), liveGw.c_str(), liveMask.c_str(),
        static_cast<int>(check));
  }
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
    else if (isCreate) record.enabled = false;
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
  ScopedRegistryLock guard(this);
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
  ScopedRegistryLock guard(this);
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

  if (next.managementIp != _records[index].managementIp) {
    next.status = ExternalAccessPoint::ReachabilityStatus::Unknown;
    next.latencyValid = false;
    next.latencyMs = 0;
    next.lastCheckMs = 0;
    next.lastSuccessfulCheckMs = 0;
    next.lastError = nullptr;
    next.capIcmp = false;
    next.capHttp = false;
    next.capHttps = false;
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
  ScopedRegistryLock guard(this);
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

void ExternalAccessPointManager::notifyWorker() {
  if (_workerTask) xTaskNotifyGive(_workerTask);
}

void ExternalAccessPointManager::workerTaskEntry(void *param) {
  auto *self = static_cast<ExternalAccessPointManager *>(param);
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    self->runQueuedJob();
  }
}

ExternalAccessPoint::CheckEnqueueStatus ExternalAccessPointManager::enqueueCheck(
    const String &id, uint32_t &jobIdOut) {
  return enqueueProbeJob(id, false, jobIdOut);
}

ExternalAccessPoint::CheckEnqueueStatus ExternalAccessPointManager::enqueueSync(
    const String &id, uint32_t &jobIdOut) {
  return enqueueProbeJob(id, true, jobIdOut);
}

ExternalAccessPoint::CheckEnqueueStatus ExternalAccessPointManager::enqueueProbeJob(
    const String &id, bool activateOnSuccess, uint32_t &jobIdOut) {
  jobIdOut = 0;
  if (_storage && _storage->sdRecoveryInProgress()) {
    return ExternalAccessPoint::CheckEnqueueStatus::StorageRecovery;
  }
  if (!_workerTask) {
    return ExternalAccessPoint::CheckEnqueueStatus::WorkerUnavailable;
  }

  char ip[16] = {};
  {
    ScopedRegistryLock guard(this);
    const int index = findIndex(id);
    if (index < 0) return ExternalAccessPoint::CheckEnqueueStatus::NotFound;
    copyId(ip, sizeof(ip), _records[index].managementIp);
  }

  uint32_t jobId = 0;
  {
    ScopedJobLock guard(this);
    if (_job.state == ExternalAccessPoint::CheckJobState::Queued ||
        _job.state == ExternalAccessPoint::CheckJobState::Running) {
      return ExternalAccessPoint::CheckEnqueueStatus::Busy;
    }
    _job = CheckJob{};
    _job.jobId = _nextJobId++;
    if (_nextJobId == 0) _nextJobId = 1;
    copyId(_job.accessPointId, sizeof(_job.accessPointId), id);
    _job.activateOnSuccess = activateOnSuccess;
    _job.state = ExternalAccessPoint::CheckJobState::Queued;
    _job.startedAt = millis();
    jobId = _job.jobId;
  }

  jobIdOut = jobId;
  DmaMemoryMonitor::logSnapshot("ap-check-enqueue");
  Serial.printf("[ap-check] queued id=%u ap=%s ip=%s sync=%s\n",
                static_cast<unsigned>(jobId), id.c_str(), ip,
                activateOnSuccess ? "yes" : "no");
  notifyWorker();
  return ExternalAccessPoint::CheckEnqueueStatus::Ok;
}

bool ExternalAccessPointManager::pollCheckJob(uint32_t jobId,
                                              CheckJobSnapshot &out) const {
  ScopedJobLock guard(this);
  if (jobId == 0 || jobId != _job.jobId) return false;
  out.jobId = _job.jobId;
  copyId(out.accessPointId, sizeof(out.accessPointId),
         String(_job.accessPointId));
  out.state = ExternalAccessPoint::jobStateLabel(_job.state);
  out.ok = _job.ok;
  out.status = ExternalAccessPoint::reachabilityLabel(_job.status);
  out.latencyValid = _job.latencyValid;
  out.latencyMs = _job.latencyMs;
  out.startedAt = _job.startedAt;
  out.completedAt = _job.completedAt;
  out.errorCode = _job.errorCode;
  strncpy(out.message, _job.message, sizeof(out.message) - 1);
  out.message[sizeof(out.message) - 1] = '\0';
  return true;
}

bool ExternalAccessPointManager::checkBusy() const {
  ScopedJobLock guard(this);
  return _job.state == ExternalAccessPoint::CheckJobState::Queued ||
         _job.state == ExternalAccessPoint::CheckJobState::Running;
}

void ExternalAccessPointManager::applyRamStatus(
    Record &record, ExternalAccessPoint::ReachabilityStatus status,
    bool latencyValid, uint32_t latencyMs, const char *errorCode,
    ExternalAccessPoint::ManagementTransport transport) {
  record.status = status;
  record.latencyValid = latencyValid;
  record.latencyMs = latencyValid ? latencyMs : 0;
  record.lastCheckMs = millis();
  record.lastError = errorCode;
  record.capIcmp = status == ExternalAccessPoint::ReachabilityStatus::Online ||
                   status == ExternalAccessPoint::ReachabilityStatus::NetworkReachable;
  record.capHttp = transport == ExternalAccessPoint::ManagementTransport::Http;
  record.capHttps = transport == ExternalAccessPoint::ManagementTransport::Https;
  if (ExternalAccessPoint::reachabilityIsSuccessful(status) ||
      status == ExternalAccessPoint::ReachabilityStatus::Disabled) {
    if (ExternalAccessPoint::reachabilityIsSuccessful(status)) {
      record.lastSuccessfulCheckMs = record.lastCheckMs;
    }
  }
}

void ExternalAccessPointManager::applyMikroTikCheckResult(const String &id,
                                                          bool online,
                                                          const char *method,
                                                          uint32_t latencyMs) {
  ScopedRegistryLock guard(this);
  const int index = findIndex(id);
  if (index < 0) return;
  const ExternalAccessPoint::ReachabilityStatus status =
      online ? ExternalAccessPoint::ReachabilityStatus::Online
             : ExternalAccessPoint::ReachabilityStatus::Unreachable;
  const char *errorCode = online ? nullptr : "ACCESS_POINT_OFFLINE";
  applyRamStatus(_records[index], status, latencyMs > 0, latencyMs, errorCode,
                 ExternalAccessPoint::ManagementTransport::None);
  // MikroTik ICMP confirmation — treat as network-reachable capability.
  if (online && method &&
      (strcmp(method, "arp") == 0 || strcmp(method, "ping") == 0)) {
    _records[index].capIcmp = true;
  }
  Serial.printf("[access-points] mikrotik-check id=%s online=%s method=%s\n",
                id.c_str(), online ? "true" : "false",
                method ? method : "?");
}

void ExternalAccessPointManager::finishJob(
    uint32_t jobId, ExternalAccessPoint::CheckJobState state, bool ok,
    ExternalAccessPoint::ReachabilityStatus status, bool latencyValid,
    uint32_t latencyMs, const char *errorCode, const char *message) {
  ScopedJobLock guard(this);
  if (jobId != _job.jobId) return;
  _job.ok = ok;
  _job.status = status;
  _job.latencyValid = latencyValid;
  _job.latencyMs = latencyValid ? latencyMs : 0;
  _job.completedAt = millis();
  _job.errorCode = errorCode;
  _job.message[0] = '\0';
  if (message) {
    strncpy(_job.message, message, sizeof(_job.message) - 1);
    _job.message[sizeof(_job.message) - 1] = '\0';
  }
  _job.state = state;
}

void ExternalAccessPointManager::runQueuedJob() {
  uint32_t jobId = 0;
  uint32_t startedMs = millis();
  char accessPointId[20] = {};
  bool activateOnSuccess = false;
  {
    ScopedJobLock guard(this);
    if (_job.state != ExternalAccessPoint::CheckJobState::Queued) return;
    _job.state = ExternalAccessPoint::CheckJobState::Running;
    _job.startedAt = millis();
    startedMs = _job.startedAt;
    jobId = _job.jobId;
    activateOnSuccess = _job.activateOnSuccess;
    copyId(accessPointId, sizeof(accessPointId), String(_job.accessPointId));
  }

  DmaMemoryMonitor::logSnapshot("ap-check-start");
  Serial.printf("[ap-check] started id=%u ap=%s\n",
                static_cast<unsigned>(jobId), accessPointId);

  bool enabled = false;
  bool found = false;
  char ip[16] = {};
  {
    ScopedRegistryLock guard(this);
    const int index = findIndex(String(accessPointId));
    if (index >= 0) {
      found = true;
      enabled = _records[index].enabled;
      copyId(ip, sizeof(ip), _records[index].managementIp);
    }
  }

  if (!found) {
    finishJob(jobId, ExternalAccessPoint::CheckJobState::Failed, false,
              ExternalAccessPoint::ReachabilityStatus::Unknown, false, 0,
              "ACCESS_POINT_NOT_FOUND", "Access point not found");
    Serial.printf("[ap-check] completed id=%u status=unknown error=ACCESS_POINT_NOT_FOUND elapsed=%lu\n",
                  static_cast<unsigned>(jobId),
                  static_cast<unsigned long>(millis() - startedMs));
    return;
  }

  const bool ethernetReady = _eth && _eth->hasIp();
  if (!enabled && !activateOnSuccess) {
    {
      ScopedRegistryLock guard(this);
      const int index = findIndex(String(accessPointId));
      if (index >= 0) {
        applyRamStatus(_records[index],
                       ExternalAccessPoint::ReachabilityStatus::Disabled, false,
                       0, "ACCESS_POINT_DISABLED",
                       ExternalAccessPoint::ManagementTransport::None);
      }
    }
    finishJob(jobId, ExternalAccessPoint::CheckJobState::Completed, true,
              ExternalAccessPoint::ReachabilityStatus::Disabled, false, 0,
              "ACCESS_POINT_DISABLED", "Access point is disabled");
    Serial.printf("[ap-check] completed id=%u status=disabled elapsed=%lu\n",
                  static_cast<unsigned>(jobId),
                  static_cast<unsigned long>(millis() - startedMs));
    return;
  }

  if (!ethernetReady) {
    {
      ScopedRegistryLock guard(this);
      const int index = findIndex(String(accessPointId));
      if (index >= 0) {
        applyRamStatus(_records[index],
                       ExternalAccessPoint::ReachabilityStatus::Unknown, false,
                       0, "ETHERNET_NOT_READY",
                       ExternalAccessPoint::ManagementTransport::None);
      }
    }
    finishJob(jobId, ExternalAccessPoint::CheckJobState::Completed, false,
              ExternalAccessPoint::ReachabilityStatus::Unknown, false, 0,
              "ETHERNET_NOT_READY", "Ethernet does not have a usable IP");
    Serial.printf("[ap-check] completed id=%u status=unknown error=ETHERNET_NOT_READY elapsed=%lu\n",
                  static_cast<unsigned>(jobId),
                  static_cast<unsigned long>(millis() - startedMs));
    return;
  }

  DmaMemoryMonitor::logSnapshot("ap-check-before-icmp");
  ExternalAccessPoint::ProbeTarget target;
  target.managementIp = ip;
  const ExternalAccessPoint::ProbeResult probe = _driver.probe(target);
  vTaskDelay(pdMS_TO_TICKS(1));

  const ExternalAccessPoint::ReachabilityStatus status =
      ExternalAccessPoint::classifyReachability(false, true, probe.icmpOk,
                                                probe.tcpOk);
  bool latencyValid = false;
  uint32_t latencyMs = 0;
  if (probe.icmpOk && probe.icmpLatencyValid) {
    latencyValid = true;
    latencyMs = probe.icmpLatencyMs;
  } else if (probe.tcpOk && probe.tcpLatencyValid) {
    latencyValid = true;
    latencyMs = probe.tcpLatencyMs;
  }

  {
    ScopedRegistryLock guard(this);
    const int index = findIndex(String(accessPointId));
    if (index >= 0) {
      applyRamStatus(_records[index], status, latencyValid, latencyMs,
                     probe.errorCode, probe.transport);
      if (activateOnSuccess &&
          ExternalAccessPoint::reachabilityIsSuccessful(status)) {
        _records[index].enabled = true;
      }
    }
  }

  const bool ok = ExternalAccessPoint::reachabilityIsSuccessful(status);
  if (activateOnSuccess && ok) {
    Record syncedRecord;
    ExternalAccessPoint::CrudStatus persistStatus =
        ExternalAccessPoint::CrudStatus::StorageError;
    {
      ScopedRegistryLock guard(this);
      const int index = findIndex(String(accessPointId));
      if (index >= 0) {
        syncedRecord = _records[index];
        persistStatus = persist();
      }
    }
    if (persistStatus != ExternalAccessPoint::CrudStatus::Ok) {
      finishJob(jobId, ExternalAccessPoint::CheckJobState::Failed, false, status,
                latencyValid, latencyMs, "PERSIST_FAILED",
                "Verified but could not save enabled state");
      Serial.printf(
          "[ap-check] completed id=%u status=%s error=PERSIST_FAILED elapsed=%lu\n",
          static_cast<unsigned>(jobId),
          ExternalAccessPoint::reachabilityLabel(status),
          static_cast<unsigned long>(millis() - startedMs));
      return;
    }
    logSafe("synced", syncedRecord);
  }

  const char *message = activateOnSuccess ? "Sync complete" : "Check complete";
  if (activateOnSuccess && ok) {
    message = "Access point synced and enabled";
  } else if (activateOnSuccess && !ok) {
    message = "Sync failed — management IP not reachable";
  } else if (status == ExternalAccessPoint::ReachabilityStatus::Unreachable) {
    message = "Access point is unreachable";
  } else if (status == ExternalAccessPoint::ReachabilityStatus::NetworkReachable) {
    message = "Host replies to ping; management port is closed";
  } else if (status == ExternalAccessPoint::ReachabilityStatus::ManagementReachable) {
    message = "Management port is open; ICMP is blocked or failed";
  } else if (status == ExternalAccessPoint::ReachabilityStatus::Online) {
    message = "Access point is reachable";
  }
  finishJob(jobId, ExternalAccessPoint::CheckJobState::Completed, ok, status,
            latencyValid, latencyMs, probe.errorCode, message);

  const UBaseType_t stackLeft = uxTaskGetStackHighWaterMark(nullptr);
  DmaMemoryMonitor::logSnapshot("ap-check-complete");
  Serial.printf(
      "[ap-check] completed id=%u status=%s elapsed=%lu stackHighWater=%u\n",
      static_cast<unsigned>(jobId),
      ExternalAccessPoint::reachabilityLabel(status),
      static_cast<unsigned long>(millis() - startedMs),
      static_cast<unsigned>(stackLeft));
}
