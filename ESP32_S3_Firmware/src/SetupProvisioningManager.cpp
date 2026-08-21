#include "SetupProvisioningManager.h"

#include <cstring>

#include "AuthCredentials.h"
#include "AuthManager.h"
#include "Config.h"
#include "CredentialProtector.h"
#include "EthernetManager.h"
#include "InstallationStateManager.h"
#include "StorageManager.h"
#include "SetupRouterConnectionManager.h"
#include "SetupWizardConfigManager.h"

namespace {

constexpr size_t kDocCapacity = RenzFiConfig::JSON_DOC_SMALL;
constexpr const char *kDefaultSetupUnlockPassword = "renzfi-setup";

String trimCopy(const String &value) {
  String out = value;
  out.trim();
  return out;
}

bool isValidUsername(const String &username) {
  if (username.length() < 3 || username.length() > 32) return false;
  for (size_t i = 0; i < username.length(); ++i) {
    const char c = username.charAt(i);
    const bool ok =
        (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

InstallationState installationStateFromLegacyLabel(const char *label) {
  return parseInstallationState(label);
}

}  // namespace

void SetupProvisioningManager::begin(StorageManager *storage, AuthManager *auth,
                                     InstallationStateManager *installation,
                                     SetupRouterConnectionManager *routerConnection,
                                     SetupWizardConfigManager *wizardConfig) {
  _storage           = storage;
  _auth              = auth;
  _installation      = installation;
  _routerConnection  = routerConnection;
  _wizardConfig      = wizardConfig;
  load();
}

void SetupProvisioningManager::loop() {
  if (_factoryResetInProgress) return;
  if (!_pendingOwnerDurableCommit) return;
  if (commitPendingOwnerDurableState()) {
    _pendingOwnerDurableCommit = false;
    Serial.println("[setup] deferred owner durable commit complete");
  }
}

bool SetupProvisioningManager::commitPendingOwnerDurableState() {
  if (!persist()) {
    Serial.println("[setup] deferred owner persist failed; will retry");
    return false;
  }
  if (!syncInstallationState(InstallationState::OwnerCreated)) {
    Serial.println("[setup] deferred installation sync failed; will retry");
    return false;
  }
  return true;
}

void SetupProvisioningManager::applyDefaults() {
  _ownerCreated      = false;
  _ownerUsername     = "";
  _ownerDisplayName  = "";
  _ownerPasswordHash = "";
  _setupUnlockPasswordHash = AuthCredentials::hashPassword(kDefaultSetupUnlockPassword);
  _setupUnlockPasswordProtected = "";
  _createdAt         = 0;
  _updatedAt         = 0;
  _schemaVersion     = SCHEMA_VERSION;
  _setupUnlockSessionExpiresAt = 0;
}

bool SetupProvisioningManager::migrateDocument(JsonDocument &doc) {
  bool changed = false;
  uint16_t version = doc["schemaVersion"] | 0U;

  if (version < 1) {
    if (!doc["ownerCreated"].is<bool>()) {
      doc["ownerCreated"] = false;
    }
    if (!doc["createdAt"].is<uint32_t>()) {
      doc["createdAt"] = millis();
    }
    if (!doc["updatedAt"].is<uint32_t>()) {
      doc["updatedAt"] = millis();
    }
    doc["schemaVersion"] = 1;
    changed              = true;
    version              = 1;
  }

  if (version < 2) {
    // Phase 2B: installationState lives only in installation.json.
    if (doc.containsKey("installationState")) {
      doc.remove("installationState");
      changed = true;
    }
    doc["schemaVersion"] = SCHEMA_VERSION;
    changed              = true;
  }

  return changed;
}

void SetupProvisioningManager::applyDocument(JsonObjectConst doc) {
  _ownerCreated      = doc["ownerCreated"] | false;
  _ownerUsername     = doc["ownerUsername"] | "";
  _ownerDisplayName  = doc["ownerDisplayName"] | "";
  _ownerPasswordHash = doc["ownerPasswordHash"] | "";
  _setupUnlockPasswordHash = doc["setupUnlockPasswordHash"] | "";
  _setupUnlockPasswordProtected = doc["setupUnlockPasswordProtected"] | "";
  if (_setupUnlockPasswordHash.isEmpty()) {
    _setupUnlockPasswordHash = AuthCredentials::hashPassword(kDefaultSetupUnlockPassword);
  }
  _createdAt         = doc["createdAt"] | 0U;
  _updatedAt         = doc["updatedAt"] | 0U;
  _schemaVersion     = doc["schemaVersion"] | SCHEMA_VERSION;

  // Phase 2A migration: legacy installationState field in provisioning.json.
  if (doc.containsKey("installationState")) {
    const char *legacy = doc["installationState"] | "factory";
    if (_ownerCreated &&
        installationStateFromLegacyLabel(legacy) >= InstallationState::OwnerCreated) {
      // ownerCreated flag is authoritative; installation sync happens at boot.
    }
  }
}

void SetupProvisioningManager::buildDocument(JsonDocument &doc) const {
  doc.clear();
  doc["schemaVersion"]    = _schemaVersion;
  doc["ownerCreated"]     = _ownerCreated;
  doc["ownerUsername"]    = _ownerUsername;
  doc["ownerDisplayName"] = _ownerDisplayName;
  doc["ownerPasswordHash"] = _ownerPasswordHash;
  doc["setupUnlockPasswordHash"] = _setupUnlockPasswordHash;
  doc["setupUnlockPasswordProtected"] = _setupUnlockPasswordProtected;
  doc["createdAt"]        = _createdAt;
  doc["updatedAt"]        = _updatedAt;
}

bool SetupProvisioningManager::hasActiveSetupUnlockSession() const {
  if (!_setupReentrySession) return false;
  return setupUnlockRemainingMs() > 0;
}

bool SetupProvisioningManager::requiresSetupUnlock() const {
  // Re-entry after a finished install, or currently inside an unlocked re-entry
  // session (installation may be temporarily reopened away from Ready).
  if (_setupReentrySession) return true;
  if (_installation && _installation->isReady()) return true;
  // Owner already exists but setup is incomplete (power loss / SD recovery).
  // Do not offer owner creation again — require the Setup Unlock Password.
  if (_ownerCreated) return true;
  return false;
}

bool SetupProvisioningManager::verifySetupUnlockPassword(const String &password) const {
  if (_setupUnlockPasswordHash.isEmpty()) return false;
  return AuthCredentials::hashPassword(password) == _setupUnlockPasswordHash;
}

bool SetupProvisioningManager::protectSetupUnlockPassword(const String &password) {
  String blob;
  if (!CredentialProtector::protectSecret(password, blob) || blob.isEmpty()) {
    return false;
  }
  _setupUnlockPasswordProtected = blob;
  return true;
}

bool SetupProvisioningManager::ensureFactoryUnlockProtect() {
  if (_setupUnlockPasswordProtected.length() > 0) return false;
  if (_setupUnlockPasswordHash !=
      AuthCredentials::hashPassword(kDefaultSetupUnlockPassword)) {
    return false;
  }
  return protectSetupUnlockPassword(kDefaultSetupUnlockPassword);
}

bool SetupProvisioningManager::recoverSetupUnlockPassword(String &outPlaintext) const {
  outPlaintext = "";
  if (_setupUnlockPasswordProtected.length() > 0) {
    String recovered;
    if (CredentialProtector::unprotectSecret(_setupUnlockPasswordProtected,
                                             recovered) &&
        !recovered.isEmpty() &&
        AuthCredentials::hashPassword(recovered) == _setupUnlockPasswordHash) {
      outPlaintext = recovered;
      recovered = "";
      return true;
    }
    recovered = "";
    return false;
  }
  // Legacy hash-only factory default: recognize the known default, do not brute-force.
  if (_setupUnlockPasswordHash ==
      AuthCredentials::hashPassword(kDefaultSetupUnlockPassword)) {
    outPlaintext = kDefaultSetupUnlockPassword;
    return true;
  }
  return false;
}

bool SetupProvisioningManager::unlockSetup(const String &password) {
  if (!verifySetupUnlockPassword(password)) return false;
  _setupReentrySession = true;
  _setupUnlockSessionExpiresAt = millis() + SETUP_UNLOCK_SESSION_MS;
  return true;
}

void SetupProvisioningManager::lockSetup() {
  _setupUnlockSessionExpiresAt = 0;
  _setupReentrySession = false;
}

bool SetupProvisioningManager::closeUnlockedSetup() {
  const bool wasReentry = _setupReentrySession;
  lockSetup();
  if (wasReentry && _installation && _installation->needsSetup()) {
    return _installation->setState(InstallationState::Ready);
  }
  return true;
}

bool SetupProvisioningManager::enforceActiveUnlockSession() {
  if (!_setupReentrySession) return true;
  if (hasActiveSetupUnlockSession()) return true;
  closeUnlockedSetup();
  return false;
}

bool SetupProvisioningManager::setSetupUnlockPassword(const String &password) {
  if (password.length() < 8) return false;
  if (!protectSetupUnlockPassword(password)) return false;
  _setupUnlockPasswordHash = AuthCredentials::hashPassword(password);
  return persist();
}

bool SetupProvisioningManager::changeSetupUnlockPassword(
    const String &currentPassword, const String &newPassword,
    String &errorCodeOut) {
  errorCodeOut = "";
  if (!verifySetupUnlockPassword(currentPassword)) {
    errorCodeOut = "SETUP_UNLOCK_INVALID";
    return false;
  }
  if (newPassword.length() < 8) {
    errorCodeOut = "SETUP_UNLOCK_PASSWORD_TOO_SHORT";
    return false;
  }
  if (newPassword == currentPassword) {
    errorCodeOut = "SETUP_UNLOCK_PASSWORD_UNCHANGED";
    return false;
  }
  if (!setSetupUnlockPassword(newPassword)) {
    errorCodeOut = "SETUP_UNLOCK_PERSIST_FAILED";
    return false;
  }
  return true;
}

uint32_t SetupProvisioningManager::setupUnlockRemainingMs() const {
  if (!_setupReentrySession) return 0;
  const int32_t remaining =
      static_cast<int32_t>(_setupUnlockSessionExpiresAt - millis());
  return remaining > 0 ? static_cast<uint32_t>(remaining) : 0;
}

bool SetupProvisioningManager::load() {
  applyDefaults();
  if (!_storage) return false;

  // NVS is authoritative for Setup Unlock Key (survives SD replace/erase).
  String nvsHash;
  String nvsBlob;
  const bool nvsHasUnlock = AuthCredentials::loadSetupUnlockHash(nvsHash);

  DynamicJsonDocument doc(kDocCapacity);
  const bool sdOk = _storage->readJson(StoragePaths::ProvisioningFile, doc);
  if (!sdOk) {
    const uint32_t now = millis();
    _createdAt         = now;
    _updatedAt         = now;
    if (nvsHasUnlock) {
      _setupUnlockPasswordHash = nvsHash;
      if (AuthCredentials::loadSetupUnlockProtected(nvsBlob)) {
        _setupUnlockPasswordProtected = nvsBlob;
      }
      Serial.println(
          "[setup] unlock key restored from NVS (provisioning.json missing)");
    } else {
      ensureFactoryUnlockProtect();
    }
    return persist();
  }

  const bool migrated = migrateDocument(doc);
  applyDocument(doc.as<JsonObjectConst>());

  if (nvsHasUnlock) {
    // Internal primary: never let blank/stale SD overwrite a valid NVS unlock.
    _setupUnlockPasswordHash = nvsHash;
    if (AuthCredentials::loadSetupUnlockProtected(nvsBlob) &&
        !nvsBlob.isEmpty()) {
      _setupUnlockPasswordProtected = nvsBlob;
    }
  } else if (!_setupUnlockPasswordHash.isEmpty()) {
    // One-time migration: SD/SPIFFS unlock → NVS.
    AuthCredentials::saveSetupUnlockHash(_setupUnlockPasswordHash);
    if (!_setupUnlockPasswordProtected.isEmpty()) {
      AuthCredentials::saveSetupUnlockProtected(_setupUnlockPasswordProtected);
    }
    Serial.println("[setup] migrated Setup Unlock Key to NVS (appliance-bound)");
  }

  const bool wrapped = ensureFactoryUnlockProtect();
  if (migrated || wrapped || !nvsHasUnlock) {
    persist();
  }
  return true;
}

bool SetupProvisioningManager::persist() {
  if (_factoryResetInProgress) return false;
  if (!_storage) return false;

  // NVS first — Setup Unlock Key must survive SD absence/replacement.
  if (!_setupUnlockPasswordHash.isEmpty()) {
    AuthCredentials::saveSetupUnlockHash(_setupUnlockPasswordHash);
    AuthCredentials::saveSetupUnlockProtected(_setupUnlockPasswordProtected);
  }

  DynamicJsonDocument doc(kDocCapacity);
  buildDocument(doc);
  _updatedAt       = millis();
  doc["updatedAt"] = _updatedAt;
  return _storage->writeJson(StoragePaths::ProvisioningFile, doc);
}

void SetupProvisioningManager::beginFactoryResetQuiesce() {
  _factoryResetInProgress    = true;
  _pendingOwnerDurableCommit = false;
  lockSetup();
  _ownerCreated      = false;
  _ownerUsername     = "";
  _ownerDisplayName  = "";
  _ownerPasswordHash = "";
  _setupUnlockPasswordHash      = "";
  _setupUnlockPasswordProtected = "";
  AuthCredentials::clearSetupUnlockCredentials();
}

bool SetupProvisioningManager::factoryResetCredentialsCleared() const {
  return !_ownerCreated && _setupUnlockPasswordHash.isEmpty() &&
         _setupUnlockPasswordProtected.isEmpty() &&
         !_setupReentrySession;
}

bool SetupProvisioningManager::syncInstallationState(InstallationState target) {
  if (!_installation) return false;
  if (installationStateIndex(_installation->current()) >=
      installationStateIndex(target)) {
    return true;
  }
  if (!_installation->advanceTo(target)) return false;
  Serial.printf("[setup] installation state synchronized: %s\n",
                installationStateLabel(_installation->current()));
  return true;
}

bool SetupProvisioningManager::synchronizeAtBoot() {
  if (!_installation) return false;

  bool repaired = false;

  // Owner credentials are NVS-durable immediately on POST /api/setup/owner.
  // provisioning.json / installation.json may still be pending if the HTTP
  // path deferred SD commits (TWDT) or power was lost before loop() flushed.
  if (!_ownerCreated && _auth && _auth->firstBootCompleted()) {
    _ownerCreated = true;
    if (_ownerUsername.isEmpty()) {
      _ownerUsername = _auth->ownerUsername();
    }
    if (_ownerDisplayName.isEmpty()) {
      _ownerDisplayName = _ownerUsername;
    }
    if (_createdAt == 0) _createdAt = millis();
    _updatedAt = millis();
    if (persist()) {
      repaired = true;
      Serial.println("[setup] repaired provisioning.json from NVS owner state");
    } else {
      // Keep trying on subsequent boots; in-memory already reflects owner.
      _pendingOwnerDurableCommit = true;
      Serial.println(
          "[setup] NVS owner repair: provisioning persist deferred to loop");
    }
  }

  if (_ownerCreated &&
      _installation->current() == InstallationState::Factory) {
    if (syncInstallationState(InstallationState::OwnerCreated)) {
      repaired = true;
    } else {
      _pendingOwnerDurableCommit = true;
    }
  }

  if (installationStateAtLeast(_installation->current(),
                               InstallationState::OwnerCreated) &&
      !_ownerCreated && _auth && _auth->firstBootCompleted()) {
    // Covered by the NVS repair above; retained as a safety net.
    _ownerCreated = true;
    if (_createdAt == 0) _createdAt = millis();
    _updatedAt = millis();
    persist();
    repaired     = true;
  }

  if (repaired) {
    Serial.printf("[setup] installation state synchronized: %s\n",
                  installationStateLabel(_installation->current()));
  }

  if (_wizardConfig && _auth) {
    _wizardConfig->reconcileOperatorCredentials(*_auth);
  }

  return repaired;
}

const char *SetupProvisioningManager::wizardStepForPhase(
    bool applyJobActive, bool existingNetworkConfigured,
    bool wifiSelectionConfigured) const {
  if (_installation && _installation->isReady()) return "complete";
  if (!_ownerCreated) return "owner";
  if (_installation &&
      !installationStateAtLeast(_installation->current(),
                                InstallationState::RouterConfigured)) {
    return "router";
  }
  if (applyJobActive) return "applying";
  if (existingNetworkConfigured) return "complete";
  if (!wifiSelectionConfigured) return "wifi";
  return "review";
}

const char *SetupProvisioningManager::wizardStepLabel() const {
  return wizardStepForPhase(false, false, false);
}

void SetupProvisioningManager::fillSetupStatus(JsonObject data,
                                               EthernetManager *eth,
                                               const SetupStatusContext &ctx) const {
  const InstallationState state =
      _installation ? _installation->current() : InstallationState::Factory;
  data["installationState"] = installationStateLabel(state);
  data["ownerCreated"]      = _ownerCreated;
  data["wizardStep"] =
      wizardStepForPhase(ctx.applyJobActive, ctx.existingNetworkConfigured,
                         ctx.wifiSelectionConfigured);
  const bool unlocked = !requiresSetupUnlock() || hasActiveSetupUnlockSession();
  const bool productionMode =
      (_installation && _installation->isReady()) && !unlocked;
  data["productionMode"]    = productionMode;
  data["setupWizardEnabled"] = !productionMode || unlocked;
  data["setupLocked"]        = !unlocked;
  data["setupUnlockRequired"] = requiresSetupUnlock();
  data["setupUnlockSessionRemainingMs"] = setupUnlockRemainingMs();
  data["schemaVersion"]     = _schemaVersion;

  if (_ownerCreated) {
    data["ownerUsername"]    = _ownerUsername;
    data["ownerDisplayName"] = _ownerDisplayName;
  }

  JsonObject storage = data.createNestedObject("storage");
  if (_storage) {
    _storage->fillStorageStatus(storage);
    storage["ok"] = _storage->healthy() || _storage->usingFallback();
  } else {
    storage["ok"] = false;
  }

  JsonObject ethernet = data.createNestedObject("ethernet");
  const bool linkUp   = eth && eth->linkUp();
  const bool hasIp    = eth && eth->hasIp();
  ethernet["link"]    = linkUp;
  ethernet["hasIp"]   = hasIp;
  ethernet["ip"]      = hasIp ? eth->ip() : "";
}

SetupProvisioningManager::CreateOwnerResult SetupProvisioningManager::createOwner(
    const CreateOwnerInput &raw) {
  CreateOwnerResult result;
  result.httpStatus = 400;

  if (_factoryResetInProgress) {
    result.errorCode    = "FACTORY_RESET_IN_PROGRESS";
    result.errorMessage = "Factory reset is in progress";
    result.httpStatus   = 409;
    return result;
  }

  if (_ownerCreated) {
    result.errorCode    = "OWNER_ALREADY_EXISTS";
    result.errorMessage = "An owner account has already been created on this device";
    result.httpStatus   = 409;
    return result;
  }

  CreateOwnerInput input;
  input.displayName     = trimCopy(raw.displayName);
  input.username        = trimCopy(raw.username);
  input.password        = raw.password;
  input.confirmPassword = raw.confirmPassword;
  input.setupUnlockPassword = raw.setupUnlockPassword;
  input.confirmSetupUnlockPassword = raw.confirmSetupUnlockPassword;

  if (input.displayName.isEmpty()) {
    input.displayName = input.username;
  }
  if (input.displayName.length() > 64) {
    result.errorCode    = "DISPLAY_NAME_INVALID";
    result.errorMessage = "Display name must be at most 64 characters";
    return result;
  }

  if (!isValidUsername(input.username)) {
    result.errorCode    = "USERNAME_INVALID";
    result.errorMessage =
        "Username must be 3–32 characters (letters, numbers, underscore, hyphen)";
    return result;
  }

  if (input.password.length() < 8) {
    result.errorCode    = "PASSWORD_TOO_SHORT";
    result.errorMessage = "Password must be at least 8 characters";
    return result;
  }

  if (input.password != input.confirmPassword) {
    result.errorCode    = "PASSWORD_MISMATCH";
    result.errorMessage = "Password and confirmation do not match";
    return result;
  }
  String unlockPassword = trimCopy(input.setupUnlockPassword);
  if (unlockPassword.isEmpty()) unlockPassword = input.password;
  if (!input.confirmSetupUnlockPassword.isEmpty() &&
      unlockPassword != input.confirmSetupUnlockPassword) {
    result.errorCode    = "SETUP_UNLOCK_PASSWORD_MISMATCH";
    result.errorMessage = "Setup unlock password and confirmation do not match";
    return result;
  }
  if (unlockPassword.length() < 8) {
    result.errorCode    = "SETUP_UNLOCK_PASSWORD_TOO_SHORT";
    result.errorMessage = "Setup unlock password must be at least 8 characters";
    return result;
  }

  if (!_auth) {
    result.errorCode    = "INTERNAL_ERROR";
    result.errorMessage = "Authentication service unavailable";
    result.httpStatus   = 503;
    return result;
  }

  if (!_auth->provisionOwnerCredentials(input.password)) {
    result.errorCode    = "CREDENTIALS_FAILED";
    result.errorMessage = "Unable to store owner credentials";
    result.httpStatus   = 500;
    return result;
  }
  _auth->setOwnerUsername(input.username);

  const uint32_t now = millis();
  _ownerPasswordHash = AuthCredentials::hashPassword(input.password);
  _ownerUsername     = input.username;
  _ownerDisplayName  = input.displayName;
  _ownerCreated      = true;
  if (_createdAt == 0) _createdAt = now;
  _updatedAt         = now;
  _setupUnlockPasswordHash = AuthCredentials::hashPassword(unlockPassword);
  protectSetupUnlockPassword(unlockPassword);

  // Owner password/username are already durable in NVS via AuthManager.
  // Do not run transactional SD writes (provisioning.json + installation.json)
  // on async_tcp — that path was the proven TWDT amplifier after the session
  // rewrite + log history flush were removed (TWDT_OWNER_ENDPOINT_ROOT_CAUSE).
  // Durable SD commit runs from loop(); synchronizeAtBoot repairs power loss.
  _pendingOwnerDurableCommit = true;

  result.success      = true;
  result.httpStatus   = 200;
  result.errorMessage = "Owner account created";
  return result;
}

SetupProvisioningManager::CreateOwnerResult SetupProvisioningManager::createOperator(
    const CreateOwnerInput &raw) {
  CreateOwnerResult result;
  result.httpStatus = 400;

  if (!_ownerCreated) {
    result.errorCode    = "SETUP_OWNER_REQUIRED";
    result.errorMessage = "Create the owner account first";
    result.httpStatus   = 403;
    return result;
  }

  if (_wizardConfig && _wizardConfig->operatorConfigured()) {
    result.errorCode    = "OPERATOR_ALREADY_EXISTS";
    result.errorMessage = "An operator account has already been created";
    result.httpStatus   = 409;
    return result;
  }

  CreateOwnerInput input;
  input.username        = trimCopy(raw.username);
  input.password        = raw.password;
  input.confirmPassword = raw.confirmPassword;
  input.displayName     = trimCopy(raw.displayName);

  if (!isValidUsername(input.username)) {
    result.errorCode    = "USERNAME_INVALID";
    result.errorMessage =
        "Operator username must be 3–32 characters (letters, numbers, underscore, hyphen)";
    return result;
  }

  if (input.username == _ownerUsername) {
    result.errorCode    = "USERNAME_CONFLICT";
    result.errorMessage = "Operator username must differ from the owner username";
    return result;
  }

  if (input.password.length() < 8) {
    result.errorCode    = "PASSWORD_TOO_SHORT";
    result.errorMessage = "Password must be at least 8 characters";
    return result;
  }

  if (input.password != input.confirmPassword) {
    result.errorCode    = "PASSWORD_MISMATCH";
    result.errorMessage = "Password and confirmation do not match";
    return result;
  }

  if (!_auth) {
    result.errorCode    = "INTERNAL_ERROR";
    result.errorMessage = "Authentication service unavailable";
    result.httpStatus   = 503;
    return result;
  }

  String authError;
  if (!_auth->provisionOperatorCredentials(input.username, input.password, authError)) {
    if (authError == "OPERATOR_PERSISTENCE_FAILED") {
      result.errorCode    = authError;
      result.errorMessage = "Operator credentials could not be saved to device storage";
      result.httpStatus   = 500;
      return result;
    }
    if (authError == "OPERATOR_PERSISTENCE_MISMATCH") {
      result.errorCode    = authError;
      result.errorMessage =
          "Operator credentials failed verification after save; try again";
      result.httpStatus   = 500;
      return result;
    }
    if (authError == "PASSWORD_TOO_SHORT") {
      result.errorCode    = authError;
      result.errorMessage = "Password must be at least 8 characters";
      return result;
    }
    result.errorCode    = authError.isEmpty() ? "CREDENTIALS_FAILED" : authError;
    result.errorMessage = "Unable to store operator credentials";
    result.httpStatus   = 500;
    return result;
  }

  if (_wizardConfig) {
    DynamicJsonDocument body(RenzFiConfig::JSON_DOC_SMALL);
    JsonObject obj = body.to<JsonObject>();
    obj["username"] = input.username;
    const String hash = AuthCredentials::hashPassword(input.password);
    const auto saveResult = _wizardConfig->saveOperator(obj, hash);
    if (!saveResult.success) {
      result.errorCode    = saveResult.errorCode;
      result.errorMessage = saveResult.errorMessage;
      result.httpStatus   = saveResult.httpStatus;
      return result;
    }
  }

  result.success      = true;
  result.httpStatus   = 200;
  result.errorMessage = "Operator account created";
  return result;
}
