#include "InstallationStateManager.h"

#include "Config.h"
#include "EventBus.h"
#include "FinishTrace.h"
#include "InstallationEvents.h"
#include "Logger.h"
#include "StorageManager.h"
#include "StoragePaths.h"

void InstallationStateManager::begin(StorageManager *storage, Logger *logger,
                                     EventBus *events) {
  _storage = storage;
  _logger  = logger;
  _events  = events;
  load();
}

void InstallationStateManager::setDeviceId(const String &deviceId) {
  _deviceId = deviceId;
  if (_session.active() && _session.deviceId.isEmpty()) {
    _session.deviceId = deviceId;
    persist();
  }
}

uint8_t InstallationStateManager::progressPercent() const {
  return installationStateProgress(_state);
}

void InstallationStateManager::fillCompletedSteps(JsonArray &out) const {
  out.clear();
  if (installationStateAtLeast(_state, InstallationState::RouterSelected)) {
    out.add(InstallationSteps::Router);
  }
  if (installationStateAtLeast(_state, InstallationState::PortalConfigured)) {
    out.add(InstallationSteps::Portal);
  }
  if (installationStateAtLeast(_state, InstallationState::CoinConfigured)) {
    out.add(InstallationSteps::Coin);
  }
  if (installationStateAtLeast(_state, InstallationState::ValidationPassed)) {
    out.add(InstallationSteps::Validation);
  }
}

void InstallationStateManager::ensureSessionDefaults() {
  if (_session.active()) return;

  const uint32_t now = millis();
  _session.sessionId    = generateInstallationSessionId(_deviceId);
  _session.startedAt    = now;
  _session.lastActivity = now;
  _session.deviceId     = _deviceId;
  _session.resumeToken  = generateInstallationResumeToken(_session.sessionId);
  _session.attempt      = 1;
  _session.isRecovery   = false;
}

bool InstallationStateManager::migrateDocument(JsonDocument &doc) {
  uint16_t version = doc["installationVersion"] | 0U;
  bool changed     = false;

  if (version < 1) {
    if (!doc["state"].is<const char *>()) {
      doc["state"] = installationStateLabel(InstallationState::Factory);
    }
    if (!doc["updatedAt"].is<uint32_t>()) {
      doc["updatedAt"] = millis();
    }

    JsonArray steps = doc["completedSteps"].to<JsonArray>();
    if (steps.isNull() || steps.size() == 0) {
      steps.clear();
      const InstallationState state =
          parseInstallationState(doc["state"] | "factory");
      if (installationStateAtLeast(state, InstallationState::RouterSelected)) {
        steps.add(InstallationSteps::Router);
      }
      if (installationStateAtLeast(state, InstallationState::PortalConfigured)) {
        steps.add(InstallationSteps::Portal);
      }
      if (installationStateAtLeast(state, InstallationState::CoinConfigured)) {
        steps.add(InstallationSteps::Coin);
      }
      if (installationStateAtLeast(state, InstallationState::ValidationPassed)) {
        steps.add(InstallationSteps::Validation);
      }
    }
    changed = true;
    version = 1;
  }

  if (version < 2) {
    JsonObject session = doc["session"].to<JsonObject>();
    if (session.isNull() || !session["sessionId"].is<const char *>()) {
      const uint32_t updatedAt = doc["updatedAt"] | millis();
      const String sessionId   = generateInstallationSessionId("");
      session["sessionId"]     = sessionId;
      session["startedAt"]     = updatedAt;
      session["lastActivity"]  = updatedAt;
      session["installerName"] = "";
      session["deviceId"]      = "";
      session["resumeToken"]   = generateInstallationResumeToken(sessionId);
      session["isRecovery"]    = false;
      session["attempt"]       = 1;
    }
    changed = true;
    version = 2;
  }

  doc["firmwareVersion"]     = RenzFiConfig::FIRMWARE_VERSION;
  doc["installationVersion"] = INSTALLATION_SCHEMA_VERSION;
  return changed;
}

void InstallationStateManager::applyDocument(JsonObjectConst doc) {
  _state               = parseInstallationState(doc["state"] | "factory");
  _updatedAt           = doc["updatedAt"] | 0U;
  _installationVersion = doc["installationVersion"] | INSTALLATION_SCHEMA_VERSION;
  _firmwareVersion     = doc["firmwareVersion"] | RenzFiConfig::FIRMWARE_VERSION;

  if (!doc["session"].isNull()) {
    _session.fromJson(doc["session"]);
  } else {
    _session.clear();
  }
}

