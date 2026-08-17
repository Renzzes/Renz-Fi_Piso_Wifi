#include "NetworkSettingsManager.h"

#include "Config.h"
#include "StorageManager.h"
#include "W5500Config.h"

#include <IPAddress.h>
#include <Preferences.h>

namespace {

bool isValidIpString(const String &value) {
  if (value.isEmpty()) return false;
  IPAddress ip;
  return ip.fromString(value);
}

// A valid IPv4 subnet mask is a contiguous run of 1 bits followed by 0 bits
// (e.g. 255.255.255.0). Rejects garbage like "255.0.255.0".
bool isValidSubnetMaskString(const String &value) {
  IPAddress mask;
  if (!mask.fromString(value)) return false;
  uint32_t bits = (uint32_t(mask[0]) << 24) | (uint32_t(mask[1]) << 16) |
                  (uint32_t(mask[2]) << 8) | uint32_t(mask[3]);
  // Count trailing zero run, then confirm remaining high bits are all 1s.
  uint32_t inverted = ~bits;
  // inverted+1 is a power of two only when `inverted` is all-1s from LSB
  // (i.e. bits was a valid mask), except the all-zero (mask=0) case which
  // we explicitly allow to fail validation (no usable subnet).
  if (bits == 0) return false;
  return (inverted & (inverted + 1)) == 0;
}

void writeNetworkObject(JsonObject network, const NetworkSettings &settings) {
  network["addressMode"] = ethernetAddressModeLabel(settings.addressMode);
  network["staticIp"] = settings.staticIp;
  network["staticGateway"] = settings.staticGateway;
  network["staticSubnetMask"] = settings.staticSubnetMask;
  network["staticDnsPrimary"] = settings.staticDnsPrimary;
  network["staticDnsSecondary"] = settings.staticDnsSecondary;
  network["provisioned"] = settings.provisioned;
  network["managementAp"]["keepEnabledAfterSetup"] =
      settings.managementApKeepEnabledAfterSetup;

  // Legacy keys — kept for any external reader of older field names.
  // Not read back by applyNetworkObject() unless the new keys are absent.
  network["ip"] = settings.staticIp;
  network["gateway"] = settings.staticGateway;
  network["subnet"] = settings.staticSubnetMask;
  network["dns"] = settings.staticDnsPrimary;
}

void applyNetworkObject(JsonObjectConst network, NetworkSettings &out) {
  if (network.isNull()) return;

  if (network["addressMode"].is<const char *>()) {
    out.addressMode = parseEthernetAddressMode(network["addressMode"].as<const char *>());
  }

  // New field names take priority; fall back to legacy ip/gateway/subnet/dns
  // so pre-Phase-B settings.json files keep working (tolerant optional
  // fields, no schema version bump — matches project convention).
  if (network["staticIp"].is<const char *>()) {
    out.staticIp = network["staticIp"].as<const char *>();
  } else if (network["ip"].is<const char *>()) {
    out.staticIp = network["ip"].as<const char *>();
  }

  if (network["staticGateway"].is<const char *>()) {
    out.staticGateway = network["staticGateway"].as<const char *>();
  } else if (network["gateway"].is<const char *>()) {
    out.staticGateway = network["gateway"].as<const char *>();
  }

  if (network["staticSubnetMask"].is<const char *>()) {
    out.staticSubnetMask = network["staticSubnetMask"].as<const char *>();
  } else if (network["subnet"].is<const char *>()) {
    out.staticSubnetMask = network["subnet"].as<const char *>();
  }

  if (network["staticDnsPrimary"].is<const char *>()) {
    out.staticDnsPrimary = network["staticDnsPrimary"].as<const char *>();
  } else if (network["dns"].is<const char *>()) {
    out.staticDnsPrimary = network["dns"].as<const char *>();
  }

  if (network["staticDnsSecondary"].is<const char *>()) {
    out.staticDnsSecondary = network["staticDnsSecondary"].as<const char *>();
  }

  if (network["provisioned"].is<bool>()) {
    out.provisioned = network["provisioned"].as<bool>();
  }

  JsonObjectConst mgmtAp = network["managementAp"];
  if (!mgmtAp.isNull()) {
    if (mgmtAp["keepEnabledAfterSetup"].is<bool>()) {
      out.managementApKeepEnabledAfterSetup =
          mgmtAp["keepEnabledAfterSetup"].as<bool>();
    }
  }
}

void loadNvsInto(NetworkSettings &settings, bool readOnly) {
  Preferences prefs;
  if (!prefs.begin(RenzFiConfig::NVS_NETWORK_NS, readOnly)) return;

  const String modeStr = prefs.getString(
      "addrMode", ethernetAddressModeLabel(settings.addressMode));
  settings.addressMode = parseEthernetAddressMode(modeStr.c_str());
  settings.staticIp = prefs.getString("ip", settings.staticIp);
  settings.staticGateway = prefs.getString("gateway", settings.staticGateway);
  settings.staticSubnetMask = prefs.getString("subnet", settings.staticSubnetMask);
  settings.staticDnsPrimary = prefs.getString("dns", settings.staticDnsPrimary);
  settings.staticDnsSecondary =
      prefs.getString("dns2", settings.staticDnsSecondary);
  settings.provisioned = prefs.getBool("provisioned", settings.provisioned);
  settings.managementApKeepEnabledAfterSetup =
      prefs.getBool("mgmtApKeep", settings.managementApKeepEnabledAfterSetup);
  prefs.end();
}

void saveNvsFrom(const NetworkSettings &settings) {
  Preferences prefs;
  if (!prefs.begin(RenzFiConfig::NVS_NETWORK_NS, false)) return;
  prefs.putString("addrMode", ethernetAddressModeLabel(settings.addressMode));
  prefs.putString("ip", settings.staticIp);
  prefs.putString("gateway", settings.staticGateway);
  prefs.putString("subnet", settings.staticSubnetMask);
  prefs.putString("dns", settings.staticDnsPrimary);
  prefs.putString("dns2", settings.staticDnsSecondary);
  prefs.putBool("provisioned", settings.provisioned);
  prefs.putBool("mgmtApKeep", settings.managementApKeepEnabledAfterSetup);
  prefs.end();
}

}  // namespace

