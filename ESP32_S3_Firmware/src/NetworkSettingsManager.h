#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "Models.h"

class StorageManager;

// Persisted Ethernet network configuration (DHCP-first, optional static).
// Persisted in NVS renz-network and mirrored to settings.json "network".
//
// Phase B: EthernetManager now consults these settings directly. DHCP is
// always used unless `provisioned == true` AND `addressMode == Static` AND
// the stored static fields pass validation — any other combination (never
// configured, corrupt/partial data, invalid IPs) safely falls back to DHCP.
class NetworkSettingsManager {
 public:
  void begin(StorageManager *storage);

  NetworkSettings settings() const;
  bool save(const NetworkSettings &settings, String *errorOut = nullptr);
  void resetToDefaults();

  // NVS-only reset for early-boot RecoveryManager (before SD mount).
  static void applyRecoveryResetNvs();

  // NVS-only read for early boot (Phase 1, before StorageManager/SD mount).
  // Ethernet must initialize before SD per the existing boot sequence, so
  // this lets EthernetManager honor a previously-saved static configuration
  // without waiting for SD. begin() below reconciles with settings.json
  // once storage is available and re-applies only if values changed.
  static NetworkSettings loadNvsOnly();

  static NetworkSettings factoryDefaults();

  // Validates static-mode fields (dotted-quad IPv4 parse + basic sanity).
  // No-op / always true for DHCP mode. Returns false and fills `errorOut`
  // (if provided) with a human-readable reason on failure.
  static bool validateStaticConfig(const NetworkSettings &settings,
                                   String *errorOut = nullptr);

 private:
  StorageManager *_storage = nullptr;
  NetworkSettings _settings;

  void loadFromNvs();
  void saveToNvs() const;
  bool loadFromSettingsJson();
  bool saveToSettingsJson() const;
};
