#include "ManagementApLifecycle.h"

#include <cstring>

#include "AuthManager.h"
#include "InstallationState.h"
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
// After production ETH routes are live, stop unused SoftAP quickly.
// Proven Guru (owner_created + Admin System Config): SoftAP still up at ~19s
// with dma_largest≈532 while W5500 RX needed 548. A 60s idle wait was too late.
constexpr uint32_t kProductionSoftApIdleYieldMs = 2000;

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
  if (!_mgmtAp) return false;

  // Classic setup SoftAP is owned by applyBootPolicy / wizard — do not start a
  // parallel "maintenance" AP while SoftAP is already the installer path.
  // Exception: production ETH plane is live and SoftAP was idle-yielded (DMA);
  // owner/installer must be able to re-open SoftAP with a timed maintenance window.
  if (isFactoryMode() && !_productionPlaneReady) return false;
  if (isFactoryMode() && _mgmtAp->isRunning()) return false;

  if (durationSeconds == 0) {
    durationSeconds = ManagementApConfig::MAINTENANCE_TIMEOUT_SECONDS;
  }
  if (durationSeconds < kMinTemporaryDurationSeconds ||
      durationSeconds > kMaxTemporaryDurationSeconds) {
    return false;
  }

  _timeoutSeconds = durationSeconds;
  if (!_mgmtAp->start()) return false;
  // Timed window even when installation is still pre-Ready (post-yield reopen).
  resetInactivityTimer();
  _softApYieldArmed = false;
  Serial.printf(
      "[mgmt-ap] temporary SoftAP started durationSec=%u productionReady=%s "
      "needsSetup=%s\n",
      static_cast<unsigned>(durationSeconds),
      _productionPlaneReady ? "yes" : "no",
      isFactoryMode() ? "yes" : "no");
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

  // Untimed installer SoftAP (boot policy, production not registered yet).
  if (isFactoryMode() && !_productionPlaneReady) {
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
    // If production is live and setup still incomplete, re-arm idle yield so
    // a later SoftAP reopen can yield again after clients leave.
    if (_productionPlaneReady && isFactoryMode()) {
      _softApYieldArmed = true;
      _softApYieldSinceMs = millis();
    }
  }
}

void ManagementApLifecycle::loop() {
  processSetupCompletion();
  processProductionSoftApYield();
  processMaintenanceTimeout();
}

void ManagementApLifecycle::notifyProductionPlaneReady() {
  _productionPlaneReady = true;
  if (!_mgmtAp || !_mgmtAp->isRunning()) {
    _softApYieldArmed = false;
    return;
  }
  _softApYieldArmed = true;
  _softApYieldSinceMs = millis();
  Serial.printf(
      "[mgmt-ap] production plane ready — SoftAP idle yield armed "
      "(%u ms if no SoftAP clients)\n",
      static_cast<unsigned>(kProductionSoftApIdleYieldMs));
  // Attempt immediately; processProductionSoftApYield enforces the settle window.
  processProductionSoftApYield();
}

void ManagementApLifecycle::processProductionSoftApYield() {
  if (!_productionPlaneReady || !_softApYieldArmed || !_mgmtAp) return;
  if (!_mgmtAp->isRunning()) {
    _softApYieldArmed = false;
    return;
  }

  // True factory (needs full SoftAP installer path) — do not yield yet.
  if (_installation &&
      _installation->current() == InstallationState::Factory) {
    _softApYieldSinceMs = millis();
    return;
  }

  const bool keepEnabled =
      _networkSettings &&
      _networkSettings->settings().managementApKeepEnabledAfterSetup;
  if (keepEnabled) {
    _softApYieldArmed = false;
    return;
  }

  _mgmtAp->refreshRuntimeState();
  if (_mgmtAp->connectedClients() > 0) {
    _softApYieldSinceMs = millis();
    return;
  }

  if ((millis() - _softApYieldSinceMs) < kProductionSoftApIdleYieldMs) return;

  Serial.println(
      "[mgmt-ap] production ETH active + SoftAP idle — stopping Management AP "
      "(DMA headroom; re-enable via maintenance AP if needed)");
  SetupDnsPolicy::restoreProductionDns();
  _mgmtAp->stop();
  clearInactivityTimer();
  _softApYieldArmed = false;
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
