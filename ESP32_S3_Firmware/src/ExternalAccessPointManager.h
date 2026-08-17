#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "ExternalAccessPointTypes.h"
#include "ap/GenericApDriver.h"

class EthernetManager;
class Logger;
class StorageManager;

// Optional registry of external LAN coverage APs.
// Stage B: persist + CRUD + live IP validation.
// Stage C: single-flight reachability worker (ICMP + TCP). No RouterOS. No SD on check.
class ExternalAccessPointManager {
 public:
  static constexpr uint8_t kMaxAccessPoints = ExternalAccessPoint::kMaxAccessPoints;
  static constexpr uint32_t kWorkerStackWords = 4096;
  static constexpr UBaseType_t kWorkerPriority = 1;
  static constexpr BaseType_t kWorkerCore = 0;

  struct CheckJobSnapshot {
    uint32_t jobId = 0;
    char accessPointId[20] = {};
    const char *state = "idle";
    bool ok = false;
    const char *status = "unknown";
    bool latencyValid = false;
    uint32_t latencyMs = 0;
    uint32_t startedAt = 0;
    uint32_t completedAt = 0;
    const char *errorCode = nullptr;
    char message[96] = {};
  };

  void begin(StorageManager *storage, EthernetManager *eth, Logger *logger);
  void loop();

  bool empty() const { return _count == 0; }
  uint8_t count() const { return _count; }
  const char *registryError() const {
    return _registryError.length() > 0 ? _registryError.c_str() : nullptr;
  }

  void fillList(JsonDocument &doc) const;
  ExternalAccessPoint::CrudStatus getById(const String &id, JsonDocument &doc) const;
  ExternalAccessPoint::CrudStatus create(JsonObjectConst input, JsonDocument &out);
  ExternalAccessPoint::CrudStatus update(const String &id, JsonObjectConst input,
                                         JsonDocument &out);
  ExternalAccessPoint::CrudStatus remove(const String &id);

  ExternalAccessPoint::CheckEnqueueStatus enqueueCheck(const String &id,
                                                       uint32_t &jobIdOut);
  bool pollCheckJob(uint32_t jobId, CheckJobSnapshot &out) const;
  bool checkBusy() const;

 private:
  struct Record {
    String id;
    String name;
    bool enabled = true;
    ExternalAccessPoint::Vendor vendor = ExternalAccessPoint::Vendor::Generic;
    String model;
    String managementIp;
    String username;
    String passwordProtected;
    String ssid;
    String location;
    String notes;
    ExternalAccessPoint::ReachabilityStatus status =
        ExternalAccessPoint::ReachabilityStatus::Unknown;
    bool latencyValid = false;
    uint32_t latencyMs = 0;
    uint32_t lastCheckMs = 0;
    uint32_t lastSuccessfulCheckMs = 0;
    const char *lastError = nullptr;
    bool capIcmp = false;
    bool capHttp = false;
    bool capHttps = false;
  };

  struct CheckJob {
    uint32_t jobId = 0;
    char accessPointId[20] = {};
    ExternalAccessPoint::CheckJobState state =
        ExternalAccessPoint::CheckJobState::Idle;
    bool ok = false;
    ExternalAccessPoint::ReachabilityStatus status =
        ExternalAccessPoint::ReachabilityStatus::Unknown;
    bool latencyValid = false;
    uint32_t latencyMs = 0;
    uint32_t startedAt = 0;
    uint32_t completedAt = 0;
    const char *errorCode = nullptr;
    char message[96] = {};
  };

  class ScopedRegistryLock;
  class ScopedJobLock;

  StorageManager *_storage = nullptr;
  EthernetManager *_eth = nullptr;
  Logger *_logger = nullptr;
  Record _records[kMaxAccessPoints];
  uint8_t _count = 0;
  String _registryError;
  mutable SemaphoreHandle_t _registryMutex = nullptr;
  mutable SemaphoreHandle_t _jobMutex = nullptr;
  TaskHandle_t _workerTask = nullptr;
  GenericApDriver _driver;
  CheckJob _job;
  uint32_t _nextJobId = 1;

  void lockRegistry() const;
  void unlockRegistry() const;
  void lockJob() const;
  void unlockJob() const;

  void loadFromStorage();
  ExternalAccessPoint::CrudStatus persist();
  void fillPublicRecord(JsonObject obj, const Record &record) const;
  int findIndex(const String &id) const;
  int findIndexByIp(const String &ip, int exceptIndex) const;
  String generateId() const;
  bool applyInput(Record &record, JsonObjectConst input, bool isCreate,
                  String &passwordPlain, bool &passwordProvided,
                  ExternalAccessPoint::CrudStatus &status);
  ExternalAccessPoint::CrudStatus validateName(const String &name) const;
  ExternalAccessPoint::CrudStatus validateIp(const String &ip, int exceptIndex) const;
  void logSafe(const char *action, const Record &record) const;
  void notifyWorker();
  void runQueuedJob();
  static void workerTaskEntry(void *param);
  void applyRamStatus(Record &record, ExternalAccessPoint::ReachabilityStatus status,
                      bool latencyValid, uint32_t latencyMs, const char *errorCode,
                      ExternalAccessPoint::ManagementTransport transport);
  void finishJob(uint32_t jobId, ExternalAccessPoint::CheckJobState state, bool ok,
                 ExternalAccessPoint::ReachabilityStatus status,
                 bool latencyValid, uint32_t latencyMs, const char *errorCode,
                 const char *message);
  static void copyId(char *dest, size_t destSize, const String &id);
};
