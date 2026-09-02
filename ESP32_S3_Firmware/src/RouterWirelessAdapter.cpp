#include "RouterWirelessAdapter.h"

#include "Config.h"
#include "DmaMemoryMonitor.h"
#include "RouterCommandScratch.h"
#include "RouterApiTransportGate.h"
#include "RouterProvisioningTypes.h"
#include "RouterWorkerDiagnostics.h"
#include "StorageManager.h"
#include "StoragePaths.h"
#include "RenzFiDebug.h"

#if RENZFI_DEBUG_ROUTER
#define RENZFI_WL_LOG(...) Serial.printf(__VA_ARGS__)
#define RENZFI_WL_LN(msg)  Serial.println(msg)
#else
#define RENZFI_WL_LOG(...) ((void)0)
#define RENZFI_WL_LN(msg)  ((void)0)
#endif

namespace {

bool replyAttr(const RouterOsClient::CommandResult &result, uint8_t replyIdx,
               const char *key, String &valueOut) {
  valueOut = "";
  if (replyIdx >= result.replyCount) return false;
  const RouterOsClient::ReplyRecord &rec = result.replyAt(replyIdx);
  for (uint8_t i = 0; i < rec.attrCount; ++i) {
    String k, v;
    if (!RouterOsClient::parseAttr(rec.attr(i), k, v)) continue;
    if (k == key) {
      valueOut = v;
      return true;
    }
  }
  return false;
}

String normalizeBandLabel(const String &raw) {
  return RouterWireless::formatBandLabel(raw);
}

// Feeds the MikroTik CPU-protection gate from a /system/resource/print reply
// that was already fetched for another reason. Never triggers a dedicated
// poll of its own — see RouterApiTransportGate::recordObservedCpuLoad.
// Returns true when a supported cpu-load attribute was parsed; false leaves
// CPU as UNKNOWN (255) — never invents a value.
bool parseCpuLoadFromResource(const RouterOsClient::CommandResult &resource,
                              uint8_t &percentOut, String &rawOut) {
  percentOut = 255;
  rawOut.clear();
  if (resource.trapReceived || resource.replyCount == 0) return false;

  auto parsePercentToken = [](const String &token, int &percentOutLocal) -> bool {
    String trimmed = token;
    trimmed.trim();
    if (trimmed.endsWith("%")) {
      trimmed.remove(trimmed.length() - 1);
      trimmed.trim();
    }
    if (trimmed.isEmpty()) return false;
    const long value = trimmed.toInt();
    if (value < 0 || value > 100) return false;
    percentOutLocal = static_cast<int>(value);
    return true;
  };

  auto parsePercentListMax = [&](const String &value, int &percentOutLocal) -> bool {
    int maxPercent = -1;
    int start      = 0;
    while (start <= value.length()) {
      int comma = value.indexOf(',', start);
      const String token =
          comma < 0 ? value.substring(start) : value.substring(start, comma);
      int parsed = -1;
      if (parsePercentToken(token, parsed)) {
        if (parsed > maxPercent) maxPercent = parsed;
      }
      if (comma < 0) break;
      start = comma + 1;
    }
    if (maxPercent < 0) return false;
    percentOutLocal = maxPercent;
    return true;
  };

  // Temporary verification logging (BUG #9) — log every attribute before parsing.
  for (uint8_t r = 0; r < resource.replyCount; ++r) {
    for (uint8_t i = 0; i < resource.replyAt(r).attrCount; ++i) {
      String key, value;
      if (!RouterOsClient::parseAttr(resource.replyAt(r).attr(i), key, value)) continue;
      Serial.printf("[router-api] resource raw attr reply=%u %s=%s\n",
                    static_cast<unsigned>(r), key.c_str(), value.c_str());
    }
  }

  // Prefer the overall cpu-load attribute (RouterOS v6/v7/stable/long-term).
  for (uint8_t r = 0; r < resource.replyCount; ++r) {
    for (uint8_t i = 0; i < resource.replyAt(r).attrCount; ++i) {
      String key, value;
      if (!RouterOsClient::parseAttr(resource.replyAt(r).attr(i), key, value)) continue;
      if (key != "cpu-load") continue;
      rawOut = value;
      int parsed = -1;
      if (!parsePercentToken(value, parsed)) {
        Serial.printf("[router-api] cpu-load parse failed raw=%s (treating as UNKNOWN)\n",
                      rawOut.c_str());
        return false;
      }
      percentOut = static_cast<uint8_t>(parsed);
      Serial.printf("[router-api] cpu-load parsed raw=%s percent=%u\n", rawOut.c_str(),
                    static_cast<unsigned>(percentOut));
      return true;
    }
  }

  // Fallback: per-CPU load list (some RouterOS builds omit overall cpu-load).
  for (uint8_t r = 0; r < resource.replyCount; ++r) {
    for (uint8_t i = 0; i < resource.replyAt(r).attrCount; ++i) {
      String key, value;
      if (!RouterOsClient::parseAttr(resource.replyAt(r).attr(i), key, value)) continue;
      if (key != "cpu-load-per-cpu") continue;
      rawOut = value;
      int parsed = -1;
      if (!parsePercentListMax(value, parsed)) {
        Serial.printf(
            "[router-api] cpu-load-per-cpu parse failed raw=%s (treating as UNKNOWN)\n",
            rawOut.c_str());
        return false;
      }
      percentOut = static_cast<uint8_t>(parsed);
      Serial.printf("[router-api] cpu-load-per-cpu parsed raw=%s percent=%u\n",
                    rawOut.c_str(), static_cast<unsigned>(percentOut));
      return true;
    }
  }

  Serial.println("[router-api] cpu-load attribute missing (treating as UNKNOWN)");
  return false;
}

void recordCpuLoadFromResource(const RouterOsClient::CommandResult &resource) {
  uint8_t percent = 255;
  String raw;
  if (!parseCpuLoadFromResource(resource, percent, raw)) return;
  RouterApiTransportGate::recordObservedCpuLoad(percent);
}

String classifyInterfaceStatus(bool enabled, bool running, bool hidden,
                               const String &ssid) {
  if (!enabled) return "disabled";
  if (ssid.isEmpty()) return "no_ssid";
  if (hidden) return "hidden";
  if (running) return "running";
  return "configured";
}

bool isOpenAuthTypes(const String &authTypes) {
  if (authTypes.isEmpty()) return true;
  String lower = authTypes;
  lower.toLowerCase();
  return lower == "none" || lower == "open";
}

bool appendNetworkRow(JsonArray out, const String &id, const String &ssid,
                      bool enabled, bool running, bool hidden,
                      const String &bandRaw, const String &driver,
                      const String &securityProfile, bool securityOpen) {
  if (id.isEmpty()) return false;
  for (JsonVariant v : out) {
    JsonObject existing = v.as<JsonObject>();
    if (existing["id"] == id) return false;
  }
  const String status = classifyInterfaceStatus(enabled, running, hidden, ssid);
  JsonObject row      = out.createNestedObject();
  row["id"]           = id;
  row["ssid"]         = ssid;
  row["enabled"]      = enabled;
  row["running"]      = running;
  row["hidden"]       = hidden;
  row["status"]       = status;
  row["driver"]       = driver;
  row["band"]         = normalizeBandLabel(bandRaw);
  if (!securityProfile.isEmpty()) row["securityProfile"] = securityProfile;
  row["securityOpen"] = securityOpen;
  return true;
}

bool lookupWifiConfigurationSsid(RouterOsClient &client, const String &configName,
                                 String &ssidOut) {
  ssidOut = "";
  if (configName.isEmpty()) return false;
  RouterOsClient::CommandResult &configs = RouterCommandScratchContext::acquire();
  const String filter[] = {"?name=" + configName};
  if (!client.executeCommand("/interface/wifi/configuration/print", filter, 1,
                             configs) ||
      configs.trapReceived) {
    return false;
  }
  return replyAttr(configs, 0, "ssid", ssidOut) && !ssidOut.isEmpty();
}

// Optional scratch reuse: when called in a tight per-row loop (discovery),
// callers pass a scratch buffer allocated once outside the loop instead of
// allocating/freeing on every iteration (heap churn/fragmentation guard).
bool isSecurityProfileOpen(RouterOsClient &client, const String &profileName,
                           RouterOsClient::CommandResult *scratchInOut = nullptr) {
  if (profileName.isEmpty() || profileName == "default") return true;
  RouterOsClient::CommandResult *sec = scratchInOut;
  if (!sec) {
    sec = &RouterCommandScratchContext::acquire();
  } else {
    RouterOsClient::initializeCommandResult(*sec);
  }
  const String filter[] = {"?name=" + profileName};
  if (!client.executeCommand("/interface/wireless/security-profiles/print", filter,
                             1, *sec) ||
      sec->trapReceived || sec->replyCount == 0) {
    return false;
  }
  String authTypes, psk;
  replyAttr(*sec, 0, "authentication-types", authTypes);
  replyAttr(*sec, 0, "wpa2-pre-shared-key", psk);
  return isOpenAuthTypes(authTypes) && psk.isEmpty();
}

bool bridgeHasInterface(RouterOsClient &client, const String &bridgeName,
                        const String &ifaceName) {
  RouterOsClient::CommandResult &ports = RouterCommandScratchContext::acquire();
  // Bound proplist — full port rows exceed ReplyRecord::MAX_ATTRS (24) and
  // only bridge+interface are required for membership checks.
  const String portAttrs[] = {"=.proplist=bridge,interface"};
  if (!client.executeCommand("/interface/bridge/port/print", portAttrs, 1,
                             ports) ||
      ports.trapReceived) {
    return false;
  }
  for (uint8_t i = 0; i < ports.replyCount; ++i) {
    String bridge, iface;
    replyAttr(ports, i, "bridge", bridge);
    replyAttr(ports, i, "interface", iface);
    if (bridge == bridgeName && iface == ifaceName) return true;
  }
  return false;
}

bool ensureBridgePort(RouterOsClient &client, const String &bridgeName,
                      const String &ifaceName, String &errorOut) {
  if (bridgeName.isEmpty()) return true;
  if (bridgeHasInterface(client, bridgeName, ifaceName)) return true;
  const String attrs[] = {"=bridge=" + bridgeName, "=interface=" + ifaceName,
                          "=comment=" + String(RouterProvisioning::COMMENT_PREFIX) +
                              "setup bridge port"};
  RouterOsClient::CommandResult &add = RouterCommandScratchContext::acquire();
  if (!client.executeCommand("/interface/bridge/port/add", attrs, 3, add) ||
      add.trapReceived) {
    errorOut = add.trapMessage.isEmpty() ? "Unable to add interface to guest bridge"
                                         : add.trapMessage;
    return false;
  }
  return true;
}

constexpr const char *kHotspotHtmlDir = "hotspot";

bool resolveInterfaceAddressCidr(RouterOsClient &client, const String &ifaceName,
                                 String &cidrOut) {
  cidrOut = "";
  if (ifaceName.isEmpty()) return false;
  RouterOsClient::CommandResult &addrs = RouterCommandScratchContext::acquire();
  const String filter[] = {
      "?interface=" + ifaceName,
      "=.proplist=.id,address,interface",
  };
  RouterOsClient::initializeCommandResult(addrs);
  if (!client.executeCommand("/ip/address/print", filter, 2, addrs) ||
      addrs.trapReceived || addrs.replyCount == 0) {
    return false;
  }
  String address;
  replyAttr(addrs, 0, "address", address);
  if (address.isEmpty()) return false;
  cidrOut = address;
  return true;
}

bool ensureHotspotProfileHtmlDirectory(RouterOsClient &client,
                                       const String &profileName,
                                       String &htmlDirOut, String &errorOut,
                                       bool *repairedOut) {
  htmlDirOut = "";
  if (repairedOut) *repairedOut = false;
  if (profileName.isEmpty()) {
    errorOut = "Hotspot profile name is empty";
    return false;
  }
  RouterOsClient::CommandResult &result = RouterCommandScratchContext::acquire();
  const String filter[] = {"?name=" + profileName,
                           "=.proplist=.id,name,html-directory"};
  RouterOsClient::initializeCommandResult(result);
  if (!client.executeCommand("/ip/hotspot/profile/print", filter, 2, result) ||
      result.trapReceived) {
    errorOut = "Unable to read hotspot profile";
    return false;
  }
  if (result.replyCount == 0) {
    // Create the managed profile with html-directory=hotspot.
    const String addAttrs[] = {
        "=name=" + profileName,
        "=html-directory=" + String(kHotspotHtmlDir),
        "=comment=" + String(RouterProvisioning::COMMENT_GUEST_HS_PROFILE),
    };
    RouterOsClient::initializeCommandResult(result);
    if (!client.executeCommand("/ip/hotspot/profile/add", addAttrs, 3, result) ||
        result.trapReceived) {
      errorOut = result.trapMessage.isEmpty()
                     ? "Unable to create hotspot profile"
                     : result.trapMessage;
      return false;
    }
    htmlDirOut = kHotspotHtmlDir;
    if (repairedOut) *repairedOut = true;
    Serial.printf("[router-hotspot] profile created name=%s html-directory=%s\n",
                  profileName.c_str(), kHotspotHtmlDir);
    return true;
  }

  String id, htmlDir;
  replyAttr(result, 0, ".id", id);
  replyAttr(result, 0, "html-directory", htmlDir);
  htmlDirOut = htmlDir.isEmpty() ? String(kHotspotHtmlDir) : htmlDir;
  if (htmlDir == kHotspotHtmlDir || id.isEmpty()) return true;

  const String setAttrs[] = {"=.id=" + id,
                             "=html-directory=" + String(kHotspotHtmlDir)};
  RouterOsClient::initializeCommandResult(result);
  if (!client.executeCommand("/ip/hotspot/profile/set", setAttrs, 2, result) ||
      result.trapReceived) {
    errorOut = result.trapMessage.isEmpty()
                   ? "Unable to set hotspot html-directory"
                   : result.trapMessage;
    return false;
  }
  htmlDirOut = kHotspotHtmlDir;
  if (repairedOut) *repairedOut = true;
  Serial.printf("[router-hotspot] html-directory repaired profile=%s -> %s\n",
                profileName.c_str(), kHotspotHtmlDir);
  return true;
}

struct HotspotRow {
  String id;
  String name;
  String iface;
  String profile;
  bool disabled = false;
};

bool loadHotspotRows(RouterOsClient &client, HotspotRow *rows, uint8_t maxRows,
                     uint8_t &countOut, String &errorOut) {
  countOut = 0;
  RouterOsClient::CommandResult &hs = RouterCommandScratchContext::acquire();
  RouterOsClient::initializeCommandResult(hs);
  if (!client.executeCommand("/ip/hotspot/print", hs) || hs.trapReceived) {
    errorOut = hs.trapMessage.isEmpty() ? "Unable to read hotspot servers"
                                        : hs.trapMessage;
    return false;
  }
  for (uint8_t i = 0; i < hs.replyCount && countOut < maxRows; ++i) {
    HotspotRow &row = rows[countOut];
    String disabled;
    replyAttr(hs, i, ".id", row.id);
    replyAttr(hs, i, "name", row.name);
    replyAttr(hs, i, "interface", row.iface);
    replyAttr(hs, i, "profile", row.profile);
    replyAttr(hs, i, "disabled", disabled);
    row.disabled = (disabled == "true");
    if (!row.id.isEmpty()) ++countOut;
  }
  return true;
}

bool setHotspotInterfaceEnabled(RouterOsClient &client, const String &id,
                                const String &iface, bool enable,
                                String &errorOut) {
  RouterOsClient::CommandResult &result = RouterCommandScratchContext::acquire();
  String attrs[3];
  uint8_t n = 0;
  attrs[n++] = "=.id=" + id;
  if (!iface.isEmpty()) attrs[n++] = "=interface=" + iface;
  if (enable) attrs[n++] = "=disabled=no";
  RouterOsClient::initializeCommandResult(result);
  if (!client.executeCommand("/ip/hotspot/set", attrs, n, result) ||
      result.trapReceived) {
    errorOut = result.trapMessage.isEmpty() ? "Unable to update hotspot server"
                                           : result.trapMessage;
    return false;
  }
  return true;
}

/**
 * MikroTik captive interception requires Hotspot on the same L3 path as
 * guest DHCP. When production WLAN is a bridge port, Hotspot MUST bind to
 * the bridge — not the wireless slave interface alone.
 */
bool reconcileHotspotCaptivePathImpl(RouterOsClient &client,
                                     const String &ifaceName,
                                     const String &bridgeName,
                                     JsonObject reportOut, String &errorOut,
                                     String &stageOut) {
  errorOut = "";
  stageOut = "hotspot-reconcile";
  const auto defaults = RouterProvisioning::defaultSettings();
  const bool bridged =
      !bridgeName.isEmpty() && !ifaceName.isEmpty() &&
      bridgeHasInterface(client, bridgeName, ifaceName);
  const String requiredIface = bridged ? bridgeName : ifaceName;

  if (reportOut) {
    reportOut["bridged"]           = bridged;
    reportOut["wirelessInterface"] = ifaceName;
    reportOut["bridge"]            = bridgeName;
    reportOut["requiredInterface"] = requiredIface;
    reportOut["repaired"]          = false;
    reportOut["action"]            = "none";
  }

  if (requiredIface.isEmpty()) {
    errorOut = "Wireless interface is not configured";
    return false;
  }

  HotspotRow rows[8];
  uint8_t count = 0;
  if (!loadHotspotRows(client, rows, 8, count, errorOut)) return false;

  Serial.printf(
      "[router-hotspot] reconcile begin wireless=%s bridge=%s required=%s "
      "hotspotCount=%u\n",
      ifaceName.c_str(), bridgeName.c_str(), requiredIface.c_str(),
      static_cast<unsigned>(count));

  int onRequired = -1;
  int onWireless = -1;
  int managed    = -1;
  for (uint8_t i = 0; i < count; ++i) {
    if (rows[i].iface == requiredIface) onRequired = static_cast<int>(i);
    if (!ifaceName.isEmpty() && rows[i].iface == ifaceName) {
      onWireless = static_cast<int>(i);
    }
    if (rows[i].name == defaults.hotspotServerName) managed = static_cast<int>(i);
  }

  auto finalizeProfile = [&](HotspotRow &row) -> bool {
    String profile = row.profile;
    if (profile.isEmpty()) profile = defaults.hotspotProfileName;
    String htmlDir;
    bool repairedHtml = false;
    if (!ensureHotspotProfileHtmlDirectory(client, profile, htmlDir, errorOut,
                                           &repairedHtml)) {
      return false;
    }
    if (reportOut) {
      reportOut["hotspotId"]        = row.id;
      reportOut["hotspotName"]      = row.name;
      reportOut["hotspotInterface"] = row.iface;
      reportOut["profile"]          = profile;
      reportOut["htmlDirectory"]    = htmlDir;
      reportOut["disabled"]         = row.disabled;
      if (repairedHtml) {
        reportOut["repaired"] = true;
        const String priorAction = reportOut["action"] | "none";
        if (priorAction == "none") {
          reportOut["action"] = "html-directory";
        }
      }
    }
    Serial.printf(
        "[router-hotspot] ok iface=%s name=%s profile=%s html-directory=%s\n",
        row.iface.c_str(), row.name.c_str(), profile.c_str(), htmlDir.c_str());
    return true;
  };

  // Case A — already on required interface.
  if (onRequired >= 0) {
    HotspotRow &row = rows[static_cast<size_t>(onRequired)];
    if (row.disabled) {
      stageOut = "hotspot-enable";
      if (!setHotspotInterfaceEnabled(client, row.id, "", true, errorOut)) {
        return false;
      }
      row.disabled = false;
      if (reportOut) {
        reportOut["repaired"] = true;
        reportOut["action"]   = "enabled";
      }
      Serial.printf("[router-hotspot] enabled id=%s iface=%s\n", row.id.c_str(),
                    row.iface.c_str());
    }
    return finalizeProfile(row);
  }

  // Case B — bridged topology but Hotspot still on wireless slave: move to bridge.
  if (bridged && onWireless >= 0) {
    HotspotRow &row = rows[static_cast<size_t>(onWireless)];
    stageOut = "hotspot-move-bridge";
    if (!setHotspotInterfaceEnabled(client, row.id, requiredIface, true,
                                    errorOut)) {
      return false;
    }
    row.iface    = requiredIface;
    row.disabled = false;
    if (reportOut) {
      reportOut["repaired"] = true;
      reportOut["action"]   = "moved-to-bridge";
    }
    Serial.printf("[router-hotspot] moved wireless→bridge id=%s → %s\n",
                  row.id.c_str(), requiredIface.c_str());
    return finalizeProfile(row);
  }

  // Case C — managed server exists on wrong interface: retarget.
  if (managed >= 0) {
    HotspotRow &row = rows[static_cast<size_t>(managed)];
    stageOut = "hotspot-retarget";
    if (!setHotspotInterfaceEnabled(client, row.id, requiredIface, true,
                                    errorOut)) {
      return false;
    }
    row.iface    = requiredIface;
    row.disabled = false;
    if (reportOut) {
      reportOut["repaired"] = true;
      reportOut["action"]   = "retargeted";
    }
    Serial.printf("[router-hotspot] retargeted id=%s → %s\n", row.id.c_str(),
                  requiredIface.c_str());
    return finalizeProfile(row);
  }

  // Case D — create on required interface.
  stageOut = "hotspot-create";
  String htmlDir;
  bool repairedHtml = false;
  if (!ensureHotspotProfileHtmlDirectory(client, defaults.hotspotProfileName,
                                         htmlDir, errorOut, &repairedHtml)) {
    return false;
  }

  String addressCidr = defaults.guestGatewayCidr;
  String liveCidr;
  if (resolveInterfaceAddressCidr(client, requiredIface, liveCidr)) {
    addressCidr = liveCidr;
  }

  RouterOsClient::CommandResult &result = RouterCommandScratchContext::acquire();
  const String attrs[] = {
      "=name=" + defaults.hotspotServerName,
      "=interface=" + requiredIface,
      "=profile=" + defaults.hotspotProfileName,
      "=address=" + addressCidr,
      "=comment=" + String(RouterProvisioning::COMMENT_GUEST_HS_SERVER)};
  RouterOsClient::initializeCommandResult(result);
  Serial.printf(
      "[router-hotspot] hotspot/add name=%s iface=%s address=%s profile=%s\n",
      defaults.hotspotServerName.c_str(), requiredIface.c_str(),
      addressCidr.c_str(), defaults.hotspotProfileName.c_str());
  if (!client.executeCommand("/ip/hotspot/add", attrs, 5, result) ||
      result.trapReceived) {
    errorOut = result.trapMessage.isEmpty()
                   ? "Unable to create hotspot on captive path"
                   : result.trapMessage;
    return false;
  }

  if (reportOut) {
    reportOut["repaired"]          = true;
    reportOut["action"]            = "created";
    reportOut["hotspotName"]       = defaults.hotspotServerName;
    reportOut["hotspotInterface"]  = requiredIface;
    reportOut["profile"]           = defaults.hotspotProfileName;
    reportOut["htmlDirectory"]     = htmlDir;
    reportOut["disabled"]          = false;
    reportOut["address"]           = addressCidr;
  }
  Serial.println(F("[router-hotspot] created on required captive interface"));
  return true;
}

bool ensureHotspotOnInterface(RouterOsClient &client, const String &ifaceName,
                              const String &bridgeName, String &errorOut,
                              String &stageOut) {
  return reconcileHotspotCaptivePathImpl(client, ifaceName, bridgeName,
                                         JsonObject(), errorOut, stageOut);
}

bool findWirelessInterface(RouterOsClient &client, const String &interfaceId,
                           uint8_t &indexOut, String &driverOut) {
  indexOut  = 255;
  driverOut = "wireless";
  RouterOsClient::CommandResult &result = RouterCommandScratchContext::acquire();
  const String idProplist[] = {"=.proplist=.id,name"};

  RouterOsClient::initializeCommandResult(result);
  if (client.executeCommand("/interface/wireless/print", idProplist, 1, result) &&
      !result.trapReceived && result.replyCount > 0) {
    for (uint8_t i = 0; i < result.replyCount; ++i) {
      String name, id;
      replyAttr(result, i, "name", name);
      replyAttr(result, i, ".id", id);
      if (name == interfaceId || id == interfaceId) {
        indexOut = i;
        return true;
      }
    }
  }

  driverOut = "wifi";
  RouterOsClient::initializeCommandResult(result);
  if (!client.executeCommand("/interface/wifi/print", idProplist, 1, result) ||
      result.trapReceived) {
    return false;
  }
  for (uint8_t i = 0; i < result.replyCount; ++i) {
    String name, id;
    replyAttr(result, i, "name", name);
    replyAttr(result, i, ".id", id);
    if (name == interfaceId || id == interfaceId) {
      indexOut = i;
      return true;
    }
  }
  return false;
}

bool findManagedInterface(RouterOsClient &client, const String &driver,
                          const String &ifaceName, String &routerIdOut) {
  routerIdOut = "";
  RouterOsClient::CommandResult &result = RouterCommandScratchContext::acquire();
  const char *path =
      driver == "wifi" ? "/interface/wifi/print" : "/interface/wireless/print";
  const String filter[] = {"?name=" + ifaceName, "=.proplist=.id,name"};
  RouterOsClient::initializeCommandResult(result);
  if (!client.executeCommand(path, filter, 2, result) || result.trapReceived ||
      result.replyCount == 0) {
    return false;
  }
  String id;
  replyAttr(result, 0, ".id", id);
  if (id.isEmpty()) return false;
  routerIdOut = id;
  return true;
}

bool ensureOpenSecurityProfile(RouterOsClient &client, const String &profileName,
                               String &errorOut) {
  RouterOsClient::CommandResult &result = RouterCommandScratchContext::acquire();

  const String filter[] = {"?name=" + profileName};
  RouterOsClient::initializeCommandResult(result);
  if (!client.executeCommand("/interface/wireless/security-profiles/print", filter,
                             1, result) ||
      result.trapReceived) {
    errorOut = "Unable to read wireless security profiles";
    return false;
  }

  if (result.replyCount == 0) {
    const String addAttrs[] = {
        "=name=" + profileName,
        "=authentication-types=",
        "=mode=none",
        "=comment=" + String(RouterProvisioning::COMMENT_PREFIX) + "open hotspot wlan"};
    RouterOsClient::initializeCommandResult(result);
    if (!client.executeCommand("/interface/wireless/security-profiles/add", addAttrs,
                               4, result) ||
        result.trapReceived) {
      errorOut = result.trapMessage.isEmpty() ? "Unable to create open Wi-Fi profile"
                                              : result.trapMessage;
      return false;
    }
    return true;
  }

  String profileId, authTypes, psk;
  replyAttr(result, 0, ".id", profileId);
  replyAttr(result, 0, "authentication-types", authTypes);
  replyAttr(result, 0, "wpa2-pre-shared-key", psk);
  if (profileId.isEmpty()) {
    errorOut = "Security profile id missing";
    return false;
  }
  if (isOpenAuthTypes(authTypes) && psk.isEmpty()) return true;

  const String setAttrs[] = {"=.id=" + profileId,
                             "=authentication-types=",
                             "=mode=none",
                             "=wpa2-pre-shared-key="};
  RouterOsClient::initializeCommandResult(result);
  if (!client.executeCommand("/interface/wireless/security-profiles/set", setAttrs,
                             4, result) ||
      result.trapReceived) {
    errorOut = result.trapMessage.isEmpty()
                   ? "Unable to configure open Wi-Fi security profile"
                   : result.trapMessage;
    return false;
  }
  return true;
}

bool ensureOpenWifiConfiguration(RouterOsClient &client, const String &configName,
                                   const String &ssid, String &errorOut) {
  RouterOsClient::CommandResult &result = RouterCommandScratchContext::acquire();
  const String cfgFilter[] = {"?name=" + configName};
  RouterOsClient::initializeCommandResult(result);
  if (client.executeCommand("/interface/wifi/configuration/print", cfgFilter, 1,
                            result) &&
      !result.trapReceived && result.replyCount > 0) {
    String cfgId;
    replyAttr(result, 0, ".id", cfgId);
    if (!cfgId.isEmpty()) {
      const String setAttrs[] = {"=.id=" + cfgId,
                                 "=ssid=" + ssid,
                                 "=passphrase=",
                                 "=security.authentication-types="};
      RouterOsClient::initializeCommandResult(result);
      if (!client.executeCommand("/interface/wifi/configuration/set", setAttrs, 4,
                                 result) ||
          result.trapReceived) {
        errorOut = result.trapMessage.isEmpty()
                       ? "Unable to update open Wi-Fi configuration"
                       : result.trapMessage;
        return false;
      }
      return true;
    }
  }

  const String addCfg[] = {"=name=" + configName,
                           "=ssid=" + ssid,
                           "=comment=" + String(RouterProvisioning::COMMENT_PREFIX) +
                               "open hotspot wlan"};
  RouterOsClient::initializeCommandResult(result);
  if (!client.executeCommand("/interface/wifi/configuration/add", addCfg, 3, result) ||
      result.trapReceived) {
    errorOut = result.trapMessage.isEmpty() ? "Unable to create open Wi-Fi configuration"
                                            : result.trapMessage;
    return false;
  }
  return true;
}

bool setInterfaceEnabled(RouterOsClient &client, const String &driver,
                         const String &ifaceName, bool enabled,
                         String &errorOut) {
  RouterOsClient::CommandResult &result = RouterCommandScratchContext::acquire();
  String routerId;
  if (!findManagedInterface(client, driver, ifaceName, routerId) ||
      routerId.isEmpty()) {
    errorOut = "Wireless interface id missing";
    return false;
  }
  const char *path =
      driver == "wifi" ? "/interface/wifi/set" : "/interface/wireless/set";
  const String setAttrs[] = {"=.id=" + routerId,
                             enabled ? "=disabled=no" : "=disabled=yes"};
  RouterOsClient::initializeCommandResult(result);
  if (!client.executeCommand(path, setAttrs, 2, result) || result.trapReceived) {
    errorOut = result.trapMessage.isEmpty() ? "Unable to update wireless interface"
                                            : result.trapMessage;
    return false;
  }
  return true;
}

void logSystemResources(RouterOsClient &client, const char *checkpoint) {
  RouterOsClient::CommandResult &resource = RouterCommandScratchContext::acquire();
  if (!client.executeCommand("/system/resource/print", resource) ||
      resource.trapReceived || resource.replyCount == 0) {
    Serial.printf("[router-wireless] resource %s unavailable\n", checkpoint);
    return;
  }
  recordCpuLoadFromResource(resource);
  String cpu, memory, uptime, version;
  replyAttr(resource, 0, "cpu-load", cpu);
  replyAttr(resource, 0, "free-memory", memory);
  replyAttr(resource, 0, "uptime", uptime);
  replyAttr(resource, 0, "version", version);
  Serial.printf(
      "[router-wireless] resource %s cpu-load=%s free-memory=%s uptime=%s version=%s\n",
      checkpoint, cpu.c_str(), memory.c_str(), uptime.c_str(), version.c_str());
}

bool verifyManagedWireless(RouterOsClient &client, const String &ifaceName,
                           const String &bridgeName, String &errorOut) {
  errorOut = "";
  uint8_t idx = 255;
  String driver;
  if (!findWirelessInterface(client, ifaceName, idx, driver)) {
    errorOut = "Managed wireless interface not found after provisioning";
    return false;
  }

  RouterOsClient::CommandResult &result = RouterCommandScratchContext::acquire();
  const char *path =
      driver == "wifi" ? "/interface/wifi/print" : "/interface/wireless/print";
  const String filter[] = {
      "?name=" + ifaceName,
      "=.proplist=.id,name,disabled,running",
  };
  RouterOsClient::initializeCommandResult(result);
  if (!client.executeCommand(path, filter, 2, result) || result.trapReceived ||
      result.replyCount == 0) {
    errorOut = "Unable to verify wireless interface";
    return false;
  }
  String disabled, running;
  replyAttr(result, 0, "disabled", disabled);
  replyAttr(result, 0, "running", running);
  if (disabled == "true") {
    errorOut = "Wireless interface is still disabled";
    return false;
  }

  if (!bridgeName.isEmpty() && !bridgeHasInterface(client, bridgeName, ifaceName)) {
    errorOut = "Wireless interface is not attached to guest bridge";
    return false;
  }

  // Captive path: Hotspot must be on bridge when WLAN is bridged; else on WLAN.
  {
    DynamicJsonDocument verifyDoc(RenzFiConfig::JSON_DOC_SMALL);
    JsonObject verifyReport = verifyDoc.to<JsonObject>();
    String hsError;
    String hsStage;
    if (!reconcileHotspotCaptivePathImpl(client, ifaceName, bridgeName,
                                         verifyReport, hsError, hsStage)) {
      errorOut = hsError.isEmpty() ? "Hotspot server is not active on captive path"
                                   : hsError;
      return false;
    }
  }

  Serial.printf("[router-wireless] verify ok iface=%s driver=%s running=%s bridge=%s\n",
                ifaceName.c_str(), driver.c_str(), running.c_str(), bridgeName.c_str());
  return true;
}

bool createLegacyWirelessAp(RouterOsClient &client, const String &ssid,
                            const String &bridgeName, String &ifaceOut,
                            String &errorOut, String &stageOut) {
  stageOut = "wireless-read";
  RouterOsClient::CommandResult &result = RouterCommandScratchContext::acquire();

  RouterOsClient::initializeCommandResult(result);
  const String wirelessAttrs[] = {"=.proplist=.id,name,master-mode"};
  if (!client.executeCommand("/interface/wireless/print", wirelessAttrs, 1, result) ||
      result.trapReceived || result.replyCount == 0) {
    errorOut = "No wireless interfaces found on RouterOS";
    return false;
  }

  String masterName;
  for (uint8_t i = 0; i < result.replyCount; ++i) {
    String name, masterMode;
    replyAttr(result, i, "name", name);
    replyAttr(result, i, "master-mode", masterMode);
    if (name.isEmpty()) continue;
    if (masterMode == "true" || masterMode.isEmpty()) {
      masterName = name;
      break;
    }
  }
  if (masterName.isEmpty()) replyAttr(result, 0, "name", masterName);
  if (masterName.isEmpty()) {
    errorOut = "No master wireless interface available";
    return false;
  }

  stageOut = "wireless-security";
  if (!ensureOpenSecurityProfile(client, RouterWireless::kOpenSecurityProfileName,
                                   errorOut)) {
    return false;
  }

  const String ifaceName = RouterWireless::kManagedWifiIfaceName;
  stageOut               = "wireless-create";
  String existingId;
  if (findManagedInterface(client, "wireless", ifaceName, existingId) &&
      !existingId.isEmpty()) {
    const String setAttrs[] = {"=.id=" + existingId,
                               "=ssid=" + ssid,
                               "=security-profile=" +
                                   String(RouterWireless::kOpenSecurityProfileName),
                               "=disabled=no"};
    RouterOsClient::initializeCommandResult(result);
    if (!client.executeCommand("/interface/wireless/set", setAttrs, 4, result) ||
        result.trapReceived) {
      errorOut = result.trapMessage.isEmpty() ? "Unable to update wireless interface"
                                              : result.trapMessage;
      return false;
    }
  } else {
    const String addAttrs[] = {
        "=name=" + ifaceName,
        "=master-interface=" + masterName,
        "=mode=ap-bridge",
        "=ssid=" + ssid,
        "=security-profile=" + String(RouterWireless::kOpenSecurityProfileName),
        "=disabled=no",
        "=comment=" + String(RouterProvisioning::COMMENT_PREFIX) + "setup wifi"};
    RouterOsClient::initializeCommandResult(result);
    if (!client.executeCommand("/interface/wireless/add", addAttrs, 7, result) ||
        result.trapReceived) {
      errorOut = result.trapMessage.isEmpty() ? "Unable to create wireless interface"
                                              : result.trapMessage;
      return false;
    }
  }
  ifaceOut = ifaceName;

  stageOut = "bridge-port";
  if (!ensureBridgePort(client, bridgeName, ifaceName, errorOut)) return false;

  stageOut = "hotspot-create";
  if (!ensureHotspotOnInterface(client, ifaceName, bridgeName, errorOut, stageOut)) return false;
  return true;
}

bool createWifiWave2Ap(RouterOsClient &client, const String &ssid,
                       const String &bridgeName, String &ifaceOut,
                       String &errorOut, String &stageOut) {
  stageOut = "wifi-config";
  if (!ensureOpenWifiConfiguration(client, RouterWireless::kManagedWifiConfigName, ssid,
                                   errorOut)) {
    return false;
  }

  const String ifaceName = RouterWireless::kManagedWifiIfaceName;
  stageOut               = "wifi-create";
  RouterOsClient::CommandResult &result = RouterCommandScratchContext::acquire();

  String existingId;
  if (findManagedInterface(client, "wifi", ifaceName, existingId) &&
      !existingId.isEmpty()) {
    const String setAttrs[] = {"=.id=" + existingId,
                               "=configuration=" +
                                   String(RouterWireless::kManagedWifiConfigName),
                               "=disabled=no"};
    RouterOsClient::initializeCommandResult(result);
    if (!client.executeCommand("/interface/wifi/set", setAttrs, 3, result) ||
        result.trapReceived) {
      errorOut = result.trapMessage.isEmpty() ? "Unable to update Wi-Fi interface"
                                              : result.trapMessage;
      return false;
    }
  } else {
    const String addAttrs[] = {
        "=name=" + ifaceName,
        "=configuration=" + String(RouterWireless::kManagedWifiConfigName),
        "=disabled=no",
        "=comment=" + String(RouterProvisioning::COMMENT_PREFIX) + "setup wifi"};
    RouterOsClient::initializeCommandResult(result);
    if (!client.executeCommand("/interface/wifi/add", addAttrs, 4, result) ||
        result.trapReceived) {
      errorOut = result.trapMessage.isEmpty() ? "Unable to create Wi-Fi interface"
                                              : result.trapMessage;
      return false;
    }
  }
  ifaceOut = ifaceName;

  stageOut = "bridge-port";
  if (!ensureBridgePort(client, bridgeName, ifaceName, errorOut)) return false;

  stageOut = "hotspot-create";
  if (!ensureHotspotOnInterface(client, ifaceName, bridgeName, errorOut, stageOut)) return false;
  return true;
}

void logWirelessReplyAttrs(const char *driver, uint8_t replyIdx,
                           const RouterOsClient::CommandResult &result) {
  if (replyIdx >= result.replyCount) return;
  const RouterOsClient::ReplyRecord &rec = result.replyAt(replyIdx);
  Serial.printf("[wireless] raw reply driver=%s idx=%u attrs=%u\n",
                driver, static_cast<unsigned>(replyIdx),
                static_cast<unsigned>(rec.attrCount));
  for (uint8_t i = 0; i < rec.attrCount; ++i) {
    String key, value;
    if (!RouterOsClient::parseAttr(rec.attr(i), key, value)) continue;
    Serial.printf("[wireless]   %s=%s\n", key.c_str(), value.c_str());
  }
}

void summarizeDiscovery(JsonArray networks, RouterWireless::ListNetworksResult &result) {
  result.interfaceCount  = networks.size();
  result.configuredCount = 0;
  result.disabledCount   = 0;
  uint8_t emptySsidCount = 0;
  bool hasLegacy         = false;
  bool hasWifi           = false;
  for (JsonVariant v : networks) {
    JsonObject row = v.as<JsonObject>();
    const String driver = row["driver"] | "";
    if (driver == "wireless") hasLegacy = true;
    if (driver == "wifi") hasWifi = true;
    if ((row["status"] | "") == "disabled") result.disabledCount++;
    const String ssid = row["ssid"] | "";
    if (ssid.isEmpty()) {
      emptySsidCount++;
    } else {
      result.configuredCount++;
    }
    Serial.printf(
        "[wireless] iface name=%s ssid=%s disabled=%s running=%s security=%s status=%s\n",
        static_cast<const char *>(row["id"] | ""),
        ssid.c_str(),
        (row["enabled"] | true) ? "no" : "yes",
        (row["running"] | false) ? "yes" : "no",
        static_cast<const char *>(row["securityProfile"] | ""),
        static_cast<const char *>(row["status"] | ""));
  }
  if (hasLegacy && hasWifi) {
    result.driver = "mixed";
  } else if (hasWifi) {
    result.driver = "wifiwave2";
  } else if (hasLegacy) {
    result.driver = "legacy";
  } else {
    result.driver = "none";
  }

  if (result.interfaceCount == 0) {
    result.code    = "WIFI_NO_INTERFACES";
    result.message = "No wireless interfaces were found on this MikroTik";
  } else if (result.configuredCount == 0) {
    // Only when every interface truly has an empty SSID.
    result.code    = "WIFI_NO_SSIDS";
    result.message = "Wireless interfaces exist but no SSIDs are configured";
  } else if (result.disabledCount == result.interfaceCount) {
    result.code    = "WIFI_ALL_DISABLED";
    result.message = "Wireless SSIDs were found but all interfaces are disabled";
  } else if (result.disabledCount > 0) {
    result.code    = "WIFI_DISCOVERY_OK";
    result.message = "Wireless networks loaded (some interfaces disabled)";
  } else {
    result.code    = "WIFI_DISCOVERY_OK";
    result.message = "Wireless networks loaded";
  }
  result.ok = true;
  Serial.printf(
      "[wireless] discovery summary interfaces=%u ssids=%u emptySsid=%u disabled=%u code=%s\n",
      static_cast<unsigned>(result.interfaceCount),
      static_cast<unsigned>(result.configuredCount),
      static_cast<unsigned>(emptySsidCount),
      static_cast<unsigned>(result.disabledCount), result.code.c_str());
}

}  // namespace

