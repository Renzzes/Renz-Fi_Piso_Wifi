#pragma once

#include <ArduinoJson.h>

#include "InstallationSession.h"
#include "InstallationState.h"

class EventBus;
class Logger;
class StorageManager;

// Lightweight persisted installation progress for setup wizard and recovery.
class InstallationStateManager {
 public:
  void begin(StorageManager *storage, Logger *logger, EventBus *events);

  void setDeviceId(const String &deviceId);

  InstallationState current() const { return _state; }
  InstallationState nextState() const { return installationNextState(_state); }
  InstallationState previousState() const {
    return installationPreviousState(_state);
  }

  const InstallationSession &session() const { return _session; }

  uint32_t updatedAt() const { return _updatedAt; }
  uint16_t installationVersion() const { return _installationVersion; }
  const String &firmwareVersion() const { return _firmwareVersion; }

  bool isReady() const {
    return _state == InstallationState::Ready ||
           _state == InstallationState::Provisioned;
  }
  bool needsSetup() const { return !isReady(); }
  uint8_t progressPercent() const;

  bool load();
  bool setState(InstallationState state);
  bool advanceTo(InstallationState state);
  bool resetToFactory();
  // Re-opens the setup wizard after production (factory reset alternative).
  bool reopenSetupWizard();

  bool beginSession(JsonObjectConst options = JsonObject());
  bool resumeSession();
  void touchSession();
  void clearSession();

  void fillStatus(JsonDocument &doc) const;
  void fillSession(JsonObject obj) const;

  void emitProgress(const char *step, const char *message) const;

 private:
  StorageManager *_storage = nullptr;
  Logger *_logger = nullptr;
  EventBus *_events = nullptr;

  InstallationState _state = InstallationState::Factory;
  InstallationSession _session;
  String _deviceId;
  uint32_t _updatedAt = 0;
  uint16_t _installationVersion = INSTALLATION_SCHEMA_VERSION;
  String _firmwareVersion;

  bool persist();
  bool migrateDocument(JsonDocument &doc);
  void applyDocument(JsonObjectConst doc);
  void fillCompletedSteps(JsonArray &out) const;
  InstallationState inferFromStorage() const;
  void emitStateChanged() const;
  void ensureSessionDefaults();
};
