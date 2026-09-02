#pragma once

#include <Arduino.h>

class RouterOsClient;

bool gamingPriorityRouterSync(RouterOsClient &client, const String &requestJson,
                              const String &guestBridge, String &messageOut,
                              String &errorOut);
