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

// Cheap loop hook: if DMA is already below the W5500 RX floor, drop SSE
// immediately (does not wait for the 10s mem log cadence).
void checkEthDmaQuiesce();

void setInspectionActive(bool active);
bool inspectionActive();

// True when any portal session is WaitingCoin, Activating, Active, etc.
bool hasOperationalPortalLoad();

// True when non-critical loop storage work should defer (portal load + tight DMA).
bool shouldDeferNonCriticalStorageWork();

}  // namespace MemoryDiagnostics
