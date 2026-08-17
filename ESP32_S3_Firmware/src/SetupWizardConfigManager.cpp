#include "SetupWizardConfigManager.h"

#include "AuthManager.h"
#include "CoinManager.h"
#include "Config.h"
#include "CredentialProtector.h"
#include "NetworkSettingsManager.h"
#include "StorageManager.h"
#include "StoragePaths.h"

#include <IPAddress.h>

namespace {

constexpr size_t kDocCapacity = RenzFiConfig::JSON_DOC_MEDIUM;

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

SetupWizard::CoinSetupConfig defaultCoinSetup() {
  SetupWizard::CoinSetupConfig cfg;
  cfg.rateCount = 3;
  cfg.rates[0]  = {1, 5};
  cfg.rates[1]  = {5, 25};
  cfg.rates[2]  = {10, 50};
  return cfg;
}

}  // namespace

const char *SetupWizard::apDeploymentModeLabel(ApDeploymentMode mode) {
  switch (mode) {
    case ApDeploymentMode::ExternalOnly:
      return "external_only";
    case ApDeploymentMode::Both:
      return "both";
    default:
      return "mikrotik_only";
  }
}

SetupWizard::ApDeploymentMode SetupWizard::parseApDeploymentMode(const char *label) {
  if (!label) return ApDeploymentMode::MikrotikOnly;
  if (strcmp(label, "external_only") == 0) return ApDeploymentMode::ExternalOnly;
  if (strcmp(label, "both") == 0) return ApDeploymentMode::Both;
  return ApDeploymentMode::MikrotikOnly;
}

void SetupWizardConfigManager::begin(StorageManager *storage,
                                     NetworkSettingsManager *networkSettings,
                                     CoinManager *coin) {
  _storage         = storage;
  _networkSettings = networkSettings;
  _coin            = coin;
  load();
}

void SetupWizardConfigManager::applyDefaults() {
  _ethernetConfigured     = false;
  _apDeploymentConfigured = false;
  _coinConfigured         = false;
  _apMode                 = SetupWizard::ApDeploymentMode::MikrotikOnly;
  _guestWifi              = {};
  _guestWifi.securityMode = "wpa2-personal";
  _guestWifi.portalDisplayName = "Renz-Fi WiFi";
  _coinSetup              = defaultCoinSetup();
  _operator               = {};
  _schemaVersion          = SCHEMA_VERSION;
}

void SetupWizardConfigManager::clearForFactoryReset() {
  applyDefaults();
}

bool SetupWizardConfigManager::validateIpv4(const String &value) {
  if (value.isEmpty()) return false;
  IPAddress ip;
  return ip.fromString(value);
}

bool SetupWizardConfigManager::validateCoinSetup(
    const SetupWizard::CoinSetupConfig &cfg, String &errorOut) {
  if (cfg.rateCount == 0 || cfg.rateCount > SetupWizard::CoinSetupConfig::kMaxRates) {
    errorOut = "At least one coin rate row is required";
    return false;
  }
  for (uint8_t i = 0; i < cfg.rateCount; ++i) {
    if (cfg.rates[i].pesos == 0) {
      errorOut = "Coin denomination must be positive";
      return false;
    }
    if (cfg.rates[i].minutes == 0) {
      errorOut = "Minutes must be positive for each denomination";
      return false;
    }
    for (uint8_t j = i + 1; j < cfg.rateCount; ++j) {
      if (cfg.rates[j].pesos == cfg.rates[i].pesos) {
        errorOut = "Duplicate coin denomination";
        return false;
      }
    }
  }
  if (cfg.abuseCount < 1 || cfg.abuseCount > 100) {
    errorOut = "Abuse count must be between 1 and 100";
    return false;
  }
  if (cfg.banMinutes < 1 || cfg.banMinutes > 1440) {
    errorOut = "Ban minutes must be between 1 and 1440";
    return false;
  }
  if (cfg.resetWindowSec < 5 || cfg.resetWindowSec > 3600) {
    errorOut = "Reset window must be between 5 and 3600 seconds";
    return false;
  }
  return true;
}

