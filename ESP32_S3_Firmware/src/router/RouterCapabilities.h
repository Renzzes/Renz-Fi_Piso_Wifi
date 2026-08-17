#pragma once

#include <ArduinoJson.h>

// Capability flags reported by each IRouterDriver.
// Callers must query capabilities — never assume vendor-specific features.
struct RouterCapabilities {
  bool supportsVoucherControl  = false;
  bool supportsBandwidthLimit  = false;
  bool supportsHotspot         = false;
  bool supportsPauseResume     = false;
  bool supportsQueueManagement = false;
  bool supportsApi             = false;
  bool supportsIdentity        = false;
  bool supportsHealth          = false;
  bool supportsStatistics      = false;
  bool supportsRemoteConfig    = false;

  static RouterCapabilities none() { return RouterCapabilities{}; }

  void toJson(JsonObject obj) const {
    obj["supportsVoucherControl"]  = supportsVoucherControl;
    obj["supportsBandwidthLimit"]  = supportsBandwidthLimit;
    obj["supportsHotspot"]         = supportsHotspot;
    obj["supportsPauseResume"]     = supportsPauseResume;
    obj["supportsQueueManagement"] = supportsQueueManagement;
    obj["supportsApi"]             = supportsApi;
    obj["supportsIdentity"]        = supportsIdentity;
    obj["supportsHealth"]          = supportsHealth;
    obj["supportsStatistics"]      = supportsStatistics;
    obj["supportsRemoteConfig"]    = supportsRemoteConfig;
  }

  /** Installer/diagnostics-friendly keys for GET /api/health → router.capabilities. */
  void toHealthJson(JsonObject obj) const {
    obj["hotspot"]           = supportsHotspot;
    obj["pauseResume"]       = supportsPauseResume;
    obj["bandwidthControl"]  = supportsBandwidthLimit;
    obj["voucherSync"]       = supportsVoucherControl;
    obj["queueManagement"]   = supportsQueueManagement;
  }
};
