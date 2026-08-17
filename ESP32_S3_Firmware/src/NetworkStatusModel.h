#pragma once

#include <ArduinoJson.h>

class EthernetManager;
class ManagementApManager;
class ManagementApLifecycle;

namespace NetworkStatusModel {

/** Unified network status envelope for /api/system/network. */
void fill(JsonObject out,
          EthernetManager *eth,
          ManagementApManager *mgmtAp,
          ManagementApLifecycle *lifecycle = nullptr);

}  // namespace NetworkStatusModel
