#include "VoucherManager.h"

#include <esp_heap_caps.h>

#include "Config.h"
#include "DmaMemoryMonitor.h"
#include "SalesTime.h"

namespace {
constexpr uint8_t kSchemaVersion = 2;
constexpr uint8_t kGenerationAttempts = 64;

void logVoucherHeap(const char *label) {
  const size_t heapFree = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  const size_t heapLargest =
      heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  const size_t heapMin = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
  const size_t dmaFree = heap_caps_get_free_size(MALLOC_CAP_DMA);
  const size_t dmaLargest =
      heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
  const size_t dmaMin = heap_caps_get_minimum_free_size(MALLOC_CAP_DMA);
  Serial.printf(
      "[voucher-job] mem %s heap=%u largest=%u min=%u dma=%u "
      "dmaLargest=%u dmaMin=%u\n",
      label, static_cast<unsigned>(heapFree),
      static_cast<unsigned>(heapLargest), static_cast<unsigned>(heapMin),
      static_cast<unsigned>(dmaFree), static_cast<unsigned>(dmaLargest),
      static_cast<unsigned>(dmaMin));
  DmaMemoryMonitor::logSnapshot(label);
}
}  // namespace

class VoucherManager::ScopedLock {
 public:
  explicit ScopedLock(const VoucherManager *manager) : _manager(manager) {
    _manager->lock();
  }
  ~ScopedLock() { _manager->unlock(); }

 private:
  const VoucherManager *_manager;
};

class VoucherManager::ScopedJobLock {
 public:
  explicit ScopedJobLock(const VoucherManager *manager) : _manager(manager) {
    _manager->lockJob();
  }
  ~ScopedJobLock() { _manager->unlockJob(); }

 private:
  const VoucherManager *_manager;
};

void VoucherManager::begin(StorageManager *storage, Logger *logger,
                           EventBus *events) {
  _storage = storage;
  _logger = logger;
  _events = events;
  if (!_mutex) _mutex = xSemaphoreCreateMutex();
  if (!_jobMutex) _jobMutex = xSemaphoreCreateMutex();
  if (_workerTask) return;

  const BaseType_t coreId =
      RenzFiConfig::VOUCHER_WORKER_CORE_AFFINITY < 0
          ? tskNO_AFFINITY
          : RenzFiConfig::VOUCHER_WORKER_CORE_AFFINITY;
  const BaseType_t ok = xTaskCreatePinnedToCore(
      workerTaskEntry, "voucher_worker",
      RenzFiConfig::RENZFI_VOUCHER_WORKER_STACK_WORDS, this, 1, &_workerTask,
      coreId);
  if (ok != pdPASS) {
    _workerTask = nullptr;
    Serial.println("[voucher-job] worker create failed");
    return;
  }
  Serial.printf(
      "[voucher-job] worker started stackWords=%u coreAffinity=%d\n",
      static_cast<unsigned>(RenzFiConfig::RENZFI_VOUCHER_WORKER_STACK_WORDS),
      static_cast<int>(RenzFiConfig::VOUCHER_WORKER_CORE_AFFINITY));
}

bool VoucherManager::list(JsonDocument &doc) {
  ScopedLock guard(this);
  DynamicJsonDocument stored(RenzFiConfig::JSON_DOC_LARGE);
  if (!loadLocked(stored)) return false;

  doc.clear();
  JsonArray output = doc.to<JsonArray>();
  for (JsonObjectConst item : stored.as<JsonArrayConst>()) {
    copyNormalized(item, output.createNestedObject());
  }
  return true;
}

bool VoucherManager::find(const String &code, JsonDocument &doc) {
  const String wanted = normalizeCode(code);
  if (wanted.isEmpty()) return false;

  ScopedLock guard(this);
  DynamicJsonDocument stored(RenzFiConfig::JSON_DOC_LARGE);
  if (!loadLocked(stored)) return false;
  for (JsonObjectConst item : stored.as<JsonArrayConst>()) {
    if (wanted == normalizeCode(item["code"] | "")) {
      doc.clear();
      copyNormalized(item, doc.to<JsonObject>());
      return true;
    }
  }
  return false;
}

bool VoucherManager::generate(int count, int amount, int minutes,
                              const String &expires,
                              JsonDocument &response) {
  DynamicJsonDocument config(256);
  config["count"] = count;
  config["amount"] = amount;
  config["minutes"] = minutes;
  config["expires"] = expires;
  return generate(config.as<JsonObjectConst>(), response);
}

