#pragma once

#include <Arduino.h>

#include "Models.h"

// Boot-time GPIO2 recovery (L1 password / L2 password+network).
// Phase 0: 5 s detection window before W5500 — hold timer starts at boot.
// Continues in loop() if button still held after the window (non-blocking).
class RecoveryManager {
 public:
  static void runBootCheck();
  static void loop();
  static bool isMonitoring();
};
