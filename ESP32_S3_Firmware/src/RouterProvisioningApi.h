#pragma once

#include <ArduinoJson.h>

class RouterProvisioningManager;

// Thin API surface for production HTTP handlers — avoids including
// RouterProvisioningManager.h (and its RouterOS/wireless deps) in ApiServer.cpp.
void routerProvisioningFillNetworkModeStatus(RouterProvisioningManager *mgr,
                                             JsonObject dataOut);
void routerProvisioningFillWirelessStatus(RouterProvisioningManager *mgr,
                                          JsonObject dataOut);
