#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "RouterCapabilities.h"

enum class DriverStability : uint8_t { Stable, Experimental };

// Declarative metadata for a registered router driver.
// Used by setup wizard / admin UI to show support matrix and block unsupported firmware.
struct RouterDriverManifest {
  const char *driverId           = "";
  const char *vendor             = "";
  const char *model              = "";  // empty = any model for vendor
  /** Installer-facing product name (e.g. Renz-Fi Gateway). */
  const char *productName        = "";
  /** Product subtitle (e.g. Powered by MikroTik RouterOS). */
  const char *productSubtitle    = "";
  const char *supportedFirmware  = "";
  const char *minimumVersion     = "";
  RouterCapabilities capabilities;

  const char *const *supportedFeatures = nullptr;
  size_t supportedFeatureCount         = 0;

  DriverStability stability      = DriverStability::Stable;
  const char *documentationUrl = "";
  const char *driverVersion    = "1.0.0";

  bool matchesFirmware(const String &firmware) const;
  bool isVersionSupported(const String &version) const;
  bool isSupported(const String &firmware, const String &version) const;
  String unsupportedReason(const String &firmware, const String &version) const;

  void toJson(JsonObject obj) const;
};

// Compare dotted version strings (e.g. 7.12.1 vs 6.0).
int compareRouterVersions(const String &left, const String &right);
