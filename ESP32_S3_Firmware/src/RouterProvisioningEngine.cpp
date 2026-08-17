#include "RouterProvisioningEngine.h"

#include <memory>

#include <SPIFFS.h>

#include "Config.h"
#include "InstallationState.h"
#include "InstallationStateManager.h"
#include "RouterOsClient.h"
#include "RouterProvisioningManager.h"
#include "RouterProvisioningTypes.h"
#include "SetupProvisioningManager.h"
#include "SetupRouterConnectionManager.h"
#include "SetupStatusContext.h"
#include "SetupWizardConfigManager.h"
#include "EthernetManager.h"
#include "ManagementApLifecycle.h"
#include "router/RouterPlatform.h"
#include "StorageManager.h"
#include "StoragePaths.h"
#include "RouterCommandScratch.h"
#include "FinishTrace.h"
#include "ProductionNetworkTrace.h"
#include "DmaMemoryMonitor.h"
#include "RouterApiTransportGate.h"

namespace {

constexpr const char *kSsidPolicyKeep   = "keep";
constexpr const char *kSsidPolicyRename  = "rename";
constexpr const char *kHotspotDirDefault = "hotspot";
constexpr const char *kPortalPlaceholder = "__RENZFI_APPLIANCE_BASE_URL__";
constexpr const char *kWalledGardenComment = "Renz-Fi ESP32 appliance API";
constexpr const char *kFinishTokenPrefix   = "renzfi-finish-";

// Essential MikroTik captive-portal entry under the profile html-directory:
// <html-directory>/login.html (queried with one targeted /file/print).

const char *portalDeploymentModeLabel(
    RouterProvisioningEngine::PortalDeploymentMode mode) {
  return RouterProvisioningEngine::portalDeploymentModeLabel(mode);
}

RouterProvisioningEngine::PortalDeploymentMode parsePortalDeploymentMode(
    const char *raw) {
  return RouterProvisioningEngine::parsePortalDeploymentModeLabel(raw);
}

bool replyAttr(const RouterOsClient::CommandResult &result, uint8_t replyIdx,
               const char *key, String &valueOut) {
  if (replyIdx >= result.replyCount) return false;
  const auto &record = result.replyAt(replyIdx);
  for (uint8_t i = 0; i < record.attrCount; ++i) {
    String keyPart, valPart;
    if (!RouterOsClient::parseAttr(record.attr(i), keyPart, valPart)) continue;
    if (keyPart == key) {
      valueOut = valPart;
      return true;
    }
  }
  return false;
}

int findReplyByAttr(const RouterOsClient::CommandResult &result, const char *key,
                    const String &value) {
  for (uint8_t i = 0; i < result.replyCount; ++i) {
    String found;
    if (replyAttr(result, i, key, found) && found == value) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

class RouterSession {
 public:
  explicit RouterSession(EthernetManager *eth) : _eth(eth) {
    _client.setTimeouts(RenzFiConfig::SETUP_ROUTER_CONNECT_TIMEOUT_MS,
                        RenzFiConfig::SETUP_ROUTER_IO_TIMEOUT_MS);
  }

  bool open(const SetupRouterConnectionManager::RouterInput &input,
            String &errorOut, String &errorCodeOut) {
    errorOut.clear();
    errorCodeOut.clear();
    if (!_eth || !_eth->linkUp() || !_eth->hasIp()) {
      errorOut     = "Ethernet link and DHCP IP are required";
      errorCodeOut = "ETHERNET_NOT_READY";
      return false;
    }
    _client.setCredentials(input.host, input.username, input.password,
                          input.apiPort);
    _client.setCredentialSource("finish-provisioning");
    if (!_client.connect()) {
      errorOut     = _client.lastError();
      errorCodeOut = _client.lastErrorCode().isEmpty() ? "TCP_CONNECT_FAILED"
                                                       : _client.lastErrorCode();
      return false;
    }
    if (!_client.login()) {
      errorOut     = _client.lastError();
      errorCodeOut = _client.lastErrorCode().isEmpty() ? "API_LOGIN_FAILED"
                                                       : _client.lastErrorCode();
      _client.disconnect();
      return false;
    }
    _open = true;
    return true;
  }

  void close() {
    if (_open) _client.disconnect("success");
    _open = false;
  }

  bool ensureAuthenticated(const SetupRouterConnectionManager::RouterInput &input,
                           String &errorOut, String &errorCodeOut,
                           bool &reconnectedOut) {
    reconnectedOut = false;
    if (_client.isConnected() && _client.isLoggedIn()) return true;
    if (_open) {
      _client.disconnect("reconnect");
      _open = false;
    }
    reconnectedOut = true;
    return open(input, errorOut, errorCodeOut);
  }

  RouterOsClient &client() { return _client; }

 private:
  EthernetManager *_eth = nullptr;
  RouterOsClient   _client;
  bool             _open = false;
};

bool bridgeHasInterface(RouterOsClient &client, const String &bridgeName,
                        const String &ifaceName, String &errorOut) {
  FinishTrace::BlockingOpScope op(
      FinishTrace::pipelineActive()
          ? FinishTrace::routerApiOp("RouterOS API: /interface/bridge/port/print")
          : FinishTrace::BlockingOpConfig{""});
  RouterOsClient::CommandResult &ports = RouterCommandScratchContext::acquire();
  // Bound proplist — membership check only needs bridge + interface.
  const String portAttrs[] = {"=.proplist=bridge,interface"};
  if (!client.executeCommand("/interface/bridge/port/print", portAttrs, 1,
                             ports) ||
      ports.trapReceived) {
    if (FinishTrace::pipelineActive()) op.fail("RouterOS command failed");
    errorOut = "Unable to read bridge ports";
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

bool hotspotOnInterface(RouterOsClient &client, const String &ifaceName,
                        String &hotspotIdOut, String &profileOut) {
  FinishTrace::BlockingOpScope op(
      FinishTrace::pipelineActive()
          ? FinishTrace::routerApiOp("RouterOS API: /ip/hotspot/print")
          : FinishTrace::BlockingOpConfig{""});
  hotspotIdOut = "";
  profileOut   = "";
  RouterOsClient::CommandResult &hs = RouterCommandScratchContext::acquire();
  if (!client.executeCommand("/ip/hotspot/print", hs) || hs.trapReceived) {
    if (FinishTrace::pipelineActive()) op.fail("RouterOS command failed");
    return false;
  }
  for (uint8_t i = 0; i < hs.replyCount; ++i) {
    String iface, disabled, id, profile;
    replyAttr(hs, i, "interface", iface);
    replyAttr(hs, i, "disabled", disabled);
    replyAttr(hs, i, ".id", id);
    replyAttr(hs, i, "profile", profile);
    if (iface != ifaceName || disabled == "true") continue;
    hotspotIdOut = id;
    profileOut     = profile;
    return true;
  }
  return false;
}

void hotspotVerifyLog(const char *key, const char *value) {
  Serial.printf("[hotspot-verify] %s=%s\n", key, value ? value : "");
}

void hotspotVerifyLogBool(const char *key, bool value) {
  Serial.printf("[hotspot-verify] %s=%s\n", key, value ? "true" : "false");
}

void hotspotVerifyFail(const char *reason, const char *expected = nullptr,
                       const char *actual = nullptr) {
  Serial.println(F("[hotspot-verify] RESULT=FAIL"));
  Serial.printf("[hotspot-verify] FAILURE_REASON=%s\n",
                reason ? reason : "UNKNOWN");
  if (expected && expected[0]) {
    Serial.printf("[hotspot-verify] expected=%s\n", expected);
  }
  if (actual && actual[0]) {
    Serial.printf("[hotspot-verify] actual=%s\n", actual);
  }
}

void hotspotVerifyPass() { Serial.println(F("[hotspot-verify] RESULT=PASS")); }

// Finish-pipeline hotspot check — wireless direct, then bridged fallback.
bool verifyFinishHotspot(RouterOsClient &client, const String &wirelessIface,
                         const String &bridgeName, String &hotspotIdOut,
                         String &profileOut, String &errorOut) {
  hotspotIdOut = "";
  profileOut   = "";
  errorOut.clear();

  hotspotVerifyLog("interfaceExpected", wirelessIface.c_str());
  hotspotVerifyLog("bridgeExpected", bridgeName.c_str());

  RouterOsClient::CommandResult &hs = RouterCommandScratchContext::acquire();
  if (!client.executeCommand("/ip/hotspot/print", hs) || hs.trapReceived) {
    hotspotVerifyFail("ROUTEROS_COMMAND_FAILED", "/ip/hotspot/print",
                      hs.trapMessage.c_str());
    errorOut = hs.trapMessage.isEmpty() ? "Unable to read hotspot servers"
                                        : hs.trapMessage;
    return false;
  }

  Serial.printf("[hotspot-verify] hotspotCount=%u\n",
                static_cast<unsigned>(hs.replyCount));

  bool foundOnWireless = false;
  bool foundOnBridge   = false;
  String wirelessHotspotName;
  String bridgeHotspotName;
  String bridgeHotspotProfile;
  String bridgeHotspotId;
  String firstEnabledIface;

  for (uint8_t i = 0; i < hs.replyCount; ++i) {
    String name, iface, disabled, id, profile;
    replyAttr(hs, i, "name", name);
    replyAttr(hs, i, "interface", iface);
    replyAttr(hs, i, "disabled", disabled);
    replyAttr(hs, i, ".id", id);
    replyAttr(hs, i, "profile", profile);

    Serial.printf("[hotspot-verify] hotspot[%u] name=%s interface=%s disabled=%s "
                  "profile=%s id=%s\n",
                  static_cast<unsigned>(i), name.c_str(), iface.c_str(),
                  disabled.c_str(), profile.c_str(), id.c_str());

    if (disabled == "true") continue;
    if (firstEnabledIface.isEmpty()) firstEnabledIface = iface;

    if (!wirelessIface.isEmpty() && iface == wirelessIface) {
      foundOnWireless     = true;
      wirelessHotspotName = name;
      hotspotIdOut        = id;
      profileOut          = profile;
    }
    if (!bridgeName.isEmpty() && iface == bridgeName) {
      foundOnBridge         = true;
      bridgeHotspotName     = name;
      bridgeHotspotId       = id;
      bridgeHotspotProfile  = profile;
    }
  }

  hotspotVerifyLogBool("hotspotFoundOnWireless", foundOnWireless);
  if (foundOnWireless) {
    hotspotVerifyLog("hotspotName", wirelessHotspotName.c_str());
    hotspotVerifyLog("profile", profileOut.c_str());
    hotspotVerifyLog("interfaceActual", wirelessIface.c_str());
  }

  hotspotVerifyLogBool("hotspotFoundOnBridge", foundOnBridge);
  if (foundOnBridge) {
    hotspotVerifyLog("bridgeHotspotName", bridgeHotspotName.c_str());
    hotspotVerifyLog("bridgeHotspotProfile", bridgeHotspotProfile.c_str());
    hotspotVerifyLog("interfaceActual", bridgeName.c_str());
  }

  bool wirelessInBridge = false;
  if (!bridgeName.isEmpty() && !wirelessIface.isEmpty()) {
    String bridgeErr;
    wirelessInBridge =
        bridgeHasInterface(client, bridgeName, wirelessIface, bridgeErr);
    hotspotVerifyLogBool("wirelessInBridge", wirelessInBridge);
    if (!wirelessInBridge && !bridgeErr.isEmpty()) {
      hotspotVerifyLog("bridgePortReadError", bridgeErr.c_str());
    }
  }

  if (foundOnWireless) {
    if (profileOut.isEmpty()) {
      hotspotVerifyFail("MISSING_HOTSPOT_PROFILE", "profile", "(empty)");
      errorOut = "Hotspot on selected wireless interface has no profile";
      return false;
    }
    hotspotVerifyLog("Validation Mode", "WIRELESS_DIRECT");
    hotspotVerifyPass();
    return true;
  }

  if (!bridgeName.isEmpty() && foundOnBridge) {
    hotspotVerifyLog("Validation Mode", "BRIDGE_FALLBACK");
    if (wirelessIface.isEmpty()) {
      hotspotVerifyFail("WIRELESS_INTERFACE_MISSING", "selectedWirelessInterface",
                        "(empty)");
      errorOut = "Selected wireless interface is required for bridge fallback";
      return false;
    }
    if (!wirelessInBridge) {
      hotspotVerifyLog("expectedBridge", bridgeName.c_str());
      hotspotVerifyLog("actualBridge", bridgeName.c_str());
      hotspotVerifyLogBool("wirelessInBridge", false);
      hotspotVerifyFail("WIRELESS_NOT_IN_BRIDGE", bridgeName.c_str(),
                        wirelessIface.c_str());
      errorOut = "Selected wireless interface is not a member of the guest bridge";
      return false;
    }
    if (bridgeHotspotProfile.isEmpty()) {
      hotspotVerifyFail("MISSING_HOTSPOT_PROFILE", "profile", "(empty)");
      errorOut = "Hotspot on guest bridge has no profile";
      return false;
    }
    hotspotIdOut = bridgeHotspotId;
    profileOut   = bridgeHotspotProfile;
    Serial.println(F("[hotspot-verify] Bridge fallback accepted."));
    hotspotVerifyLog("wireless", wirelessIface.c_str());
    hotspotVerifyLog("bridge", bridgeName.c_str());
    hotspotVerifyLog("hotspotInterface", bridgeName.c_str());
    hotspotVerifyPass();
    return true;
  }

  if (!bridgeName.isEmpty() && !foundOnBridge) {
    hotspotVerifyFail("HOTSPOT_NOT_ON_BRIDGE", bridgeName.c_str(),
                      firstEnabledIface.isEmpty() ? "(none)"
                                                  : firstEnabledIface.c_str());
  } else if (!firstEnabledIface.isEmpty()) {
    hotspotVerifyFail("HOTSPOT_INTERFACE_MISMATCH", wirelessIface.c_str(),
                      firstEnabledIface.c_str());
  } else {
    hotspotVerifyFail("HOTSPOT_NOT_FOUND", wirelessIface.c_str(), "(none)");
  }

  errorOut =
      "Hotspot must already exist on the selected wireless interface or guest bridge";
  return false;
}

bool fileExistsOnRouter(RouterOsClient &client, const String &path) {
  RouterOsClient::CommandResult &files = RouterCommandScratchContext::acquire();
  const String filter[] = {"?name=" + path};
  if (!client.executeCommand("/file/print", filter, 1, files) ||
      files.trapReceived) {
    return false;
  }
  return files.replyCount > 0;
}

bool resolveHotspotHtmlDirectory(RouterOsClient &client,
                                 const String &profileName,
                                 String &htmlDirOut, String &errorOut) {
  htmlDirOut = kHotspotDirDefault;
  RouterOsClient::CommandResult &result = RouterCommandScratchContext::acquire();
  RouterOsClient::initializeCommandResult(result);
  if (!client.executeCommand("/ip/hotspot/profile/print", result) ||
      result.trapReceived) {
    errorOut = "Unable to read hotspot profiles";
    return false;
  }
  int idx = findReplyByAttr(result, "name", profileName);
  if (idx < 0) {
    errorOut = "Hotspot profile not found: " + profileName;
    return false;
  }
  String htmlDir;
  replyAttr(result, static_cast<uint8_t>(idx), "html-directory", htmlDir);
  if (!htmlDir.isEmpty()) htmlDirOut = htmlDir;
  return true;
}

struct PortalVerifyReport {
  bool success = true;          // false only when Managed + missing essentials
  bool blocking = false;
  const char *status = "verified";  // verified | unverified | skipped
  String htmlDirectory;
  String detail;
  uint8_t inventoryQueryCount = 0;
  bool loginHtmlPresent = false;
};

// MANUAL_EXTERNAL / MANAGED: resolve profile html-directory, then ONE targeted
// /file/print ?name=<html-directory>/login.html. Never enumerate the whole FS.
// SKIPPED: zero file queries.
PortalVerifyReport verifyMikroTikCaptivePortal(
    RouterOsClient &client, const String &profileName,
    RouterProvisioningEngine::PortalDeploymentMode mode) {
  PortalVerifyReport report;
  report.blocking = false;
  const char *modeLabel = portalDeploymentModeLabel(mode);
  Serial.printf("[portal-verify] mode=%s\n", modeLabel);
  Serial.printf("[portal-verify] profile=%s\n",
                profileName.isEmpty() ? "(unknown)" : profileName.c_str());

  if (mode == RouterProvisioningEngine::PortalDeploymentMode::Skipped) {
    report.status = "skipped";
    report.detail = "Portal verification skipped by installer";
    Serial.println(F("[portal-verify] result=SKIPPED"));
    Serial.println(F("[portal-verify] blocking=false"));
    return report;
  }

  DmaMemoryMonitor::logSnapshot("before portal-profile");
  String htmlDir;
  String resolveError;
  if (!resolveHotspotHtmlDirectory(client, profileName, htmlDir, resolveError)) {
    DmaMemoryMonitor::logSnapshot("after portal-profile");
    report.status = "unverified";
    report.detail = resolveError;
    Serial.printf("[portal-verify] htmlDirectory=(unresolved) error=%s\n",
                  resolveError.c_str());
    if (mode == RouterProvisioningEngine::PortalDeploymentMode::Managed) {
      report.success  = false;
      report.blocking = true;
      Serial.println(F("[portal-verify] result=FAILED"));
      Serial.println(F("[portal-verify] blocking=true"));
    } else {
      Serial.println(F("[portal-verify] result=UNVERIFIED"));
      Serial.println(F("[portal-verify] blocking=false"));
    }
    return report;
  }
  DmaMemoryMonitor::logSnapshot("after portal-profile");
  report.htmlDirectory = htmlDir;
  Serial.printf("[portal-verify] htmlDirectory=%s\n", htmlDir.c_str());

  if (!DmaMemoryMonitor::hasEthTransmitHeadroom()) {
    DmaMemoryMonitor::logSnapshot("before portal-file-query");
    report.status = "unverified";
    report.detail = "SPI DMA memory low — portal file query deferred";
    Serial.println(F("[portal-verify] SPI_DMA_ALLOCATION_FAILED (precheck)"));
    if (mode == RouterProvisioningEngine::PortalDeploymentMode::Managed) {
      report.success  = false;
      report.blocking = true;
      Serial.println(F("[portal-verify] result=FAILED"));
      Serial.println(F("[portal-verify] blocking=true"));
    } else {
      Serial.println(F("[portal-verify] result=UNVERIFIED"));
      Serial.println(F("[portal-verify] blocking=false"));
    }
    return report;
  }

  const String loginPath = htmlDir + "/login.html";
  DmaMemoryMonitor::logSnapshot("before portal-file-query");
  RouterOsClient::CommandResult &files = RouterCommandScratchContext::acquire();
  RouterOsClient::initializeCommandResult(files);
  const String filter[] = {"?name=" + loginPath};
  const bool queryOk =
      client.executeCommand("/file/print", filter, 1, files) && !files.trapReceived;
  report.inventoryQueryCount = 1;
  DmaMemoryMonitor::logSnapshot("after portal-file-query");

  if (!queryOk) {
    report.status = "unverified";
    report.detail = files.trapMessage.isEmpty()
                        ? (client.lastError().isEmpty()
                               ? "Unable to query MikroTik portal file"
                               : client.lastError())
                        : files.trapMessage;
    // Propagate transport DMA failures as controlled unverified in manual mode.
    if (client.lastErrorCode() == "SPI_DMA_ALLOCATION_FAILED" ||
        client.lastErrorCode() == "ETH_DMA_LOW") {
      report.detail = "SPI_DMA_ALLOCATION_FAILED";
    }
    Serial.printf("[portal-verify] file query failed: %s\n", report.detail.c_str());
    if (mode == RouterProvisioningEngine::PortalDeploymentMode::Managed) {
      report.success  = false;
      report.blocking = true;
      Serial.println(F("[portal-verify] result=FAILED"));
      Serial.println(F("[portal-verify] blocking=true"));
    } else {
      Serial.println(F("[portal-verify] result=UNVERIFIED"));
      Serial.println(F("[portal-verify] blocking=false"));
    }
    return report;
  }

  report.loginHtmlPresent = files.replyCount > 0;
  Serial.printf("[portal-verify] inventoryQueryCount=1\n");
  Serial.printf("[portal-verify] loginHtml=%s\n",
                report.loginHtmlPresent ? "yes" : "no");

  if (report.loginHtmlPresent) {
    report.status = "verified";
    Serial.println(F("[portal-verify] result=VERIFIED"));
    Serial.println(F("[portal-verify] blocking=false"));
    return report;
  }

  report.status = "unverified";
  report.detail = "Missing captive portal file: " + loginPath;
  Serial.printf("[portal-verify] missing=%s\n", loginPath.c_str());
  if (mode == RouterProvisioningEngine::PortalDeploymentMode::Managed) {
    report.success  = false;
    report.blocking = true;
    Serial.println(F("[portal-verify] result=FAILED"));
    Serial.println(F("[portal-verify] blocking=true"));
  } else {
    Serial.println(F("[portal-verify] result=UNVERIFIED"));
    Serial.println(F("[portal-verify] blocking=false"));
  }
  return report;
}

bool ensureHotspotProfileHtmlDirectory(RouterOsClient &client,
                                       const String &profileName,
                                       String &errorOut,
                                       bool mutateDirectory) {
  RouterOsClient::CommandResult &result = RouterCommandScratchContext::acquire();

  RouterOsClient::initializeCommandResult(result);
  if (!client.executeCommand("/ip/hotspot/profile/print", result) ||
      result.trapReceived) {
    errorOut = "Unable to read hotspot profiles";
    return false;
  }
  int idx = findReplyByAttr(result, "name", profileName);
  if (idx < 0) {
    errorOut = "Hotspot profile not found: " + profileName;
    return false;
  }
  String id, htmlDir;
  replyAttr(result, static_cast<uint8_t>(idx), ".id", id);
  replyAttr(result, static_cast<uint8_t>(idx), "html-directory", htmlDir);
  if (!mutateDirectory) {
    Serial.printf("[finish-stage] hotspot-profile leave html-directory=%s\n",
                  htmlDir.isEmpty() ? "(empty)" : htmlDir.c_str());
    return true;
  }
  if (htmlDir == kHotspotDirDefault) return true;
  const String setAttrs[] = {"=.id=" + id,
                             "=html-directory=" + String(kHotspotDirDefault)};
  RouterOsClient::initializeCommandResult(result);
  if (!client.executeCommand("/ip/hotspot/profile/set", setAttrs, 2, result) ||
      result.trapReceived) {
    errorOut = result.trapMessage.isEmpty()
                   ? "Unable to set hotspot html-directory"
                   : result.trapMessage;
    return false;
  }
  return true;
}

bool fetchPortalFile(RouterOsClient &client, const String &url,
                     const String &dstPath, String &errorOut) {
  FinishTrace::BlockingOpScope fetchOp(
      FinishTrace::pipelineActive()
          ? FinishTrace::portalFetchOp("RouterOS /tool/fetch")
          : FinishTrace::BlockingOpConfig{""});
  Serial.printf("[finish-op] fetch url=%s dst=%s\n", url.c_str(), dstPath.c_str());
  if (fileExistsOnRouter(client, dstPath)) {
    FinishTrace::opEvent("RouterOS fetch skipped — file already on router");
    FinishTrace::opReturn("fetchPortalFile", true);
    return true;
  }
  const String attrs[] = {"=url=" + url, "=dst-path=" + dstPath, "=mode=http",
                            "=check-certificate=no"};
  RouterOsClient::CommandResult &fetch = RouterCommandScratchContext::acquire();
  FinishTrace::BlockingOpScope::updateActiveState("awaiting /tool/fetch",
                                                  "RouterOS reply");
  if (!client.executeCommand("/tool/fetch", attrs, 4, fetch) || fetch.trapReceived) {
    fetchOp.fail(fetch.trapMessage.isEmpty() ? "Portal file fetch failed"
                                             : fetch.trapMessage.c_str());
    errorOut = fetch.trapMessage.isEmpty() ? "Portal file fetch failed"
                                           : fetch.trapMessage;
    FinishTrace::opReturn("fetchPortalFile", false);
    return false;
  }
  FinishTrace::opEvent("RouterOS replied");
  {
    FinishTrace::BlockingOpScope waitOp(FinishTrace::fixedDelayOp("delay(1500) post-fetch", 1500));
    delay(1500);
  }
  {
    FinishTrace::BlockingOpScope verifyOp(
        FinishTrace::routerApiOp("RouterOS /file/print verify"));
    if (!fileExistsOnRouter(client, dstPath)) {
      verifyOp.fail("Portal file not present after fetch");
      fetchOp.fail("Portal file not present after fetch");
      errorOut = "Portal file not present after fetch: " + dstPath;
      FinishTrace::opReturn("fetchPortalFile", false);
      return false;
    }
  }
  FinishTrace::opEvent("RouterOS fetch finished");
  FinishTrace::opReturn("fetchPortalFile", true);
  return true;
}

bool ensureWalledGardenRule(RouterOsClient &client, const String &espIp,
                            String &errorOut) {
  RouterOsClient::CommandResult &result = RouterCommandScratchContext::acquire();

  RouterOsClient::initializeCommandResult(result);
  if (!client.executeCommand("/ip/hotspot/walled-garden/ip/print", result) ||
      result.trapReceived) {
    errorOut = "Unable to read walled-garden rules";
    return false;
  }
  const String target = espIp + "/32";
  for (uint8_t i = 0; i < result.replyCount; ++i) {
    String dst, comment, action, disabled;
    replyAttr(result, i, "dst-address", dst);
    replyAttr(result, i, "comment", comment);
    replyAttr(result, i, "action", action);
    replyAttr(result, i, "disabled", disabled);
    if (disabled == "true") continue;
    if (dst == target && action == "accept") {
      if (comment == kWalledGardenComment ||
          comment.startsWith(RouterProvisioning::COMMENT_PREFIX)) {
        return true;
      }
      return true;
    }
    if (comment == kWalledGardenComment) {
      String id;
      if (replyAttr(result, i, ".id", id)) {
        const String setAttrs[] = {"=.id=" + id, "=dst-address=" + target,
                                   "=action=accept"};
        RouterOsClient::initializeCommandResult(result);
        if (!client.executeCommand("/ip/hotspot/walled-garden/ip/set", setAttrs, 3,
                                   result) ||
            result.trapReceived) {
          errorOut = result.trapMessage.isEmpty()
                         ? "Unable to update walled-garden rule"
                         : result.trapMessage;
          return false;
        }
        return true;
      }
    }
  }
  const String addAttrs[] = {"=dst-address=" + target, "=action=accept",
                             "=comment=" + String(kWalledGardenComment)};
  RouterOsClient::initializeCommandResult(result);
  if (!client.executeCommand("/ip/hotspot/walled-garden/ip/add", addAttrs, 3, result) ||
      result.trapReceived) {
    errorOut = result.trapMessage.isEmpty() ? "Unable to add walled-garden rule"
                                            : result.trapMessage;
    return false;
  }
  return true;
}

bool ensureManagedScript(RouterOsClient &client, const char *scriptName,
                         const char *source, String &errorOut) {
  // OPTIONAL finish helper: creates a log-only marker script. Callers must not
  // treat failure as a blocking install gate (see runFinishPipeline scripts).
  RouterOsClient::CommandResult &result = RouterCommandScratchContext::acquire();

  RouterOsClient::initializeCommandResult(result);
  if (!client.executeCommand("/system/script/print", result) ||
      result.trapReceived) {
    errorOut = "Unable to read system scripts";
    return false;
  }
  for (uint8_t i = 0; i < result.replyCount; ++i) {
    String name;
    if (replyAttr(result, i, "name", name) && name == scriptName) {
      return true;
    }
  }
  const String attrs[] = {"=name=" + String(scriptName),
                            "=comment=" + String(RouterProvisioning::COMMENT_PREFIX) +
                                " managed script",
                            "=source=" + String(source)};
  RouterOsClient::initializeCommandResult(result);
  if (!client.executeCommand("/system/script/add", attrs, 3, result) ||
      result.trapReceived) {
    errorOut = result.trapMessage.isEmpty() ? "Unable to add system script"
                                            : result.trapMessage;
    return false;
  }
  return true;
}

String spiffsPortalPath(const String &filename) {
  return String(StoragePaths::Spiffs::Portal) + "/" + filename;
}

String contentTypeForPortalFile(const String &filename) {
  if (filename.endsWith(".html")) return "text/html";
  if (filename.endsWith(".js")) return "application/javascript";
  if (filename.endsWith(".css")) return "text/css";
  if (filename.endsWith(".png")) return "image/png";
  if (filename.endsWith(".ico")) return "image/x-icon";
  if (filename.endsWith(".mp3")) return "audio/mpeg";
  return "application/octet-stream";
}

bool readSpiffsBinary(const String &path, String &out) {
  if (!SPIFFS.exists(path)) return false;
  File f = SPIFFS.open(path, "r");
  if (!f) return false;
  out = f.readString();
  f.close();
  return true;
}

bool commitFinishInstallationState(InstallationStateManager *installation) {
  FinishTrace::StageScope stage("commitFinishInstallationState");
  if (!installation) {
    stage.fail();
    FinishTrace::opReturn("commitFinishInstallationState", false);
    return false;
  }
  if (installation->isReady()) {
    FinishTrace::opReturn("commitFinishInstallationState", true);
    return true;
  }
  bool ok = installation->setState(InstallationState::Provisioned);
  if (!ok) {
    stage.fail();
    FinishTrace::opReturn("commitFinishInstallationState", false);
    return false;
  }
  if (!installation->isReady()) {
    stage.fail();
    FinishTrace::opReturn("commitFinishInstallationState", false);
    return false;
  }
  FinishTrace::opReturn("commitFinishInstallationState", true);
  return true;
}

void logFinishLifecycleStatus(SetupProvisioningManager *provisioning,
                              InstallationStateManager *installation,
                              RouterProvisioningManager *routerProvisioning,
                              EthernetManager *eth) {
  if (!provisioning || !installation || !eth) return;
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  JsonObject data = doc.to<JsonObject>();
  provisioning->fillSetupStatus(
      data, eth, buildSetupStatusContext(routerProvisioning, nullptr));
}

bool verifyProvisionedPersisted(StorageManager *storage) {
  FinishTrace::StageScope stage("verifyProvisionedPersisted");
  if (!storage) {
    stage.fail();
    FinishTrace::opReturn("verifyProvisionedPersisted", false);
    return false;
  }
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_SMALL);
  bool readOk = false;
  bool ok = false;
  {
    FinishTrace::OpScope verifyOp("verify()");
    {
      FinishTrace::BlockingOpScope readOp(FinishTrace::storageReadOp(
          "readJson installation.json",
          (storage && storage->healthy()) ? "SD card read" : "LittleFS read"));
      readOk = storage->readJson(StoragePaths::InstallationFile, doc);
      if (!readOk) readOp.fail("readJson failed");
    }
    if (!readOk) {
      verifyOp.fail();
      stage.fail();
      FinishTrace::opReturn("verifyProvisionedPersisted", false);
      return false;
    }
    const char *state = doc["state"] | "";
    {
      FinishTrace::OpScope fieldOp("verify state field");
      ok = strcmp(state, "provisioned") == 0 || strcmp(state, "ready") == 0;
      if (!ok) fieldOp.fail();
    }
    if (!ok) verifyOp.fail();
  }
  if (!ok) stage.fail();
  FinishTrace::opReturn("verifyProvisionedPersisted", ok);
  return ok;
}

}  // namespace

const char *RouterProvisioningEngine::portalDeploymentModeLabel(
    PortalDeploymentMode mode) {
  switch (mode) {
    case PortalDeploymentMode::Managed:
      return "MANAGED";
    case PortalDeploymentMode::Skipped:
      return "SKIPPED";
    case PortalDeploymentMode::ManualExternal:
    default:
      return "MANUAL_EXTERNAL";
  }
}

RouterProvisioningEngine::PortalDeploymentMode
RouterProvisioningEngine::parsePortalDeploymentModeLabel(const char *raw) {
  if (!raw || !*raw) return PortalDeploymentMode::ManualExternal;
  String value(raw);
  value.toLowerCase();
  if (value == "skipped" || value == "skip") {
    return PortalDeploymentMode::Skipped;
  }
  if (value == "managed") return PortalDeploymentMode::Managed;
  if (value == "manual_external" || value == "manual-external" ||
      value == "manual") {
    return PortalDeploymentMode::ManualExternal;
  }
  return PortalDeploymentMode::ManualExternal;
}

void RouterProvisioningEngine::setPortalDeploymentModeFromLabel(
    const char *label) {
  _portalDeploymentMode = parsePortalDeploymentModeLabel(label);
}

void RouterProvisioningEngine::begin(
    StorageManager *storage, InstallationStateManager *installation,
    SetupProvisioningManager *setupProvisioning,
    SetupRouterConnectionManager *routerConnection,
    RouterProvisioningManager *routerProvisioning,
    SetupWizardConfigManager *wizardConfig, EthernetManager *eth,
    RouterPlatform *router, ManagementApLifecycle *mgmtApLifecycle) {
  _storage             = storage;
  _installation        = installation;
  _setupProvisioning   = setupProvisioning;
  _routerConnection    = routerConnection;
  _routerProvisioning  = routerProvisioning;
  _wizardConfig        = wizardConfig;
  _eth                 = eth;
  _router              = router;
  _mgmtApLifecycle     = mgmtApLifecycle;
  loadWirelessSelection();
  if (_installation && _installation->isReady()) {
    _finishCompleted = true;
  }
}

void RouterProvisioningEngine::reportProgress(ProgressFn fn, void *ctx,
                                              const char *stageId,
                                              const char *label) const {
  if (fn) fn(ctx, stageId, label);
}

String RouterProvisioningEngine::applianceBaseUrl() const {
  if (_eth && _eth->hasIp()) {
    return String("http://") + _eth->ip();
  }
  return "";
}

String RouterProvisioningEngine::portalFetchUrl(const String &filename) const {
  return applianceBaseUrl() + "/api/setup/provisioning/portal/" + filename +
         "?token=" + _portalFetchToken;
}

RouterProvisioningEngine::OperationResult
RouterProvisioningEngine::ensurePreconditions() const {
  OperationResult result;
  if (!_storage || !_installation || !_setupProvisioning || !_routerConnection ||
      !_routerProvisioning || !_eth || !_router) {
    result.errorCode    = "INTERNAL_ERROR";
    result.errorMessage = "Router provisioning engine unavailable";
    result.httpStatus   = 503;
    return result;
  }
  if (!_setupProvisioning->ownerCreated()) {
    result.errorCode    = "OWNER_REQUIRED";
    result.errorMessage = "Owner account must be created before finishing setup";
    result.httpStatus   = 409;
    return result;
  }
  if (!_routerConnection->hasVerifiedConnection()) {
    result.errorCode    = "ROUTER_CONNECTION_REQUIRED";
    result.errorMessage = "Saved router credentials are required";
    result.httpStatus   = 409;
    return result;
  }
  if (!_routerProvisioning->isExistingNetworkAdopted()) {
    result.errorCode    = "ADOPTION_REQUIRED";
    result.errorMessage = "Existing network must be adopted before finishing setup";
    result.httpStatus   = 409;
    return result;
  }
  if (_eth->hasIp() == false) {
    result.errorCode    = "ETHERNET_NOT_READY";
    result.errorMessage = "Ethernet IP is required for router provisioning";
    result.httpStatus   = 409;
    return result;
  }
  result.success    = true;
  result.httpStatus = 200;
  return result;
}

bool RouterProvisioningEngine::loadWirelessSelection() {
  _selectedWirelessInterface = "";
  _ssidPolicy                = kSsidPolicyKeep;
  _targetSsid                = "";
  if (!_storage) return false;
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_SMALL);
  if (!_storage->readJson(StoragePaths::RouterProvisioningFile, doc)) {
    return false;
  }
  _selectedWirelessInterface = doc["selectedWirelessInterface"] | "";
  _ssidPolicy                = doc["ssidPolicy"] | kSsidPolicyKeep;
  _targetSsid                = doc["targetSsid"] | "";
  return true;
}

bool RouterProvisioningEngine::persistWirelessSelection(
    const String &interfaceName, const String &ssidPolicy,
    const String &targetSsid) {
  if (!_storage) return false;
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_SMALL);
  {
    FinishTrace::BlockingOpScope readOp(FinishTrace::pipelineActive()
                                            ? FinishTrace::storageReadOp(
                                                  "readJson router-provisioning.json",
                                                  (_storage && _storage->healthy())
                                                      ? "SD card read"
                                                      : "LittleFS read")
                                            : FinishTrace::BlockingOpConfig{""});
    if (!_storage->readJson(StoragePaths::RouterProvisioningFile, doc)) {
      if (FinishTrace::pipelineActive()) readOp.fail("read failed");
      doc.clear();
    }
  }
  doc["selectedWirelessInterface"] = interfaceName;
  doc["ssidPolicy"]                = ssidPolicy;
  doc["targetSsid"]                = targetSsid;
  _selectedWirelessInterface       = interfaceName;
  _ssidPolicy                      = ssidPolicy;
  _targetSsid                      = targetSsid;
  FinishTrace::BlockingOpScope writeOp(
      FinishTrace::pipelineActive()
          ? FinishTrace::storageWriteOp(
                "SD write router-provisioning.json",
                (_storage && _storage->healthy()) ? "SD card write"
                                                  : "LittleFS write")
          : FinishTrace::BlockingOpConfig{""});
  const bool ok =
      _storage->writeJson(StoragePaths::RouterProvisioningFile, doc, true);
  if (!ok && FinishTrace::pipelineActive()) writeOp.fail("write failed");
  return ok;
}

bool RouterProvisioningEngine::syncProductionRouterCredentials(String &errorOut) {
  SetupRouterConnectionManager::ResolvedRouterCredentials credentials;
  SetupRouterConnectionManager::OperationResult credResult;
  {
    FinishTrace::OpScope op("resolveRouterCredentials(Persisted)");
    if (!_routerConnection->resolveRouterCredentials(
            SetupRouterConnectionManager::RouterCredentialSource::Persisted, nullptr,
            credentials, credResult)) {
      op.fail();
      errorOut = credResult.errorMessage.isEmpty()
                     ? "Unable to load saved router credentials"
                     : credResult.errorMessage;
      return false;
    }
  }
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_SMALL);
  doc["driverType"] = "mikrotik";
  doc["host"]       = credentials.host;
  doc["username"]   = credentials.username;
  doc["password"]   = credentials.password;
  doc["profile"]    = "default";
  {
    FinishTrace::BlockingOpScope op(FinishTrace::storageWriteOp(
        "SD write ROUTER_FILE",
        (_storage && _storage->healthy()) ? "SD card write" : "LittleFS write"));
    if (!_storage->writeJson(RenzFiConfig::ROUTER_FILE, doc)) {
      op.fail("Unable to persist production router credentials");
      errorOut = "Unable to persist production router credentials";
      return false;
    }
  }
  _productionCredentialsOk = !credentials.host.isEmpty() &&
                             !credentials.username.isEmpty() &&
                             !credentials.password.isEmpty();
  Serial.printf(
      "[router-config] host=%s port=%u usernameConfigured=%s "
      "usernameLength=%u passwordConfigured=%s passwordLength=%u "
      "source=reconciled-from-setup\n",
      credentials.host.c_str(), (unsigned)RenzFiConfig::ROUTEROS_API_PORT,
      credentials.username.isEmpty() ? "no" : "yes",
      (unsigned)credentials.username.length(),
      credentials.password.isEmpty() ? "no" : "yes",
      (unsigned)credentials.password.length());
  return true;
}

