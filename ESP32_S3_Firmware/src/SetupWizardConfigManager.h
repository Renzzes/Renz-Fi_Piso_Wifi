#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "Models.h"

class CoinManager;
class NetworkSettingsManager;
class StorageManager;
class AuthManager;

namespace SetupWizard {

enum class ApDeploymentMode : uint8_t {
  MikrotikOnly = 0,
  ExternalOnly = 1,
  Both = 2,
};

const char *apDeploymentModeLabel(ApDeploymentMode mode);
ApDeploymentMode parseApDeploymentMode(const char *label);

struct CoinRateRow {
  uint8_t  pesos   = 0;
  uint16_t minutes = 0;
};

struct CoinSetupConfig {
  static constexpr size_t kMaxRates = 8;
  CoinRateRow rates[kMaxRates]{};
  uint8_t     rateCount = 0;
  uint16_t    abuseCount      = 5;
  uint16_t    banMinutes      = 10;
  uint16_t    resetWindowSec  = 60;
};

struct GuestWifiConfig {
  String ssid;
  String securityMode = "wpa2-personal";
  String portalDisplayName = "Renz-Fi WiFi";
  String passwordProtected;
  bool   configured = false;
};

struct OperatorConfig {
  String username;
  String passwordHash;
  bool   configured = false;
};

}  // namespace SetupWizard

class SetupWizardConfigManager {
 public:
  static constexpr uint16_t SCHEMA_VERSION = 1;

  struct SaveResult {
    bool   success = false;
    int    httpStatus = 400;
    String errorCode;
    String errorMessage;
  };

  void begin(StorageManager *storage, NetworkSettingsManager *networkSettings,
             CoinManager *coin = nullptr);

  bool load();
  bool persist();

  bool ethernetConfigured() const { return _ethernetConfigured; }
  bool guestWifiConfigured() const { return _guestWifi.configured; }
  bool apDeploymentConfigured() const { return _apDeploymentConfigured; }
  bool coinConfigured() const { return _coinConfigured; }
  bool operatorConfigured() const { return _operator.configured; }

  SetupWizard::ApDeploymentMode apDeploymentMode() const { return _apMode; }
  const SetupWizard::GuestWifiConfig &guestWifi() const { return _guestWifi; }
  const SetupWizard::CoinSetupConfig &coinSetup() const { return _coinSetup; }
  const SetupWizard::OperatorConfig &operatorAccount() const { return _operator; }

  SaveResult saveEthernet(JsonObjectConst body);
  SaveResult saveGuestWifi(JsonObjectConst body);
  SaveResult saveApDeployment(JsonObjectConst body);
  SaveResult saveCoinSetup(JsonObjectConst body);
  SaveResult saveOperator(JsonObjectConst body, const String &passwordHash);

  void clearForFactoryReset();
  void fillSafeStatus(JsonObject data) const;
  void fillReviewSummary(JsonObject data) const;
  void reconcileOperatorCredentials(const AuthManager &auth);

  static bool validateIpv4(const String &value);
  static bool validateCoinSetup(const SetupWizard::CoinSetupConfig &cfg,
                                String &errorOut);

 private:
  StorageManager          *_storage          = nullptr;
  NetworkSettingsManager  *_networkSettings  = nullptr;
  CoinManager             *_coin             = nullptr;

  bool _ethernetConfigured    = false;
  bool _apDeploymentConfigured = false;
  bool _coinConfigured        = false;
  SetupWizard::ApDeploymentMode _apMode = SetupWizard::ApDeploymentMode::MikrotikOnly;
  SetupWizard::GuestWifiConfig _guestWifi;
  SetupWizard::CoinSetupConfig _coinSetup;
  SetupWizard::OperatorConfig  _operator;
  uint16_t _schemaVersion = SCHEMA_VERSION;

  void applyDefaults();
  void applyDocument(JsonObjectConst doc);
  void buildDocument(JsonDocument &doc) const;
  bool syncCoinSettingsToStorage();
};
