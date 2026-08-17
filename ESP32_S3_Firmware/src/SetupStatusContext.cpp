#include "SetupStatusContext.h"

#include "RouterProvisioningManager.h"
#include "RouterProvisioningWorker.h"

SetupStatusContext buildSetupStatusContext(
    RouterProvisioningManager *routerProvisioning,
    RouterProvisioningWorker *routerWorker) {
  SetupStatusContext ctx;
  ctx.applyJobActive =
      routerWorker && routerWorker->hasActiveApplyJob();
  ctx.existingNetworkConfigured =
      routerProvisioning && routerProvisioning->isExistingNetworkAdopted();
  ctx.wifiSelectionConfigured =
      routerProvisioning && routerProvisioning->wifiSetupComplete();
  return ctx;
}