namespace RouterWireless {

bool reconcileCaptiveHotspotPath(RouterOsClient &client,
                                 const String &wirelessIface,
                                 const String &bridgeName,
                                 JsonObject reportOut, String &errorOut) {
  String stage;
  return reconcileHotspotCaptivePathImpl(client, wirelessIface, bridgeName,
                                         reportOut, errorOut, stage);
}

bool parseWifiSelection(JsonObjectConst body, WifiSelection &out, String &errorOut) {
  errorOut = "";
  out      = {};
  const char *mode = body["wifiMode"] | "";
  if (strcmp(mode, kModeExisting) != 0 && strcmp(mode, kModeNew) != 0 &&
      strcmp(mode, kModeExternalAp) != 0) {
    errorOut = "wifiMode must be existing, new, or external_ap";
    return false;
  }
  out.mode = mode;
  if (out.mode == kModeExternalAp) {
    return true;
  }
  if (out.mode == kModeExisting) {
    out.interfaceId = body["interfaceId"] | "";
    if (out.interfaceId.isEmpty()) {
      errorOut = "interfaceId is required for existing Wi-Fi";
      return false;
    }
    return true;
  }

  out.ssid = body["ssid"] | "";
  out.ssid.trim();
  out.password = body["password"] | "";
  if (out.ssid.isEmpty()) {
    errorOut = "SSID is required";
    return false;
  }
  if (out.ssid.length() > 32) {
    errorOut = "SSID must be 32 characters or fewer";
    return false;
  }
  return true;
}

bool listNetworks(RouterOsClient &client, JsonArray out, ListNetworksResult &result) {
  result = {};
  out.clear();
  RouterWorkerDiagnostics::checkStackMargin("wireless-listNetworks-entry");

  (void)DmaMemoryMonitor::waitForRouterOsConnectHeadroom(1500);

  RENZFI_WL_LN(F("[wireless] step 1 detect RouterOS version"));
  {
    RouterOsClient::CommandResult &resource = RouterCommandScratchContext::acquire();
      String routerVersion;
      if (client.executeCommand("/system/resource/print", resource) &&
          !resource.trapReceived && resource.replyCount > 0) {
        replyAttr(resource, 0, "version", routerVersion);
      }
      recordCpuLoadFromResource(resource);
      RENZFI_WL_LOG("[wireless] step 1 version=%s\n",
                    routerVersion.isEmpty() ? "(unknown)" : routerVersion.c_str());
  }

  RouterOsClient::CommandResult &legacy = RouterCommandScratchContext::acquire();

  RENZFI_WL_LN(F("[wireless] step 2 legacy probe"));
  bool legacyMissing = false;
  bool legacyOk =
      client.executeOptionalCommand("/interface/wireless/print", legacy, legacyMissing);
  RENZFI_WL_LOG("[wireless] step 3 legacy response ok=%d missing=%d replies=%u\n",
                legacyOk ? 1 : 0, legacyMissing ? 1 : 0,
                static_cast<unsigned>(legacy.replyCount));
  RouterWorkerDiagnostics::checkStackMargin("wireless-after-legacy-probe");

  if (legacyOk) {
    for (uint8_t i = 0; i < legacy.replyCount; ++i) {
      String name, ssid, disabled, running, hidden, band, radioName, secProfile;
      replyAttr(legacy, i, "name", name);
      replyAttr(legacy, i, "ssid", ssid);
      replyAttr(legacy, i, "disabled", disabled);
      replyAttr(legacy, i, "running", running);
      replyAttr(legacy, i, "hidden", hidden);
      replyAttr(legacy, i, "band", band);
      replyAttr(legacy, i, "radio-name", radioName);
      replyAttr(legacy, i, "security-profile", secProfile);
      if (ssid.isEmpty()) {
        logWirelessReplyAttrs("wireless", i, legacy);
      }
      const String bandRaw = !band.isEmpty() ? band : radioName;
      const bool enabled     = disabled != "true";
      const bool isRunning   = running == "true";
      const bool isHidden    = hidden == "true";
      appendNetworkRow(out, name, ssid, enabled, isRunning, isHidden, bandRaw,
                       "wireless", secProfile, false);
    }
  }

  // Task 4 (RouterOS discovery optimization): a physical MikroTik radio
  // uses either the legacy "wireless" package or WiFiWave2's "wifi"
  // package, never both. Once the legacy probe has already produced actual
  // interfaces, the wifiwave2 probe is redundant work (another API round
  // trip, plus the "no such command" trap/drain path on packages where it
  // genuinely doesn't exist) — skip it. Still probe wifiwave2 whenever the
  // legacy probe came back empty/missing, so wifiwave2-only and mixed
  // v6/v7 routers are unaffected.
  const bool skipWifiWave2Probe = legacyOk && legacy.replyCount > 0;
  bool wifiMissing = false;
  bool wifiOk      = false;
  if (skipWifiWave2Probe) {
    RENZFI_WL_LN(F("[wireless] step 4 wifiwave2 probe skipped (legacy interfaces present)"));
  } else {
    RENZFI_WL_LN(F("[wireless] step 4 wifiwave2 probe"));
    RouterOsClient::CommandResult &wifi = RouterCommandScratchContext::acquire();
    wifiOk = client.executeOptionalCommand("/interface/wifi/print", wifi, wifiMissing);
    RENZFI_WL_LOG("[wireless] step 5 wifiwave2 response ok=%d missing=%d replies=%u\n",
                  wifiOk ? 1 : 0, wifiMissing ? 1 : 0,
                  static_cast<unsigned>(wifi.replyCount));
    RouterWorkerDiagnostics::checkStackMargin("wireless-after-wifiwave2-probe");

    if (wifiOk) {
      for (uint8_t i = 0; i < wifi.replyCount; ++i) {
        String name, disabled, running, hidden, configuration, radio;
        replyAttr(wifi, i, "name", name);
        replyAttr(wifi, i, "disabled", disabled);
        replyAttr(wifi, i, "running", running);
        replyAttr(wifi, i, "hidden", hidden);
        replyAttr(wifi, i, "configuration", configuration);
        replyAttr(wifi, i, "radio", radio);
        String ssid;
        if (!lookupWifiConfigurationSsid(client, configuration, ssid)) {
          ssid = "";
        }
        const bool enabled   = disabled != "true";
        const bool isRunning = running == "true";
        const bool isHidden  = hidden == "true";
        appendNetworkRow(out, name, ssid, enabled, isRunning, isHidden, radio, "wifi",
                         "open", true);
      }
    }
  }

  RENZFI_WL_LOG("[wireless] step 6 merge interfaces count=%u\n",
                static_cast<unsigned>(out.size()));

  // Task 5/14 (MikroTik CPU protection / safety validation): the per-row
  // security-profile lookup below is discovery *detail*, not essential to
  // returning the interface/SSID list itself. When the last observed
  // cpu-load sample says the router is already under pressure, skip these
  // extra commands entirely rather than adding more load — default to the
  // safe (non-open) assumption instead of guessing.
  if (RouterApiTransportGate::cpuUnderPressure()) {
    RENZFI_WL_LN(F("[wireless] step 7 verify security SKIPPED (MikroTik CPU under pressure)"));
    for (JsonVariant v : out) {
      JsonObject row = v.as<JsonObject>();
      if ((row["driver"] | "") != "wireless") continue;
      const String profile = row["securityProfile"] | "";
      row["securityOpen"] = profile.isEmpty();
    }
  } else {
  RENZFI_WL_LN(F("[wireless] step 7 verify security"));
  {
    // One reusable worker scratch for the whole loop instead of one
    // alloc/free per interface, and a small dedupe cache (interface counts
    // are always tiny) so N interfaces sharing one security profile only
    // issue one RouterOS command instead of N.
    RouterOsClient::CommandResult &secScratch = RouterCommandScratchContext::acquire();
    static constexpr uint8_t kMaxCachedProfiles = 8;
    String  cachedProfileNames[kMaxCachedProfiles];
    bool    cachedProfileOpen[kMaxCachedProfiles] = {false};
    uint8_t cachedProfileCount = 0;

    for (JsonVariant v : out) {
      JsonObject row = v.as<JsonObject>();
      if ((row["driver"] | "") != "wireless") continue;
      const String profile = row["securityProfile"] | "";
      if (profile.isEmpty()) {
        row["securityOpen"] = true;
        continue;
      }

      bool cached = false;
      for (uint8_t i = 0; i < cachedProfileCount; ++i) {
        if (cachedProfileNames[i] == profile) {
          row["securityOpen"] = cachedProfileOpen[i];
          cached = true;
          break;
        }
      }
      if (cached) continue;

      const bool open = isSecurityProfileOpen(client, profile, &secScratch);
      row["securityOpen"] = open;
      if (cachedProfileCount < kMaxCachedProfiles) {
        cachedProfileNames[cachedProfileCount] = profile;
        cachedProfileOpen[cachedProfileCount]  = open;
        ++cachedProfileCount;
      }
    }
  }
  }
  RouterWorkerDiagnostics::checkStackMargin("wireless-after-security-verify");

  if (legacyMissing && wifiMissing) {
    result.code    = "WIFI_NO_WIRELESS_PACKAGE";
    result.driver  = "none";
    result.message = "No wireless package is installed on this MikroTik";
    result.ok      = true;
    RENZFI_WL_LN(F("[wireless] step 8 return WIFI_NO_WIRELESS_PACKAGE"));
    return true;
  }

  if (!legacyOk && !wifiOk && out.size() == 0) {
    result.error = client.lastError().isEmpty() ? "Unable to scan wireless interfaces"
                                                : client.lastError();
    result.code  = "WIFI_SCAN_FAILED";
    RENZFI_WL_LOG("[wireless] step 8 return WIFI_SCAN_FAILED err=%s\n",
                  result.error.c_str());
    return false;
  }

  summarizeDiscovery(out, result);
  RENZFI_WL_LOG("[wireless] step 8 return code=%s interfaces=%u\n",
                result.code.c_str(), static_cast<unsigned>(result.interfaceCount));
  return true;
}

bool applyWifiSelection(RouterOsClient &client, const WifiSelection &selection,
                        const String &bridgeName, String &errorOut,
                        String &stageOut) {
  errorOut = "";
  stageOut = "wifi-apply";
  logSystemResources(client, "before-provision");

  if (selection.mode == kModeExisting) {
    stageOut = "wireless-resolve";
    uint8_t idx = 255;
    String driver;
    if (!findWirelessInterface(client, selection.interfaceId, idx, driver)) {
      errorOut = "Selected wireless interface was not found";
      return false;
    }

    String ifaceName = selection.interfaceId;
    RouterOsClient::CommandResult &result = RouterCommandScratchContext::acquire();
    RouterOsClient::initializeCommandResult(result);
    if (driver == "wireless") {
      const String filter[] = {
          "?name=" + ifaceName,
          "=.proplist=.id,name,disabled,security-profile",
      };
      client.executeCommand("/interface/wireless/print", filter, 2, result);
    } else {
      const String filter[] = {
          "?name=" + ifaceName,
          "=.proplist=.id,name,disabled",
      };
      client.executeCommand("/interface/wifi/print", filter, 2, result);
    }
    String disabled, secProfile;
    if (result.replyCount > 0) {
      String name;
      replyAttr(result, 0, "name", name);
      replyAttr(result, 0, "disabled", disabled);
      replyAttr(result, 0, "security-profile", secProfile);
      if (!name.isEmpty()) ifaceName = name;
    }

    if (driver == "wireless" && !secProfile.isEmpty()) {
      stageOut = "wireless-security-verify";
      if (!ensureOpenSecurityProfile(client, secProfile, errorOut)) return false;
      if (!isSecurityProfileOpen(client, secProfile)) {
        errorOut = "Selected SSID security profile is not open";
        return false;
      }
    }

    if (disabled == "true") {
      stageOut = "wireless-enable";
      if (!setInterfaceEnabled(client, driver, ifaceName, true, errorOut)) return false;
    }

    stageOut = "bridge-port";
    if (!ensureBridgePort(client, bridgeName, ifaceName, errorOut)) return false;

    stageOut = "hotspot-verify";
    if (!ensureHotspotOnInterface(client, ifaceName, bridgeName, errorOut, stageOut)) return false;

    stageOut = "wireless-verify";
    if (!verifyManagedWireless(client, ifaceName, bridgeName, errorOut)) return false;
    logSystemResources(client, "after-provision");
    return true;
  }

  RouterOsClient::CommandResult &result = RouterCommandScratchContext::acquire();

  bool legacyMissing = false;
  RouterOsClient::initializeCommandResult(result);
  const bool hasLegacy =
      client.executeOptionalCommand("/interface/wireless/print", result,
                                    legacyMissing) &&
      result.replyCount > 0;

  String createdIface;
  if (hasLegacy) {
    if (!createLegacyWirelessAp(client, selection.ssid, bridgeName, createdIface,
                                errorOut, stageOut)) {
      return false;
    }
  } else {
    if (!createWifiWave2Ap(client, selection.ssid, bridgeName, createdIface, errorOut,
                           stageOut)) {
      return false;
    }
  }

  stageOut = "wireless-verify";
  if (!verifyManagedWireless(client, createdIface, bridgeName, errorOut)) return false;
  logSystemResources(client, "after-provision");
  return true;
}

bool loadCanonicalConfig(StorageManager *storage, CanonicalConfig &out) {
  out = {};
  if (!storage) return false;
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_SMALL);
  if (!storage->readJson(StoragePaths::RouterProvisioningFile, doc)) {
    return false;
  }
  out.configured   = doc["wifiSelectionConfigured"] | false;
  out.mode         = doc["wifiMode"] | "";
  out.interfaceId  = doc["wifiInterfaceId"] | "";
  if (out.interfaceId.isEmpty()) {
    out.interfaceId = doc["selectedWirelessInterface"] | "";
  }
  out.ssid         = doc["wifiSsid"] | "";
  if (out.ssid.isEmpty() && out.mode == kModeNew) {
    out.ssid = doc["targetSsid"] | "";
  }
  out.password     = doc["wifiPassword"] | "";
  return out.configured && !out.interfaceId.isEmpty();
}