bool VoucherManager::generate(JsonObjectConst config,
                              JsonDocument &response) {
  // Do not invent defaults for missing fields — Admin must send explicit values.
  if (config["count"].isNull() || config["amount"].isNull() ||
      config["minutes"].isNull()) {
    Serial.println(
        "[voucher-job] generate reject reason=validation missing_fields");
    return false;
  }
  const int count = config["count"] | 0;
  const int amount = config["amount"] | -1;
  const int minutes = config["minutes"] | 0;
  const String expires = config["expires"] | "";
  const String requestedCode = normalizeCode(config["code"] | "");
  const String profileName = config["profileName"] | "";
  const String speed = config["speed"] | "";
  if (count < 1 || count > 20 || amount < 0 || minutes <= 0 ||
      minutes > 525600) {
    Serial.printf(
        "[voucher-job] generate reject reason=validation "
        "count=%d amount=%d minutes=%d\n",
        count, amount, minutes);
    return false;
  }
  if (!requestedCode.isEmpty() && count != 1) {
    Serial.println(
        "[voucher-job] generate reject reason=custom_code_requires_count_1");
    return false;
  }

  const uint32_t tAll = millis();
  DynamicJsonDocument historyItems(RenzFiConfig::JSON_DOC_MEDIUM);
  JsonArray historyArr = historyItems.to<JsonArray>();
  {
    ScopedLock guard(this);

    Serial.println("[voucher-job] load start");
    const uint32_t tLoad = millis();
    DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_LARGE);
    if (!loadLocked(doc)) {
      Serial.printf(
          "[voucher-job] storage fail stage=load elapsed=%lu reason=read\n",
          static_cast<unsigned long>(millis() - tLoad));
      return false;
    }
    Serial.printf("[voucher-job] load complete elapsed=%lu\n",
                  static_cast<unsigned long>(millis() - tLoad));

    JsonArray vouchers = doc.as<JsonArray>();
    if (vouchers.isNull()) return false;

    DynamicJsonDocument generated(RenzFiConfig::JSON_DOC_MEDIUM);
    JsonArray created = generated.to<JsonArray>();
    for (int i = 0; i < count; i++) {
      String code = requestedCode;
      if (!code.isEmpty()) {
        if (codeExists(doc.as<JsonArrayConst>(), code)) return false;
      } else {
        for (uint8_t attempt = 0; attempt < kGenerationAttempts; ++attempt) {
          const String candidate = makeCode();
          if (!codeExists(doc.as<JsonArrayConst>(), candidate)) {
            code = candidate;
            break;
          }
        }
      }
      if (code.isEmpty()) return false;

      JsonObject item = vouchers.createNestedObject();
      item["schemaVersion"] = kSchemaVersion;
      item["code"] = code;
      item["amount"] = amount;
      item["minutes"] = minutes;
      item["status"] = "unused";
      item["expires"] = expires.isEmpty() ? "never" : expires;
      item["validUntil"] = expires.isEmpty() ? "never" : expires;
      item["boundMac"] = "";
      item["sessionId"] = "";
      item["redeemedAt"] = "";
      item["activatedAt"] = "";
      item["serviceExpiresAt"] = "";
      item["profileName"] = profileName;
      item["speed"] = speed;
      item["terminalReason"] = "";
      item["updatedAt"] = "";
      item["archivedAt"] = "";
      created.add(code);
    }

    Serial.printf("[voucher-job] mutate complete count=%d\n", count);
    const uint32_t tSave = millis();
    if (!saveLocked(doc)) {
      Serial.printf(
          "[voucher-job] storage fail stage=persist elapsed=%lu reason=writeJson\n",
          static_cast<unsigned long>(millis() - tSave));
      return false;
    }
    Serial.printf("[voucher-job] persist complete elapsed=%lu\n",
                  static_cast<unsigned long>(millis() - tSave));

    // Snapshot created records for history; persist already committed on SD.
    for (JsonObjectConst item : doc.as<JsonArrayConst>()) {
      const String code = normalizeCode(item["code"] | "");
      for (JsonVariantConst made : created) {
        if (code == String(made.as<const char *>())) {
          copyNormalized(item, historyArr.createNestedObject());
          break;
        }
      }
    }
    response.clear();
    response["created"].set(created);
  }

  const uint32_t tHist = millis();
  Serial.println("[voucher-job] history start");
  Serial.printf("[voucher-job] history items=%u\n",
                static_cast<unsigned>(historyArr.size()));
  if (!appendHistoryBatch(historyArr, "created", "")) {
    Serial.printf(
        "[voucher-job] storage fail stage=history elapsed=%lu reason=batch\n",
        static_cast<unsigned long>(millis() - tHist));
    return false;
  }
  Serial.printf("[voucher-job] history complete elapsed=%lu\n",
                static_cast<unsigned long>(millis() - tHist));

  if (_logger) _logger->infoLocal("vouchers", "Vouchers generated");
  Serial.printf("[voucher-job] generate complete elapsed=%lu\n",
                static_cast<unsigned long>(millis() - tAll));
  return true;
}