bool InstallationStateManager::load() {
  if (!_storage) return false;

  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_SMALL);
  if (!_storage->readJson(StoragePaths::InstallationFile, doc)) {
    _state               = inferFromStorage();
    _updatedAt           = millis();
    _installationVersion = INSTALLATION_SCHEMA_VERSION;
    _firmwareVersion     = RenzFiConfig::FIRMWARE_VERSION;
    ensureSessionDefaults();
    persist();
    if (_logger) {
      _logger->info("install",
                    String("Installation state inferred: ") +
                        installationStateLabel(_state));
    }
    return true;
  }

  const bool migrated = migrateDocument(doc);
  applyDocument(doc.as<JsonObjectConst>());
  if (migrated) {
    persist();
    if (_logger) {
      _logger->info("install",
                    String("Installation schema migrated to v") +
                        String(_installationVersion));
    }
  }
  return true;
}

InstallationState InstallationStateManager::inferFromStorage() const {
  if (!_storage) return InstallationState::Factory;

  DynamicJsonDocument router(RenzFiConfig::JSON_DOC_SMALL);
  if (!_storage->readJson(RenzFiConfig::ROUTER_FILE, router)) {
    return InstallationState::Factory;
  }

  const String host     = router["host"] | "";
  const String username = router["username"] | "";
  const String password = router["password"] | "";
  if (host.isEmpty() || username.isEmpty() || password.isEmpty()) {
    return InstallationState::Factory;
  }

  DynamicJsonDocument portal(RenzFiConfig::JSON_DOC_SMALL);
  if (!_storage->readJson(RenzFiConfig::PORTAL_CONFIG_FILE, portal)) {
    return InstallationState::RouterSelected;
  }

  const bool portalConfigured = (portal["revision"] | 0) > 0 ||
                                (portal["hasBanner"] | false) ||
                                (portal["hasMusic"] | false);
  if (!portalConfigured) {
    return InstallationState::RouterSelected;
  }

  DynamicJsonDocument settings(RenzFiConfig::JSON_DOC_SMALL);
  if (!_storage->readJson(RenzFiConfig::SETTINGS_FILE, settings)) {
    return InstallationState::PortalConfigured;
  }

  if (!(settings["coin"]["enabled"] | true)) {
    return InstallationState::PortalConfigured;
  }

  return InstallationState::Ready;
}

bool InstallationStateManager::persist() {
  if (!_storage) return false;

  FinishTrace::BlockingOpScope op(FinishTrace::pipelineActive()
                                      ? FinishTrace::BlockingOpConfig{
                                            "persist()",
                                            "file write",
                                            0,
                                            0,
                                            "installation state persist",
                                            "writing"}
                                      : FinishTrace::BlockingOpConfig{""});

  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_SMALL);
  doc["state"]               = installationStateLabel(_state);
  doc["updatedAt"]           = _updatedAt;
  doc["firmwareVersion"]     = RenzFiConfig::FIRMWARE_VERSION;
  doc["installationVersion"] = INSTALLATION_SCHEMA_VERSION;

  JsonArray steps = doc["completedSteps"].to<JsonArray>();
  fillCompletedSteps(steps);

  JsonObject session = doc["session"].to<JsonObject>();
  _session.toJson(session);

  _firmwareVersion     = RenzFiConfig::FIRMWARE_VERSION;
  _installationVersion = INSTALLATION_SCHEMA_VERSION;
  const bool ok = _storage->writeJson(StoragePaths::InstallationFile, doc);
  if (!ok && FinishTrace::pipelineActive()) {
    op.fail(_storage->lastError().c_str());
  }
  return ok;
}

void InstallationStateManager::touchSession() {
  if (!_session.active()) return;
  _session.lastActivity = millis();
  persist();
}

bool InstallationStateManager::beginSession(JsonObjectConst options) {
  const uint32_t now = millis();

  if (_session.active() && _state != InstallationState::Ready) {
    if (!options.isNull()) {
      if (options["installerName"].is<const char *>()) {
        _session.installerName = options["installerName"].as<const char *>();
      }
      if (options["isRecovery"].is<bool>()) {
        _session.isRecovery = options["isRecovery"].as<bool>();
      }
    }
    _session.lastActivity = now;
    if (_session.deviceId.isEmpty()) {
      _session.deviceId = _deviceId;
    }
    return persist();
  }

  _session.clear();
  _session.sessionId     = generateInstallationSessionId(_deviceId);
  _session.startedAt     = now;
  _session.lastActivity  = now;
  _session.deviceId      = _deviceId;
  _session.resumeToken   = generateInstallationResumeToken(_session.sessionId);
  _session.attempt       = 1;
  _session.isRecovery    = options["isRecovery"] | false;
  _session.installerName = options["installerName"] | "";

  if (_logger) {
    _logger->info("install",
                  String("Installation session started: ") + _session.sessionId);
  }
  return persist();
}