bool saveCanonicalFields(StorageManager *storage, const CanonicalConfig &cfg) {
  if (!storage) return false;
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_SMALL);
  if (!storage->readJson(StoragePaths::RouterProvisioningFile, doc)) {
    doc.clear();
  }
  doc["wifiSelectionConfigured"]     = cfg.configured;
  doc["wifiMode"]                    = cfg.mode;
  doc["wifiInterfaceId"]             = cfg.interfaceId;
  doc["selectedWirelessInterface"]   = cfg.interfaceId;
  doc["wifiSsid"]                    = cfg.ssid;
  doc["wifiPassword"]                = cfg.password;
  if (cfg.mode == kModeNew && !cfg.ssid.isEmpty()) {
    doc["targetSsid"]   = cfg.ssid;
    doc["ssidPolicy"]   = "rename";
  }
  return storage->writeJson(StoragePaths::RouterProvisioningFile, doc, true);
}

bool readInterfaceSecurity(RouterOsClient &client, const String &securityProfile,
                           JsonObject out) {
  if (securityProfile.isEmpty()) {
    // No profile name from RouterOS — cannot prove Open vs secured.
    out["security"]     = "unknown";
    out["securityOpen"] = false;
    return true;
  }

  // Bounded proplist: full security-profile replies exceed MAX_ATTRS=24 and
  // drop authentication-types before we can classify Open vs secured.
  RouterOsClient::CommandResult &secResult = RouterCommandScratchContext::acquire();
  const String secFilter[] = {
      "?name=" + securityProfile,
      "=.proplist=name,mode,authentication-types,wpa-pre-shared-key,wpa2-pre-shared-key",
  };
  if (!client.executeCommand("/interface/wireless/security-profiles/print",
                             secFilter, 2, secResult) ||
      secResult.trapReceived || secResult.replyCount == 0) {
    out["security"]     = "unknown";
    out["securityOpen"] = false;
    return true;
  }
  if (secResult.replyLimitReached) {
    Serial.printf(
        "[router-api] attribute capacity exceeded "
        "endpoint=/interface/wireless/security-profiles/print count>=%u limit=%u\n",
        static_cast<unsigned>(secResult.replyAt(0).attrCount),
        static_cast<unsigned>(RouterOsClient::ReplyRecord::MAX_ATTRS));
  }
  String authTypes, psk, pskWpa;
  replyAttr(secResult, 0, "authentication-types", authTypes);
  replyAttr(secResult, 0, "wpa2-pre-shared-key", psk);
  replyAttr(secResult, 0, "wpa-pre-shared-key", pskWpa);
  if (psk.isEmpty()) psk = pskWpa;
  // Empty authentication-types after a successful bounded read = Open.
  out["security"]     = authTypes.isEmpty() ? "none" : authTypes;
  out["securityOpen"] = isOpenAuthTypes(authTypes) && psk.isEmpty();
  if (!psk.isEmpty()) out["password"] = psk;
  return true;
}