uint32_t VoucherManager::enqueueGenerate(JsonObjectConst config,
                                         bool &alreadyRunning) {
  alreadyRunning = false;
  uint32_t jobId = 0;
  {
    ScopedJobLock guard(this);
    if (_genState == GenState::Queued || _genState == GenState::Running) {
      alreadyRunning = true;
      Serial.printf("[voucher-job] duplicate request existingJob=%u\n",
                    static_cast<unsigned>(_genJobId));
      return _genJobId;
    }

    _genRequestJson = "";
    serializeJson(config, _genRequestJson);
    if (_genRequestJson.isEmpty()) {
      _genRequestJson = "{}";
    }
    _genResultJson = "";
    _genError = "";
    _genResultCount = 0;
    _genJobId = _genNextId++;
    if (_genNextId == 0) _genNextId = 1;
    _jobKind = JobKind::Generate;
    _genState = GenState::Queued;
    _genStartedMs = millis();
    jobId = _genJobId;
  }
  Serial.printf("[voucher-job] accepted job=%u\n",
                static_cast<unsigned>(jobId));
  notifyWorker();
  return jobId;
}

uint32_t VoucherManager::enqueueBulkDelete(JsonArrayConst codes,
                                           bool &alreadyRunning) {
  alreadyRunning = false;
  if (codes.isNull() || codes.size() == 0 || codes.size() > 20) return 0;
  uint32_t jobId = 0;
  {
    ScopedJobLock guard(this);
    if (_genState == GenState::Queued || _genState == GenState::Running) {
      alreadyRunning = true;
      Serial.printf("[voucher-job] duplicate delete existingJob=%u\n",
                    static_cast<unsigned>(_genJobId));
      return _genJobId;
    }
    _genRequestJson = "";
    DynamicJsonDocument wrap(RenzFiConfig::JSON_DOC_MEDIUM);
    wrap["codes"] = codes;
    serializeJson(wrap, _genRequestJson);
    if (_genRequestJson.isEmpty()) return 0;
    _genResultJson = "";
    _genError = "";
    _genResultCount = 0;
    _genJobId = _genNextId++;
    if (_genNextId == 0) _genNextId = 1;
    _jobKind = JobKind::BulkDelete;
    _genState = GenState::Queued;
    _genStartedMs = millis();
    jobId = _genJobId;
  }
  Serial.printf("[voucher-job] accepted delete job=%u count=%u\n",
                static_cast<unsigned>(jobId),
                static_cast<unsigned>(codes.size()));
  notifyWorker();
  return jobId;
}

bool VoucherManager::pollGenerateJob(uint32_t jobId,
                                     GenerateJobSnapshot &out) const {
  ScopedJobLock guard(this);
  if (jobId == 0 || jobId != _genJobId) return false;
  out.jobId = _genJobId;
  out.error = _genError;
  out.resultJson = _genResultJson;
  out.type = _jobKind == JobKind::BulkDelete ? "voucher-bulk-delete"
                                             : "voucher-generate";
  out.ok = false;
  out.count = _genResultCount;
  switch (_genState) {
    case GenState::Queued:
      out.state = "queued";
      break;
    case GenState::Running:
      out.state = "running";
      break;
    case GenState::Completed:
      out.state = "completed";
      out.ok = true;
      break;
    case GenState::Failed:
      out.state = "failed";
      out.ok = false;
      break;
    default:
      out.state = "idle";
      break;
  }
  return true;
}

bool VoucherManager::generateBusy() const {
  ScopedJobLock guard(this);
  return _genState == GenState::Queued || _genState == GenState::Running;
}

void VoucherManager::notifyWorker() {
  if (_workerTask) xTaskNotifyGive(_workerTask);
}

void VoucherManager::loop() {
  // Safety wake if a job was queued before the worker was ready.
  if (generateBusy()) notifyWorker();
}

void VoucherManager::workerTaskEntry(void *param) {
  auto *self = static_cast<VoucherManager *>(param);
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    self->runQueuedJob();
  }
}

