#include "SetupRouterConnectionManager.h"

#include "Config.h"
#include "CredentialProtector.h"
#include "EthernetManager.h"
#include "InstallationState.h"
#include "InstallationStateManager.h"
#include "RouterCredentials.h"
#include "RouterWorkerDiagnostics.h"
#include "StorageManager.h"
#include "StoragePaths.h"
namespace {

constexpr size_t kDocCapacity = RenzFiConfig::JSON_DOC_SMALL;

bool isValidHost(const String &host) {
  IPAddress addr;
  if (!addr.fromString(host)) return false;
  return addr != IPAddress(0, 0, 0, 0);
}

// True for any RouterOS API code that represents a rejected login
// (wrong credentials, trap/fatal during auth, or an incomplete login
// exchange) rather than a transport/connectivity failure.
bool isLoginFailureCode(const String &code) {
  return code == "API_LOGIN_FAILED" || code == "ROUTEROS_LOGIN_FAILED" ||
         code == "ROUTEROS_API_AUTH_TRAP" || code == "ROUTEROS_API_AUTH_FATAL";
}

}  // namespace

void SetupRouterConnectionManager::begin(StorageManager *storage,
                                         InstallationStateManager *installation,
                                         EthernetManager *eth) {
  _storage      = storage;
  _installation = installation;
  _eth          = eth;
  load();
}

void SetupRouterConnectionManager::applyDefaults() {
  _routerType          = "mikrotik";
  _host                = "";
  _apiPort             = 8728;
  _username            = "admin";
  _passwordProtected   = "";
  _connectionVerified  = false;
  _verifiedAt          = 0;
  _createdAt           = 0;
  _updatedAt           = 0;
  _schemaVersion       = SCHEMA_VERSION;
}

void SetupRouterConnectionManager::clearForFactoryReset() {
  applyDefaults();
}

bool SetupRouterConnectionManager::migrateDocument(JsonDocument &doc) {
  bool changed = false;
  const uint16_t version = doc["schemaVersion"] | 0U;

  if (version < 1) {
    if (!doc["routerType"].is<const char *>()) doc["routerType"] = "mikrotik";
    if (!doc["apiPort"].is<uint16_t>()) doc["apiPort"] = 8728;
    if (!doc["connectionVerified"].is<bool>()) doc["connectionVerified"] = false;
    if (!doc["createdAt"].is<uint32_t>()) doc["createdAt"] = millis();
    if (!doc["updatedAt"].is<uint32_t>()) doc["updatedAt"] = millis();
    doc["schemaVersion"] = SCHEMA_VERSION;
    changed              = true;
  }

  return changed;
}

void SetupRouterConnectionManager::applyDocument(JsonObjectConst doc) {
  _routerType         = doc["routerType"] | "mikrotik";
  _host               = doc["host"] | "";
  _apiPort            = doc["apiPort"] | 8728;
  _username           = doc["username"] | "admin";
  _passwordProtected  = doc["passwordProtected"] | "";
  _connectionVerified = doc["connectionVerified"] | false;
  _verifiedAt         = doc["verifiedAt"] | 0U;
  _createdAt          = doc["createdAt"] | 0U;
  _updatedAt          = doc["updatedAt"] | 0U;
  _schemaVersion      = doc["schemaVersion"] | SCHEMA_VERSION;
}

void SetupRouterConnectionManager::buildDocument(JsonDocument &doc) const {
  doc.clear();
  doc["schemaVersion"]       = _schemaVersion;
  doc["routerType"]          = _routerType;
  doc["host"]                = _host;
  doc["apiPort"]             = _apiPort;
  doc["username"]            = _username;
  doc["passwordProtected"]   = _passwordProtected;
  doc["connectionVerified"]  = _connectionVerified;
  doc["verifiedAt"]          = _verifiedAt;
  doc["createdAt"]           = _createdAt;
  doc["updatedAt"]           = _updatedAt;
}

