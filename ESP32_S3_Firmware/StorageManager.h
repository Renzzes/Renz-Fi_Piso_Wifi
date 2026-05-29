#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD.h>

class StorageManager {
 public:
  bool begin();
  bool healthy() const;
  String lastError() const;

  bool ensureLayout();
  bool readJson(const char *path, JsonDocument &doc);
  bool writeJson(const char *path, JsonDocument &doc);
  bool appendJsonArrayItem(const char *path, JsonObject item, size_t capacity);
  bool clearJsonArray(const char *path);
  bool exists(const char *path) const;
  File openStatic(const String &path);
  String contentType(const String &path) const;
  uint64_t totalBytes() const;
  uint64_t usedBytes() const;

 private:
  bool _healthy = false;
  String _lastError;

  void setError(const String &message);
  bool ensureDir(const char *path);
  bool ensureJsonFile(const char *path, const char *contents);
};
