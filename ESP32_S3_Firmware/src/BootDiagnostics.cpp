#include "BootDiagnostics.h"

#include <SPIFFS.h>

#include "AssetManager.h"
#include "Config.h"
#include "DeviceIdentity.h"
#include "EthernetManager.h"
#include "InstallationStateManager.h"
#include "RenzFiDebug.h"
#include "router/IRouterDriver.h"
#include "router/RouterPlatform.h"
#include "StorageManager.h"
#include "web/PortalSpiffsLayout.h"

namespace BootDiagnostics {

namespace {

bool hasAdminDashboardAssets() {
  if (!SPIFFS.exists("/index.html")) return false;
  File dir = SPIFFS.open("/assets");
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }
  File entry = dir.openNextFile();
  const bool found = (entry && !entry.isDirectory());
  if (entry) entry.close();
  dir.close();
  return found;
}

String routerDriverDisplayLabel(RouterPlatform *router) {
  if (!router) return "None";
  const IRouterDriver *active = router->activeDriver();
  if (!active) return "None";
  const String id = active->driverId();
  if (id.equalsIgnoreCase("mikrotik")) return "MikroTik";
  if (id.equalsIgnoreCase("tplink")) return "TP-Link";
  if (id.equalsIgnoreCase("openwrt")) return "OpenWRT";
  if (id.equalsIgnoreCase("ruijie")) return "Ruijie";
  if (id.equalsIgnoreCase("generic_ap")) return "Generic AP";
  if (id.length() == 0) return "None";
  String label = id;
  label.setCharAt(0, static_cast<char>(toupper(static_cast<unsigned char>(label.charAt(0)))));
  return label;
}

String storageStatusLabel(StorageManager *storage) {
  if (!storage) return "Unavailable";
  if (storage->healthy() && !storage->usingFallback()) return "SD Ready";
  if (storage->usingFallback()) return "SPIFFS Fallback";
  if (storage->healthy()) return "Ready";
  return "Degraded";
}

String readinessLabel(bool ok) { return ok ? "Ready" : "Unavailable"; }

void logLine(const char *label, const String &value) {
  Serial.printf("%-17s: %s\n", label, value.c_str());
}

void logLine(const char *label, const char *value) {
  Serial.printf("%-17s: %s\n", label, value);
}

void collectMissingPortalAssets(const char *missing[],
                                size_t &missingCount) {
  missingCount = 0;
  for (size_t i = 0; i < PortalSpiffsLayout::kRequiredPortalAssetCount; ++i) {
    const char *path = PortalSpiffsLayout::kRequiredPortalAssets[i].spiffsPath;
    if (!path || !SPIFFS.exists(path)) {
      missing[missingCount++] =
          PortalSpiffsLayout::kRequiredPortalAssets[i].label;
    }
  }
}

}  // namespace

const char *installationDisplayLabel(InstallationState state) {
  switch (state) {
    case InstallationState::Factory:
      return "Factory";
    case InstallationState::OwnerCreated:
      return "Owner Created";
    case InstallationState::RouterConfigured:
      return "Router Configured";
    case InstallationState::Provisioned:
      return "Provisioned";
    case InstallationState::RouterSelected:
      return "Router Selected";
    case InstallationState::RouterConnected:
      return "Router Connected";
    case InstallationState::PortalConfigured:
      return "Portal Configured";
    case InstallationState::CoinConfigured:
      return "Coin Configured";
    case InstallationState::ValidationPassed:
      return "Validation Passed";
    case InstallationState::Ready:
      return "Ready";
    default:
      return "Factory";
  }
}

bool checkRequiredPortalAssets() {
  for (size_t i = 0; i < PortalSpiffsLayout::kRequiredPortalAssetCount; ++i) {
    const char *path = PortalSpiffsLayout::kRequiredPortalAssets[i].spiffsPath;
    if (!path || !SPIFFS.exists(path)) return false;
  }
  return true;
}

