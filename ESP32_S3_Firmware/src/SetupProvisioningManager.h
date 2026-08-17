#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "InstallationState.h"
#include "SetupStatusContext.h"

class AuthManager;
class EthernetManager;
class InstallationStateManager;
class SetupRouterConnectionManager;
class SetupWizardConfigManager;
class StorageManager;

// Owner account fields persisted at StoragePaths::ProvisioningFile.
// Installation lifecycle state is authoritative in InstallationStateManager
// (/config/installation.json) — this manager keeps owner metadata in sync.
class SetupProvisioningManager {
 public:
  static constexpr uint16_t SCHEMA_VERSION = 2;
  static constexpr uint32_t SETUP_UNLOCK_SESSION_MS = 20UL * 60UL * 1000UL;

  struct CreateOwnerInput {
    String displayName;
    String username;
    String password;
    String confirmPassword;
    String setupUnlockPassword;
    String confirmSetupUnlockPassword;
  };

  struct CreateOwnerResult {
    bool   success = false;
    int    httpStatus = 400;
    String errorCode;
    String errorMessage;
  };

  void begin(StorageManager *storage, AuthManager *auth,
             InstallationStateManager *installation,
             SetupRouterConnectionManager *routerConnection = nullptr,
             SetupWizardConfigManager *wizardConfig = nullptr);

  // Completes deferred owner SD commits off async_tcp (provisioning.json /
  // installation.json). Call from FirmwareApp::loop().
  void loop();

  bool load();
  bool persist();
  bool synchronizeAtBoot();

  bool   ownerCreated() const { return _ownerCreated; }
  bool   factoryResetInProgress() const { return _factoryResetInProgress; }
  bool   factoryResetCredentialsCleared() const;
  void   beginFactoryResetQuiesce();
  String ownerUsername() const { return _ownerUsername; }
  String ownerDisplayName() const { return _ownerDisplayName; }
  bool   setupUnlockConfigured() const { return !_setupUnlockPasswordHash.isEmpty(); }
  bool   hasActiveSetupUnlockSession() const;
  bool   requiresSetupUnlock() const;
  bool   isUnlockedReentry() const { return _setupReentrySession; }
  bool   verifySetupUnlockPassword(const String &password) const;
  /** Decrypts the RAM-resident protected blob. Never logs plaintext. */
  bool   recoverSetupUnlockPassword(String &outPlaintext) const;
  bool   unlockSetup(const String &password);
  void   lockSetup();
  // Clears unlock session and, for re-entry, restores Ready so setup locks again.
  bool   closeUnlockedSetup();
  // If a re-entry session expired, close it and restore Ready. Returns false when
  // the caller must treat setup as locked.
  bool   enforceActiveUnlockSession();
  bool   setSetupUnlockPassword(const String &password);
  /** Change unlock password after verifying the current one. Never logs plaintext. */
  bool   changeSetupUnlockPassword(const String &currentPassword,
                                   const String &newPassword,
                                   String &errorCodeOut);
  uint32_t setupUnlockRemainingMs() const;

  const char *wizardStepLabel() const;
  const char *wizardStepForPhase(bool applyJobActive,
                                 bool existingNetworkConfigured,
                                 bool wifiSelectionConfigured) const;

  CreateOwnerResult createOperator(const CreateOwnerInput &input);

  CreateOwnerResult createOwner(const CreateOwnerInput &input);

  void fillSetupStatus(JsonObject data, EthernetManager *eth,
                       const SetupStatusContext &ctx) const;

 private:
  StorageManager           *_storage       = nullptr;
  AuthManager              *_auth          = nullptr;
  InstallationStateManager *_installation  = nullptr;
  SetupRouterConnectionManager *_routerConnection = nullptr;
  SetupWizardConfigManager     *_wizardConfig     = nullptr;

  bool   _ownerCreated     = false;
  String _ownerUsername;
  String _ownerDisplayName;
  String _ownerPasswordHash;
  String _setupUnlockPasswordHash;
  String _setupUnlockPasswordProtected;
  uint32_t _createdAt      = 0;
  uint32_t _updatedAt      = 0;
  uint16_t _schemaVersion  = SCHEMA_VERSION;
  // In-memory only: temporary unlock after Setup Unlock Password (not persisted).
  bool     _setupReentrySession = false;
  uint32_t _setupUnlockSessionExpiresAt = 0;
  // Owner create: NVS + RAM commit on async_tcp; SD persist/sync deferred to loop().
  bool     _pendingOwnerDurableCommit = false;
  bool     _factoryResetInProgress    = false;

  void applyDefaults();
  void applyDocument(JsonObjectConst doc);
  void buildDocument(JsonDocument &doc) const;
  bool migrateDocument(JsonDocument &doc);
  bool syncInstallationState(InstallationState target);
  bool commitPendingOwnerDurableState();
  bool protectSetupUnlockPassword(const String &password);
  bool ensureFactoryUnlockProtect();
};