void VoucherManager::runQueuedJob() {
  String requestJson;
  uint32_t jobId = 0;
  JobKind kind = JobKind::None;
  {
    ScopedJobLock guard(this);
    if (_genState != GenState::Queued) return;
    _genState = GenState::Running;
    jobId = _genJobId;
    kind = _jobKind;
    requestJson = _genRequestJson;
    _genStartedMs = millis();
  }

  Serial.printf("[voucher-job] started job=%u type=%s\n",
                static_cast<unsigned>(jobId),
                kind == JobKind::BulkDelete ? "bulk-delete" : "generate");
  logVoucherHeap("before");
  DynamicJsonDocument body(RenzFiConfig::JSON_DOC_MEDIUM);
  if (!requestJson.isEmpty()) deserializeJson(body, requestJson);

  DynamicJsonDocument response(RenzFiConfig::JSON_DOC_MEDIUM);
  Serial.println("[voucher-job] storage start");
  const uint32_t tStorage = millis();
  bool ok = false;
  String error;
  if (kind == JobKind::BulkDelete) {
    ok = runBulkDelete(body["codes"].as<JsonArrayConst>(), response);
    if (!ok) error = "Unable to delete vouchers";
  } else {
    ok = generate(body.as<JsonObjectConst>(), response);
    if (!ok) error = "Unable to generate vouchers";
  }

  String resultJson;
  uint32_t resultCount = 0;
  if (ok) {
    serializeJson(response, resultJson);
    if (kind == JobKind::BulkDelete) {
      const size_t deletedCount =
          response["deleted"].is<JsonArrayConst>()
              ? response["deleted"].as<JsonArrayConst>().size()
              : 0;
      resultCount = static_cast<uint32_t>(deletedCount);
      Serial.printf("[voucher-job] deleted count=%u\n",
                    static_cast<unsigned>(deletedCount));
    } else {
      const size_t createdCount =
          response["created"].is<JsonArrayConst>()
              ? response["created"].as<JsonArrayConst>().size()
              : 0;
      resultCount = static_cast<uint32_t>(createdCount);
      Serial.printf("[voucher-job] generated count=%u\n",
                    static_cast<unsigned>(createdCount));
    }
    Serial.printf("[voucher-job] storage complete elapsed=%lu\n",
                  static_cast<unsigned long>(millis() - tStorage));
  } else {
    Serial.printf(
        "[voucher-job] storage fail stage=%s elapsed=%lu reason=job\n",
        kind == JobKind::BulkDelete ? "bulk-delete" : "generate",
        static_cast<unsigned long>(millis() - tStorage));
  }
  logVoucherHeap("after");

  const uint32_t duration = millis() - tStorage;
  {
    ScopedJobLock guard(this);
    if (jobId != _genJobId) return;
    if (ok) {
      _genResultJson = resultJson;
      _genResultCount = resultCount;
      _genError = "";
      _genState = GenState::Completed;
    } else {
      _genResultJson = "";
      _genResultCount = 0;
      _genError = error;
      _genState = GenState::Failed;
    }
  }
  Serial.printf("[voucher-job] finished job=%u ok=%s\n",
                static_cast<unsigned>(jobId), ok ? "yes" : "no");
  Serial.printf("[voucher-job] terminal state=%s\n",
                ok ? "completed" : "failed");
  Serial.printf("[voucher-job] duration=%lu\n",
                static_cast<unsigned long>(duration));
}