bool fillWirelessFieldsFromPrint(RouterOsClient &client,
                                 const RouterOsClient::CommandResult &printResult,
                                 const String &interfaceId, JsonObject out) {
  String name, ssid, secProfile;
  replyAttr(printResult, 0, "name", name);
  replyAttr(printResult, 0, "ssid", ssid);
  replyAttr(printResult, 0, "security-profile", secProfile);
  out["interface"] = name.isEmpty() ? interfaceId : name;
  out["ssid"]      = ssid;
  String band, frequency;
  replyAttr(printResult, 0, "band", band);
  replyAttr(printResult, 0, "frequency", frequency);
  if (!band.isEmpty()) {
    out["band"] = normalizeBandLabel(band);
  } else if (!frequency.isEmpty()) {
    const long freqMhz = frequency.toInt();
    const String derived = RouterWireless::formatBandFromFrequencyMhz(freqMhz);
    if (!derived.isEmpty()) out["band"] = derived;
  }
  readInterfaceSecurity(client, secProfile, out);
  return true;
}

bool fillWifiFieldsFromPrint(RouterOsClient &client,
                             const RouterOsClient::CommandResult &printResult,
                             const String &interfaceId, JsonObject out) {
  String name, configuration, radio;
  replyAttr(printResult, 0, "name", name);
  replyAttr(printResult, 0, "configuration", configuration);
  replyAttr(printResult, 0, "radio", radio);
  String ssid;
  if (!lookupWifiConfigurationSsid(client, configuration, ssid)) {
    ssid = name;
  }
  out["interface"]  = name.isEmpty() ? interfaceId : name;
  out["ssid"]       = ssid;
  out["security"]   = "open";
  out["securityOpen"] = true;
  if (!radio.isEmpty()) out["band"] = normalizeBandLabel(radio);
  return true;
}

