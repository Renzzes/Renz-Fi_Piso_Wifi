#pragma once

#include <Arduino.h>

class EventBus;
class PortalSessionManager;
class RouterProvisioningWorker;

namespace MemoryDiagnostics {

void begin(RouterProvisioningWorker *worker, EventBus *events,
             PortalSessionManager *portalSessions = nullptr);

// [mem] snapshot every 10s (heap + DMA + worker/event/inspection counters).
void periodicLog();

void setInspectionActive(bool active);
bool inspectionActive();

}  // namespace MemoryDiagnostics