bool RouterProvisioningEngine::ensureProductionRouterCredentials(
    String &errorOut) {
  errorOut = "";
  _productionCredentialsOk = false;
  if (!_storage) {
    errorOut = "Storage unavailable";
    return false;
  }

  // /config/router.json is seeded with empty username/password on first boot
  // (StorageManager::kDefaultRouter). Production telemetry/sync reads only this
  // file. Always re-validate from disk: a sticky in-memory OK flag would skip
  // reconcile after SD remount when fallback/SPIFFS served an empty copy.
  {
    DynamicJsonDocument stored(RenzFiConfig::JSON_DOC_SMALL);
    if (_storage->readJson(RenzFiConfig::ROUTER_FILE, stored)) {
      const String host     = stored["host"] | "";
      const String username = stored["username"] | "";
      const String password = stored["password"] | "";
      Serial.printf(
          "[router-config] host=%s port=%u usernameConfigured=%s "
          "usernameLength=%u passwordConfigured=%s passwordLength=%u "
          "source=router.json\n",
          host.c_str(), (unsigned)RenzFiConfig::ROUTEROS_API_PORT,
          username.isEmpty() ? "no" : "yes",
          (unsigned)username.length(),
          password.isEmpty() ? "no" : "yes",
          (unsigned)password.length());
      if (!host.isEmpty() && !username.isEmpty() && !password.isEmpty()) {
        _productionCredentialsOk = true;
        return true;
      }
    } else {
      Serial.println("[router-config] source=router.json readable=no");
    }
  }

  if (!_routerConnection) {
    errorOut = "Setup router connection unavailable";
    return false;
  }

  Serial.println(
      "[router-credentials] production router.json incomplete — reconciling "
      "from setup-verified connection");
  if (!syncProductionRouterCredentials(errorOut)) {
    Serial.printf("[router-credentials] reconcile failed: %s\n",
                  errorOut.c_str());
    return false;
  }
  Serial.println("[router-credentials] production router.json reconciled");
  return _productionCredentialsOk;
}

