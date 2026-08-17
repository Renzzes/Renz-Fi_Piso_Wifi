#include "SetupModeController.h"

bool SetupModeController::isEnabled() {
  return false;
}

bool SetupModeController::shouldEnterSetupMode() {
  return false;
}

void SetupModeController::enterSetupMode() {}

void SetupModeController::exitSetupMode() {}

const char *SetupModeController::setupSsid() {
  return "RENZ-FI-SETUP";
}

const char *SetupModeController::setupPortalUrl() {
  return "http://192.168.4.1";
}
