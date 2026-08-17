#include "IRouterDriver.h"

bool IRouterDriver::fillWireless(JsonDocument &out) {
  out["error"] = "Wireless configuration is not supported by this driver";
  return false;
}

bool IRouterDriver::saveWireless(JsonObjectConst /*settings*/, JsonDocument &out) {
  out["error"] = "Wireless configuration is not supported by this driver";
  return false;
}

void IRouterDriver::detect(JsonObject out) const {
  out["driverId"]   = driverId();
  out["detected"]   = false;
  out["confidence"] = "none";
  out["reason"]     = "Automatic detection not implemented for this driver";
  JsonObject manifestObj = out["manifest"].to<JsonObject>();
  manifest().toJson(manifestObj);
}

bool IRouterDriver::collectCacheSnapshot(JsonDocument &out,
                                         RouterCacheCollectMode /*mode*/) {
  out["error"] = "Router cache snapshot is not supported by this driver";
  return false;
}

bool IRouterDriver::pauseHotspotUser(const String &mac) {
  // Default: full deauth (appliance-side pause). MikroTik overrides to keep user.
  return deauthorizeUser(mac);
}

bool IRouterDriver::queryHotspotActivePresent(const String & /*mac*/,
                                              bool &presentOut) {
  presentOut = false;
  return false;
}