void RouterProvisioningEngine::completeSetupAfterFinishSuccess() {
  if (_mgmtApLifecycle) {
    _mgmtApLifecycle->completeSetupProvisioning();
  }
}

RouterProvisioningEngine::OperationResult
RouterProvisioningEngine::persistLocalState() {
  FinishTrace::OpScope fn("persistLocalState");
  OperationResult result;
  result.stage = "persist-local";

  String errorOut;
  {
    FinishTrace::OpScope op("syncProductionRouterCredentials");
    if (!syncProductionRouterCredentials(errorOut)) {
      op.fail();
      fn.fail();
      result.errorCode    = "ROUTER_CREDENTIAL_SYNC_FAILED";
      result.errorMessage = errorOut;
      FinishTrace::opReturn("persistLocalState", false);
      return result;
    }
  }

  String iface, policy, ssid;
  {
    FinishTrace::OpScope op("resolveWirelessSelection");
    const OperationResult wireless = resolveWirelessSelection(iface, policy, ssid);
    if (!wireless.success) {
      op.fail();
      fn.fail();
      FinishTrace::opReturn("persistLocalState", false);
      return wireless;
    }
  }

  {
    FinishTrace::OpScope op("persistWirelessSelection");
    if (!persistWirelessSelection(iface, policy, ssid)) {
      op.fail();
      fn.fail();
      result.errorCode    = "PERSIST_FAILED";
      result.errorMessage = "Unable to persist wireless interface selection";
      FinishTrace::opReturn("persistLocalState", false);
      return result;
    }
  }

  result.success = true;
  FinishTrace::opReturn("persistLocalState", true);
  return result;
}