void SetupWizardConfigManager::applyDocument(JsonObjectConst doc) {
  _schemaVersion          = doc["schemaVersion"] | SCHEMA_VERSION;
  _ethernetConfigured     = doc["ethernetConfigured"] | false;
  _apDeploymentConfigured = doc["apDeploymentConfigured"] | false;
  _coinConfigured         = doc["coinConfigured"] | false;

  _apMode = SetupWizard::parseApDeploymentMode(doc["apDeploymentMode"] | "mikrotik_only");

  JsonObjectConst guest = doc["guestWifi"];
  _guestWifi.configured = guest["configured"] | false;
  _guestWifi.ssid = guest["ssid"] | "";
  _guestWifi.securityMode = guest["securityMode"] | "wpa2-personal";
  _guestWifi.portalDisplayName = guest["portalDisplayName"] | "Renz-Fi WiFi";
  _guestWifi.passwordProtected = guest["passwordProtected"] | "";

  JsonObjectConst coin = doc["coinSetup"];
  _coinSetup = defaultCoinSetup();
  if (!coin.isNull()) {
    _coinSetup.abuseCount     = coin["abuseCount"] | 5U;
    _coinSetup.banMinutes     = coin["banMinutes"] | 10U;
    _coinSetup.resetWindowSec = coin["resetWindowSec"] | 60U;
    JsonArrayConst rates = coin["rates"];
    if (!rates.isNull() && rates.size() > 0) {
      _coinSetup.rateCount = 0;
      for (JsonObjectConst row : rates) {
        if (_coinSetup.rateCount >= SetupWizard::CoinSetupConfig::kMaxRates) break;
        const uint8_t pesos = row["pesos"] | 0;
        const uint16_t minutes = row["minutes"] | 0;
        if (pesos == 0 || minutes == 0) continue;
        _coinSetup.rates[_coinSetup.rateCount++] = {pesos, minutes};
      }
    }
  }

  JsonObjectConst op = doc["operator"];
  _operator.configured = op["configured"] | false;
  _operator.username   = op["username"] | "";
  _operator.passwordHash = op["passwordHash"] | "";
}

void SetupWizardConfigManager::buildDocument(JsonDocument &doc) const {
  doc.clear();
  doc["schemaVersion"]          = _schemaVersion;
  doc["ethernetConfigured"]     = _ethernetConfigured;
  doc["apDeploymentConfigured"] = _apDeploymentConfigured;
  doc["coinConfigured"]         = _coinConfigured;
  doc["apDeploymentMode"]       = SetupWizard::apDeploymentModeLabel(_apMode);

  JsonObject guest = doc["guestWifi"].to<JsonObject>();
  guest["configured"]        = _guestWifi.configured;
  guest["ssid"]                = _guestWifi.ssid;
  guest["securityMode"]        = _guestWifi.securityMode;
  guest["portalDisplayName"]   = _guestWifi.portalDisplayName;
  guest["passwordProtected"]   = _guestWifi.passwordProtected;
  guest["hasSavedPassword"]    = !_guestWifi.passwordProtected.isEmpty();

  JsonObject coin = doc["coinSetup"].to<JsonObject>();
  JsonArray rates = coin["rates"].to<JsonArray>();
  for (uint8_t i = 0; i < _coinSetup.rateCount; ++i) {
    JsonObject row = rates.add<JsonObject>();
    row["pesos"]   = _coinSetup.rates[i].pesos;
    row["minutes"] = _coinSetup.rates[i].minutes;
  }
  coin["abuseCount"]     = _coinSetup.abuseCount;
  coin["banMinutes"]     = _coinSetup.banMinutes;
  coin["resetWindowSec"] = _coinSetup.resetWindowSec;

  JsonObject op = doc["operator"].to<JsonObject>();
  op["configured"] = _operator.configured;
  op["username"]   = _operator.username;
  op["passwordHash"] = _operator.passwordHash;
}

bool SetupWizardConfigManager::load() {
  applyDefaults();
  if (!_storage) return false;

  DynamicJsonDocument doc(kDocCapacity);
  if (!_storage->readJson(StoragePaths::SetupWizardFile, doc)) {
    return persist();
  }
  applyDocument(doc.as<JsonObjectConst>());
  return true;
}

