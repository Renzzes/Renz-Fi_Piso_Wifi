#include "ProvisioningEngine.h"

#include "CoinManager.h"
#include "Config.h"
#include "InstallationEvents.h"
#include "InstallationStateManager.h"
#include "InstallationSession.h"
#include "Logger.h"
#include "PortalConfigManager.h"
#include "router/RouterDriverManifest.h"
#include "router/RouterPlatform.h"
#include "StorageManager.h"

namespace {

const char *workflowStepForState(InstallationState state) {
  switch (state) {
    case InstallationState::Factory:
      return "welcome";
    case InstallationState::OwnerCreated:
      return "router_connection";
    case InstallationState::RouterConfigured:
      return "router_configuration";
    case InstallationState::Provisioned:
      return "ready";
    case InstallationState::RouterSelected:
      return "driver_selection";
    case InstallationState::RouterConnected:
      return "router_connection";
    case InstallationState::PortalConfigured:
      return "portal_configuration";
    case InstallationState::CoinConfigured:
      return "coin_configuration";
    case InstallationState::ValidationPassed:
      return "summary";
    case InstallationState::Ready:
      return "ready";
    default:
      return "welcome";
  }
}

void addCheck(JsonArray &checks, const char *id, bool passed, const char *detail) {
  JsonObject row = checks.add<JsonObject>();
  row["id"]     = id;
  row["passed"] = passed;
  row["detail"] = detail;
}

}  // namespace

void ProvisioningEngine::begin(StorageManager *storage, Logger *logger,
                               EventBus *events, RouterPlatform *router,
                               InstallationStateManager *installation,
                               PortalConfigManager *portalConfig,
                               CoinManager *coin) {
  _storage      = storage;
  _logger       = logger;
  _events       = events;
  _router       = router;
  _installation = installation;
  _portalConfig = portalConfig;
  _coin         = coin;
}

const char *ProvisioningEngine::workflowStepId() const {
  if (!_installation) return "welcome";
  return workflowStepForState(_installation->current());
}

void ProvisioningEngine::attachInstallationStatus(JsonObject obj) const {
  if (!_installation) return;
  DynamicJsonDocument installDoc(RenzFiConfig::JSON_DOC_SMALL);
  _installation->fillStatus(installDoc);
  obj["installation"].set(installDoc.as<JsonObjectConst>());
}

void ProvisioningEngine::attachWorkflowContext(JsonObject obj) const {
  attachInstallationStatus(obj);
  obj["workflowStep"] = workflowStepId();
  obj["ready"]        = _installation && _installation->isReady();
  obj["needsSetup"]   = _installation && _installation->needsSetup();
}

void ProvisioningEngine::reportProgress(const char *step,
                                        const char *message) const {
  if (_installation) {
    _installation->emitProgress(step, message);
  }
}

bool ProvisioningEngine::beginInstallation(JsonObjectConst body,
                                           JsonDocument &out) {
  if (!_installation) {
    out["ok"]    = false;
    out["error"] = "Installation unavailable";
    return false;
  }

  _installation->beginSession(body);
  reportProgress("welcome", "Installation workflow started");

  out["ok"]           = true;
  out["started"]      = true;
  out["alreadyReady"] = _installation->isReady();
  attachWorkflowContext(out.to<JsonObject>());

  if (_logger) {
    _logger->info("install", "Installation workflow started");
  }
  return true;
}

bool ProvisioningEngine::resumeInstallation(JsonDocument &out) {
  if (!_installation) {
    out["ok"]    = false;
    out["error"] = "Installation unavailable";
    return false;
  }

  _installation->resumeSession();
  reportProgress(workflowStepId(), "Resuming installation session");

  out["ok"]      = true;
  out["resumed"] = true;
  attachWorkflowContext(out.to<JsonObject>());

  const InstallationSession &session = _installation->session();
  if (session.active()) {
    out["sessionId"]        = session.sessionId;
    out["resumePrompt"]     = true;
    const uint32_t elapsedMs =
        session.lastActivity >= session.startedAt
            ? (session.lastActivity - session.startedAt)
            : 0;
    out["elapsedMinutes"] = elapsedMs / 60000U;
  }

  return true;
}

