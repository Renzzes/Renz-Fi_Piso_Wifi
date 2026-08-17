#pragma once

#include "RenzFiDebug.h"

#if RENZFI_BURN_IN_DIAG

class EthernetManager;
class RouterPlatform;
class RouterProvisioningWorker;

// Periodic heap / task stack high-water logging for overnight stability runs.
// Compiled out of production firmware (RENZFI_BURN_IN_DIAG=0).
namespace BurnInDiagnostics {

void begin(RouterProvisioningWorker *routerWorker, EthernetManager *eth,
           RouterPlatform *router);
void loop();

}  // namespace BurnInDiagnostics

#endif  // RENZFI_BURN_IN_DIAG