bool SetupWizardConfigManager::persist() {
  if (!_storage) return false;
  DynamicJsonDocument doc(kDocCapacity);
  buildDocument(doc);
  return _storage->writeJson(StoragePaths::SetupWizardFile, doc);
}

SetupWizardConfigManager::SaveResult SetupWizardConfigManager::saveEthernet(
    JsonObjectConst body) {
  SaveResult result;
  if (!_networkSettings) {
    result.errorCode    = "INTERNAL_ERROR";
    result.errorMessage = "Network settings unavailable";
    result.httpStatus   = 503;
    return result;
  }

  const char *modeLabel = body["addressMode"] | "dhcp";
  NetworkSettings settings = _networkSettings->settings();
  settings.addressMode = parseEthernetAddressMode(modeLabel);
  settings.provisioned = true;

  if (settings.addressMode == EthernetAddressMode::Static) {
    settings.staticIp         = trimCopy(body["staticIp"] | "");
    settings.staticSubnetMask = trimCopy(body["staticSubnetMask"] | "");
    settings.staticGateway    = trimCopy(body["staticGateway"] | "");
    settings.staticDnsPrimary = trimCopy(body["staticDnsPrimary"] | "");
    settings.staticDnsSecondary = trimCopy(body["staticDnsSecondary"] | "");

    if (!validateIpv4(settings.staticIp) || !validateIpv4(settings.staticGateway) ||
        !validateIpv4(settings.staticSubnetMask)) {
      result.errorCode    = "INVALID_IPV4";
      result.errorMessage = "Static IP, gateway, and subnet mask must be valid IPv4 values";
      return result;
    }
    if (!settings.staticDnsPrimary.isEmpty() &&
        !validateIpv4(settings.staticDnsPrimary)) {
      result.errorCode    = "INVALID_IPV4";
      result.errorMessage = "Primary DNS must be a valid IPv4 address";
      return result;
    }
    if (!settings.staticDnsSecondary.isEmpty() &&
        !validateIpv4(settings.staticDnsSecondary)) {
      result.errorCode    = "INVALID_IPV4";
      result.errorMessage = "Secondary DNS must be a valid IPv4 address";
      return result;
    }
    if (settings.staticIp == settings.staticGateway) {
      result.errorCode    = "IP_GATEWAY_CONFLICT";
      result.errorMessage = "Static IP must not equal the gateway address";
      return result;
    }
  }

  String saveError;
  if (!_networkSettings->save(settings, &saveError)) {
    result.errorCode    = "NETWORK_SAVE_FAILED";
    result.errorMessage = saveError.isEmpty() ? "Unable to save network settings" : saveError;
    result.httpStatus   = 500;
    return result;
  }

  _ethernetConfigured = true;
  persist();
  result.success      = true;
  result.httpStatus   = 200;
  result.errorMessage = "Ethernet settings saved";
  return result;
}

