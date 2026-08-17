#pragma once

#include <Arduino.h>

#include "InstallationState.h"

class AssetManager;
class EthernetManager;
class InstallationStateManager;
class RouterPlatform;
class StorageManager;

namespace BootDiagnostics {

struct BootSummaryContext {
  EthernetManager           *eth           = nullptr;
  StorageManager            *storage        = nullptr;
  InstallationStateManager  *installation   = nullptr;
  RouterPlatform            *router         = nullptr;
  AssetManager              *assets         = nullptr;
  bool                       spiffsMounted  = false;
  bool                       coinEnabled    = false;
};

// Returns true when all required portal SPIFFS objects exist (no serial output).
bool checkRequiredPortalAssets();

// Prints verified checklist or WARNING blocks. Never halts boot.
bool logPortalAssetValidation(bool spiffsMounted);

// Production installer summary (replaces legacy Driver/Link/IP-only footer).
void logProductionSummary(const BootSummaryContext &ctx);

const char *installationDisplayLabel(InstallationState state);

}  // namespace BootDiagnostics