bool InstallationStateManager::resumeSession() {
  if (!_session.active()) {
    ensureSessionDefaults();
  }
  touchSession();
  return true;
}

void InstallationStateManager::clearSession() {
  _session.clear();
}

bool InstallationStateManager::setState(InstallationState state) {
  FinishTrace::BlockingOpScope op(FinishTrace::pipelineActive()
                                      ? FinishTrace::BlockingOpConfig{
                                            "setState()",
                                            "file write",
                                            0,
                                            0,
                                            "installation state transition",
                                            "persisting"}
                                      : FinishTrace::BlockingOpConfig{""});
  if (_state == state) {
    touchSession();
    return true;
  }

  const InstallationState previous = _state;
  _state     = state;
  _updatedAt = millis();
  touchSession();

  if (!persist()) {
    if (FinishTrace::pipelineActive()) op.fail("persist failed");
    return false;
  }

  if (_logger) {
    _logger->info("install",
                  String("Installation state -> ") + installationStateLabel(_state));
  }
  emitStateChanged();
  return true;
}

bool InstallationStateManager::advanceTo(InstallationState state) {
  if (installationStateIndex(state) < installationStateIndex(_state)) {
    return false;
  }
  return setState(state);
}

bool InstallationStateManager::resetToFactory() {
  _installationVersion = INSTALLATION_SCHEMA_VERSION;
  _firmwareVersion     = RenzFiConfig::FIRMWARE_VERSION;
  clearSession();
  return setState(InstallationState::Factory);
}

bool InstallationStateManager::reopenSetupWizard() {
  if (!isReady()) return false;
  clearSession();
  return setState(InstallationState::RouterConfigured);
}

void InstallationStateManager::fillSession(JsonObject obj) const {
  _session.toJson(obj);
  if (_session.active() && _session.startedAt > 0) {
    const uint32_t elapsedMs =
        (_session.lastActivity >= _session.startedAt)
            ? (_session.lastActivity - _session.startedAt)
            : 0;
    obj["elapsedMs"]       = elapsedMs;
    obj["elapsedMinutes"]  = elapsedMs / 60000U;
  }
}

void InstallationStateManager::fillStatus(JsonDocument &doc) const {
  doc["state"]               = installationStateLabel(_state);
  doc["updatedAt"]           = _updatedAt;
  doc["firmwareVersion"]     = _firmwareVersion;
  doc["installationVersion"] = _installationVersion;
  doc["progressPercent"]     = progressPercent();
  doc["stepIndex"]           = installationStateIndex(_state);
  doc["stepCount"]           = installationStateCount();
  doc["needsSetup"]          = needsSetup();
  doc["ready"]               = isReady();

  doc["nextState"]     = installationStateLabel(nextState());
  doc["previousState"] = installationStateLabel(previousState());

  JsonArray steps = doc["completedSteps"].to<JsonArray>();
  fillCompletedSteps(steps);

  fillSession(doc["session"].to<JsonObject>());
}

void InstallationStateManager::emitStateChanged() const {
  if (!_events) return;

  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_SMALL);
  JsonObject obj = doc.to<JsonObject>();
  obj["state"]           = installationStateLabel(_state);
  obj["nextState"]       = installationStateLabel(nextState());
  obj["progressPercent"] = progressPercent();
  if (_session.active()) {
    obj["sessionId"] = _session.sessionId;
  }
  _events->emit(InstallationEvents::StateChanged, doc.as<String>());
}

void InstallationStateManager::emitProgress(const char *step,
                                            const char *message) const {
  if (!_events) return;

  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_SMALL);
  JsonObject obj = doc.to<JsonObject>();
  obj["step"]    = step;
  obj["percent"] = progressPercent();
  obj["message"] = message;
  if (_session.active()) {
    obj["sessionId"] = _session.sessionId;
  }
  _events->emit(InstallationEvents::Progress, doc.as<String>());
}