bool SetupRouterConnectionManager::load() {
  applyDefaults();
  if (!_storage) return false;

  DynamicJsonDocument doc(kDocCapacity);
  if (!_storage->readJson(StoragePaths::RouterConnectionFile, doc)) {
    const uint32_t now = millis();
    _createdAt         = now;
    _updatedAt         = now;
    return true;
  }

  if (migrateDocument(doc)) {
    applyDocument(doc.as<JsonObjectConst>());
    persist();
    return true;
  }

  applyDocument(doc.as<JsonObjectConst>());
  // After a normal RESET, reconstruct verified state from integrity of the
  // persisted secret — not from a bare file-exists check. Require host +
  // protected password that successfully unprotects. Do not trust UI masking.
  if (!_connectionVerified && !_host.isEmpty() &&
      !_passwordProtected.isEmpty()) {
    String plaintext;
    if (CredentialProtector::unprotectSecret(_passwordProtected, plaintext) &&
        !plaintext.isEmpty()) {
      _connectionVerified = true;
      if (_verifiedAt == 0) _verifiedAt = millis();
      Serial.println(
          "[setup] router-connection: reconstructed connectionVerified "
          "from persisted credentials after boot");
      persist();
    }
  }
  return true;
}

bool SetupRouterConnectionManager::persist() {
  if (!_storage) return false;

  DynamicJsonDocument doc(kDocCapacity);
  buildDocument(doc);
  _updatedAt       = millis();
  doc["updatedAt"] = _updatedAt;
  return _storage->writeJson(StoragePaths::RouterConnectionFile, doc);
}

String SetupRouterConnectionManager::defaultHost() const {
  if (_eth && _eth->hasIp()) {
    const String gateway = _eth->gateway();
    if (isValidHost(gateway)) return gateway;
  }
  return String("10.10.10.1");
}

void SetupRouterConnectionManager::fillSafeConfig(JsonObject data,
                                                  bool includeDefaults) const {
  data["routerType"]         = _routerType;
  data["host"]               = _host.isEmpty() && includeDefaults ? defaultHost()
                                                                  : _host;
  data["apiPort"]            = _apiPort ? _apiPort : 8728;
  data["username"]           = _username.isEmpty() ? "admin" : _username;
  data["connectionVerified"] = _connectionVerified;
  data["verifiedAt"]         = _verifiedAt;
  data["hasSavedPassword"]   = hasSavedPassword();
}

bool SetupRouterConnectionManager::unprotectStoredPassword(
    String &outPlaintext) const {
  outPlaintext = "";
  if (!hasSavedPassword()) return false;
  return CredentialProtector::unprotectSecret(_passwordProtected, outPlaintext);
}

bool SetupRouterConnectionManager::resolveRouterCredentials(
    RouterCredentialSource source, const RouterInput *request,
    ResolvedRouterCredentials &out, OperationResult &result,
    bool allowPersistedPasswordFallback) const {
  RouterWorkerDiagnostics::logStage("resolve-creds-entry");
  result = OperationResult{};

  if (source == RouterCredentialSource::Request) {
    if (!request) {
      result.errorCode    = "INVALID_PASSWORD";
      result.errorMessage = "Router request credentials are missing";
      return false;
    }

    out.host     = request->host;
    out.apiPort  = request->apiPort ? request->apiPort : 8728;
    out.username = request->username;
    out.password = request->password;
    out.host.trim();
    out.username.trim();

    if (out.password.isEmpty()) {
      if (allowPersistedPasswordFallback && unprotectStoredPassword(out.password)) {
        Serial.println("[router-credentials] Credential Source: PERSISTED");
        RouterCredentials::logSafeDiagnostics("persisted-fallback", out.password);
        RouterWorkerDiagnostics::logStage("resolve-creds-exit");
        return true;
      }
      result.httpStatus   = 401;
      result.errorCode    = "AUTHENTICATION_FAILED";
      result.errorMessage = "Router password is required";
      return false;
    }

    Serial.println("[router-credentials] Credential Source: REQUEST");
    RouterCredentials::logSafeDiagnostics("request", out.password);
    RouterWorkerDiagnostics::logStage("resolve-creds-exit");
    return true;
  }

  if (!hasVerifiedConnection()) {
    result.httpStatus   = 409;
    result.errorCode    = "ROUTER_CONNECTION_REQUIRED";
    result.errorMessage = "Saved MikroTik connection is unavailable";
    return false;
  }

  out.host     = _host;
  out.apiPort  = _apiPort ? _apiPort : 8728;
  out.username = _username.isEmpty() ? String("admin") : _username;
  out.password = "";
  if (!unprotectStoredPassword(out.password) || out.password.isEmpty()) {
    result.httpStatus   = 409;
    result.errorCode    = "CREDENTIAL_UNAVAILABLE";
    result.errorMessage = "Unable to load saved router credentials";
    return false;
  }

  Serial.println("[router-credentials] Credential Source: PERSISTED");
  RouterCredentials::logSafeDiagnostics("persisted", out.password);
  RouterWorkerDiagnostics::logStage("resolve-creds-exit");
  return true;
}