SetupWizardConfigManager::SaveResult SetupWizardConfigManager::saveGuestWifi(
    JsonObjectConst body) {
  SaveResult result;

  const String ssid = trimCopy(body["ssid"] | "");
  const String confirm = body["confirmPassword"] | "";
  const String password = body["password"] | "";
  const String securityMode = trimCopy(body["securityMode"] | "wpa2-personal");
  const String portalName = trimCopy(body["portalDisplayName"] | "Renz-Fi WiFi");

  if (ssid.isEmpty() || ssid.length() > 32) {
    result.errorCode    = "SSID_INVALID";
    result.errorMessage = "Guest Wi-Fi SSID must be 1–32 characters";
    return result;
  }
  if (securityMode != "wpa2-personal") {
    result.errorCode    = "SECURITY_MODE_UNSUPPORTED";
    result.errorMessage = "Only WPA2-Personal is supported in this version";
    return result;
  }
  if (portalName.isEmpty() || portalName.length() > 64) {
    result.errorCode    = "PORTAL_NAME_INVALID";
    result.errorMessage = "Portal display name must be 1–64 characters";
    return result;
  }

  String protectedPassword = _guestWifi.passwordProtected;
  if (!password.isEmpty()) {
    if (password.length() < 8) {
      result.errorCode    = "PASSWORD_TOO_SHORT";
      result.errorMessage = "Guest Wi-Fi password must be at least 8 characters";
      return result;
    }
    if (password != confirm) {
      result.errorCode    = "PASSWORD_MISMATCH";
      result.errorMessage = "Guest Wi-Fi passwords do not match";
      return result;
    }
    if (!CredentialProtector::protectSecret(password, protectedPassword)) {
      result.errorCode    = "CREDENTIAL_PROTECT_FAILED";
      result.errorMessage = "Unable to protect guest Wi-Fi password";
      result.httpStatus   = 500;
      return result;
    }
  } else if (protectedPassword.isEmpty()) {
    result.errorCode    = "PASSWORD_REQUIRED";
    result.errorMessage = "Guest Wi-Fi password is required";
    return result;
  }

  _guestWifi.ssid                = ssid;
  _guestWifi.securityMode        = securityMode;
  _guestWifi.portalDisplayName   = portalName;
  _guestWifi.passwordProtected   = protectedPassword;
  _guestWifi.configured          = true;
  persist();

  result.success      = true;
  result.httpStatus   = 200;
  result.errorMessage = "Guest Wi-Fi settings saved";
  return result;
}

SetupWizardConfigManager::SaveResult SetupWizardConfigManager::saveApDeployment(
    JsonObjectConst body) {
  SaveResult result;
  _apMode = SetupWizard::parseApDeploymentMode(body["mode"] | "mikrotik_only");
  _apDeploymentConfigured = true;
  persist();
  result.success      = true;
  result.httpStatus   = 200;
  result.errorMessage = "AP deployment choice saved";
  return result;
}

bool SetupWizardConfigManager::syncCoinSettingsToStorage() {
  if (!_storage) return false;

  DynamicJsonDocument doc(kDocCapacity);
  if (!_storage->readJson(StoragePaths::SettingsFile, doc)) {
    doc.clear();
  }
  JsonObject coin = doc["coin"].to<JsonObject>();
  JsonArray rates = coin["rateTable"].to<JsonArray>();
  rates.clear();
  for (uint8_t i = 0; i < _coinSetup.rateCount; ++i) {
    JsonObject row = rates.add<JsonObject>();
    row["pesos"]   = _coinSetup.rates[i].pesos;
    row["minutes"] = _coinSetup.rates[i].minutes;
  }
  coin["abuseCount"]     = _coinSetup.abuseCount;
  coin["banMinutes"]     = _coinSetup.banMinutes;
  coin["resetWindowSec"] = _coinSetup.resetWindowSec;
  if (_coinSetup.rateCount > 0) {
    coin["defaultMinutesPerPeso"] = _coinSetup.rates[0].minutes;
  }
  if (!_storage->writeJson(StoragePaths::SettingsFile, doc)) return false;
  if (_coin) {
    DynamicJsonDocument reload(kDocCapacity);
    if (_storage->readJson(StoragePaths::SettingsFile, reload)) {
      JsonObjectConst coinObj = reload["coin"];
      if (!coinObj.isNull()) {
        CoinSettings settings;
        // CoinManager reload happens on next access; trigger via saveSettings path.
        (void)settings;
      }
    }
  }
  return true;
}

