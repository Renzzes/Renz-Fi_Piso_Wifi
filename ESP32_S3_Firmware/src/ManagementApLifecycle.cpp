#include "ManagementApLifecycle.h"

#include <cstring>

#include "AuthManager.h"
#include "InstallationStateManager.h"
#include "ManagementApConfig.h"
#include "ManagementApManager.h"
#include "NetworkSettingsManager.h"
#include "RenzFiDebug.h"
#include "SalesTime.h"
#include "SetupDnsPolicy.h"

namespace {

constexpr uint32_t kMinTemporaryDurationSeconds = 60;
constexpr uint32_t kMaxTemporaryDurationSeconds = 3600;

}  // namespace

void ManagementApLifecycle::begin(ManagementApManager *mgmtAp,
                                  AuthManager *auth,
                                  InstallationStateManager *installation,
                                  NetworkSettingsManager *networkSettings) {
  _mgmtAp           = mgmtAp;
  _auth             = auth;
  _installation     = installation;
  _networkSettings  = networkSettings;
}

bool ManagementApLifecycle::isFactoryMode() const {
  return _installation && _installation->needsSetup();
}

void ManagementApLifecycle::applyBootPolicy() {
  if (!_mgmtAp) return;

#if RENZFI_DISABLE_MGMT_AP_BOOT
  Serial.println("[mgmt-ap] Boot policy skipped (RENZFI_DISABLE_MGMT_AP_BOOT)");
  return;
#endif

  if (isFactoryMode()) {
    _mgmtAp->start();
    clearInactivityTimer();
    return;
  }

  const bool keepEnabled =
      _networkSettings &&
      _networkSettings->settings().managementApKeepEnabledAfterSetup;

  if (keepEnabled) {
    _timeoutSeconds = ManagementApConfig::MAINTENANCE_TIMEOUT_SECONDS;
    _mgmtAp->start();
    resetInactivityTimer();
  }
}

bool ManagementApLifecycle::applyPostSetupPreference(bool keepEnabled) {
  if (!_mgmtAp || !_networkSettings) return false;

  NetworkSettings settings = _networkSettings->settings();
  settings.managementApKeepEnabledAfterSetup = keepEnabled;
  if (!_networkSettings->save(settings)) return false;

  if (keepEnabled) {
    _timeoutSeconds = ManagementApConfig::MAINTENANCE_TIMEOUT_SECONDS;
    if (!_mgmtAp->start()) return false;
    resetInactivityTimer();
  } else {
    _mgmtAp->stop();
    clearInactivityTimer();
  }
  return true;
}

bool ManagementApLifecycle::startMaintenance() {
  return startTemporary(ManagementApConfig::MAINTENANCE_TIMEOUT_SECONDS);
}

bool ManagementApLifecycle::startTemporary(uint32_t durationSeconds) {
  if (!_mgmtAp || isFactoryMode()) return false;

  if (durationSeconds == 0) {
    durationSeconds = ManagementApConfig::MAINTENANCE_TIMEOUT_SECONDS;
  }
  if (durationSeconds < kMinTemporaryDurationSeconds ||
      durationSeconds > kMaxTemporaryDurationSeconds) {
    return false;
  }

  _timeoutSeconds = durationSeconds;
  if (!_mgmtAp->start()) return false;
  resetInactivityTimer();
  return true;
}

bool ManagementApLifecycle::stopMaintenance() {
  if (!_mgmtAp) return false;
  _mgmtAp->stop();
  clearInactivityTimer();
  return true;
}

void ManagementApLifecycle::resetInactivityTimer() {
  _inactivitySinceMs = millis();
  _inactivityActive  = true;
}

void ManagementApLifecycle::clearInactivityTimer() {
  _inactivitySinceMs = 0;
  _inactivityActive  = false;
}

void ManagementApLifecycle::processMaintenanceTimeout() {
  if (!_mgmtAp || !_mgmtAp->isRunning()) {
    clearInactivityTimer();
    return;
  }

  if (isFactoryMode()) {
    clearInactivityTimer();
    return;
  }

  if (_auth && _auth->hasActiveSessions()) {
    clearInactivityTimer();
    return;
  }

  if (!_inactivityActive) {
    _inactivitySinceMs = millis();
    _inactivityActive  = true;
    return;
  }

  const uint32_t elapsedMs = millis() - _inactivitySinceMs;
  if (elapsedMs >= _timeoutSeconds * 1000UL) {
    Serial.println("[mgmt-ap] maintenance inactivity timeout — stopping AP");
    _mgmtAp->stop();
    clearInactivityTimer();
  }
}

void ManagementApLifecycle::loop() {
  processSetupCompletion();
  processMaintenanceTimeout();
}

void ManagementApLifecycle::processSetupCompletion() {
  if (!_installation || !_mgmtAp) return;
  if (!_installation->isReady()) return;
  if (!_mgmtAp->isRunning()) return;

  Serial.println(
      "[mgmt-ap] setup complete (provisioned/ready) — stopping Management AP "
      "and captive DNS");
  SetupDnsPolicy::restoreProductionDns();
  _mgmtAp->stop();
  clearInactivityTimer();
  salesTimeBegin();
}

void ManagementApLifecycle::completeSetupProvisioning() {
  if (!_mgmtAp) return;

  Serial.println("[Provisioning] Stopping Setup AP...");
  if (_mgmtAp->isRunning()) {
    SetupDnsPolicy::restoreProductionDns();
    _mgmtAp->stop();
    clearInactivityTimer();
  }
  salesTimeBegin();
}

void ManagementApLifecycle::patchStatus(JsonObject managementAp) const {
  if (managementAp.isNull()) return;

  const char *mode = managementAp["mode"] | "disabled";
  if (strcmp(mode, "maintenance") == 0) {
    managementAp["timeoutSeconds"] = _timeoutSeconds;
  } else {
    managementAp["timeoutSeconds"] = nullptr;
  }
}