bool SetupRouterConnectionManager::resolveCredentialsForApi(
    RouterInput &input, OperationResult &result) {
  ResolvedRouterCredentials resolved;
  if (!resolveRouterCredentials(RouterCredentialSource::Request, &input, resolved,
                                result, true)) {
    return false;
  }
  input = resolved.toRouterInput();
  return true;
}

bool SetupRouterConnectionManager::hasVerifiedConnection() const {
  return _connectionVerified && !_host.isEmpty() && !_passwordProtected.isEmpty();
}

SetupRouterConnectionManager::ConfigSnapshot
SetupRouterConnectionManager::captureConfig() const {
  ConfigSnapshot snapshot;
  snapshot.routerType         = _routerType;
  snapshot.host               = _host;
  snapshot.apiPort            = _apiPort;
  snapshot.username           = _username;
  snapshot.passwordProtected  = _passwordProtected;
  snapshot.connectionVerified = _connectionVerified;
  snapshot.verifiedAt         = _verifiedAt;
  snapshot.createdAt          = _createdAt;
  snapshot.updatedAt          = _updatedAt;
  snapshot.schemaVersion      = _schemaVersion;
  return snapshot;
}

void SetupRouterConnectionManager::applySnapshot(const ConfigSnapshot &snapshot) {
  _routerType         = snapshot.routerType;
  _host               = snapshot.host;
  _apiPort            = snapshot.apiPort;
  _username           = snapshot.username;
  _passwordProtected  = snapshot.passwordProtected;
  _connectionVerified = snapshot.connectionVerified;
  _verifiedAt         = snapshot.verifiedAt;
  _createdAt          = snapshot.createdAt;
  _updatedAt          = snapshot.updatedAt;
  _schemaVersion      = snapshot.schemaVersion;
}

void SetupRouterConnectionManager::restoreConfigSnapshot(
    const ConfigSnapshot &snapshot) {
  applySnapshot(snapshot);
  if (_storage && !persist()) {
    Serial.println("[setup] router credential rollback persist failed");
  }
}

bool SetupRouterConnectionManager::persistAndReloadProtected(
    String &protectedFromStorage, size_t &fileBytes, String &failureStage) {
  failureStage = "";
  protectedFromStorage = "";
  fileBytes            = 0;

  if (!_storage) {
    failureStage = "write";
    return false;
  }

  DynamicJsonDocument doc(kDocCapacity);
  buildDocument(doc);
  _updatedAt       = millis();
  doc["updatedAt"] = _updatedAt;

  if (!_storage->writeJson(StoragePaths::RouterConnectionFile, doc)) {
    failureStage = "write";
    return false;
  }

  fileBytes = _storage->fileSizeBytes(StoragePaths::RouterConnectionFile);
  RouterCredentials::logStageAfterWrite(fileBytes);

  DynamicJsonDocument verifyDoc(kDocCapacity);
  if (!_storage->readJson(StoragePaths::RouterConnectionFile, verifyDoc)) {
    failureStage = "parse";
    return false;
  }

  const char *protectedField = verifyDoc["passwordProtected"] | "";
  if (!protectedField || protectedField[0] == '\0') {
    failureStage = "serialize";
    return false;
  }
  protectedFromStorage = protectedField;
  return true;
}

