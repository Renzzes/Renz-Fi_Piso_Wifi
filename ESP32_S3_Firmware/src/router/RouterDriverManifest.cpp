#include "RouterDriverManifest.h"

namespace {

int readVersionComponent(const String &value, int &index) {
  int component = 0;
  while (index < static_cast<int>(value.length()) &&
         isdigit(static_cast<unsigned char>(value[index]))) {
    component = component * 10 + (value[index] - '0');
    ++index;
  }
  return component;
}

}  // namespace

int compareRouterVersions(const String &left, const String &right) {
  int leftIndex  = 0;
  int rightIndex = 0;

  for (int part = 0; part < 8; ++part) {
    const int leftPart  = readVersionComponent(left, leftIndex);
    const int rightPart = readVersionComponent(right, rightIndex);
    if (leftPart != rightPart) {
      return leftPart > rightPart ? 1 : -1;
    }
    if (leftIndex < static_cast<int>(left.length()) && left[leftIndex] == '.') {
      ++leftIndex;
    }
    if (rightIndex < static_cast<int>(right.length()) && right[rightIndex] == '.') {
      ++rightIndex;
    }
    if (leftIndex >= static_cast<int>(left.length()) &&
        rightIndex >= static_cast<int>(right.length())) {
      break;
    }
  }
  return 0;
}

bool RouterDriverManifest::matchesFirmware(const String &firmware) const {
  if (!supportedFirmware || strlen(supportedFirmware) == 0) return true;
  if (firmware.isEmpty()) return true;
  return firmware.equalsIgnoreCase(supportedFirmware);
}

bool RouterDriverManifest::isVersionSupported(const String &version) const {
  if (!minimumVersion || strlen(minimumVersion) == 0) return true;
  if (version.isEmpty()) return true;
  return compareRouterVersions(version, minimumVersion) >= 0;
}

bool RouterDriverManifest::isSupported(const String &firmware,
                                       const String &version) const {
  return matchesFirmware(firmware) && isVersionSupported(version);
}

String RouterDriverManifest::unsupportedReason(const String &firmware,
                                               const String &version) const {
  if (!matchesFirmware(firmware)) {
    return String("Firmware '") + firmware + "' is not supported by the " +
           vendor + " driver";
  }
  if (!isVersionSupported(version)) {
    return String("Firmware version ") + version + " is below the minimum supported " +
           minimumVersion;
  }
  return "";
}

void RouterDriverManifest::toJson(JsonObject obj) const {
  obj["driverId"]          = driverId;
  obj["vendor"]            = vendor;
  obj["model"]             = model;
  if (productName && strlen(productName) > 0) {
    JsonObject product = obj["product"].to<JsonObject>();
    product["name"]     = productName;
    if (productSubtitle && strlen(productSubtitle) > 0) {
      product["subtitle"] = productSubtitle;
    }
  }
  obj["supportedFirmware"] = supportedFirmware;
  obj["minimumVersion"]    = minimumVersion;
  obj["stability"]         = stability == DriverStability::Stable ? "stable" : "experimental";
  obj["documentationUrl"]  = documentationUrl;
  obj["driverVersion"]     = driverVersion;

  JsonObject caps = obj["capabilities"].to<JsonObject>();
  capabilities.toJson(caps);

  JsonArray features = obj["supportedFeatures"].to<JsonArray>();
  if (supportedFeatures) {
    for (size_t i = 0; i < supportedFeatureCount; ++i) {
      if (supportedFeatures[i] && strlen(supportedFeatures[i]) > 0) {
        features.add(supportedFeatures[i]);
      }
    }
  }
}