bool ProvisioningEngine::abortInstallation(JsonDocument &out) {
  if (!_installation) {
    out["ok"]    = false;
    out["error"] = "Installation unavailable";
    return false;
  }

  out["ok"]     = true;
  out["aborted"] = true;
  attachWorkflowContext(out.to<JsonObject>());

  if (_events) {
    DynamicJsonDocument payload(RenzFiConfig::JSON_DOC_SMALL);
    payload["state"] = installationStateLabel(_installation->current());
    _events->emit(InstallationEvents::Aborted, payload.as<String>());
  }

  if (_logger) {
    _logger->info("install", "Installation workflow aborted (state preserved)");
  }
  return true;
}

bool ProvisioningEngine::factoryReset(JsonDocument &out) {
  if (!_installation) {
    out["ok"]    = false;
    out["error"] = "Installation unavailable";
    return false;
  }

  if (!_installation->resetToFactory()) {
    out["ok"]    = false;
    out["error"] = "Unable to reset installation";
    return false;
  }

  out["ok"]     = true;
  out["reset"]  = true;
  attachWorkflowContext(out.to<JsonObject>());

  if (_logger) {
    _logger->info("install", "Installation factory reset");
  }
  return true;
}

bool ProvisioningEngine::detectRouters(JsonDocument &out) {
  if (!_router) {
    out["ok"]    = false;
    out["error"] = "Router subsystem unavailable";
    return false;
  }

  reportProgress("detect_routers", "Scanning for router drivers");

  JsonArray available = out["available"].to<JsonArray>();
  _router->availableDrivers(available);
  _router->detectDrivers(out);
  out["ok"]           = true;
  out["workflowStep"] = "router_detection";
  return true;
}

bool ProvisioningEngine::checkFirmwareCompatibility(JsonObjectConst body,
                                                    JsonDocument &out) const {
  if (!body["firmware"].is<const char *>() && !body["version"].is<const char *>()) {
    out["supported"] = true;
    out["skipped"]   = true;
    return true;
  }

  const String firmware = body["firmware"] | "";
  const String version  = body["version"] | "";
  String reason;
  const bool supported =
      _router->isFirmwareSupported(firmware, version, reason);

  out["supported"] = supported;
  out["firmware"]  = firmware;
  out["version"]   = version;
  if (!supported) {
    out["reason"] = reason;
  }
  return supported;
}

bool ProvisioningEngine::selectDriver(JsonObjectConst body, JsonDocument &out) {
  reportProgress("select_driver", "Selecting router driver");

  if (!_router || !_installation) {
    out["ok"]    = false;
    out["error"] = "Router subsystem unavailable";
    return false;
  }

  const String driverId = body["driverId"] | "";
  if (driverId.isEmpty()) {
    out["ok"]    = false;
    out["error"] = "driverId is required";
    return false;
  }

  if (!checkFirmwareCompatibility(body, out)) {
    out["ok"]    = false;
    out["error"] = out["reason"] | "Firmware not supported";
    return false;
  }

  if (!_router->switchDriver(driverId)) {
    out["ok"]    = false;
    out["error"] = "Unable to select router driver";
    return false;
  }

  if (!body.isNull() && body.size() > 0) {
    DynamicJsonDocument settings(RenzFiConfig::JSON_DOC_SMALL);
    for (JsonPairConst kv : body) {
      if (strcmp(kv.key().c_str(), "driverId") == 0 ||
          strcmp(kv.key().c_str(), "firmware") == 0 ||
          strcmp(kv.key().c_str(), "version") == 0) {
        continue;
      }
      settings[kv.key()] = kv.value();
    }
    if (settings.size() > 0 && !_router->save(settings.as<JsonObjectConst>())) {
      out["ok"]    = false;
      out["error"] = "Unable to save router settings";
      return false;
    }
  }

  _router->driverManifest(driverId).toJson(out["manifest"].to<JsonObject>());
  _installation->advanceTo(InstallationState::RouterSelected);

  out["ok"]        = true;
  out["driverId"]  = driverId;
  attachWorkflowContext(out.to<JsonObject>());
  return true;
}