bool SetupRouterConnectionManager::verifyRouterCredentialRoundTrip(
    const String &originalPassword, String &failureStage) {
  String protectedFromStorage;
  size_t fileBytes = 0;
  if (!persistAndReloadProtected(protectedFromStorage, fileBytes, failureStage)) {
    return false;
  }

  load();

  return RouterCredentials::verifyRouterCredentialRoundTrip(
      originalPassword, protectedFromStorage, failureStage);
}

SetupRouterConnectionManager::OperationResult
SetupRouterConnectionManager::validateAndBuild(
    const ResolvedRouterCredentials &credentials,
    SetupRouterValidator::Result &validationOut) {
  OperationResult result;

  ResolvedRouterCredentials normalized = credentials;
  normalized.host.trim();
  normalized.username.trim();

  if (normalized.host.isEmpty() || !isValidHost(normalized.host)) {    result.errorCode    = "INVALID_HOST";
    result.errorMessage = "Router IP address is invalid";
    return result;
  }

  if (normalized.username.isEmpty()) {
    result.errorCode    = "INVALID_USERNAME";
    result.errorMessage = "Router username is required";
    return result;
  }

  if (normalized.password.isEmpty()) {
    result.errorCode    = "INVALID_PASSWORD";
    result.errorMessage = "Router password is required";
    return result;
  }

  if (normalized.apiPort == 0) {
    result.errorCode    = "INVALID_HOST";
    result.errorMessage = "API port is invalid";
    return result;
  }

  SetupRouterValidator::Input vInput;
  vInput.host     = normalized.host;
  vInput.apiPort  = normalized.apiPort;
  vInput.username = normalized.username;
  vInput.password = normalized.password;

  validationOut = SetupRouterValidator::validate(vInput, _eth);
  result.validationCode = validationOut.code;
  result.routerIdentity = validationOut.identity;
  result.routerBoard    = validationOut.board;
  result.routerOs       = validationOut.routerOsVersion;

  if (!validationOut.success) {
    if (isLoginFailureCode(validationOut.code)) {
      result.httpStatus = 401;
    } else if (validationOut.code == "TCP_CONNECT_FAILED") {
      result.httpStatus = 503;
    } else {
      result.httpStatus = 400;
    }
    result.errorCode    = validationOut.code;
    result.errorMessage = validationOut.message;
    result.stage        = validationOut.stage;
    return result;
  }

  result.success = true;
  return result;
}

SetupRouterConnectionManager::OperationResult
SetupRouterConnectionManager::testConnection(const RouterInput &input) {
  Serial.println("[router-test] Credential Source: REQUEST");
  OperationResult result;
  ResolvedRouterCredentials credentials;
  if (!resolveRouterCredentials(RouterCredentialSource::Request, &input,
                                credentials, result, false)) {
    return result;
  }

  SetupRouterValidator::Result validation;
  result = validateAndBuild(credentials, validation);
  if (!result.success) return result;

  result.success      = true;
  result.httpStatus   = 200;
  result.errorMessage = "RouterOS API connection validated";
  RouterWorkerDiagnostics::logStackHighWaterMark("test-after-validation");
  Serial.printf("[setup] router test ok: host=%s port=%u\n",
                credentials.host.c_str(),
                static_cast<unsigned>(credentials.apiPort));
  return result;
}

