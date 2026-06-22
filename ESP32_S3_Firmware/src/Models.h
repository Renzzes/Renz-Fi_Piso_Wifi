#pragma once

#include <Arduino.h>

enum class LogLevel {
  Info,
  Warn,
  Error
};

enum class LedMode {
  Off,
  Waiting,
  Active,
  Error
};

struct SaleRecord {
  String id;
  String timestamp;
  String recordedAt;
  int amount = 0;
  String sessionId;
  String paymentType;
  int durationMinutes = 0;
};

struct HotspotUser {
  String mac;
  String ip;
  String username;
  String profile;
  uint32_t timeoutSeconds = 0;
};

struct CoinSettings {
  int pesoPerPulse = 1;
  int defaultMinutesPerPeso = 5;
  uint32_t debounceMs = 35;
  uint32_t settleMs = 450;
  uint32_t timeoutSeconds = 60;
  bool enabled = true;
};

struct RouterSettings {
  String host;
  String username;
  String password;
  String profile;
  String ssid;
};
