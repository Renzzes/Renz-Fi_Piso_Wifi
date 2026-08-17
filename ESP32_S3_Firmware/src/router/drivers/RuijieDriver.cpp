#include "RuijieDriver.h"

#include "../RouterDriverManifest.h"

namespace {

static const char *kFeatures[] = {"foundation_stub", nullptr};

RouterDriverManifest makeRuijieManifest() {
  RouterDriverManifest manifest;
  manifest.driverId          = "ruijie";
  manifest.vendor            = "Ruijie";
  manifest.model             = "Reyee / RG Series";
  manifest.supportedFirmware = "RuijieOS";
  manifest.minimumVersion    = "";
  manifest.capabilities      = RouterCapabilities::none();
  manifest.supportedFeatures = kFeatures;
  manifest.supportedFeatureCount = 1;
  manifest.stability         = DriverStability::Experimental;
  manifest.documentationUrl  = "";
  manifest.driverVersion     = "0.1.0-foundation";
  return manifest;
}

}  // namespace

RuijieDriver::RuijieDriver() : FoundationRouterDriver(makeRuijieManifest()) {}