NetworkSettings NetworkSettingsManager::factoryDefaults() {
  NetworkSettings defaults;
  defaults.addressMode = EthernetAddressMode::Dhcp;
  defaults.staticIp = W5500Config::IP.toString();
  defaults.staticGateway = W5500Config::GATEWAY.toString();
  defaults.staticSubnetMask = W5500Config::SUBNET.toString();
  defaults.staticDnsPrimary = W5500Config::DNS.toString();
  defaults.staticDnsSecondary = "";
  defaults.provisioned = false;
  return defaults;
}

bool NetworkSettingsManager::validateStaticConfig(const NetworkSettings &settings,
                                                  String *errorOut) {
  if (settings.addressMode != EthernetAddressMode::Static) return true;

  if (!isValidIpString(settings.staticIp)) {
    if (errorOut) *errorOut = "Invalid static IP address";
    return false;
  }
  if (!isValidIpString(settings.staticGateway)) {
    if (errorOut) *errorOut = "Invalid gateway address";
    return false;
  }
  if (!isValidSubnetMaskString(settings.staticSubnetMask)) {
    if (errorOut) *errorOut = "Invalid subnet mask";
    return false;
  }
  if (!isValidIpString(settings.staticDnsPrimary)) {
    if (errorOut) *errorOut = "Invalid primary DNS address";
    return false;
  }
  if (settings.staticDnsSecondary.length() > 0 &&
      !isValidIpString(settings.staticDnsSecondary)) {
    if (errorOut) *errorOut = "Invalid secondary DNS address";
    return false;
  }
  return true;
}

NetworkSettings NetworkSettingsManager::loadNvsOnly() {
  NetworkSettings settings = factoryDefaults();
  loadNvsInto(settings, /*readOnly=*/true);

  // Corrupt/partial NVS static data must never block boot — fall back to
  // DHCP + unprovisioned rather than trusting an unvalidated static config.
  if (settings.addressMode == EthernetAddressMode::Static &&
      !validateStaticConfig(settings)) {
    settings.addressMode = EthernetAddressMode::Dhcp;
  }
  return settings;
}

void NetworkSettingsManager::applyRecoveryResetNvs() {
  const NetworkSettings defaults = factoryDefaults();
  saveNvsFrom(defaults);
  Preferences prefs;
  if (prefs.begin(RenzFiConfig::NVS_NETWORK_NS, false)) {
    prefs.putBool("syncToSd", true);
    prefs.end();
  }
}

void NetworkSettingsManager::loadFromNvs() {
  _settings = factoryDefaults();
  loadNvsInto(_settings, /*readOnly=*/true);
}

void NetworkSettingsManager::saveToNvs() const {
  saveNvsFrom(_settings);
}

bool NetworkSettingsManager::loadFromSettingsJson() {
  if (!_storage) return false;

  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  if (!_storage->readJson(RenzFiConfig::SETTINGS_FILE, doc)) return false;

  JsonObject network = doc["network"];
  if (network.isNull()) return false;

  applyNetworkObject(network, _settings);
  return true;
}

bool NetworkSettingsManager::saveToSettingsJson() const {
  if (!_storage) return false;

  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  if (!_storage->readJson(RenzFiConfig::SETTINGS_FILE, doc)) return false;

  writeNetworkObject(doc["network"].to<JsonObject>(), _settings);
  return _storage->writeJson(RenzFiConfig::SETTINGS_FILE, doc);
}

void NetworkSettingsManager::begin(StorageManager *storage) {
  _storage = storage;
  _settings = factoryDefaults();
  loadFromNvs();

  bool syncToSd = false;
  Preferences prefs;
  if (prefs.begin(RenzFiConfig::NVS_NETWORK_NS, true)) {
    syncToSd = prefs.getBool("syncToSd", false);
    prefs.end();
  }

  if (_storage) {
    DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
    if (_storage->readJson(RenzFiConfig::SETTINGS_FILE, doc)) {
      JsonObject network = doc["network"];
      if (syncToSd || network.isNull()) {
        saveToSettingsJson();
        if (prefs.begin(RenzFiConfig::NVS_NETWORK_NS, false)) {
          prefs.putBool("syncToSd", false);
          prefs.end();
        }
      } else {
        applyNetworkObject(network, _settings);
        saveToNvs();
      }
    }
  }

  // Corrupt/incomplete/missing static config anywhere in the resolved
  // settings must never brick Ethernet or hide the Management AP — always
  // fall back to DHCP mode when static validation fails.
  if (_settings.addressMode == EthernetAddressMode::Static &&
      !validateStaticConfig(_settings)) {
    Serial.println("[net-settings] Stored static config invalid — falling back to DHCP");
    _settings.addressMode = EthernetAddressMode::Dhcp;
  }
}

NetworkSettings NetworkSettingsManager::settings() const {
  return _settings;
}

bool NetworkSettingsManager::save(const NetworkSettings &settings, String *errorOut) {
  if (settings.addressMode == EthernetAddressMode::Static &&
      !validateStaticConfig(settings, errorOut)) {
    return false;
  }

  _settings = settings;
  saveToNvs();
  return saveToSettingsJson();
}

void NetworkSettingsManager::resetToDefaults() {
  _settings = factoryDefaults();
  saveToNvs();
  saveToSettingsJson();
}
