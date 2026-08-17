#include "DeviceIdentity.h"

#include <esp_mac.h>
#include <esp_random.h>

#include "Config.h"
#include "EthernetManager.h"
#include "StorageManager.h"
#include "router/RouterPlatform.h"

namespace DeviceIdentity {
namespace {

DynamicJsonDocument g_cachedProfile(RenzFiConfig::JSON_DOC_MEDIUM);
bool g_profileValid = false;

String resolveMacAddress(EthernetManager *eth) {
  if (eth && eth->driverReady()) {
    const String ethMac = eth->macAddress();
    if (ethMac.length() > 0) return ethMac;
  }
  return stableChipMacAddress();
}

String resolveRouterDriverId(RouterPlatform *router) {
  if (!router || !router->activeDriver()) return String();
  const RouterDriverManifest manifest = router->driverManifest();
  if (manifest.driverId && strlen(manifest.driverId) > 0) {
    return String(manifest.driverId);
  }
  return String();
}

}  // namespace

String formatDeviceId(const String &macAddress) {
  String hex;
  hex.reserve(12);
  for (size_t i = 0; i < macAddress.length(); ++i) {
    const char c = macAddress.charAt(i);
    if (c == ':' || c == '-') continue;
    hex += static_cast<char>(toupper(static_cast<unsigned char>(c)));
  }
  if (hex.length() < 6) return String("RF-000000");
  return String("RF-") + hex.substring(hex.length() - 6);
}

String readFriendlyName(StorageManager *storage) {
  if (!storage) return String("Renz-Fi Appliance");

  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_SMALL);
  if (!storage->readJson(RenzFiConfig::SETTINGS_FILE, doc)) {
    return String("Renz-Fi Appliance");
  }
  const char *name = doc["device"]["name"] | "Renz-Fi Appliance";
  return String(name);
}

String stableChipMacAddress() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY);
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
           mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

String bootInstanceId() {
  static String cached;
  if (cached.length() == 0) {
    cached = String(esp_random(), HEX) + String(esp_random(), HEX);
  }
  return cached;
}

void fillCapabilities(JsonObject caps, RouterPlatform *router,
                      bool coinEnabled) {
  const String routerDriver = resolveRouterDriverId(router);

  caps["coin"]        = coinEnabled;
  caps["voucher"]     = true;
  caps["assetUpload"] = true;
  caps["fleet"]       = true;
  if (!routerDriver.isEmpty()) {
    caps["router"] = routerDriver;
  } else {
    caps["router"] = nullptr;
  }
}

void fillProfile(JsonObject out, EthernetManager *eth, StorageManager *storage,
                 RouterPlatform *router, bool coinEnabled) {
  const String mac = resolveMacAddress(eth);
  const String routerDriver = resolveRouterDriverId(router);
  const String friendlyName = readFriendlyName(storage);
  const String firmwareVersion = RenzFiConfig::FIRMWARE_VERSION;
  const bool online = eth && eth->isServiceReady();

  out["deviceId"]          = formatDeviceId(mac);
  out["serialNumber"]      = mac;
  out["friendlyName"]      = friendlyName;
  out["deviceName"]        = friendlyName;
  out["firmwareVersion"]   = firmwareVersion;
  out["version"]           = firmwareVersion;
  out["hardwareRevision"]  = RenzFiConfig::HARDWARE_REVISION;
  out["macAddress"]        = mac;
  out["ipAddress"]         = (eth && eth->hasIp()) ? eth->ip() : String("");
  if (!routerDriver.isEmpty()) {
    out["routerDriver"] = routerDriver;
  } else {
    out["routerDriver"] = nullptr;
  }
  out["online"] = online;
  fillCapabilities(out["capabilities"].to<JsonObject>(), router, coinEnabled);
}

void refreshRuntimeProfile(EthernetManager *eth, StorageManager *storage,
                           RouterPlatform *router, bool coinEnabled) {
  g_cachedProfile.clear();
  fillProfile(g_cachedProfile.to<JsonObject>(), eth, storage, router,
              coinEnabled);
  g_profileValid = true;
}

void invalidateRuntimeProfile() { g_profileValid = false; }

void fillRuntimeProfile(JsonObject out) {
  if (!g_profileValid) return;
  out.set(g_cachedProfile.as<JsonObjectConst>());
}

}  // namespace DeviceIdentity