bool logPortalAssetValidation(bool spiffsMounted) {
  if (!spiffsMounted) {
    Serial.println("[portal] WARNING");
    Serial.println("SPIFFS not mounted — portal assets cannot be verified.");
    Serial.println("Captive Portal disabled.");
    Serial.println("Admin Dashboard remains available if uploaded separately.");
    return false;
  }

  const char *missing[PortalSpiffsLayout::kRequiredPortalAssetCount];
  size_t missingCount = 0;
  collectMissingPortalAssets(missing, missingCount);

#if RENZFI_DEBUG_PORTAL
  Serial.println("[portal] Debug: required asset SPIFFS paths:");
  for (size_t i = 0; i < PortalSpiffsLayout::kRequiredPortalAssetCount; ++i) {
    const char *path = PortalSpiffsLayout::kRequiredPortalAssets[i].spiffsPath;
    Serial.printf("[portal]   %s (%s) -> %s\n",
                  PortalSpiffsLayout::kRequiredPortalAssets[i].label,
                  path,
                  SPIFFS.exists(path) ? "present" : "MISSING");
  }
#endif

  if (missingCount == 0) {
    Serial.println("[portal] Portal assets verified");
    for (size_t i = 0; i < PortalSpiffsLayout::kRequiredPortalAssetCount; ++i) {
      Serial.printf("  ✓ %s\n",
                    PortalSpiffsLayout::kRequiredPortalAssets[i].label);
    }
    return true;
  }

  Serial.println("[portal] WARNING");
  if (missingCount == 1) {
    Serial.println("Portal asset missing:");
    Serial.printf("  %s\n", missing[0]);
    Serial.println("Captive Portal will NOT function.");
  } else {
    Serial.println("Missing portal assets:");
    for (size_t i = 0; i < missingCount; ++i) {
      Serial.printf("  %s\n", missing[i]);
    }
    Serial.println("Captive Portal disabled.");
  }
  Serial.println("Admin Dashboard remains available.");
  return false;
}

void logProductionSummary(const BootSummaryContext &ctx) {
  const String mac = ctx.eth ? ctx.eth->macAddress() : "";
  const String deviceId = DeviceIdentity::formatDeviceId(mac);
  const String friendlyName =
      ctx.storage ? DeviceIdentity::readFriendlyName(ctx.storage)
                  : String("Renz-Fi Appliance");
  const InstallationState installState =
      ctx.installation ? ctx.installation->current() : InstallationState::Factory;
  const bool portalOk =
      ctx.spiffsMounted && checkRequiredPortalAssets();
  const bool adminOk = ctx.spiffsMounted && hasAdminDashboardAssets();
  const bool assetsOk = ctx.assets && ctx.assets->ready();

  Serial.println("================================================");
  Serial.println("RENZ-FI APPLIANCE READY");
  Serial.println("------------------------------------------------");
  logLine("Device Name", friendlyName);
  logLine("Device ID", deviceId);
  logLine("Serial Number", mac.length() > 0 ? mac : deviceId);
  logLine("Firmware", RenzFiConfig::FIRMWARE_VERSION);
  logLine("Hardware", RenzFiConfig::HARDWARE_REVISION);
  logLine("Installation", installationDisplayLabel(installState));
  logLine("Router Driver", routerDriverDisplayLabel(ctx.router));
  Serial.println("------------------------------------------------");

  if (ctx.eth) {
    logLine("Ethernet Driver", ctx.eth->driverReady() ? "UP" : "DOWN");
    logLine("Ethernet Link", ctx.eth->linkUp() ? "UP" : "DOWN");
    logLine("IP Address", ctx.eth->ip());
    logLine("MAC Address", mac);
  } else {
    logLine("Ethernet Driver", "DOWN");
    logLine("Ethernet Link", "DOWN");
    logLine("IP Address", "unknown");
    logLine("MAC Address", mac);
  }

  Serial.println("------------------------------------------------");
  logLine("Storage", storageStatusLabel(ctx.storage));
  logLine("Assets", readinessLabel(assetsOk));
  logLine("Portal", portalOk ? String("Ready") : String("Incomplete"));
  logLine("Admin Dashboard", readinessLabel(adminOk));
  logLine("Provisioning", "Ready");
  Serial.println("------------------------------------------------");

  const String ip = ctx.eth ? ctx.eth->ip() : String("unknown");
  Serial.println("Admin URL");
  Serial.printf("http://%s/admin\n", ip.c_str());
  Serial.println("================================================");

#if RENZFI_DEBUG_BOOT
  Serial.printf("[boot] coin manager   : %s\n",
                ctx.coinEnabled ? "enabled" : "disabled");
  Serial.printf("[boot] spiffs mounted : %s\n",
                ctx.spiffsMounted ? "yes" : "no");
  Serial.printf("[boot] portal assets  : %s\n", portalOk ? "verified" : "missing");
#endif
}

}  // namespace BootDiagnostics