VoucherManager::ReserveResult VoucherManager::reserve(
    const String &code, const String &mac, const String &sessionId,
    const String &redeemedAt) {
  ReserveResult result;
  result.code = normalizeCode(code);
  const String normalizedMac = normalizeMac(mac);
  const String cleanSessionId = sessionId;
  if (result.code.isEmpty() || normalizedMac.isEmpty() ||
      cleanSessionId.isEmpty()) {
    result.result = ReserveStatus::InvalidInput;
    return result;
  }

  ScopedLock guard(this);
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_LARGE);
  if (!loadLocked(doc)) {
    result.result = ReserveStatus::StorageError;
    return result;
  }

  for (JsonObject item : doc.as<JsonArray>()) {
    if (result.code != normalizeCode(item["code"] | "")) continue;

    normalizeRecord(item);
    const String status = item["status"] | "unused";
    const String boundMac = normalizeMac(item["boundMac"] | "");
    const String validUntil = item["validUntil"] | "never";
    fillReserveResult(item, result);

    if (status == "unused") {
      if (validUntil != "never") {
        if (redeemedAt.isEmpty()) {
          result.result = ReserveStatus::Unavailable;
          return result;
        }
        const bool dateOnly = validUntil.length() == 10;
        const String comparableNow =
            dateOnly ? redeemedAt.substring(0, 10) : redeemedAt;
        if (comparableNow > validUntil) {
          item["status"] = "expired";
          item["terminalReason"] = "redemption_deadline";
          item["updatedAt"] = redeemedAt;
          if (!saveLocked(doc)) {
            result.result = ReserveStatus::StorageError;
            return result;
          }
          appendHistory(item, "expired", redeemedAt);
          result.result = ReserveStatus::Unavailable;
          return result;
        }
      }
      // Immutable absolute service expiry: redeemedAt + minutes (validity).
      const int minutes = item["minutes"] | 0;
      String stampedExpiry;
      if (minutes > 0) {
        stampedExpiry = salesAddSecondsToIso(
            redeemedAt, static_cast<uint32_t>(minutes) * 60U);
        if (stampedExpiry.isEmpty()) {
          result.result = ReserveStatus::Unavailable;
          return result;
        }
      }
      item["status"] = "redeeming";
      item["boundMac"] = normalizedMac;
      item["sessionId"] = cleanSessionId;
      item["redeemedAt"] = redeemedAt;
      item["updatedAt"] = redeemedAt;
      if (!stampedExpiry.isEmpty()) {
        item["serviceExpiresAt"] = stampedExpiry;
      }
      if (!saveLocked(doc)) {
        result.result = ReserveStatus::StorageError;
        return result;
      }
      appendHistory(item, "reserved", redeemedAt);
      fillReserveResult(item, result);
      result.result = ReserveStatus::Reserved;
      Serial.printf(
          "[voucher-expiry] mac=%s code=%s redeemedAt=%s expiresAt=%s "
          "remaining= action=stamp\n",
          normalizedMac.c_str(), result.code.c_str(), redeemedAt.c_str(),
          stampedExpiry.c_str());
      return result;
    }

    if ((status == "redeeming" || status == "active") &&
        boundMac == normalizedMac) {
      // Legacy normalize: stamp absolute expiry if missing (do not invent
      // redeemedAt). Prefer redeemedAt; else activatedAt + minutes.
      String existingExpiry = item["serviceExpiresAt"] | "";
      if (existingExpiry.isEmpty()) {
        const int minutes = item["minutes"] | 0;
        String base = item["redeemedAt"] | "";
        if (base.isEmpty()) base = item["activatedAt"] | "";
        if (!base.isEmpty() && minutes > 0) {
          existingExpiry = salesAddSecondsToIso(
              base, static_cast<uint32_t>(minutes) * 60U);
          if (!existingExpiry.isEmpty()) {
            item["serviceExpiresAt"] = existingExpiry;
            item["updatedAt"] = redeemedAt.isEmpty() ? base : redeemedAt;
            if (!saveLocked(doc)) {
              result.result = ReserveStatus::StorageError;
              return result;
            }
          }
        }
      }
      fillReserveResult(item, result);
      result.result = ReserveStatus::Idempotent;
      return result;
    }
    result.result =
        (status == "redeeming" || status == "active")
            ? ReserveStatus::BoundToAnotherDevice
            : ReserveStatus::Unavailable;
    return result;
  }

  result.result = ReserveStatus::NotFound;
  return result;
}

bool VoucherManager::markActivated(const String &code, const String &mac,
                                   const String &sessionId,
                                   const String &activatedAt,
                                   const String &serviceExpiresAt) {
  const String wanted = normalizeCode(code);
  const String normalizedMac = normalizeMac(mac);
  if (wanted.isEmpty() || normalizedMac.isEmpty() || sessionId.isEmpty()) {
    return false;
  }

  ScopedLock guard(this);
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_LARGE);
  if (!loadLocked(doc)) return false;
  for (JsonObject item : doc.as<JsonArray>()) {
    if (wanted != normalizeCode(item["code"] | "")) continue;
    normalizeRecord(item);
    const String status = item["status"] | "";
    if (normalizeMac(item["boundMac"] | "") != normalizedMac) return false;
    if (String(item["sessionId"] | "") != sessionId) return false;
    if (status == "active") return true;
    if (status != "redeeming") return false;

    item["status"] = "active";
    item["sessionId"] = sessionId;
    item["activatedAt"] = activatedAt;
    // Never overwrite an absolute expiry stamped at redeem.
    const String existingExpiry = item["serviceExpiresAt"] | "";
    if (existingExpiry.isEmpty()) {
      if (!serviceExpiresAt.isEmpty()) {
        item["serviceExpiresAt"] = serviceExpiresAt;
      } else {
        const String redeemed = item["redeemedAt"] | "";
        const int minutes = item["minutes"] | 0;
        if (!redeemed.isEmpty() && minutes > 0) {
          const String derived = salesAddSecondsToIso(
              redeemed, static_cast<uint32_t>(minutes) * 60U);
          if (!derived.isEmpty()) item["serviceExpiresAt"] = derived;
        }
      }
    }
    item["updatedAt"] = activatedAt;
    const bool saved = saveLocked(doc);
    if (saved) appendHistory(item, "activated", activatedAt);
    return saved;
  }
  return false;
}