bool tryTargetedWirelessPrint(RouterOsClient &client, const String &interfaceId,
                              RouterOsClient::CommandResult &printResult) {
  const String filter[] = {
      "?name=" + interfaceId,
      "=.proplist=.id,name,ssid,security-profile,band,frequency,disabled,running",
  };
  RouterOsClient::initializeCommandResult(printResult);
  if (client.executeCommand("/interface/wireless/print", filter, 2, printResult) &&
      !printResult.trapReceived && printResult.replyCount > 0) {
    return true;
  }
  const String idFilter[] = {
      "?.id=" + interfaceId,
      "=.proplist=.id,name,ssid,security-profile,band,frequency,disabled,running",
  };
  RouterOsClient::initializeCommandResult(printResult);
  return client.executeCommand("/interface/wireless/print", idFilter, 2, printResult) &&
         !printResult.trapReceived && printResult.replyCount > 0;
}

bool tryTargetedWifiPrint(RouterOsClient &client, const String &interfaceId,
                          RouterOsClient::CommandResult &printResult) {
  const String wifiFilter[] = {
      "?name=" + interfaceId,
      "=.proplist=.id,name,configuration,radio",
  };
  RouterOsClient::initializeCommandResult(printResult);
  return client.executeCommand("/interface/wifi/print", wifiFilter, 2, printResult) &&
         !printResult.trapReceived && printResult.replyCount > 0;
}

