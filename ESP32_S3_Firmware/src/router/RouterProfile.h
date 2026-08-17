#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "RouterCapabilities.h"

// Standard router description returned by RouterPlatform and drivers.
struct RouterProfile {
  String vendor;
  String model;
  String firmware;
  String identity;
  String ipAddress;
  String username;
  String driverId;
  String connectionType;
  String apiVersion;
  String status;

  RouterCapabilities capabilities;

  void toJson(JsonObject obj) const {
    obj["vendor"]         = vendor;
    obj["model"]          = model;
    obj["firmware"]       = firmware;
    obj["identity"]       = identity;
    obj["ipAddress"]      = ipAddress;
    obj["username"]       = username;
    obj["driverId"]       = driverId;
    obj["connectionType"] = connectionType;
    obj["apiVersion"]     = apiVersion;
    obj["status"]         = status;
    JsonObject caps = obj["capabilities"].to<JsonObject>();
    capabilities.toJson(caps);
  }

  void fromJson(JsonObjectConst obj) {
    vendor         = obj["vendor"] | "";
    model          = obj["model"] | "";
    firmware       = obj["firmware"] | "";
    identity       = obj["identity"] | "";
    ipAddress      = obj["ipAddress"] | "";
    username       = obj["username"] | "";
    driverId       = obj["driverId"] | "";
    connectionType = obj["connectionType"] | "";
    apiVersion     = obj["apiVersion"] | "";
    status         = obj["status"] | "";
    if (!obj["capabilities"].isNull()) {
      JsonObjectConst caps = obj["capabilities"];
      capabilities.supportsVoucherControl = caps["supportsVoucherControl"] | false;
      capabilities.supportsBandwidthLimit = caps["supportsBandwidthLimit"] | false;
      capabilities.supportsHotspot        = caps["supportsHotspot"] | false;
      capabilities.supportsApi            = caps["supportsApi"] | false;
      capabilities.supportsIdentity       = caps["supportsIdentity"] | false;
      capabilities.supportsHealth         = caps["supportsHealth"] | false;
      capabilities.supportsStatistics     = caps["supportsStatistics"] | false;
      capabilities.supportsRemoteConfig   = caps["supportsRemoteConfig"] | false;
    }
  }
};