bool VoucherManager::expire(const String &code, const String &terminalReason,
                            const String &updatedAt) {
  return transitionTerminal(code, "expired", terminalReason, updatedAt);
}

bool VoucherManager::disable(const String &code, const String &terminalReason,
                             const String &updatedAt) {
  return transitionTerminal(code, "disabled", terminalReason, updatedAt);
}

bool VoucherManager::archive(const String &code, const String &terminalReason,
                             const String &updatedAt) {
  return transitionTerminal(code, "archived", terminalReason, updatedAt);
}

bool VoucherManager::ownerAction(const String &code, const String &action,
                                 const String &terminalReason,
                                 const String &updatedAt) {
  const String normalized = normalizeAction(action);
  if (normalized == "expire") return expire(code, terminalReason, updatedAt);
  if (normalized == "disable") return disable(code, terminalReason, updatedAt);
  if (normalized == "archive") return archive(code, terminalReason, updatedAt);
  return false;
}

bool VoucherManager::remove(const String &code) {
  const String wanted = normalizeCode(code);
  if (wanted.isEmpty()) return false;

  ScopedLock guard(this);
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_LARGE);
  if (!loadLocked(doc)) return false;
  JsonArray vouchers = doc.as<JsonArray>();
  if (vouchers.isNull()) return false;

  for (size_t i = 0; i < vouchers.size(); ++i) {
    JsonObject item = vouchers[i];
    if (wanted != normalizeCode(item["code"] | "")) continue;
    normalizeRecord(item);
    const String status = item["status"] | "";
    // Active/redeeming vouchers must be terminated first; delete is for
    // unused/expired/disabled/archived inventory cleanup.
    if (status == "active" || status == "redeeming") return false;
    appendHistory(item, "deleted", "");
    vouchers.remove(i);
    return saveLocked(doc);
  }
  return false;
}

bool VoucherManager::markActive(const String &code) {
  const String wanted = normalizeCode(code);
  if (wanted.isEmpty()) return false;

  ScopedLock guard(this);
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_LARGE);
  if (!loadLocked(doc)) return false;
  for (JsonObject item : doc.as<JsonArray>()) {
    if (wanted != normalizeCode(item["code"] | "")) continue;
    normalizeRecord(item);
    const String status = item["status"] | "";
    if (status == "active") return true;
    if (status == "expired" || status == "disabled" || status == "archived") {
      return false;
    }
    item["status"] = "active";
    const bool saved = saveLocked(doc);
    if (saved) appendHistory(item, "activated", "");
    return saved;
  }
  return false;
}

void VoucherManager::lock() const {
  if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
}

void VoucherManager::unlock() const {
  if (_mutex) xSemaphoreGive(_mutex);
}

void VoucherManager::lockJob() const {
  if (_jobMutex) xSemaphoreTake(_jobMutex, portMAX_DELAY);
}

void VoucherManager::unlockJob() const {
  if (_jobMutex) xSemaphoreGive(_jobMutex);
}

bool VoucherManager::loadLocked(JsonDocument &doc) const {
  return _storage &&
         _storage->readJson(RenzFiConfig::VOUCHERS_FILE, doc) &&
         doc.is<JsonArray>();
}

bool VoucherManager::saveLocked(const JsonDocument &doc) {
  if (!_storage ||
      !_storage->writeJson(RenzFiConfig::VOUCHERS_FILE, doc)) {
    return false;
  }
  if (_events) _events->emit("vouchers.changed");
  return true;
}

bool VoucherManager::transitionTerminal(const String &code, const char *status,
                                        const String &terminalReason,
                                        const String &updatedAt) {
  const String wanted = normalizeCode(code);
  if (wanted.isEmpty()) return false;

  ScopedLock guard(this);
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_LARGE);
  if (!loadLocked(doc)) return false;
  for (JsonObject item : doc.as<JsonArray>()) {
    if (wanted != normalizeCode(item["code"] | "")) continue;
    normalizeRecord(item);
    const String current = item["status"] | "";
    if (current == status) return true;
    if (current == "archived") return false;
    if (String(status) == "archived" &&
        current != "unused" && current != "expired" &&
        current != "disabled") {
      return false;
    }

    item["status"] = status;
    item["terminalReason"] = terminalReason;
    item["updatedAt"] = updatedAt;
    if (String(status) == "archived") item["archivedAt"] = updatedAt;
    const bool saved = saveLocked(doc);
    if (saved) appendHistory(item, status, updatedAt);
    return saved;
  }
  return false;
}

