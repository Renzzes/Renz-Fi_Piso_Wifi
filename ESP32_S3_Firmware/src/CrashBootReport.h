#pragma once

#include <Arduino.h>

class Logger;
class StorageManager;

// One-shot boot reporter for unexpected resets (Guru / WDT / brownout / SW).
// Not a continuous monitor: runs once after storage+logger are ready, writes a
// single dated NDJSON history line (SD, or SPIFFS spool when SD is missing).
namespace CrashBootReport {

void reportPreviousReset(StorageManager *storage, Logger *logger);

}  // namespace CrashBootReport