SetupWizardConfigManager::SaveResult SetupWizardConfigManager::saveCoinSetup(
    JsonObjectConst body) {
  SaveResult result;

  SetupWizard::CoinSetupConfig trial = _coinSetup;
  JsonArrayConst rates = body["rates"];
  if (!rates.isNull()) {
    trial.rateCount = 0;
    for (JsonObjectConst row : rates) {
      if (trial.rateCount >= SetupWizard::CoinSetupConfig::kMaxRates) break;
      const uint8_t pesos = row["pesos"] | 0;
      const uint16_t minutes = row["minutes"] | 0;
      if (pesos == 0 || minutes == 0) continue;
      trial.rates[trial.rateCount++] = {pesos, minutes};
    }
  }
  trial.abuseCount     = body["abuseCount"] | trial.abuseCount;
  trial.banMinutes     = body["banMinutes"] | trial.banMinutes;
  trial.resetWindowSec = body["resetWindowSec"] | trial.resetWindowSec;

  String validationError;
  if (!validateCoinSetup(trial, validationError)) {
    result.errorCode    = "COIN_SETUP_INVALID";
    result.errorMessage = validationError;
    return result;
  }

  _coinSetup      = trial;
  _coinConfigured = true;
  if (!persist() || !syncCoinSettingsToStorage()) {
    result.errorCode    = "STORAGE_WRITE_FAILED";
    result.errorMessage = "Unable to persist coin settings";
    result.httpStatus   = 500;
    return result;
  }

  result.success      = true;
  result.httpStatus   = 200;
  result.errorMessage = "Coin settings saved";
  return result;
}

SetupWizardConfigManager::SaveResult SetupWizardConfigManager::saveOperator(
    JsonObjectConst body, const String &passwordHash) {
  SaveResult result;

  const String username = trimCopy(body["username"] | "");
  if (!isValidUsername(username)) {
    result.errorCode    = "USERNAME_INVALID";
    result.errorMessage =
        "Operator username must be 3–32 characters (letters, numbers, underscore, hyphen)";
    return result;
  }
  if (passwordHash.isEmpty()) {
    result.errorCode    = "PASSWORD_INVALID";
    result.errorMessage = "Operator password hash missing";
    result.httpStatus   = 500;
    return result;
  }

  _operator.username     = username;
  _operator.passwordHash = passwordHash;
  _operator.configured   = true;
  persist();

  result.success      = true;
  result.httpStatus   = 200;
  result.errorMessage = "Operator account saved";
  return result;
}

void SetupWizardConfigManager::reconcileOperatorCredentials(
    const AuthManager &auth) {
  if (!_operator.configured) return;
  if (auth.hasOperatorNvsCredentials()) return;

  _operator.configured   = false;
  _operator.username     = "";
  _operator.passwordHash = "";
  persist();
  Serial.println(
      F("[auth] operator configuration incomplete; re-create operator account "
        "required"));
}

void SetupWizardConfigManager::fillSafeStatus(JsonObject data) const {
  JsonObject wizard = data["wizard"].to<JsonObject>();
  wizard["ethernetConfigured"]     = _ethernetConfigured;
  wizard["guestWifiConfigured"]    = _guestWifi.configured;
  wizard["apDeploymentConfigured"] = _apDeploymentConfigured;
  wizard["coinConfigured"]         = _coinConfigured;
  wizard["operatorConfigured"]     = _operator.configured;
  wizard["apDeploymentMode"]       = SetupWizard::apDeploymentModeLabel(_apMode);

  if (_guestWifi.configured) {
    JsonObject guest = wizard["guestWifi"].to<JsonObject>();
    guest["ssid"]              = _guestWifi.ssid;
    guest["securityMode"]      = _guestWifi.securityMode;
    guest["portalDisplayName"] = _guestWifi.portalDisplayName;
    guest["hasSavedPassword"]  = !_guestWifi.passwordProtected.isEmpty();
  }
  if (_operator.configured) {
    wizard["operatorUsername"] = _operator.username;
  }
}

void SetupWizardConfigManager::fillReviewSummary(JsonObject data) const {
  fillSafeStatus(data);
  JsonObject coin = data["coinSetup"].to<JsonObject>();
  JsonArray rates = coin["rates"].to<JsonArray>();
  for (uint8_t i = 0; i < _coinSetup.rateCount; ++i) {
    JsonObject row = rates.add<JsonObject>();
    row["pesos"]   = _coinSetup.rates[i].pesos;
    row["minutes"] = _coinSetup.rates[i].minutes;
  }
  coin["abuseCount"]     = _coinSetup.abuseCount;
  coin["banMinutes"]     = _coinSetup.banMinutes;
  coin["resetWindowSec"] = _coinSetup.resetWindowSec;
}