bool VoucherManager::codeExists(JsonArrayConst vouchers,
                                const String &code) const {
  for (JsonObjectConst item : vouchers) {
    if (code == normalizeCode(item["code"] | "")) return true;
  }
  return false;
}

void VoucherManager::normalizeRecord(JsonObject item) const {
  const String code = normalizeCode(item["code"] | "");
  const String mac = normalizeMac(item["boundMac"] | "");
  const String expires = item["expires"] | "never";
  item["schemaVersion"] = kSchemaVersion;
  item["code"] = code;
  item["amount"] = item["amount"] | 0;
  item["minutes"] = item["minutes"] | 0;
  item["status"] = item["status"] | "unused";
  item["expires"] = expires;
  if (!item["validUntil"].is<const char *>()) item["validUntil"] = expires;
  item["boundMac"] = mac;
  if (!item["sessionId"].is<const char *>()) item["sessionId"] = "";
  if (!item["redeemedAt"].is<const char *>()) item["redeemedAt"] = "";
  if (!item["activatedAt"].is<const char *>()) item["activatedAt"] = "";
  if (!item["serviceExpiresAt"].is<const char *>())
    item["serviceExpiresAt"] = "";
  if (!item["profileName"].is<const char *>()) item["profileName"] = "";
  if (!item["speed"].is<const char *>()) item["speed"] = "";
  if (!item["terminalReason"].is<const char *>()) item["terminalReason"] = "";
  if (!item["updatedAt"].is<const char *>()) item["updatedAt"] = "";
  if (!item["archivedAt"].is<const char *>()) item["archivedAt"] = "";
}

void VoucherManager::copyNormalized(JsonObjectConst source,
                                    JsonObject destination) const {
  destination.set(source);
  normalizeRecord(destination);
}

void VoucherManager::fillReserveResult(JsonObjectConst item,
                                       ReserveResult &result) const {
  result.code = normalizeCode(item["code"] | "");
  result.status = item["status"] | "";
  result.boundMac = normalizeMac(item["boundMac"] | "");
  result.sessionId = item["sessionId"] | "";
  result.amount = item["amount"] | 0;
  result.minutes = item["minutes"] | 0;
  result.validUntil = item["validUntil"] | "never";
  result.activatedAt = item["activatedAt"] | "";
  result.serviceExpiresAt = item["serviceExpiresAt"] | "";
  result.profileName = item["profileName"] | "";
  result.speed = item["speed"] | "";
}

void VoucherManager::appendHistory(JsonObjectConst item, const String &action,
                                   const String &eventAt) {
  if (!_storage) return;
  const String code = normalizeCode(item["code"] | "");
  if (code.isEmpty() || action.isEmpty()) return;
  DynamicJsonDocument event(measureJson(item) + 192);
  event["event"] = String("voucher_") + action;
  event["voucher"].set(item);
  const String recordedAt = eventAt.isEmpty() ? salesRecordedAtNow() : eventAt;
  _storage->appendHistory(
      NdjsonLedger::Kind::Vouchers,
      String("voucher:") + code + ":" + action, recordedAt,
      event.as<JsonObjectConst>());
}

bool VoucherManager::appendHistoryBatch(JsonArrayConst items,
                                        const String &action,
                                        const String &eventAt) {
  if (!_storage || action.isEmpty() || items.isNull() || items.size() == 0) {
    return items.size() == 0;
  }
  const size_t n = items.size();
  if (n > 20) return false;

  Serial.println("[voucher-job] history serialize start");
  const uint32_t tSer = millis();
  const String recordedAt = eventAt.isEmpty() ? salesRecordedAtNow() : eventAt;
  String eventIds[20];
  String lines[20];
  size_t prepared = 0;
  size_t totalBytes = 0;
  for (JsonObjectConst item : items) {
    if (prepared >= 20) break;
    const String code = normalizeCode(item["code"] | "");
    if (code.isEmpty()) continue;
    DynamicJsonDocument event(measureJson(item) + 256);
    event["event"] = String("voucher_") + action;
    event["voucher"].set(item);
    event["eventId"] = String("voucher:") + code + ":" + action;
    event["eventAt"] = recordedAt;
    String line;
    serializeJson(event, line);
    if (line.isEmpty()) return false;
    eventIds[prepared] = event["eventId"].as<String>();
    lines[prepared] = line;
    totalBytes += line.length();
    ++prepared;
  }
  Serial.printf(
      "[voucher-job] history serialize complete elapsed=%lu bytes=%u items=%u\n",
      static_cast<unsigned long>(millis() - tSer),
      static_cast<unsigned>(totalBytes), static_cast<unsigned>(prepared));
  if (prepared == 0) return true;

  Serial.println("[voucher-job] history lock wait start");
  const uint32_t tLock = millis();
  Serial.println("[voucher-job] history write start");
  const bool ok = _storage->appendHistoryPreparedLines(
      NdjsonLedger::Kind::Vouchers, recordedAt, eventIds, lines, prepared);
  Serial.printf(
      "[voucher-job] history write %s elapsed=%lu lockWaitHint=%lu\n",
      ok ? "complete" : "fail",
      static_cast<unsigned long>(millis() - tLock),
      static_cast<unsigned long>(millis() - tLock));
  return ok;
}

