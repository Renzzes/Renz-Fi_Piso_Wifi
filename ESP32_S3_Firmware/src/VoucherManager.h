#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "EventBus.h"
#include "Logger.h"
#include "StorageManager.h"

class VoucherManager {
 public:
  enum class ReserveStatus : uint8_t {
    Reserved,
    Idempotent,
    NotFound,
    InvalidInput,
    Unavailable,
    BoundToAnotherDevice,
    StorageError
  };

  struct ReserveResult {
    ReserveStatus result = ReserveStatus::InvalidInput;
    String code;
    String status;
    String boundMac;
    String sessionId;
    int amount = 0;
    int minutes = 0;
    String validUntil;
    String activatedAt;
    String serviceExpiresAt;
    String profileName;
    String speed;

    bool accepted() const {
      return result == ReserveStatus::Reserved ||
             result == ReserveStatus::Idempotent;
    }
  };

  void begin(StorageManager *storage, Logger *logger, EventBus *events);

  bool list(JsonDocument &doc);
  bool find(const String &code, JsonDocument &doc);

  bool generate(int count, int amount, int minutes, const String &expires,
                JsonDocument &response);
  bool generate(JsonObjectConst config, JsonDocument &response);

  /**
   * Single-flight jobs: HTTP enqueues; work runs on voucher_worker.
   * Job state uses a separate mutex so GET /api/vouchers/jobs/* never blocks
   * behind SD I/O.
   */
  struct JobSnapshot {
    uint32_t jobId = 0;
    const char *state = "idle";  // idle|queued|running|completed|failed
    const char *type = "idle";   // voucher-generate|voucher-bulk-delete
    bool ok = false;
    uint32_t count = 0;
    String error;
    String resultJson;
  };
  using GenerateJobSnapshot = JobSnapshot;

  uint32_t enqueueGenerate(JsonObjectConst config, bool &alreadyRunning);
  uint32_t enqueueBulkDelete(JsonArrayConst codes, bool &alreadyRunning);
  bool pollGenerateJob(uint32_t jobId, JobSnapshot &out) const;
  bool generateBusy() const;
  void loop();

  ReserveResult reserve(const String &code, const String &mac,
                        const String &sessionId, const String &redeemedAt);
  bool markActivated(const String &code, const String &mac,
                     const String &sessionId, const String &activatedAt,
                     const String &serviceExpiresAt);

  bool expire(const String &code, const String &terminalReason,
              const String &updatedAt);
  bool disable(const String &code, const String &terminalReason,
               const String &updatedAt);
  bool archive(const String &code, const String &terminalReason,
               const String &updatedAt);
  bool ownerAction(const String &code, const String &action,
                   const String &terminalReason, const String &updatedAt);

  bool remove(const String &code);
  bool markActive(const String &code);

 private:
  class ScopedLock;
  class ScopedJobLock;

  StorageManager *_storage = nullptr;
  Logger *_logger = nullptr;
  EventBus *_events = nullptr;
  mutable SemaphoreHandle_t _mutex = nullptr;
  mutable SemaphoreHandle_t _jobMutex = nullptr;
  TaskHandle_t _workerTask = nullptr;

  enum class GenState : uint8_t { Idle, Queued, Running, Completed, Failed };
  enum class JobKind : uint8_t { None, Generate, BulkDelete };
  GenState _genState = GenState::Idle;
  JobKind _jobKind = JobKind::None;
  uint32_t _genJobId = 0;
  uint32_t _genNextId = 1;
  String _genRequestJson;
  String _genResultJson;
  String _genError;
  uint32_t _genStartedMs = 0;
  uint32_t _genResultCount = 0;

  void lock() const;
  void unlock() const;
  void lockJob() const;
  void unlockJob() const;
  bool loadLocked(JsonDocument &doc) const;
  bool saveLocked(const JsonDocument &doc);
  bool transitionTerminal(const String &code, const char *status,
                          const String &terminalReason,
                          const String &updatedAt);
  bool codeExists(JsonArrayConst vouchers, const String &code) const;
  void normalizeRecord(JsonObject item) const;
  void copyNormalized(JsonObjectConst source, JsonObject destination) const;
  void fillReserveResult(JsonObjectConst item, ReserveResult &result) const;
  void appendHistory(JsonObjectConst item, const String &action,
                     const String &eventAt);
  bool appendHistoryBatch(JsonArrayConst items, const String &action,
                          const String &eventAt);

  bool runBulkDelete(JsonArrayConst codes, JsonDocument &response);

  void notifyWorker();
  void runQueuedJob();
  static void workerTaskEntry(void *param);

  static String normalizeCode(const String &code);
  static String normalizeMac(const String &mac);
  static String normalizeAction(const String &action);
  static String makeCode();
};
