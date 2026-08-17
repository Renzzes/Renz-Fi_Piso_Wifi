#pragma once

#include <Arduino.h>

#include "Models.h"

// Legacy extension point — superseded by ManagementApManager (Phase 7C.1).
// RENZ-FI-SETUP AP @ 192.168.4.1
class SetupModeController {
 public:
  static bool isEnabled();
  static bool shouldEnterSetupMode();
  static void enterSetupMode();
  static void exitSetupMode();
  static const char *setupSsid();
  static const char *setupPortalUrl();
};