bool VoucherManager::runBulkDelete(JsonArrayConst codes,
                                   JsonDocument &response) {
  if (codes.isNull() || codes.size() == 0 || codes.size() > 20) return false;

  DynamicJsonDocument historyItems(RenzFiConfig::JSON_DOC_MEDIUM);
  JsonArray historyArr = historyItems.to<JsonArray>();
  DynamicJsonDocument outDoc(RenzFiConfig::JSON_DOC_MEDIUM);
  JsonArray deleted = outDoc.createNestedArray("deleted");
  JsonArray skipped = outDoc.createNestedArray("skipped");

  {
    ScopedLock guard(this);
    DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_LARGE);
    if (!loadLocked(doc)) return false;
    JsonArray vouchers = doc.as<JsonArray>();
    if (vouchers.isNull()) return false;

    for (JsonVariantConst codeVar : codes) {
      const String wanted = normalizeCode(codeVar.as<const char *>());
      if (wanted.isEmpty()) continue;
      bool found = false;
      for (size_t i = 0; i < vouchers.size(); ++i) {
        JsonObject item = vouchers[i];
        if (wanted != normalizeCode(item["code"] | "")) continue;
        found = true;
        normalizeRecord(item);
        const String status = item["status"] | "";
        if (status == "active" || status == "redeeming") {
          JsonObject skip = skipped.createNestedObject();
          skip["code"] = wanted;
          skip["reason"] = "active_or_redeeming";
          break;
        }
        copyNormalized(item, historyArr.createNestedObject());
        vouchers.remove(i);
        deleted.add(wanted);
        break;
      }
      if (!found) {
        JsonObject skip = skipped.createNestedObject();
        skip["code"] = wanted;
        skip["reason"] = "not_found";
      }
    }

    if (deleted.size() == 0) {
      response.clear();
      response["deleted"].set(deleted);
      response["skipped"].set(skipped);
      return true;
    }
    if (!saveLocked(doc)) return false;
  }

  Serial.printf("[voucher-job] bulk-delete persist ok deleted=%u\n",
                static_cast<unsigned>(deleted.size()));
  if (historyArr.size() > 0 &&
      !appendHistoryBatch(historyArr, "deleted", "")) {
    Serial.println("[voucher-job] bulk-delete history batch failed");
    return false;
  }
  response.clear();
  response["deleted"].set(deleted);
  response["skipped"].set(skipped);
  return true;
}

String VoucherManager::normalizeCode(const String &code) {
  String normalized = code;
  normalized.trim();
  normalized.toUpperCase();
  return normalized;
}

String VoucherManager::normalizeMac(const String &mac) {
  String compact;
  compact.reserve(12);
  for (size_t i = 0; i < mac.length(); ++i) {
    const char value = mac.charAt(i);
    if ((value >= '0' && value <= '9') ||
        (value >= 'a' && value <= 'f') ||
        (value >= 'A' && value <= 'F')) {
      compact += value;
    }
  }
  compact.toUpperCase();
  if (compact.length() != 12) {
    String fallback = mac;
    fallback.trim();
    fallback.toUpperCase();
    return fallback;
  }

  String normalized;
  normalized.reserve(17);
  for (size_t i = 0; i < compact.length(); ++i) {
    if (i > 0 && (i % 2) == 0) normalized += ':';
    normalized += compact.charAt(i);
  }
  return normalized;
}

String VoucherManager::normalizeAction(const String &action) {
  String normalized = action;
  normalized.trim();
  normalized.toLowerCase();
  return normalized;
}

String VoucherManager::makeCode() {
  const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  String code;
  code.reserve(11);
  for (int i = 0; i < 10; i++) {
    code += alphabet[esp_random() % (sizeof(alphabet) - 1)];
    if (i == 4) code += "-";
  }
  return code;
}
