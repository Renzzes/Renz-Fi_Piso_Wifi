#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "InstallationState.h"
#include "SetupRouterValidator.h"

class EthernetManager;
class InstallationStateManager;
class StorageManager;

// Setup-plane MikroTik router connection record (/config/router-connection.json).
// Excluded from backup/export bundles — credentials stay on-device only.
class SetupRouterConnectionManager {
 public:
  static constexpr uint16_t SCHEMA_VERSION = 1;

  struct RouterInput {
    String   host;
    uint16_t apiPort = 8728;
    String   username;
    String   password;
    String   connectionId;
  };

  // Owned credential bundle returned by the canonical resolver. All RouterOS
  // jobs must copy from this object — never hold pointers into request JSON,
  // worker locals, or temporary Strings beyond the resolver call.
  struct ResolvedRouterCredentials {
    String   host;
    uint16_t apiPort = 8728;
    String   username;
    String   password;

    RouterInput toRouterInput() const {
      RouterInput out;
      out.host     = host;
      out.apiPort  = apiPort;
      out.username = username;
      out.password = password;
      return out;
    }
  };

  enum class RouterCredentialSource : uint8_t {
    Request = 0,
    Persisted,
  };

  struct OperationResult {
    bool   success = false;
    int    httpStatus = 400;
    String errorCode;
    String errorMessage;
    String stage;
    String validationCode;
    String routerIdentity;
    String routerBoard;
    String routerOs;
  };

  void begin(StorageManager *storage, InstallationStateManager *installation,
             EthernetManager *eth);

  bool load();
  bool persist();

  bool   connectionVerified() const { return _connectionVerified; }
  uint32_t verifiedAt() const { return _verifiedAt; }
  String host() const { return _host; }
  uint16_t apiPort() const { return _apiPort; }
  String username() const { return _username; }

  bool resolveCredentialsForApi(RouterInput &input, OperationResult &result);

  // Canonical credential resolver for all RouterOS API jobs.
  // Setup test/save must pass allowPersistedPasswordFallback=false so blank
  // request passwords never substitute saved credentials.
  bool resolveRouterCredentials(RouterCredentialSource source,
                                const RouterInput *request,
                                ResolvedRouterCredentials &out,
                                OperationResult &result,
                                bool allowPersistedPasswordFallback = false) const;

  OperationResult testConnection(const RouterInput &input);
  OperationResult saveConnection(const RouterInput &input);

  void fillSafeConfig(JsonObject data, bool includeDefaults) const;
  bool hasVerifiedConnection() const;
  bool hasSavedPassword() const { return !_passwordProtected.isEmpty(); }
  void clearForFactoryReset();

  // Reloads protected blob from storage after atomic write; used by save path.
  bool verifyRouterCredentialRoundTrip(const String &originalPassword,
                                       String &failureStage);

 private:
  struct ConfigSnapshot {
    String   routerType;
    String   host;
    uint16_t apiPort = 8728;
    String   username;
    String   passwordProtected;
    bool     connectionVerified = false;
    uint32_t verifiedAt = 0;
    uint32_t createdAt = 0;
    uint32_t updatedAt = 0;
    uint16_t schemaVersion = SCHEMA_VERSION;
  };

  ConfigSnapshot captureConfig() const;
  void applySnapshot(const ConfigSnapshot &snapshot);
  bool persistAndReloadProtected(String &protectedFromStorage, size_t &fileBytes,
                                 String &failureStage);
  void restoreConfigSnapshot(const ConfigSnapshot &snapshot);
  StorageManager           *_storage      = nullptr;
  InstallationStateManager *_installation = nullptr;
  EthernetManager          *_eth          = nullptr;

  String   _routerType         = "mikrotik";
  String   _host;
  uint16_t _apiPort            = 8728;
  String   _username;
  String   _passwordProtected;
  bool     _connectionVerified = false;
  uint32_t _verifiedAt         = 0;
  uint32_t _createdAt          = 0;
  uint32_t _updatedAt          = 0;
  uint16_t _schemaVersion      = SCHEMA_VERSION;

  void applyDefaults();
  void applyDocument(JsonObjectConst doc);
  void buildDocument(JsonDocument &doc) const;
  bool migrateDocument(JsonDocument &doc);

  OperationResult validateAndBuild(const ResolvedRouterCredentials &credentials,
                                   SetupRouterValidator::Result &validationOut);
  bool unprotectStoredPassword(String &outPlaintext) const;
  String defaultHost() const;

};