bool ProvisioningEngine::connectRouter(JsonObjectConst body, JsonDocument &out) {
  reportProgress("connect_router", "Testing router connection");

  if (!_router || !_installation) {
    out["ok"]    = false;
    out["error"] = "Router subsystem unavailable";
    return false;
  }

  if (!body.isNull() && body.size() > 0) {
    if (!_router->save(body)) {
      out["ok"]    = false;
      out["error"] = "Unable to save router settings";
      return false;
    }
  }

  const bool connected = _router->test(body, out);
  if (connected) {
    _installation->advanceTo(InstallationState::RouterConnected);
  }

  out["ok"]         = connected;
  out["connected"]  = connected;
  attachWorkflowContext(out.to<JsonObject>());
  return connected;
}

bool ProvisioningEngine::listRouterProfiles(JsonDocument &out) {
  reportProgress("connect_router", "Loading router profiles");

  if (!_router) {
    out["ok"]    = false;
    out["error"] = "Router subsystem unavailable";
    return false;
  }

  const bool ok = _router->listProfiles(out);
  out["ok"] = ok;
  return ok;
}

bool ProvisioningEngine::configurePortal(JsonObjectConst body, JsonDocument &out) {
  reportProgress("configure_portal", "Applying portal configuration");

  (void)body;

  if (!_portalConfig || !_installation) {
    out["ok"]    = false;
    out["error"] = "Portal subsystem unavailable";
    return false;
  }

  if (!_portalConfig->loadMeta()) {
    out["ok"]    = false;
    out["error"] = "Unable to load portal configuration";
    return false;
  }

  DynamicJsonDocument branding(RenzFiConfig::JSON_DOC_SMALL);
  JsonObject brandingObj = branding.to<JsonObject>();
  if (!_portalConfig->fillBrandingJson(brandingObj, "/api/portal")) {
    out["ok"]    = false;
    out["error"] = "Portal branding verification failed";
    return false;
  }

  out["verified"]  = true;
  out["revision"]  = _portalConfig->revision();
  out["hasBanner"] = _portalConfig->hasCustomBanner();
  out["hasMusic"]  = _portalConfig->hasCustomMusic();

  _installation->advanceTo(InstallationState::PortalConfigured);

  out["ok"] = true;
  attachWorkflowContext(out.to<JsonObject>());
  return true;
}

bool ProvisioningEngine::configureCoin(JsonObjectConst body, JsonDocument &out) {
  reportProgress("configure_coin", "Configuring coin acceptor");

  if (!_installation) {
    out["ok"]    = false;
    out["error"] = "Installation unavailable";
    return false;
  }

  if (!RenzFiConfig::ENABLE_COIN_MANAGER || !_coin) {
    _installation->advanceTo(InstallationState::CoinConfigured);
    out["ok"]      = true;
    out["skipped"] = true;
    out["reason"]  = "Coin hardware disabled in firmware build";
    attachWorkflowContext(out.to<JsonObject>());
    return true;
  }

  if (!body.isNull() && body.size() > 0) {
    JsonObjectConst coinSettings =
        body["coin"].is<JsonObjectConst>() ? body["coin"].as<JsonObjectConst>()
                                           : body;
    if (!_coin->saveSettings(coinSettings)) {
      out["ok"]    = false;
      out["error"] = "Unable to save coin settings";
      return false;
    }
  }

  DynamicJsonDocument diagnostics(RenzFiConfig::JSON_DOC_SMALL);
  if (!_coin->diagnostics(diagnostics)) {
    out["ok"]    = false;
    out["error"] = "Coin diagnostics unavailable";
    return false;
  }

  out["hardware"].set(diagnostics.as<JsonObjectConst>());
  out["hardwareOk"] = !(_coin->isFault());

  if (_coin->isFault()) {
    out["ok"]    = false;
    out["error"] = "Coin hardware fault detected";
    attachWorkflowContext(out.to<JsonObject>());
    return false;
  }

  _installation->advanceTo(InstallationState::CoinConfigured);

  out["ok"] = true;
  attachWorkflowContext(out.to<JsonObject>());
  return true;
}

