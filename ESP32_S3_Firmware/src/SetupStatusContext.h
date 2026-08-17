#pragma once

class RouterProvisioningManager;
class RouterProvisioningWorker;

// Inputs to SetupProvisioningManager::wizardStepForPhase / fillSetupStatus.
// Callers must derive this explicitly — omitting flags produces stale wizardStep.
struct SetupStatusContext {
  bool applyJobActive             = false;
  bool existingNetworkConfigured  = false;
  bool wifiSelectionConfigured    = false;
};

// Live lifecycle context for status polling. When routerWorker is non-null,
// applyJobActive reflects an in-flight ApplyConfiguration job.
SetupStatusContext buildSetupStatusContext(
    RouterProvisioningManager *routerProvisioning,
    RouterProvisioningWorker *routerWorker = nullptr);
