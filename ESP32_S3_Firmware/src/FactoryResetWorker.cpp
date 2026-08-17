#include "FactoryResetWorker.h"

#include "AssetManager.h"
#include "AssetTypes.h"
#include "AuthManager.h"
#include "Config.h"
#include "InstallationState.h"
#include "InstallationStateManager.h"
#include "Logger.h"
#include "PortalConfigManager.h"
#include "RouterProvisioningManager.h"
#include "SetupProvisioningManager.h"
#include "SetupRouterConnectionManager.h"
#include "SetupWizardConfigManager.h"
#include "StorageManager.h"
#include "StoragePaths.h"

namespace {

constexpr uint32_t kRebootDelayMs = 1500;

const char *kResetFiles[] = {
    StoragePaths::SettingsFile,
    StoragePaths::PromosFile,
    StoragePaths::RouterFile,
    StoragePaths::WifiConfigFile,
    StoragePaths::VouchersFile,
    StoragePaths::SalesFile,
    StoragePaths::LogsFile,
    StoragePaths::UsersFile,
    StoragePaths::AdminSessionsFile,
    StoragePaths::PortalSessionsFile,
    StoragePaths::PortalConfigFile,
    StoragePaths::InstallationFile,
    StoragePaths::ProvisioningFile,
    StoragePaths::RouterConnectionFile,
    StoragePaths::RouterProvisioningFile,
    StoragePaths::RouterCacheFile,
    StoragePaths::ExistingNetworkScanFile,
    StoragePaths::SetupWizardFile,
    StoragePaths::NetworkAdoptionWorkflowFile,
    StoragePaths::AccessPointsFile,
    RenzFiConfig::PORTAL_BANNER_SD,
    RenzFiConfig::PORTAL_MUSIC_SD,
};

constexpr size_t kResetFileCount = sizeof(kResetFiles) / sizeof(kResetFiles[0]);

void deletePathWithTransactions(StorageManager *storage, const char *path) {
  if (!storage || !path) return;
  storage->deleteSdOnly(path);
  const String stage  = String(path) + StoragePaths::TransactionStageSuffix;
  const String backup = String(path) + StoragePaths::TransactionBackupSuffix;
  storage->deleteSdOnly(stage.c_str());
  storage->deleteSdOnly(backup.c_str());
}

}  // namespace

void FactoryResetWorker::begin(StorageManager *storage, Logger *logger,
                               AuthManager *auth, AssetManager *assets,
                               PortalConfigManager *portalConfig,
                               InstallationStateManager *installation,
                               SetupProvisioningManager *setupProvisioning,
                               SetupRouterConnectionManager *routerConnection,
                               SetupWizardConfigManager *wizardConfig,
                               RouterProvisioningManager *routerProvisioning) {
  _storage           = storage;
  _logger            = logger;
  _auth              = auth;
  _assets            = assets;
  _portalConfig      = portalConfig;
  _installation      = installation;
  _setupProvisioning = setupProvisioning;
  _routerConnection  = routerConnection;
  _wizardConfig      = wizardConfig;
  _routerProvisioning = routerProvisioning;
}

uint32_t FactoryResetWorker::enqueue() {
  if (busy()) return 0;
  _jobId      = _nextJobId++;
  if (_nextJobId == 0) _nextJobId = 1;
  _state      = State::Queued;
  _step       = Step::Quiesce;
  _fileIndex  = 0;
  _rebootAtMs = 0;
  _error      = "";
  // RAM-only. Cancel a queued durable commit the same moment busy() becomes
  // true so FirmwareApp cannot commit, and a worker persist() cannot rewrite
  // router-provisioning.json before Quiesce runs on loopTask.
  if (_routerProvisioning) _routerProvisioning->clearForFactoryReset();
  Serial.printf("[system] factory reset queued jobId=%u\n",
                static_cast<unsigned>(_jobId));
  return _jobId;
}

bool FactoryResetWorker::busy() const {
  return _state == State::Queued || _state == State::Running ||
         _state == State::Completed;
}

void FactoryResetWorker::fillSnapshot(Snapshot &out) const {
  out.jobId     = _jobId;
  out.status    = statusLabel();
  out.phase     = phaseLabel();
  out.progress  = progressPercent();
  out.rebooting = (_state == State::Completed);
  out.error     = _error.c_str();
}

bool FactoryResetWorker::poll(uint32_t jobId, Snapshot &out) const {
  if (jobId == 0 || jobId != _jobId) return false;
  fillSnapshot(out);
  return true;
}

const char *FactoryResetWorker::statusLabel() const {
  switch (_state) {
    case State::Queued:    return "queued";
    case State::Running:   return "running";
    case State::Completed: return "completed";
    case State::Failed:    return "failed";
    case State::Idle:
    default:               return "idle";
  }
}

const char *FactoryResetWorker::phaseLabel() const {
  if (_state == State::Queued) return "Preparing";
  if (_state == State::Completed) return "Rebooting";
  if (_state == State::Failed) return "Failed";
  if (_state != State::Running) return "";
  switch (_step) {
    case Step::Quiesce:
      return "Preparing";
    case Step::DeleteBanner:
    case Step::DeleteMusic:
      return "Resetting assets";
    case Step::ClearFallback:
    case Step::DeleteFiles:
    case Step::History:
    case Step::EnsureLayout:
      return "Resetting configuration";
    case Step::ClearRouterRam:
    case Step::ClearWizardRam:
      return "Resetting credentials";
    case Step::ReloadPortal:
      return "Restoring defaults";
    case Step::ResetInstallation:
    case Step::Validate:
      return "Validating";
    case Step::Complete:
      return "Rebooting";
  }
  return "";
}

