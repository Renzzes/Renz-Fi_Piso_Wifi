#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "ExternalAccessPointTypes.h"

class EthernetManager;
class Logger;
class StorageManager;

// Optional registry of external LAN coverage APs.
// Stage B: persist + CRUD + live IP validation only. No probes, no worker.
class ExternalAccessPointManager {
 public:
  static constexpr uint8_t kMaxAccessPoints = ExternalAccessPoint::kMaxAccessPoints;

  void begin(StorageManager *storage, EthernetManager *eth, Logger *logger);

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
  };

  StorageManager *_storage = nullptr;
  EthernetManager *_eth = nullptr;
  Logger *_logger = nullptr;
  Record _records[kMaxAccessPoints];
  uint8_t _count = 0;
  String _registryError;

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
};
