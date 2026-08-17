#include "SystemHealthService.h"

#include "CoinManager.h"
#include "Config.h"
#include "EthernetManager.h"
#include "RgbController.h"

void SystemHealthService::begin(EthernetManager *eth, StorageManager *storage,
                                CoinManager *coin, RgbController *rgb) {
  _eth = eth;
  _storage = storage;
  _coin = coin;
  _rgb = rgb;
}

SystemHealthLevel SystemHealthService::overallLevel() const {
  if (_rgb) return _rgb->systemStatusLevel();
  if (_eth && (!_eth->driverReady() || !_eth->linkUp())) {
    return SystemHealthLevel::Error;
  }
  if (_storage && (_storage->usingFallback() || !_storage->healthy())) {
    return SystemHealthLevel::Warning;
  }
  return SystemHealthLevel::Healthy;
}

void SystemHealthService::fillHealth(JsonObject out) const {
  JsonObject ethernet = out["ethernet"].to<JsonObject>();
  ethernet["driver"] = (_eth && _eth->driverReady()) ? "UP" : "DOWN";
  ethernet["link"] = (_eth && _eth->linkUp()) ? "UP" : "DOWN";
  ethernet["ip"] = _eth ? _eth->ip() : "";
  ethernet["gateway"] = _eth ? _eth->gateway() : "";
  ethernet["netmask"] = _eth ? _eth->subnet() : "";
  ethernet["dns"] = _eth ? _eth->dns() : "";
  ethernet["mac"] = _eth ? _eth->macAddress() : "";
  ethernet["mode"] = _eth ? _eth->addressModeLabel() : "dhcp";

  if (_storage) {
    _storage->fillStorageStatus(out["storage"].to<JsonObject>());
  }

  if (_coin) {
    _coin->fillCoinStatus(out["coin"].to<JsonObject>());
    JsonObject coin = out["coin"];
    coin["ok"] = _coin->state() != CoinState::Fault;
    coin["hardwareState"] = CoinManager::stateLabel(_coin->state());
  } else {
    JsonObject coin = out["coin"].to<JsonObject>();
    coin["enabled"] = false;
    coin["state"] = "DISABLED";
    coin["hardwareState"] = "DISABLED";
    coin["ok"] = false;
  }

  if (_rgb) {
    _rgb->fillStatus(out["rgb"].to<JsonObject>());
  }

  JsonObject memory = out["memory"].to<JsonObject>();
  memory["heap"] = ESP.getFreeHeap();
  memory["minimumHeap"] = ESP.getMinFreeHeap();

  switch (overallLevel()) {
    case SystemHealthLevel::Error:
      out["level"] = "ERROR";
      break;
    case SystemHealthLevel::Warning:
      out["level"] = "WARNING";
      break;
    case SystemHealthLevel::ActiveSession:
      out["level"] = "ACTIVE_SESSION";
      break;
    case SystemHealthLevel::Healthy:
    default:
      out["level"] = "HEALTHY";
      break;
  }
}