uint8_t FactoryResetWorker::progressPercent() const {
  if (_state == State::Idle) return 0;
  if (_state == State::Queued) return 1;
  if (_state == State::Completed) return 100;
  if (_state == State::Failed) return 0;
  const uint8_t stepCount = static_cast<uint8_t>(Step::Complete) + 1;
  uint8_t base = static_cast<uint8_t>(
      (static_cast<uint16_t>(_step) * 99U) / stepCount);
  if (_step == Step::DeleteFiles && kResetFileCount > 0) {
    const uint8_t span = 99U / stepCount;
    base = static_cast<uint8_t>(
        base + (span * _fileIndex) / kResetFileCount);
  }
  return base;
}

void FactoryResetWorker::fail(const char *message) {
  _state = State::Failed;
  _error = message ? message : "Factory reset failed";
  if (_logger) _logger->error("system", _error);
  Serial.printf("[system] factory reset failed jobId=%u\n",
                static_cast<unsigned>(_jobId));
}

void FactoryResetWorker::advance() {
  const uint8_t next = static_cast<uint8_t>(_step) + 1;
  _step = static_cast<Step>(next);
}

void FactoryResetWorker::loop() {
  if (_state == State::Queued) {
    _state = State::Running;
    if (_logger) _logger->info("system", "factory reset started");
    Serial.println("[system] factory reset started");
  }
  if (_state == State::Running) {
    runStep();
  }
  if (_state == State::Completed && _rebootAtMs != 0 &&
      static_cast<int32_t>(millis() - _rebootAtMs) >= 0) {
    Serial.println("[system] factory reset rebooting");
    // Drop remaining owner sessions immediately before restart. Status
    // polling already observed `completed` during the reboot delay.
    if (_auth) _auth->resetToDefault(true);
    ESP.restart();
  }
}

void FactoryResetWorker::runStep() {
  switch (_step) {
    case Step::Quiesce:
      // Cancel persist + clear provisioning RAM BEFORE any file delete.
      if (_routerProvisioning) _routerProvisioning->clearForFactoryReset();
      if (_routerConnection) _routerConnection->clearForFactoryReset();
      if (_setupProvisioning) _setupProvisioning->beginFactoryResetQuiesce();
      if (_auth) _auth->resetToDefault(false);
      advance();
      return;

    case Step::DeleteBanner:
      if (_assets) _assets->deleteAsset(AssetType::Banner);
      advance();
      return;

    case Step::DeleteMusic:
      if (_assets) _assets->deleteAsset(AssetType::Music);
      advance();
      return;

    case Step::ClearFallback:
      if (_storage) _storage->clearAllFallbackData();
      advance();
      return;

    case Step::DeleteFiles:
      if (_storage && _fileIndex < kResetFileCount) {
        deletePathWithTransactions(_storage, kResetFiles[_fileIndex]);
        _fileIndex++;
        return;
      }
      advance();
      return;

    case Step::History: {
      if (!_storage) {
        advance();
        return;
      }
      bool done = false;
      if (!_storage->factoryResetHistoryTick(done)) {
        fail("Unable to reset history storage");
        return;
      }
      if (done) advance();
      return;
    }

    case Step::EnsureLayout:
      if (_storage) _storage->ensureLayout();
      advance();
      return;

    case Step::ClearRouterRam:
      if (_routerConnection) _routerConnection->clearForFactoryReset();
      if (_routerProvisioning) _routerProvisioning->clearForFactoryReset();
      advance();
      return;

    case Step::ClearWizardRam:
      if (_wizardConfig) _wizardConfig->clearForFactoryReset();
      advance();
      return;

    case Step::ReloadPortal:
      if (_portalConfig) _portalConfig->loadMeta();
      if (_assets) _assets->loadMetadata();
      advance();
      return;

    case Step::ResetInstallation:
      if (_installation && !_installation->resetToFactory()) {
        fail("Unable to reset installation state");
        return;
      }
      advance();
      return;

    case Step::Validate: {
      const bool ownerGone =
          !_setupProvisioning || !_setupProvisioning->ownerCreated();
      const bool unlockCleared =
          _setupProvisioning &&
          _setupProvisioning->factoryResetCredentialsCleared();
      const bool factoryState =
          _installation &&
          _installation->current() == InstallationState::Factory;
      const bool sessionGone =
          !_setupProvisioning ||
          !_setupProvisioning->hasActiveSetupUnlockSession();
      const bool wifiSelectionGone =
          !_routerProvisioning ||
          !_routerProvisioning->wifiSelectionConfigured();
      if (!ownerGone || !unlockCleared || !factoryState || !sessionGone ||
          !wifiSelectionGone) {
        fail("Factory reset validation failed");
        return;
      }
      advance();
      return;
    }

    case Step::Complete:
      if (_logger) _logger->info("system", "factory reset completed");
      Serial.println("[system] factory reset completed");
      _state      = State::Completed;
      _rebootAtMs = millis() + kRebootDelayMs;
      return;
  }
}