SetupRouterConnectionManager::OperationResult
SetupRouterConnectionManager::saveConnection(const RouterInput &input) {
  Serial.println("[router-save] Credential Source: REQUEST");
  const ConfigSnapshot priorConfig = captureConfig();

  OperationResult result;
  ResolvedRouterCredentials credentials;
  if (!resolveRouterCredentials(RouterCredentialSource::Request, &input,
                                credentials, result, false)) {
    return result;
  }

  credentials.host.trim();
  credentials.username.trim();

  if (credentials.host.isEmpty() || !isValidHost(credentials.host)) {
    result.errorCode    = "INVALID_HOST";
    result.errorMessage = "Router IP address is invalid";
    return result;
  }

  if (credentials.username.isEmpty()) {
    result.errorCode    = "INVALID_USERNAME";
    result.errorMessage = "Router username is required";
    return result;
  }

  if (credentials.password.isEmpty()) {
    result.errorCode    = "INVALID_PASSWORD";
    result.errorMessage = "Router password is required";
    return result;
  }

  if (credentials.apiPort == 0) {
    result.errorCode    = "INVALID_HOST";
    result.errorMessage = "API port is invalid";
    return result;
  }

  SetupRouterValidator::Result validation;
  result = validateAndBuild(credentials, validation);
  if (!result.success) return result;

  const String validatedPassword = credentials.password;

  RouterCredentials::logStageBeforeProtect(validatedPassword);

  String protectedPassword;
  CredentialProtector::ProtectedSecretParts protectParts;
  if (!CredentialProtector::protectSecret(validatedPassword, protectedPassword,
                                          &protectParts)) {
    result.success      = false;
    result.httpStatus   = 500;
    result.errorCode    = "STORAGE_WRITE_FAILED";
    result.errorMessage = "Unable to protect router credentials";
    result.stage        = "protect";
    return result;
  }
  RouterCredentials::logStageAfterProtect(protectParts);

  const uint32_t now = millis();
  _host                = credentials.host;
  _host.trim();
  _apiPort             = credentials.apiPort ? credentials.apiPort : 8728;
  _username            = credentials.username;
  _username.trim();
  _passwordProtected   = protectedPassword;
  _connectionVerified  = true;
  _verifiedAt          = now;
  if (_createdAt == 0) _createdAt = now;
  _updatedAt           = now;

  String roundTripStage;
  if (!verifyRouterCredentialRoundTrip(validatedPassword, roundTripStage)) {
    restoreConfigSnapshot(priorConfig);
    result.success      = false;
    result.httpStatus   = 500;
    result.errorCode    = roundTripStage == "compare"
                              ? "ROUTER_CREDENTIAL_PERSISTENCE_MISMATCH"
                              : "STORAGE_WRITE_FAILED";
    result.errorMessage =
        roundTripStage == "compare"
            ? "Saved router credentials failed persistence verification"
            : "Unable to verify saved router credentials";
    result.stage = roundTripStage.isEmpty() ? "compare" : roundTripStage;
    Serial.printf("[setup] router save rejected: %s stage=%s\n",
                  result.errorCode.c_str(), result.stage.c_str());
    return result;
  }

  Serial.println("[setup] router credential persistence round-trip ok");

  if (_installation &&
      !installationStateAtLeast(_installation->current(),
                                InstallationState::RouterConfigured)) {
    if (!_installation->advanceTo(InstallationState::RouterConfigured)) {
      restoreConfigSnapshot(priorConfig);
      result.success      = false;
      result.httpStatus   = 500;
      result.errorCode    = "STATE_SYNC_FAILED";
      result.errorMessage = "Unable to update installation state";
      return result;
    }
    Serial.printf("[setup] installation state synchronized: %s\n",
                  installationStateLabel(_installation->current()));
  }

  result.success      = true;
  result.httpStatus   = 200;
  result.errorMessage = "Router connection saved successfully.";
  RouterWorkerDiagnostics::logStackHighWaterMark("save-after-validation");
  Serial.printf("[setup] router credentials persisted: host=%s port=%u\n",
                _host.c_str(), static_cast<unsigned>(_apiPort));
  return result;
}