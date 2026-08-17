#pragma once

#include <ArduinoJson.h>

#include "InstallationState.h"

class CoinManager;
class EventBus;
class InstallationStateManager;
class Logger;
class PortalConfigManager;
class RouterPlatform;
class StorageManager;

// Workflow-oriented setup orchestrator. Managers are internal — never exposed to REST or UI.
class ProvisioningEngine {
 public:
  void begin(StorageManager *storage, Logger *logger, EventBus *events,
             RouterPlatform *router, InstallationStateManager *installation,
             PortalConfigManager *portalConfig, CoinManager *coin = nullptr);

  // ── Installation lifecycle ───────────────────────────────────────────────
  bool beginInstallation(JsonObjectConst body, JsonDocument &out);
  bool resumeInstallation(JsonDocument &out);
  bool abortInstallation(JsonDocument &out);
  bool factoryReset(JsonDocument &out);

  // ── Router workflow ────────────────────────────────────────────────────────
  bool detectRouters(JsonDocument &out);
  bool selectDriver(JsonObjectConst body, JsonDocument &out);
  bool connectRouter(JsonObjectConst body, JsonDocument &out);
  bool listRouterProfiles(JsonDocument &out);

  // ── Appliance configuration ──────────────────────────────────────────────
  bool configurePortal(JsonObjectConst body, JsonDocument &out);
  bool configureCoin(JsonObjectConst body, JsonDocument &out);

  // ── Validation & finish ──────────────────────────────────────────────────
  bool validateInstallation(JsonDocument &out);
  bool finalizeInstallation(JsonDocument &out);

 private:
  StorageManager *_storage = nullptr;
  Logger *_logger = nullptr;
  EventBus *_events = nullptr;
  RouterPlatform *_router = nullptr;
  InstallationStateManager *_installation = nullptr;
  PortalConfigManager *_portalConfig = nullptr;
  CoinManager *_coin = nullptr;

  void attachWorkflowContext(JsonObject obj) const;
  void attachInstallationStatus(JsonObject obj) const;
  const char *workflowStepId() const;
  bool checkFirmwareCompatibility(JsonObjectConst body, JsonDocument &out) const;
  void fillInstallationSummary(JsonObject summary) const;
  void emitCompleted() const;
  void reportProgress(const char *step, const char *message) const;
};
