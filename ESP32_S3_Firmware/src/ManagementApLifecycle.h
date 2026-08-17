#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "ManagementApConfig.h"

class AuthManager;
class InstallationStateManager;
class ManagementApManager;
class NetworkSettingsManager;

// Phase 7C.2 — boot policy, post-setup preference, maintenance timeout.
// Orchestrates ManagementApManager without changing its public API.
class ManagementApLifecycle {
 public:
  void begin(ManagementApManager *mgmtAp,
             AuthManager *auth,
             InstallationStateManager *installation,
             NetworkSettingsManager *networkSettings);

  void loop();

  void applyBootPolicy();
  void processSetupCompletion();
  bool applyPostSetupPreference(bool keepEnabled);
  bool startMaintenance();
  bool startTemporary(uint32_t durationSeconds);
  bool stopMaintenance();

  /** Called when finish-setup provisioning completes — stops installer AP. */
  void completeSetupProvisioning();

  uint32_t maintenanceTimeoutSeconds() const { return _timeoutSeconds; }

  void patchStatus(JsonObject managementAp) const;

 private:
  ManagementApManager       *_mgmtAp = nullptr;
  AuthManager               *_auth = nullptr;
  InstallationStateManager  *_installation = nullptr;
  NetworkSettingsManager    *_networkSettings = nullptr;

  uint32_t _inactivitySinceMs = 0;
  bool     _inactivityActive  = false;
  uint32_t _timeoutSeconds    = ManagementApConfig::MAINTENANCE_TIMEOUT_SECONDS;

  bool isFactoryMode() const;
  void resetInactivityTimer();
  void clearInactivityTimer();
  void processMaintenanceTimeout();
};