bool ProvisioningEngine::validateInstallation(JsonDocument &out) {
  reportProgress("validate", "Running installation checks");

  if (!_installation) {
    out["ok"]    = false;
    out["error"] = "Installation unavailable";
    return false;
  }

  JsonArray checks = out["checks"].to<JsonArray>();

  const bool routerReady =
      installationStateAtLeast(_installation->current(),
                               InstallationState::RouterConnected);
  addCheck(checks, "router_connected", routerReady,
           routerReady ? "Router connection verified"
                       : "Router connection step incomplete");

  bool portalOk = false;
  if (_portalConfig && _portalConfig->loadMeta()) {
    DynamicJsonDocument branding(RenzFiConfig::JSON_DOC_SMALL);
    portalOk = _portalConfig->fillBrandingJson(branding.to<JsonObject>(),
                                               "/api/portal");
  }
  addCheck(checks, "portal_configured", portalOk,
           portalOk ? "Portal configuration verified"
                    : "Portal configuration unavailable");

  bool coinOk = true;
  if (RenzFiConfig::ENABLE_COIN_MANAGER && _coin) {
    DynamicJsonDocument diagnostics(RenzFiConfig::JSON_DOC_SMALL);
    coinOk = _coin->diagnostics(diagnostics) && !_coin->isFault();
  }
  addCheck(checks, "coin_ready", coinOk,
           coinOk ? "Coin hardware ready" : "Coin hardware not ready");

  bool storageOk =
      _storage && (_storage->healthy() || _storage->usingFallback());
  addCheck(checks, "storage_healthy", storageOk,
           storageOk ? "Storage available (SD or SPIFFS fallback)"
                     : "Storage degraded or unavailable");

  bool allPassed = routerReady && portalOk && coinOk && storageOk;
  out["passed"]  = allPassed;
  out["ok"]      = allPassed;

  if (allPassed) {
    _installation->advanceTo(InstallationState::ValidationPassed);
  } else {
    out["error"] = "One or more installation checks failed";
  }

  attachWorkflowContext(out.to<JsonObject>());
  return allPassed;
}

void ProvisioningEngine::fillInstallationSummary(JsonObject summary) const {
  if (_router) {
    _router->profile().toJson(summary["router"].to<JsonObject>());
  }
  if (_portalConfig) {
    JsonObject portal = summary["portal"].to<JsonObject>();
    portal["revision"]  = _portalConfig->revision();
    portal["hasBanner"] = _portalConfig->hasCustomBanner();
    portal["hasMusic"]  = _portalConfig->hasCustomMusic();
  }
  if (_coin && RenzFiConfig::ENABLE_COIN_MANAGER) {
    _coin->fillCoinStatus(summary["coin"].to<JsonObject>());
  }
  summary["firmwareVersion"] = RenzFiConfig::FIRMWARE_VERSION;
}

void ProvisioningEngine::emitCompleted() const {
  if (!_events || !_installation) return;

  DynamicJsonDocument payload(RenzFiConfig::JSON_DOC_MEDIUM);
  JsonObject obj = payload.to<JsonObject>();
  attachWorkflowContext(obj);
  fillInstallationSummary(obj["summary"].to<JsonObject>());
  _events->emit(InstallationEvents::Completed, payload.as<String>());
}

bool ProvisioningEngine::finalizeInstallation(JsonDocument &out) {
  reportProgress("finish", "Finalizing installation");

  if (!_installation) {
    out["ok"]    = false;
    out["error"] = "Installation unavailable";
    return false;
  }

  if (!_installation->isReady()) {
    if (!installationStateAtLeast(_installation->current(),
                                  InstallationState::ValidationPassed)) {
      out["ok"]    = false;
      out["error"] = "Validation must pass before finishing installation";
      attachWorkflowContext(out.to<JsonObject>());
      return false;
    }
    _installation->advanceTo(InstallationState::Ready);
  }

  fillInstallationSummary(out["summary"].to<JsonObject>());
  out["ok"]      = true;
  out["finished"] = true;
  attachWorkflowContext(out.to<JsonObject>());

  emitCompleted();

  if (_logger) {
    _logger->info("install", "Installation finalized — appliance ready");
  }
  return true;
}
