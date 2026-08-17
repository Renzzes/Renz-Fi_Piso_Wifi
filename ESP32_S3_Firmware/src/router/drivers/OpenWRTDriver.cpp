#include "OpenWRTDriver.h"

#include "../RouterDriverManifest.h"

namespace {

static const char *kFeatures[] = {"foundation_stub", nullptr};

RouterDriverManifest makeOpenWRTManifest() {
  RouterDriverManifest manifest;
  manifest.driverId          = "openwrt";
  manifest.vendor            = "OpenWRT";
  manifest.model             = "";
  manifest.supportedFirmware = "OpenWrt";
  manifest.minimumVersion    = "21.02";
  manifest.capabilities      = RouterCapabilities::none();
  manifest.supportedFeatures = kFeatures;
  manifest.supportedFeatureCount = 1;
  manifest.stability         = DriverStability::Experimental;
  manifest.documentationUrl  = "https://openwrt.org/";
  manifest.driverVersion     = "0.1.0-foundation";
  return manifest;
}

}  // namespace

OpenWRTDriver::OpenWRTDriver() : FoundationRouterDriver(makeOpenWRTManifest()) {}