RouterProvisioningEngine::OperationResult
RouterProvisioningEngine::resolveWirelessSelection(String &interfaceOut,
                                                   String &ssidPolicyOut,
                                                   String &targetSsidOut) {
  OperationResult result;
  result.stage = "resolve-wireless";

  if (!_selectedWirelessInterface.isEmpty()) {
    FinishTrace::opEvent("cached wireless interface — skip RouterOS probe");
    interfaceOut   = _selectedWirelessInterface;
    ssidPolicyOut  = _ssidPolicy.isEmpty() ? String(kSsidPolicyKeep) : _ssidPolicy;
    targetSsidOut  = _targetSsid;
    result.success = true;
    FinishTrace::opReturn("resolveWirelessSelection", true);
    return result;
  }

  SetupRouterConnectionManager::ResolvedRouterCredentials credentials;
  SetupRouterConnectionManager::OperationResult credResult;
  {
    FinishTrace::OpScope op("resolveRouterCredentials(Persisted)");
    if (!_routerConnection->resolveRouterCredentials(
            SetupRouterConnectionManager::RouterCredentialSource::Persisted, nullptr,
            credentials, credResult)) {
      op.fail();
      result.errorCode    = credResult.errorCode;
      result.errorMessage = credResult.errorMessage;
      FinishTrace::opReturn("resolveWirelessSelection", false);
      return result;
    }
  }

  RouterSession session(_eth);
  String sessionError, sessionErrorCode;
  {
    FinishTrace::BlockingOpScope op(FinishTrace::BlockingOpConfig{
        "RouterSession.open",
        "TCP response",
        RenzFiConfig::SETUP_ROUTER_CONNECT_TIMEOUT_MS +
            RenzFiConfig::SETUP_ROUTER_IO_TIMEOUT_MS,
        0,
        "TCP connect + RouterOS login",
        "opening"});
    if (!session.open(credentials.toRouterInput(), sessionError, sessionErrorCode)) {
      op.fail(sessionError.c_str());
      result.errorCode    = sessionErrorCode;
      result.errorMessage = sessionError;
      FinishTrace::opReturn("resolveWirelessSelection", false);
      return result;
    }
  }

  const String bridgeName = _routerProvisioning->guestBridgeName();
  RouterOsClient::CommandResult &wireless = RouterCommandScratchContext::acquire();
  {
    FinishTrace::BlockingOpScope op(FinishTrace::routerApiOp(
        "RouterOS API: /interface/wireless/print"));
    if (!session.client().executeCommand("/interface/wireless/print", wireless) ||
        wireless.trapReceived) {
      op.fail("Unable to read wireless interfaces");
      session.close();
      result.errorCode    = "WIRELESS_READ_FAILED";
      result.errorMessage = "Unable to read wireless interfaces";
      FinishTrace::opReturn("resolveWirelessSelection", false);
      return result;
    }
  }

  String chosen;
  String hotspotFallback;
  {
    FinishTrace::OpScope op("parse interfaces");
    for (uint8_t i = 0; i < wireless.replyCount; ++i) {
      String name, disabled;
      replyAttr(wireless, i, "name", name);
      replyAttr(wireless, i, "disabled", disabled);
      if (name.isEmpty() || disabled == "true") continue;

      String hsId, profile;
      if (!hotspotOnInterface(session.client(), name, hsId, profile)) continue;

      if (!bridgeName.isEmpty() &&
          bridgeHasInterface(session.client(), bridgeName, name, sessionError)) {
        if (chosen.isEmpty() || name < chosen) chosen = name;
        continue;
      }
      if (hotspotFallback.isEmpty() || name < hotspotFallback) {
        hotspotFallback = name;
      }
    }
  }
  if (chosen.isEmpty()) chosen = hotspotFallback;
  {
    FinishTrace::OpScope op("RouterSession.close");
    session.close();
  }

  if (chosen.isEmpty()) {
    result.errorCode    = "WIRELESS_INTERFACE_NOT_FOUND";
    result.errorMessage = "No usable wireless interface found for provisioning";
    FinishTrace::opReturn("resolveWirelessSelection", false);
    return result;
  }

  interfaceOut  = chosen;
  ssidPolicyOut = kSsidPolicyKeep;
  targetSsidOut = RouterProvisioning::defaultSettings().guestSsid;
  if (_wizardConfig && _wizardConfig->guestWifiConfigured()) {
    const auto &guest = _wizardConfig->guestWifi();
    if (!guest.ssid.isEmpty()) {
      ssidPolicyOut = kSsidPolicyRename;
      targetSsidOut = guest.ssid;
    }
  }

  result.success = true;
  FinishTrace::opReturn("resolveWirelessSelection", true);
  return result;
}