bool readInterface(RouterOsClient &client, const String &interfaceId, JsonObject out,
                   String &errorOut) {
  errorOut = "";
  out["ssid"]      = "";
  out["security"]  = "";
  out["interface"] = interfaceId;
  out["error"]     = "";

  const uint32_t t0 = millis();
  Serial.println("[router-sync] wireless fetch start");

  RouterOsClient::CommandResult &printResult = RouterCommandScratchContext::acquire();

  // Prefer targeted print when interfaceId is known — avoids inventory
  // /interface/wireless/print (.id,name) that duplicated the targeted call.
  if (tryTargetedWirelessPrint(client, interfaceId, printResult)) {
    fillWirelessFieldsFromPrint(client, printResult, interfaceId, out);
    Serial.printf("[router-sync] wireless fetch success elapsed=%lums\n",
                  static_cast<unsigned long>(millis() - t0));
    Serial.println("[router-sync] wireless reuse=yes");
    return true;
  }

  if (tryTargetedWifiPrint(client, interfaceId, printResult)) {
    fillWifiFieldsFromPrint(client, printResult, interfaceId, out);
    Serial.printf("[router-sync] wireless fetch success elapsed=%lums\n",
                  static_cast<unsigned long>(millis() - t0));
    Serial.println("[router-sync] wireless reuse=yes");
    return true;
  }

  // Fallback: discovery inventory (legacy wireless vs wifiwave2).
  Serial.println("[router-sync] wireless fallback discovery");
  uint8_t idx = 255;
  String driver;
  if (!findWirelessInterface(client, interfaceId, idx, driver)) {
    errorOut = "Configured wireless interface was not found on RouterOS";
    return false;
  }

  if (driver == "wireless") {
    if (!tryTargetedWirelessPrint(client, interfaceId, printResult)) {
      errorOut = "Unable to read wireless interface";
      return false;
    }
    fillWirelessFieldsFromPrint(client, printResult, interfaceId, out);
    Serial.printf("[router-sync] wireless fetch success elapsed=%lums\n",
                  static_cast<unsigned long>(millis() - t0));
    Serial.println("[router-sync] wireless reuse=no");
    return true;
  }

  if (!tryTargetedWifiPrint(client, interfaceId, printResult)) {
    errorOut = "Unable to read Wi-Fi interface";
    return false;
  }
  fillWifiFieldsFromPrint(client, printResult, interfaceId, out);
  Serial.printf("[router-sync] wireless fetch success elapsed=%lums\n",
                static_cast<unsigned long>(millis() - t0));
  Serial.println("[router-sync] wireless reuse=no");
  return true;
}

