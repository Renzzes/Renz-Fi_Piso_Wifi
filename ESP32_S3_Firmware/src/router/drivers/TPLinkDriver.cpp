#include "TPLinkDriver.h"

#include "../RouterDriverManifest.h"

namespace {

static const char *kFeatures[] = {"foundation_stub", nullptr};

RouterDriverManifest makeTPLinkManifest() {
  RouterDriverManifest manifest;
  manifest.driverId          = "tplink";
  manifest.vendor            = "TP-Link";
  manifest.model             = "Omada / Omada Pro / ER Series";
  manifest.supportedFirmware = "TP-Link";
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

TPLinkDriver::TPLinkDriver() : FoundationRouterDriver(makeTPLinkManifest()) {}