bool RouterProvisioningEngine::servePortalAsset(const String &filename,
                                                String &contentOut,
                                                String &contentTypeOut,
                                                String &errorOut) const {
  contentOut.clear();
  contentTypeOut = contentTypeForPortalFile(filename);
  errorOut       = "";

  String sourceName = filename;
  if (filename == "status.html" || filename == "logout.html") {
    sourceName = "login.html";
  }

  const String spiffsPath = spiffsPortalPath(sourceName);
  if (!readSpiffsBinary(spiffsPath, contentOut)) {
    errorOut = "Portal asset not found: " + sourceName;
    return false;
  }

  if (sourceName == "renzfi-app.js") {
    const String declaration =
        String("var RENZFI_APPLIANCE_BASE_URL = \"") + kPortalPlaceholder + "\";";
    const String replaced =
        String("var RENZFI_APPLIANCE_BASE_URL = \"") + applianceBaseUrl() + "\";";
    contentOut.replace(declaration.c_str(), replaced.c_str());
    contentOut.replace(kPortalPlaceholder, applianceBaseUrl());
  }
  return true;
}

bool RouterProvisioningEngine::acceptsPortalFetchToken(const String &token) const {
  return !token.isEmpty() && token == _portalFetchToken;
}

RouterProvisioningEngine::OperationResult
RouterProvisioningEngine::runFinishPipeline(ProgressFn progressFn,
                                            void *progressCtx) {
  FinishTrace::enterPipeline();
  OperationResult result;

  {
    FinishTrace::StageScope stage("ensurePreconditions");
    result = ensurePreconditions();
    if (!result.success) {
      stage.fail();
      FinishTrace::exitPipeline();
      return result;
    }
  }

  // ensurePreconditions() sets success=true; clear until final commit.
  result.success = false;

  if (_finishCompleted || (_installation && _installation->isReady())) {
    if (!commitFinishInstallationState(_installation)) {
      result.errorCode    = "STATE_SYNC_FAILED";
      result.errorMessage = "Unable to commit installation state during finish";
      result.httpStatus   = 500;
      result.stage        = "finalize";
      FinishTrace::exitPipeline();
      return result;
    }
    if (!verifyProvisionedPersisted(_storage) &&
        !(_installation && _installation->isReady())) {
      result.errorCode    = "STATE_SYNC_FAILED";
      result.errorMessage = "Installation state did not persist as provisioned";
      result.httpStatus   = 500;
      result.stage        = "finalize";
      FinishTrace::exitPipeline();
      return result;
    }
    logFinishLifecycleStatus(_setupProvisioning, _installation,
                             _routerProvisioning, _eth);
    result.success      = true;
    result.errorCode    = "";
    result.errorMessage = "Setup finish already completed";
    result.reason       = "";
    result.httpStatus   = 200;
    result.stage        = "complete";
    _finishCompleted    = true;
    FinishTrace::exitPipeline();
    return result;
  }

  if (_running) {
    result.errorCode    = "FINISH_BUSY";
    result.errorMessage = "Setup finish provisioning already running";
    result.httpStatus   = 409;
    FinishTrace::exitPipeline();
    return result;
  }

  _running          = true;
  _portalFetchToken = kFinishTokenPrefix + String(millis());

  SetupRouterConnectionManager::ResolvedRouterCredentials credentials;
  SetupRouterConnectionManager::OperationResult credResult;
  RouterSession session(_eth);
  String sessionError, sessionErrorCode;
  String wirelessIface = _selectedWirelessInterface;
  String ssidPolicy    = _ssidPolicy;
  String targetSsid    = _targetSsid;
  String hotspotId, profileName;

  {
    FinishTrace::StageScope stage("persistLocalState");
    reportProgress(progressFn, progressCtx, "persist-local", "Saving configuration...");
    result = persistLocalState();
    if (!result.success) {
      stage.fail();
      _running = false;
      FinishTrace::exitPipeline();
      return result;
    }
    wirelessIface = _selectedWirelessInterface;
    ssidPolicy    = _ssidPolicy;
    targetSsid    = _targetSsid;
  }
  result.success = false;

  {
    FinishTrace::StageScope stage("router-connect");
    reportProgress(progressFn, progressCtx, "router-connect", "Connecting to router...");
    if (!_routerConnection->resolveRouterCredentials(
            SetupRouterConnectionManager::RouterCredentialSource::Persisted, nullptr,
            credentials, credResult)) {
      stage.fail();
      _running = false;
      result.errorCode    = credResult.errorCode;
      result.errorMessage = credResult.errorMessage;
      result.stage        = "router-connect";
      FinishTrace::exitPipeline();
      return result;
    }
    if (!session.open(credentials.toRouterInput(), sessionError, sessionErrorCode)) {
      stage.fail();
      _running = false;
      result.errorCode    = sessionErrorCode;
      result.errorMessage = sessionError;
      result.stage        = "router-connect";
      FinishTrace::exitPipeline();
      return result;
    }
  }

  if (ssidPolicy == kSsidPolicyRename && !targetSsid.isEmpty()) {
    FinishTrace::StageScope stage("wireless-ssid");
    reportProgress(progressFn, progressCtx, "wireless-ssid",
                   "Updating guest SSID...");
    RouterOsClient::CommandResult &wireless = RouterCommandScratchContext::acquire();
    RouterOsClient::initializeCommandResult(wireless);
    if (!session.client().executeCommand("/interface/wireless/print", wireless) ||
        wireless.trapReceived) {
      stage.fail();
      session.close();
      _running = false;
      result.errorCode    = "WIRELESS_READ_FAILED";
      result.errorMessage = "Unable to read wireless interfaces";
      result.stage        = "wireless-ssid";
      FinishTrace::exitPipeline();
      return result;
    }
    const int idx = findReplyByAttr(wireless, "name", wirelessIface);
    if (idx < 0) {
      stage.fail();
      session.close();
      _running = false;
      result.errorCode    = "WIRELESS_INTERFACE_NOT_FOUND";
      result.errorMessage = "Selected wireless interface not found";
      result.stage        = "wireless-ssid";
      FinishTrace::exitPipeline();
      return result;
    }
    String id, currentSsid;
    replyAttr(wireless, static_cast<uint8_t>(idx), ".id", id);
    replyAttr(wireless, static_cast<uint8_t>(idx), "ssid", currentSsid);
    if (currentSsid != targetSsid) {
      const String setAttrs[] = {"=.id=" + id, "=ssid=" + targetSsid};
      RouterOsClient::initializeCommandResult(wireless);
      if (!session.client().executeCommand("/interface/wireless/set", setAttrs, 2,
                                           wireless) ||
          wireless.trapReceived) {
        stage.fail();
        session.close();
        _running = false;
        result.errorCode    = "WIRELESS_SSID_UPDATE_FAILED";
        result.errorMessage = wireless.trapMessage.isEmpty()
                                  ? "Unable to rename wireless SSID"
                                  : wireless.trapMessage;
        result.stage        = "wireless-ssid";
        FinishTrace::exitPipeline();
        return result;
      }
    }
  }

  {
    FinishTrace::StageScope stage("hotspot-verify");
    reportProgress(progressFn, progressCtx, "hotspot-verify", "Verifying hotspot...");
    const String bridgeName = _routerProvisioning->guestBridgeName();
    if (!verifyFinishHotspot(session.client(), wirelessIface, bridgeName,
                               hotspotId, profileName, sessionError)) {
      stage.fail();
      session.close();
      _running = false;
      result.errorCode    = "HOTSPOT_NOT_FOUND";
      result.errorMessage = sessionError.isEmpty()
                                ? "Hotspot must already exist on the selected "
                                  "wireless interface"
                                : sessionError;
      result.stage = "hotspot-verify";
      FinishTrace::exitPipeline();
      return result;
    }
  }

  {
    FinishTrace::StageScope stage("portal-verify");
    reportProgress(progressFn, progressCtx, "portal-verify",
                   "Verifying captive portal files...");
    const auto mode = _portalDeploymentMode;
    Serial.printf("[finish-stage] portal mode=%s\n",
                  portalDeploymentModeLabel(mode));
    const PortalVerifyReport portal =
        verifyMikroTikCaptivePortal(session.client(), profileName, mode);
    result.portalDeploymentMode = portalDeploymentModeLabel(mode);
    result.portalStatus         = portal.status;
    result.portalBlocking       = portal.blocking;
    Serial.printf("[finish-stage] portal verification=%s\n",
                  (strcmp(portal.status, "skipped") == 0)
                      ? "SKIPPED"
                      : ((strcmp(portal.status, "verified") == 0)
                             ? "VERIFIED"
                             : "UNVERIFIED"));
    Serial.printf("[finish-stage] portal gate blocking=%s\n",
                  portal.blocking ? "true" : "false");
    if (!portal.success && portal.blocking) {
      stage.fail();
      session.close();
      _running = false;
      result.errorCode    = "PORTAL_FILES_MISSING";
      result.errorMessage =
          portal.detail.isEmpty()
              ? "Captive portal verification failed"
              : portal.detail;
      result.stage = "portal-verify";
      FinishTrace::exitPipeline();
      return result;
    }
    // Non-blocking MANUAL_EXTERNAL / SKIPPED: continue even when unverified.
    Serial.println(F("[finish-stage] portal gate blocking=false"));
    Serial.println(F("[finish-stage] required gates PASS"));
  }

  {
    FinishTrace::StageScope stage("hotspot-profile");
    reportProgress(progressFn, progressCtx, "hotspot-profile",
                   "Configuring hotspot profile...");
    const bool mutateHtmlDir = true;  // idempotent: write only when != hotspot
    if (!ensureHotspotProfileHtmlDirectory(session.client(), profileName,
                                           sessionError, mutateHtmlDir)) {
      stage.fail();
      session.close();
      _running = false;
      result.errorCode    = "HOTSPOT_PROFILE_FAILED";
      result.errorMessage = sessionError;
      result.stage        = "hotspot-profile";
      FinishTrace::exitPipeline();
      return result;
    }
  }

  {
    FinishTrace::StageScope stage("walled-garden");
    reportProgress(progressFn, progressCtx, "walled-garden",
                   "Applying walled-garden rules...");
    const String espIp = _eth->ip();
    if (!ensureWalledGardenRule(session.client(), espIp, sessionError)) {
      stage.fail();
      session.close();
      _running = false;
      result.errorCode    = "WALLED_GARDEN_FAILED";
      result.errorMessage = sessionError;
      result.stage        = "walled-garden";
      FinishTrace::exitPipeline();
      return result;
    }
  }

  {
    FinishTrace::StageScope stage("scripts");
    reportProgress(progressFn, progressCtx, "scripts", "Verifying RouterOS scripts...");
    // OPTIONAL / NON-BLOCKING: "renzfi-hotspot-ready" is only a log marker.
    // It is not required for Hotspot, coin, captive portal, or internet grant.
    // Under MikroTik CPU pressure (or a transient /system/script/print failure),
    // skip and continue — same pattern as non-blocking portal-verify.
    bool scriptsOk = false;
    if (RouterApiTransportGate::cpuUnderPressure()) {
      Serial.println(F("[finish-stage] OPTIONAL stage scripts unavailable"));
      Serial.println(F("[finish-stage] scripts skipped reason=router_cpu_high"));
      Serial.println(F("[finish-stage] scripts blocking=false"));
      Serial.println(F("[finish-stage] continuing finish"));
    } else if (!ensureManagedScript(session.client(), "renzfi-hotspot-ready",
                                    ":log info \"RENZFI: hotspot provisioning marker\"",
                                    sessionError)) {
      Serial.printf("[finish-stage] OPTIONAL stage scripts unavailable detail=%s\n",
                    sessionError.c_str());
      Serial.println(
          F("[finish-stage] scripts skipped reason=router_read_failed"));
      Serial.println(F("[finish-stage] scripts blocking=false"));
      Serial.println(F("[finish-stage] continuing finish"));
    } else {
      scriptsOk = true;
      Serial.println(F("[finish-stage] scripts result=PASS"));
      Serial.println(F("[finish-stage] scripts blocking=false"));
    }
    (void)scriptsOk;
  }

  {
    FinishTrace::StageScope stage("api-verify");
    reportProgress(progressFn, progressCtx, "api-verify", "Verifying router API...");
    RouterOsClient::CommandResult &identity = RouterCommandScratchContext::acquire();
    if (!session.client().executeCommand("/system/identity/print", identity) ||
        identity.trapReceived) {
      stage.fail();
      session.close();
      _running = false;
      result.errorCode = "API_VERIFY_FAILED";
      result.errorMessage =
          identity.trapMessage.isEmpty()
              ? session.client().lastError().isEmpty()
                    ? "RouterOS identity check failed"
                    : session.client().lastError()
              : identity.trapMessage;
      result.stage = "api-verify";
      FinishTrace::exitPipeline();
      return result;
    }
  }

  DynamicJsonDocument activateResult(RenzFiConfig::JSON_DOC_SMALL);
  const bool finishSessionReused = session.client().isConnected() &&
                                   session.client().isLoggedIn();
  bool finishSessionReconnected = false;
  {
    FinishTrace::StageScope stage("production-network");
    reportProgress(progressFn, progressCtx, "production-network",
                   "Activating production network...");
    ProductionNetworkTrace::enter();
    if (!finishSessionReused) {
      if (!session.ensureAuthenticated(credentials.toRouterInput(), sessionError,
                                       sessionErrorCode,
                                       finishSessionReconnected)) {
        ProductionNetworkTrace::logStageFailureStatement(
            "RouterProvisioningEngine::runFinishPipeline session.ensureAuthenticated "
            "failed before production-network");
        ProductionNetworkTrace::logReturnFalse(
            "RouterProvisioningEngine::runFinishPipeline production-network stage",
            "api-failure", sessionErrorCode.c_str(), sessionError.c_str(), nullptr);
        ProductionNetworkTrace::logActivationSummary(false, finishSessionReconnected,
                                                       false, false);
        ProductionNetworkTrace::exit();
        stage.fail();
        _running = false;
        result.errorCode    = sessionErrorCode;
        result.errorMessage = sessionError;
        result.stage        = "production-network";
        FinishTrace::exitPipeline();
        return result;
      }
    }
    const bool activated = _router->activateProductionNetworkForFinish(
        activateResult, session.client(), finishSessionReused,
        finishSessionReconnected);
    if (!activated) {
      ProductionNetworkTrace::logStageFailureStatement(
          "RouterProvisioningEngine::runFinishPipeline "
          "activateProductionNetworkForFinish returned false");
      ProductionNetworkTrace::logReturnFalse(
          "RouterProvisioningEngine::runFinishPipeline production-network stage",
          activateResult["reason"] | "api-failure",
          "PRODUCTION_NETWORK_ACTIVATION_FAILED",
          activateResult["error"] | "Unable to activate production network",
          nullptr);
      ProductionNetworkTrace::exit();
      stage.fail();
      session.close();
      _running = false;
      result.errorCode    = "PRODUCTION_NETWORK_ACTIVATION_FAILED";
      result.errorMessage =
          activateResult["error"] | "Unable to activate production network";
      result.reason       = activateResult["reason"] | "api-failure";
      result.stage        = "production-network";
      FinishTrace::exitPipeline();
      return result;
    }
    ProductionNetworkTrace::exit();
  }

  session.close();

  DynamicJsonDocument verifyActiveResult(RenzFiConfig::JSON_DOC_SMALL);
  {
    FinishTrace::StageScope stage("production-verify");
    reportProgress(progressFn, progressCtx, "production-verify",
                   "Verifying production network...");
    verifyActiveResult.set(activateResult.as<JsonObject>());
    if (!verifyActiveResult["ok"]) {
      stage.fail();
      _running = false;
      result.errorCode    = "PRODUCTION_NETWORK_VERIFY_FAILED";
      result.errorMessage = verifyActiveResult["error"] |
                            "Production network verification failed";
      result.reason       = verifyActiveResult["reason"] | "routeros-error";
      result.stage        = "production-verify";
      FinishTrace::exitPipeline();
      return result;
    }
  }

  // Router-cache is a local dashboard/metadata cache — not required for
  // customer Hotspot Internet. Production activation already succeeded above.
  // A cache write failure must NOT convert a successful production Finish into
  // HTTP 400 / InstallationState stuck (regression vs intended finish contract).
  {
    FinishTrace::StageScope cacheStage("router-cache");
    reportProgress(progressFn, progressCtx, "router-cache",
                   "Persisting router cache...");
    if (!_router || !_router->persistFinishRouterCache(
                        verifyActiveResult.as<JsonObjectConst>())) {
      cacheStage.fail();
      Serial.println(
          "[finish] WARNING router-cache persist failed after production "
          "activation — continuing (cache is non-blocking for Finish)");
      // Non-fatal: do not return. commitFinishInstallationState still runs.
    }
  }

  reportProgress(progressFn, progressCtx, "finalize", "Finalizing installation...");
  if (_routerProvisioning) {
    DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_SMALL);
    if (_storage->readJson(StoragePaths::RouterProvisioningFile, doc)) {
      doc["hotspotActivated"]          = true;
      doc["clientInterfaceAttached"]   = true;
      doc["selectedWirelessInterface"] = wirelessIface;
      doc["updatedAt"]                 = millis();
      _storage->writeJson(StoragePaths::RouterProvisioningFile, doc, true);
      _routerProvisioning->load();
    }
  }

  if (!commitFinishInstallationState(_installation)) {
    _running = false;
    result.errorCode    = "STATE_SYNC_FAILED";
    result.errorMessage = "Unable to mark installation as provisioned";
    result.stage        = "finalize";
    FinishTrace::exitPipeline();
    return result;
  }
  if (!verifyProvisionedPersisted(_storage)) {
    _running = false;
    result.errorCode    = "STATE_SYNC_FAILED";
    result.errorMessage = "Installation state did not persist as provisioned";
    result.stage        = "finalize";
    FinishTrace::exitPipeline();
    return result;
  }
  logFinishLifecycleStatus(_setupProvisioning, _installation, _routerProvisioning,
                           _eth);

  _finishCompleted       = true;
    if (_setupProvisioning) _setupProvisioning->closeUnlockedSetup();
  _running               = false;
  result.success         = true;
  result.errorCode       = "";
  result.errorMessage    = "Router provisioning complete";
  result.reason          = "";
  result.httpStatus      = 200;
  result.stage           = "complete";
  result.rebootScheduled = true;
  FinishTrace::exitPipeline();
  return result;
}