bool updateInterface(RouterOsClient &client, const String &interfaceId,
                     const String &ssid, const String &password, String &errorOut) {
  (void)password;
  errorOut = "";
  if (ssid.isEmpty()) {
    errorOut = "SSID is required";
    return false;
  }

  // Prefer targeted ?name= lookups (findManagedInterface). Do NOT call
  // findWirelessInterface here — that inventory-prints every wireless iface
  // and then probes /interface/wifi, which saturates low-resource hAP lite
  // during radio reconfiguration.
  String routerId;
  if (findManagedInterface(client, "wireless", interfaceId, routerId)) {
    RouterOsClient::CommandResult &result = RouterCommandScratchContext::acquire();
    const String setAttrs[] = {"=.id=" + routerId, "=ssid=" + ssid};
    RouterOsClient::initializeCommandResult(result);
    if (!client.executeCommand("/interface/wireless/set", setAttrs, 2, result) ||
        result.trapReceived) {
      errorOut = result.trapMessage.isEmpty() ? "Failed to update SSID"
                                              : result.trapMessage;
      return false;
    }
    return true;
  }

  if (!client.isConnected() || !client.isLoggedIn()) {
    errorOut = "RouterOS session lost while resolving wireless interface";
    return false;
  }

  // wifiwave2 path only when legacy wireless interface is absent.
  String cfgId;
  {
    RouterOsClient::CommandResult &result = RouterCommandScratchContext::acquire();
    const String wifiFilter[] = {
        "?name=" + interfaceId,
        "=.proplist=.id,name,configuration",
    };
    RouterOsClient::initializeCommandResult(result);
    if (!client.executeCommand("/interface/wifi/print", wifiFilter, 2, result) ||
        result.trapReceived || result.replyCount == 0) {
      errorOut = "Configured wireless interface was not found on RouterOS";
      return false;
    }
    String configuration;
    replyAttr(result, 0, "configuration", configuration);
    if (configuration.isEmpty()) {
      errorOut = "Wi-Fi configuration is missing";
      return false;
    }
    const String cfgFilter[] = {"?name=" + configuration, "=.proplist=.id,name"};
    RouterOsClient::initializeCommandResult(result);
    if (!client.executeCommand("/interface/wifi/configuration/print", cfgFilter, 2,
                               result) ||
        result.trapReceived || result.replyCount == 0) {
      errorOut = "Unable to read Wi-Fi configuration";
      return false;
    }
    replyAttr(result, 0, ".id", cfgId);
  }
  if (cfgId.isEmpty()) {
    errorOut = "Wi-Fi configuration id missing";
    return false;
  }
  RouterOsClient::CommandResult &setResult = RouterCommandScratchContext::acquire();
  const String setAttrs[] = {"=.id=" + cfgId, "=ssid=" + ssid};
  RouterOsClient::initializeCommandResult(setResult);
  if (!client.executeCommand("/interface/wifi/configuration/set", setAttrs, 2,
                             setResult) ||
      setResult.trapReceived) {
    errorOut = setResult.trapMessage.isEmpty() ? "Failed to update Wi-Fi"
                                               : setResult.trapMessage;
    return false;
  }
  return true;
}

