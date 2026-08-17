#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

class StorageManager;

class BuildMetadata {
 public:
  void begin(StorageManager *storage);

  bool loaded() const { return _loaded; }
  void fillJson(JsonObject out) const;

 private:
  StorageManager *_storage = nullptr;
  DynamicJsonDocument _doc{768};
  bool _loaded = false;

  void mirrorToSd(const String &rawJson);
};
