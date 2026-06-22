#pragma once

#include <Arduino.h>

// Boot-time SPIFFS inventory and path resolution for the React production build.
void logSpiffsInventory();

// Maps an HTTP path to a SPIFFS file path. Returns empty string for API paths.
// Sets *gzipOut when the served file is the .gz variant.
String resolveSpiffsServePath(const String &requestPath, bool *gzipOut);