bool applySsidOnly(RouterOsClient &client, const String &interfaceId,
                   const String &ssid, JsonObject out, String &errorOut) {
  errorOut = "";
  out["applied"]      = false;
  out["verified"]     = false;
  out["verification"] = "failed";
  out["ssid"]         = ssid;
  out["interface"]    = interfaceId;

  if (ssid.isEmpty()) {
    errorOut = "SSID is required";
    return false;
  }
  if (interfaceId.isEmpty()) {
    errorOut = "Wireless interface is not configured";
    return false;
  }
  if (!client.isConnected() || !client.isLoggedIn()) {
    errorOut = "Not connected to RouterOS API";
    return false;
  }

  // PHASE 1 — ONE targeted pre-read (legacy wireless first).
  Serial.printf("[router-wireless] ssid-save begin iface=%s\n", interfaceId.c_str());
  String routerId;
  String previousSsid;
  bool legacy = true;
  {
    RouterOsClient::CommandResult &pre = RouterCommandScratchContext::acquire();
    const String filter[] = {
        "?name=" + interfaceId,
        "=.proplist=.id,name,ssid",
    };
    RouterOsClient::initializeCommandResult(pre);
    if (client.executeCommand("/interface/wireless/print", filter, 2, pre) &&
        !pre.trapReceived && pre.replyCount > 0) {
      replyAttr(pre, 0, ".id", routerId);
      replyAttr(pre, 0, "ssid", previousSsid);
    }
  }

  if (routerId.isEmpty()) {
    if (!client.isConnected() || !client.isLoggedIn()) {
      errorOut = "RouterOS session lost during wireless pre-read";
      return false;
    }
    // wifiwave2 only if legacy target missing — still targeted, no inventory.
    legacy = false;
    RouterOsClient::CommandResult &pre = RouterCommandScratchContext::acquire();
    const String wifiFilter[] = {
        "?name=" + interfaceId,
        "=.proplist=.id,name,configuration",
    };
    RouterOsClient::initializeCommandResult(pre);
    if (!client.executeCommand("/interface/wifi/print", wifiFilter, 2, pre) ||
        pre.trapReceived || pre.replyCount == 0) {
      errorOut = "Configured wireless interface was not found on RouterOS";
      return false;
    }
    String configuration;
    replyAttr(pre, 0, "configuration", configuration);
    if (configuration.isEmpty()) {
      errorOut = "Wi-Fi configuration is missing";
      return false;
    }
    const String cfgFilter[] = {"?name=" + configuration, "=.proplist=.id,name,ssid"};
    RouterOsClient::initializeCommandResult(pre);
    if (!client.executeCommand("/interface/wifi/configuration/print", cfgFilter, 2,
                               pre) ||
        pre.trapReceived || pre.replyCount == 0) {
      errorOut = "Unable to read Wi-Fi configuration";
      return false;
    }
    replyAttr(pre, 0, ".id", routerId);
    replyAttr(pre, 0, "ssid", previousSsid);
  }

  if (routerId.isEmpty()) {
    errorOut = "Wireless interface id missing";
    return false;
  }
  if (!previousSsid.isEmpty()) out["previousSsid"] = previousSsid;

  // PHASE 2 — ONE targeted SSID-only set.
  {
    RouterOsClient::CommandResult &setResult = RouterCommandScratchContext::acquire();
    const String setAttrs[] = {"=.id=" + routerId, "=ssid=" + ssid};
    RouterOsClient::initializeCommandResult(setResult);
    const char *setPath =
        legacy ? "/interface/wireless/set" : "/interface/wifi/configuration/set";
    if (!client.executeCommand(setPath, setAttrs, 2, setResult) ||
        setResult.trapReceived) {
      errorOut = setResult.trapMessage.isEmpty() ? "Failed to update SSID"
                                                 : setResult.trapMessage;
      return false;
    }
  }
  out["applied"] = true;

  // INTENTIONAL: no post-SET wireless print / settle / wifi fallback.
  // Radio reconfiguration on hAP lite makes immediate readback collide with
  // ROUTEROS_IO_TIMEOUT (~8s). SET ACK is sufficient for APPLIED; later
  // Synchronize / Refresh Router Information may observe VERIFIED.
  out["ssid"]         = ssid;
  out["verified"]     = false;
  out["verification"] = "deferred";
  Serial.printf(
      "[router-wireless] ssid-set ack=yes iface=%s verification=deferred\n",
      interfaceId.c_str());
  return true;
}

void fillWirelessApiJson(const CanonicalConfig &canonical, JsonObject out) {
  if (!canonical.mode.isEmpty()) out["wifiMode"] = canonical.mode;
  if (!canonical.interfaceId.isEmpty()) out["interfaceId"] = canonical.interfaceId;
  out["configured"] = canonical.configured;
}

String formatBandLabel(const String &raw) {
  String lower = raw;
  lower.toLowerCase();
  // Prefer explicit ghz tokens (covers RouterOS "2ghz-b/g/n", "5ghz-a/n/ac").
  if (lower.indexOf("5ghz") >= 0) return "5GHz";
  if (lower.indexOf("2ghz") >= 0 || lower.indexOf("2.4") >= 0) return "2.4GHz";
  if (lower.indexOf("5g") >= 0) return "5GHz";
  if (lower.indexOf("2g") >= 0) return "2.4GHz";
  if (!raw.isEmpty()) return raw;
  return "Unknown";
}

String formatBandFromFrequencyMhz(long freqMhz) {
  if (freqMhz >= 5000 && freqMhz < 6000) return "5GHz";
  if (freqMhz >= 2400 && freqMhz < 2500) return "2.4GHz";
  return "";
}

}  // namespace RouterWireless
