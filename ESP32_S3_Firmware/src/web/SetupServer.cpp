#include "SetupServer.h"
#include "SetupWizardPageHtml.h"
#include "RouterProvisioningEngine.h"
#include "RouterProvisioningWorker.h"

#include <SPIFFS.h>

#include <cstring>

#include "AuthManager.h"
#include "AuthRole.h"
#include "Config.h"
#include "DeviceIdentity.h"
#include "EthernetManager.h"
#include "FinishTrace.h"
#include "HttpPlaneGate.h"
#include "JsonHeap.h"
#include "ManagementApConfig.h"
#include "NetworkSettingsManager.h"
#include "RenzFiDebug.h"
#include "SetupProvisioningManager.h"
#include "SetupRouterConnectionManager.h"
#include "SetupStatusContext.h"
#include "SetupWizardConfigManager.h"
#include "RouterProvisioningManager.h"
#include "InstallationState.h"
#include "InstallationStateManager.h"
#include "StorageManager.h"
#include "StoragePaths.h"
#include "WebRequestDiagnostics.h"
#include "WebResponse.h"
#include "WebStackDiagnostics.h"
#include "WebServerManager.h"
#include "ExistingNetworkScanCache.h"
#include "WifiDiscoveryCache.h"
#include "DmaMemoryMonitor.h"

namespace {

void bodyCollect(AsyncWebServerRequest *req, uint8_t *data, size_t len,
                 size_t index, size_t total) {
  if (total > 4096) return;
  if (index == 0) {
    if (req->_tempObject) free(req->_tempObject);
    req->_tempObject = malloc(total + 1);
  }
  if (req->_tempObject) {
    memcpy(static_cast<uint8_t *>(req->_tempObject) + index, data, len);
    if (index + len == total) {
      static_cast<char *>(req->_tempObject)[total] = '\0';
    }
  }
}

// Router test/save: owned body slot â€” never uses req->_tempObject (freed by
// AsyncWebServerRequest destructor and prone to post-send use-after-free).
class SetupRouterOwnedBodyStore {
 public:
  static constexpr size_t kMaxBodyBytes = 4096;

  static void collect(AsyncWebServerRequest *req, uint8_t *data, size_t len,
                      size_t index, size_t total) {
    if (!req || total == 0 || total > kMaxBodyBytes) return;
    if (index == 0) {
      resetSlot();
      _slot.reqKey = req;
      _slot.data   = static_cast<char *>(malloc(total + 1));
      if (!_slot.data) {
        _slot.reqKey = nullptr;
        return;
      }
      _slot.capacity = total + 1;
      _slot.received = 0;
      _slot.complete = false;
    }
    if (_slot.reqKey != req || !_slot.data) return;
    if (index + len > total || index + len > _slot.capacity - 1) return;
    memcpy(_slot.data + index, data, len);
    _slot.received = index + len;
    if (_slot.received == total) {
      _slot.data[total] = '\0';
      _slot.complete    = true;
    }
  }

  static char *take(AsyncWebServerRequest *req) {
    if (!req || _slot.reqKey != req || !_slot.complete || !_slot.data) {
      return nullptr;
    }
    char *owned         = _slot.data;
    _slot.data          = nullptr;
    _slot.reqKey        = nullptr;
    _slot.capacity      = 0;
    _slot.received      = 0;
    _slot.complete      = false;
    return owned;
  }

 private:
  struct Slot {
    AsyncWebServerRequest *reqKey = nullptr;
    char                  *data   = nullptr;
    size_t                 capacity = 0;
    size_t                 received = 0;
    bool                   complete = false;
  };

  static Slot _slot;

  static void resetSlot() {
    if (_slot.data) {
      free(_slot.data);
    }
    _slot = Slot{};
  }
};

SetupRouterOwnedBodyStore::Slot SetupRouterOwnedBodyStore::_slot;

bool ensureOwnerCreated(InstallationStateManager *installation,
                        AsyncWebServerRequest *req);

bool ensureSetupOwnerOnly(AuthManager *auth, AsyncWebServerRequest *req);

void fillSetupStatusData(SetupProvisioningManager *provisioning,
                         EthernetManager *eth, JsonObject data,
                         SetupWizardConfigManager *wizardConfig,
                         NetworkSettingsManager *networkSettings,
                         RouterProvisioningManager *routerProvisioning,
                         RouterProvisioningWorker *routerWorker);

bool ensureSetupWizardEnabled(InstallationStateManager *installation,
                              SetupProvisioningManager *provisioning,
                              AsyncWebServerRequest *req);

void routerConnectionBodyCollect(AsyncWebServerRequest *req, uint8_t *data,
                                 size_t len, size_t index, size_t total) {
  SetupRouterOwnedBodyStore::collect(req, data, len, index, total);
}

struct OwnedBodyGuard {
  char *body = nullptr;
  explicit OwnedBodyGuard(char *owned) : body(owned) {}
  ~OwnedBodyGuard() {
    if (body) free(body);
    body = nullptr;
  }
  OwnedBodyGuard(const OwnedBodyGuard &) = delete;
  OwnedBodyGuard &operator=(const OwnedBodyGuard &) = delete;
};

String buildSetupErrorJsonBody(const String &error, const String &code) {
  DynamicJsonDocument doc(256);
  doc["success"] = false;
  doc["error"]   = error;
  doc["code"]    = code;
  String out;
  serializeJson(doc, out);
  return out;
}

// Task 2 (Single Router Worker Rule): the exact busy schema requested when a
// discovery is already in flight and no cached result exists yet to fall
// back on (normally the cache absorbs this case entirely).
String buildWifiDiscoveryBusyBody() {
  DynamicJsonDocument doc(128);
  doc["status"]    = "busy";
  doc["operation"] = "wifi-discovery";
  String out;
  serializeJson(doc, out);
  return out;
}

// Async Job Queue (Phase A): the immediate 202 response body for a
// newly-enqueued router-worker job. The frontend's postRouter()/
// startExistingNetworkScan() already know to look for data.jobId and start
// polling — see pollRouterJob()/pollExistingNetworkJob() in
// SetupWizardPageHtml.h.
String buildJobQueuedBody(uint32_t jobId) {
  DynamicJsonDocument doc(160);
  doc["success"] = true;
  JsonObject data = doc.createNestedObject("data");
  data["jobId"]  = jobId;
  data["state"]  = "queued";
  String out;
  serializeJson(doc, out);
  return out;
}

// Shared poll-response body for both /api/setup/router/jobs/* and
// /api/setup/router/existing-network/jobs/*. `result` is spliced in raw
// (via ArduinoJson's serialized()) so the original endpoint's own JSON body
// is preserved byte-for-byte instead of being re-parsed/re-encoded — this
// is what pollRouterJob()/pollExistingNetworkJob() unwrap as `job.result`.
String buildJobPollBody(const RouterProvisioningWorker::JobRecord &job,
                        bool includeScanProgressAliases) {
  DynamicJsonDocument doc(320);
  doc["success"] = true;
  JsonObject data = doc.createNestedObject("data");
  data["jobId"]  = job.jobId;
  data["state"]  = RouterProvisioningWorker::jobStateLabel(job.state);
  const bool terminal =
      job.state == RouterProvisioningWorker::JobState::Completed ||
      job.state == RouterProvisioningWorker::JobState::Failed;
  if (terminal) {
    data["httpStatus"] = job.result.httpStatus;
    if (!job.result.body.isEmpty()) {
      data["result"] = serialized(job.result.body);
    }
  }
  if (!job.stageId.isEmpty()) {
    data["stage"] = job.stageId;
    if (includeScanProgressAliases) data["progressStage"] = job.stageId;
  }
  if (!job.stageLabel.isEmpty()) {
    data["stageLabel"] = job.stageLabel;
    if (includeScanProgressAliases) data["progressLabel"] = job.stageLabel;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

// Common poll-endpoint body for a GET .../jobs/<id> wildcard route: parses
// the numeric jobId suffix, looks it up via pollJob(), and serves either the
// job snapshot or 404 JOB_NOT_FOUND (the frontend disambiguates "never
// existed" from "device rebooted" via bootInstanceId — see
// confirmExistingScanRestart() in SetupWizardPageHtml.h).
void serveJobPoll(AsyncWebServerRequest *req, RouterProvisioningWorker *routerWorker,
                  const char *urlMarker, bool includeScanProgressAliases) {
  if (!routerWorker) {
    WebResponse::serveErrorJson(req, 503, "Router worker unavailable", "INTERNAL_ERROR");
    return;
  }
  String path = req->url();
  const int marker = path.indexOf(urlMarker);
  if (marker < 0) {
    WebResponse::serveErrorJson(req, 404, "Job not found", "JOB_NOT_FOUND");
    return;
  }
  String idStr = path.substring(marker + strlen(urlMarker));
  const int query = idStr.indexOf('?');
  if (query >= 0) idStr = idStr.substring(0, query);
  idStr.trim();
  const uint32_t jobId = static_cast<uint32_t>(idStr.toInt());

  RouterProvisioningWorker::JobRecord job;
  if (jobId == 0 || !routerWorker->pollJob(jobId, job)) {
    WebResponse::serveErrorJson(req, 404, "Job not found or device restarted",
                                "JOB_NOT_FOUND");
    return;
  }
  WebResponse::serveJson(req, 200, buildJobPollBody(job, includeScanProgressAliases),
                         CachePolicy::NoCache);
}

bool parseRouterInputJson(const char *jsonBody,
                            SetupRouterConnectionManager::RouterInput &out) {
  if (!jsonBody) return false;
  HeapJsonDocument heapDoc(RenzFiConfig::JSON_DOC_SMALL);
  DynamicJsonDocument &doc = heapDoc.doc();
  if (deserializeJson(doc, jsonBody)) return false;

  JsonObjectConst body = doc.as<JsonObjectConst>();
  out.host             = body["host"] | "";
  out.username         = body["username"] | "";
  out.password         = body["password"] | "";
  out.connectionId     = body["connectionId"] | "";
  out.apiPort          = body["apiPort"] | 8728;
  out.host.trim();
  out.username.trim();
  out.connectionId.trim();
  return !out.host.isEmpty() && !out.username.isEmpty() && out.apiPort > 0;
}

enum class RouterPostAction { Test, Save };

String portalContentTypeForFile(const String &filename) {
  if (filename.endsWith(".html")) return "text/html";
  if (filename.endsWith(".js")) return "application/javascript";
  if (filename.endsWith(".css")) return "text/css";
  if (filename.endsWith(".png")) return "image/png";
  if (filename.endsWith(".ico")) return "image/x-icon";
  if (filename.endsWith(".mp3")) return "audio/mpeg";
  return "application/octet-stream";
}

void sendRouterPostResponse(AsyncWebServerRequest *req,
                            WebRequestDiagnostics::RequestTimer &timer,
                            int httpStatus, const String &responseBody,
                            const char *logTag) {
  timer.finish();
  WebResponse::serveJson(req, httpStatus, responseBody, CachePolicy::NoCache);
  Serial.printf("[%s] response sent\n", logTag);
  Serial.printf("[%s] handler complete\n", logTag);
}

void fillNetworkModeResponse(JsonObject data,
                             RouterProvisioningManager *routerProvisioning) {
  if (routerProvisioning) {
    routerProvisioning->fillNetworkModeStatus(data);
  }
}

void handleRouterConnectionPost(AsyncWebServerRequest *req,
                                InstallationStateManager *installation,
                                SetupProvisioningManager *provisioning,
                                SetupRouterConnectionManager *routerConnection,
                                RouterProvisioningWorker *routerWorker,
                                RouterPostAction action) {
  const char *logTag =
      action == RouterPostAction::Test ? "router-test" : "router-save";
  const char *handlerLabel = action == RouterPostAction::Test
                                 ? "SetupServer/api/setup/router/test"
                                 : "SetupServer/api/setup/router/save";

  if (!HttpPlaneGate::ensureSetupPlane(req)) return;
  if (!ensureSetupWizardEnabled(installation, provisioning, req)) return;
  if (!ensureOwnerCreated(installation, req)) return;

  WebRequestDiagnostics::RequestTimer timer(req, handlerLabel);

  char *ownedBody = SetupRouterOwnedBodyStore::take(req);
  if (!ownedBody || !routerConnection) {
    sendRouterPostResponse(req, timer, 400,
                           buildSetupErrorJsonBody("Request body required",
                                                   "INVALID_JSON"),
                           logTag);
    return;
  }
  OwnedBodyGuard bodyGuard(ownedBody);

  Serial.printf("[%s] body parsed\n", logTag);

  SetupRouterConnectionManager::RouterInput input;
  if (!parseRouterInputJson(bodyGuard.body, input)) {
    sendRouterPostResponse(req, timer, 400,
                           buildSetupErrorJsonBody("Invalid JSON body",
                                                   "INVALID_JSON"),
                           logTag);
    return;
  }

  Serial.printf("[%s] credentials resolved\n", logTag);

  if (!routerWorker) {
    sendRouterPostResponse(req, timer, 503,
                           buildSetupErrorJsonBody("Router worker unavailable",
                                                   "INTERNAL_ERROR"),
                           logTag);
    return;
  }

  const auto outcome = action == RouterPostAction::Test
                           ? routerWorker->enqueueTest(input)
                           : routerWorker->enqueueSave(input);
  if (!outcome.accepted) {
    Serial.printf("[%s] worker busy, rejecting\n", logTag);
    sendRouterPostResponse(req, timer, 503,
                           buildSetupErrorJsonBody("Router worker is busy",
                                                   "ROUTER_WORKER_BUSY"),
                           logTag);
    return;
  }

  Serial.printf("[%s] queued jobId=%u\n", logTag,
                static_cast<unsigned>(outcome.jobId));
  sendRouterPostResponse(req, timer, 202, buildJobQueuedBody(outcome.jobId), logTag);
}

void serveSetupPage(InstallationStateManager *installation,
                    SetupProvisioningManager *provisioning,
                    AsyncWebServerRequest *req) {
#if RENZFI_DEBUG_HTTP
  Serial.println("[setup] GET /admin/setup served via AP");
#endif
  if (provisioning) {
    provisioning->enforceActiveUnlockSession();
  }
  const bool locked =
      provisioning && provisioning->requiresSetupUnlock() &&
      !provisioning->hasActiveSetupUnlockSession();
  if (locked) {
    static const char kSetupLockedHtml[] PROGMEM =
        "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Renz-Fi Setup</title><style>"
        "body{font-family:system-ui,sans-serif;background:#0f172a;color:#e2e8f0;margin:0;"
        "min-height:100vh;display:flex;align-items:center;justify-content:center;padding:16px}"
        "main{width:min(460px,100%);background:#1e293b;border-radius:12px;padding:20px}"
        "h1{margin:0 0 10px;font-size:1.25rem}p{color:#cbd5e1;line-height:1.45}"
        "input{width:100%;margin-top:10px;background:#0f172a;color:#e2e8f0;border:1px solid #475569;"
        "border-radius:8px;padding:10px 12px}"
        "button{margin-top:12px;width:100%;background:#2563eb;color:white;border:none;border-radius:8px;"
        "padding:11px 14px;font-weight:600}"
        ".err{display:none;margin-top:10px;background:#450a0a;color:#fecaca;padding:9px 11px;border-radius:8px;font-size:.9rem}"
        "</style></head><body><main><h1>Setup Locked</h1>"
        "<p>An owner account has already been created on this device.</p>"
        "<p>Enter the Setup Unlock Password to continue setup. "
        "Setup stays unlocked for about 20 minutes, then locks again.</p>"
        "<label for=\"pwd\" style=\"display:block;margin-top:12px;font-size:.9rem\">Setup Unlock Password</label>"
        "<input id=\"pwd\" type=\"password\" placeholder=\"Setup Unlock Password\" autocomplete=\"current-password\">"
        "<div id=\"err\" class=\"err\"></div>"
        "<button id=\"unlockBtn\" type=\"button\">Unlock Setup</button>"
        "<script>"
        "document.getElementById('unlockBtn').onclick=function(){"
        "var p=document.getElementById('pwd').value||'';"
        "var e=document.getElementById('err');e.style.display='none';e.textContent='';"
        "fetch('/api/setup/unlock',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({password:p})})"
        ".then(function(r){return r.json().then(function(j){return{ok:r.ok,json:j};});})"
        ".then(function(res){if(res.ok&&res.json&&res.json.success){window.location.replace('/admin/setup');return;}"
        "e.textContent=(res.json&&res.json.error)||'Unlock failed';e.style.display='block';})"
        ".catch(function(){e.textContent='Network error';e.style.display='block';});};"
        "</script></main></body></html>";
    req->send(200, "text/html; charset=utf-8", kSetupLockedHtml);
    return;
  }
  // Stream PROGMEM HTML (const char*) — do not wrap in FPSTR/String (RAM + SoftAP DMA).
  req->send(200, "text/html; charset=utf-8", kSetupWizardPageHtml);
}

bool ensureSetupWizardEnabled(InstallationStateManager *installation,
                              SetupProvisioningManager *provisioning,
                              AsyncWebServerRequest *req) {
  if (!installation) return true;
  if (provisioning && !provisioning->enforceActiveUnlockSession()) {
    WebResponse::serveErrorJson(req, 403, "Setup unlock session expired",
                                "SETUP_UNLOCK_REQUIRED");
    return false;
  }
  if (installation->needsSetup()) return true;
  if (provisioning && provisioning->requiresSetupUnlock() &&
      !provisioning->hasActiveSetupUnlockSession()) {
    WebResponse::serveErrorJson(req, 403, "Setup unlock password required",
                                "SETUP_UNLOCK_REQUIRED");
    return false;
  }
  return true;
}

void fillSetupStatusData(SetupProvisioningManager *provisioning,
                         EthernetManager *eth, JsonObject data,
                         SetupWizardConfigManager *wizardConfig,
                         NetworkSettingsManager *networkSettings,
                         RouterProvisioningManager *routerProvisioning,
                         RouterProvisioningWorker *routerWorker) {
  const String deviceId =
      DeviceIdentity::formatDeviceId(DeviceIdentity::stableChipMacAddress());
  data["deviceId"]        = deviceId;
  data["firmwareVersion"] = RenzFiConfig::FIRMWARE_VERSION;
  data["bootInstanceId"]  = DeviceIdentity::bootInstanceId();
  if (provisioning) {
    provisioning->fillSetupStatus(
        data, eth,
        buildSetupStatusContext(routerProvisioning, routerWorker));
  }
  if (wizardConfig) {
    wizardConfig->fillSafeStatus(data);
  }
  if (networkSettings) {
    const NetworkSettings net = networkSettings->settings();
    JsonObject network = data["network"].to<JsonObject>();
    network["addressMode"]   = ethernetAddressModeLabel(net.addressMode);
    network["provisioned"]   = net.provisioned;
    network["staticIp"]      = net.staticIp;
    network["staticGateway"] = net.staticGateway;
    network["staticSubnetMask"] = net.staticSubnetMask;
    network["staticDnsPrimary"]   = net.staticDnsPrimary;
    network["staticDnsSecondary"] = net.staticDnsSecondary;
  }
  if (routerProvisioning) {
    routerProvisioning->fillNetworkModeStatus(data);
  }
  JsonObject coin = data["coinHardware"].to<JsonObject>();
  coin["pulseGpio"] = RenzFiConfig::PIN_COIN;
}

bool isValidNetworkIp(const String &ip) {
  IPAddress addr;
  if (!addr.fromString(ip)) return false;
  return addr != IPAddress(0, 0, 0, 0);
}

const char *connectionStateLabel(bool linkUp, bool routerDetected) {
  if (!linkUp) return "not_connected";
  if (routerDetected) return "router_detected";
  return "waiting_for_ip";
}

void fillRouterStatusPayload(EthernetManager *eth,
                             InstallationStateManager *installation,
                             JsonObject data) {
  const bool linkUp    = eth && eth->linkUp();
  const bool dhcpReady = eth && eth->hasIp() && isValidNetworkIp(eth->ip());
  const String espIp   = dhcpReady ? eth->ip() : String("");
  const String gatewayIp =
      (linkUp && eth) ? eth->gateway() : String("");
  const String dnsIp = (linkUp && eth) ? eth->dns() : String("");
  const bool validGateway = isValidNetworkIp(gatewayIp);
  const bool routerDetected = linkUp && dhcpReady && validGateway;

  const InstallationState state =
      installation ? installation->current() : InstallationState::Factory;

  data["installationState"] = installationStateLabel(state);
  data["ethernetLink"]      = linkUp;
  data["dhcpReady"]         = dhcpReady;
  data["espIp"]             = espIp;
  data["gatewayIp"]         = validGateway ? gatewayIp : String("");
  data["dnsIp"]             = isValidNetworkIp(dnsIp) ? dnsIp : String("");
  data["routerDetected"]    = routerDetected;
  data["connectionState"] =
      connectionStateLabel(linkUp, routerDetected);
  data["nextStep"] =
      routerDetected ? "router_configuration" : "router_check";

  Serial.printf(
      "[setup] router check: link=%s dhcp=%s ip=%s gateway=%s\n",
      linkUp ? "up" : "down",
      dhcpReady ? "ready" : "waiting",
      dhcpReady ? espIp.c_str() : "none",
      validGateway ? gatewayIp.c_str() : "none");
}

bool ensureOwnerCreated(InstallationStateManager *installation,
                        AsyncWebServerRequest *req) {
  if (installation &&
      installationStateAtLeast(installation->current(),
                               InstallationState::OwnerCreated)) {
    return true;
  }
  WebResponse::serveErrorJson(req, 403,
                              "Owner account required before router setup",
                              "SETUP_OWNER_REQUIRED");
  return false;
}

bool ensureSetupOwnerOnly(AuthManager *auth, AsyncWebServerRequest *req) {
  if (!auth) return true;
  String cookie;
  if (req->hasHeader("Cookie")) {
    cookie = req->header("Cookie").c_str();
  }
  if (cookie.isEmpty()) return true;
  if (auth->sessionRole(cookie) == AuthRole::Operator) {
    WebResponse::serveErrorJson(req, 403, "Owner access required for this action",
                                "OWNER_REQUIRED");
    return false;
  }
  return true;
}

}  // namespace

void SetupServer::servePage(AsyncWebServerRequest *req) {
  serveSetupPage(_installation, _provisioning, req);
}

bool ensureRouterConfigured(InstallationStateManager *installation,
                            AsyncWebServerRequest *req) {
  if (installation &&
      installation->current() == InstallationState::RouterConfigured) {
    return true;
  }
  WebResponse::serveErrorJson(req, 403,
                              "Router connection must be saved before this step",
                              "ROUTER_CONFIGURE_REQUIRED");
  return false;
}

void SetupServer::begin(EthernetManager *eth,
                        InstallationStateManager *installation,
                        SetupProvisioningManager *provisioning,
                        StorageManager *storage, AuthManager *auth,
                        SetupRouterConnectionManager *routerConnection,
                        RouterProvisioningManager *routerProvisioning,
                        RouterProvisioningWorker *routerWorker,
                        SetupWizardConfigManager *wizardConfig,
                        NetworkSettingsManager *networkSettings,
                        RouterProvisioningEngine *finishEngine) {
  _eth              = eth;
  _installation     = installation;
  _provisioning     = provisioning;
  _storage          = storage;
  _auth             = auth;
  _routerConnection = routerConnection;
  _routerProvisioning = routerProvisioning;
  _routerWorker     = routerWorker;
  _wizardConfig     = wizardConfig;
  _networkSettings  = networkSettings;
  _finishEngine     = finishEngine;
}

void SetupServer::registerRoutes(WebServerManager &web) {
  AsyncWebServer &server = web.routeServer();

  server.on("/healthz", HTTP_GET, [](AsyncWebServerRequest *req) {
    WebRequestDiagnostics::RequestTimer timer(req, "SetupServer/healthz");
    if (!HttpPlaneGate::ensureAppliancePlane(req)) return;
    String json = String("{\"ok\":true,\"plane\":\"") +
                  HttpPlaneGate::planeLabel(req) +
                  "\",\"uptimeMs\":" + String(millis()) + "}";
    WebResponse::serveJson(req, 200, json);
  });

  // GET /, /login, /dashboard, /admin are plane-aware and registered once by
  // WebServerManager::registerAdminEntryRoute (SoftAP -> /admin/setup, ETH -> SPA).
  // Do not re-register here — SetupServer's ensureSetupPlane would reject Ethernet
  // clients with JSON 403 and steal the production admin entry.

  server.on("/admin/setup", HTTP_GET, [this](AsyncWebServerRequest *req) {
    WebRequestDiagnostics::RequestTimer timer(req, "SetupServer/admin/setup");
    if (!HttpPlaneGate::ensureSetupPlane(req)) return;
    serveSetupPage(_installation, _provisioning, req);
  });

  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *req) {
    WebRequestDiagnostics::RequestTimer timer(req, "SetupServer/favicon");
    if (!HttpPlaneGate::ensureSetupPlane(req)) return;
    req->send(204);
  });

  server.on("/api/setup/status", HTTP_GET, [this](AsyncWebServerRequest *req) {
    WebRequestDiagnostics::RequestTimer timer(req, "SetupServer/api/setup/status");
    if (!HttpPlaneGate::ensureSetupPlane(req)) return;
    if (_provisioning) _provisioning->enforceActiveUnlockSession();
    if (heap_caps_get_largest_free_block(MALLOC_CAP_DMA) < 4096) {
      DmaMemoryMonitor::logTrace("setup-status-before-json");
    }

    // Polled every 250 ms after Step 4 Finish. JSON_DOC_MEDIUM (8192) on the
    // default heap is INTERNAL/DMA SRAM (N16R8 remaining-issues class).
    PsramJsonDocument heapDoc;
    JsonDocument &envelope = heapDoc.doc();
    envelope["success"]    = true;
    envelope["message"]    = "Setup status";
    JsonObject data        = envelope.createNestedObject("data");
    fillSetupStatusData(_provisioning, _eth, data, _wizardConfig, _networkSettings,
                        _routerProvisioning, _routerWorker);
    if (heap_caps_get_largest_free_block(MALLOC_CAP_DMA) < 4096) {
      DmaMemoryMonitor::logTrace("setup-status-after-json");
    }
    Serial.printf(
        "[setup] status installationState=%s wizardStep=%s productionMode=%s\n",
        _installation ? installationStateLabel(_installation->current()) : "unknown",
        data["wizardStep"] | "?",
        (data["productionMode"] | false) ? "true" : "false");
    WebResponse::serveJsonEnvelope(req, 200, envelope);
  });

  server.on(
      "/api/setup/unlock", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        WebRequestDiagnostics::RequestTimer timer(req, "SetupServer/api/setup/unlock");
        if (!HttpPlaneGate::ensureSetupPlane(req)) return;
        if (!_provisioning || !_installation) {
          WebResponse::serveErrorJson(req, 503, "Provisioning unavailable",
                                      "INTERNAL_ERROR");
          return;
        }
        // Only skip password when Setup is already open. Never short-circuit on
        // !isReady() alone — lock UI also locks for ownerCreated mid-install
        // (docs/SETUP_LOCKED_PASSWORD_FAILURE_FORENSIC.md).
        if (!_provisioning->requiresSetupUnlock() ||
            _provisioning->hasActiveSetupUnlockSession()) {
          HeapJsonDocument responseDoc(RenzFiConfig::JSON_DOC_SMALL);
          DynamicJsonDocument &envelope = responseDoc.doc();
          envelope["success"]           = true;
          envelope["message"]           = "Setup already available";
          JsonObject data               = envelope.createNestedObject("data");
          fillSetupStatusData(_provisioning, _eth, data, _wizardConfig, _networkSettings,
                              _routerProvisioning, _routerWorker);
          WebResponse::serveJsonEnvelope(req, 200, envelope);
          return;
        }
        if (!req->_tempObject) {
          WebResponse::serveErrorJson(req, 400, "Request body required", "INVALID_JSON");
          return;
        }
        HeapJsonDocument bodyDoc(RenzFiConfig::JSON_DOC_SMALL);
        DynamicJsonDocument &body = bodyDoc.doc();
        const DeserializationError err =
            deserializeJson(body, static_cast<const char *>(req->_tempObject));
        free(req->_tempObject);
        req->_tempObject = nullptr;
        if (err) {
          WebResponse::serveErrorJson(req, 400, "Invalid JSON body", "INVALID_JSON");
          return;
        }
        const String password = body["password"] | "";
        if (!_provisioning->unlockSetup(password)) {
          WebResponse::serveErrorJson(req, 403, "Invalid setup unlock password",
                                      "SETUP_UNLOCK_INVALID");
          return;
        }
        if (_installation->isReady()) {
          _installation->reopenSetupWizard();
        }
        Serial.printf(
            "[setup] unlock ok installationState=%s setupLocked=false\n",
            installationStateLabel(_installation->current()));
        HeapJsonDocument responseDoc(RenzFiConfig::JSON_DOC_SMALL);
        DynamicJsonDocument &envelope = responseDoc.doc();
        envelope["success"]           = true;
        envelope["message"]           = "Setup unlocked";
        JsonObject data               = envelope.createNestedObject("data");
        fillSetupStatusData(_provisioning, _eth, data, _wizardConfig, _networkSettings,
                            _routerProvisioning, _routerWorker);
        WebResponse::serveJsonEnvelope(req, 200, envelope);
      },
      nullptr, bodyCollect);

  server.on("/api/setup/lock", HTTP_POST, [this](AsyncWebServerRequest *req) {
    WebRequestDiagnostics::RequestTimer timer(req, "SetupServer/api/setup/lock");
    if (!HttpPlaneGate::ensureSetupPlane(req)) return;
    if (_provisioning) _provisioning->closeUnlockedSetup();
    HeapJsonDocument responseDoc(RenzFiConfig::JSON_DOC_SMALL);
    DynamicJsonDocument &envelope = responseDoc.doc();
    envelope["success"]           = true;
    envelope["message"]           = "Setup locked";
    JsonObject data               = envelope.createNestedObject("data");
    fillSetupStatusData(_provisioning, _eth, data, _wizardConfig, _networkSettings,
                        _routerProvisioning, _routerWorker);
    WebResponse::serveJsonEnvelope(req, 200, envelope);
  });

  server.on(
      "/api/setup/owner", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        WebRequestDiagnostics::RequestTimer timer(req, "SetupServer/api/setup/owner");
        if (!HttpPlaneGate::ensureSetupPlane(req)) return;
        if (!ensureSetupWizardEnabled(_installation, _provisioning, req)) return;

        if (!req->_tempObject) {
          WebResponse::serveErrorJson(req, 400, "Request body required",
                                      "INVALID_JSON");
          Serial.println("[setup] owner creation rejected: missing body");
          return;
        }

        HeapJsonDocument heapDoc(RenzFiConfig::JSON_DOC_SMALL);
        DynamicJsonDocument &body = heapDoc.doc();
        DeserializationError err =
            deserializeJson(body, static_cast<const char *>(req->_tempObject));
        free(req->_tempObject);
        req->_tempObject = nullptr;

        if (err) {
          WebResponse::serveErrorJson(req, 400, "Invalid JSON body", "INVALID_JSON");
          Serial.println("[setup] owner creation rejected: invalid json");
          return;
        }

        if (!_provisioning) {
          WebResponse::serveErrorJson(req, 503, "Provisioning unavailable",
                                      "INTERNAL_ERROR");
          Serial.println("[setup] owner creation rejected: provisioning unavailable");
          return;
        }

        SetupProvisioningManager::CreateOwnerInput input;
        input.displayName     = body["displayName"] | "";
        input.username        = body["username"] | "";
        input.password        = body["password"] | "";
        input.confirmPassword = body["confirmPassword"] | "";
        input.setupUnlockPassword = body["setupUnlockPassword"] | "";
        input.confirmSetupUnlockPassword = body["confirmSetupUnlockPassword"] | "";

        const auto result = _provisioning->createOwner(input);

        if (!result.success) {
          WebResponse::serveErrorJson(req, result.httpStatus, result.errorMessage,
                                      result.errorCode);
          Serial.printf("[setup] owner creation rejected: %s\n",
                        result.errorCode.c_str());
          return;
        }

        Serial.println("[setup] owner account created");

        HeapJsonDocument responseDoc(RenzFiConfig::JSON_DOC_MEDIUM);
        DynamicJsonDocument &envelope = responseDoc.doc();
        envelope["success"]           = true;
        envelope["message"]           = result.errorMessage;
        JsonObject data               = envelope.createNestedObject("data");
        fillSetupStatusData(_provisioning, _eth, data, _wizardConfig, _networkSettings,
                        _routerProvisioning, _routerWorker);
        WebResponse::serveJsonEnvelope(req, 200, envelope);
      },
      nullptr, bodyCollect);

  auto handleWizardSave = [this](AsyncWebServerRequest *req, const char *logTag,
                                 auto saveFn) {
    WebRequestDiagnostics::RequestTimer timer(req, logTag);
    if (!HttpPlaneGate::ensureSetupPlane(req)) return;
    if (!ensureSetupWizardEnabled(_installation, _provisioning, req)) return;
    if (!ensureOwnerCreated(_installation, req)) return;
    if (!req->_tempObject) {
      WebResponse::serveErrorJson(req, 400, "Request body required", "INVALID_JSON");
      return;
    }
    HeapJsonDocument heapDoc(RenzFiConfig::JSON_DOC_MEDIUM);
    DynamicJsonDocument &body = heapDoc.doc();
    DeserializationError err =
        deserializeJson(body, static_cast<const char *>(req->_tempObject));
    free(req->_tempObject);
    req->_tempObject = nullptr;
    if (err) {
      WebResponse::serveErrorJson(req, 400, "Invalid JSON body", "INVALID_JSON");
      return;
    }
    const auto result = saveFn(body.as<JsonObjectConst>());
    if (!result.success) {
      timer.finish();
      WebResponse::serveErrorJson(req, result.httpStatus, result.errorMessage,
                                  result.errorCode);
      return;
    }
    HeapJsonDocument responseDoc(RenzFiConfig::JSON_DOC_MEDIUM);
    DynamicJsonDocument &envelope = responseDoc.doc();
    envelope["success"]           = true;
    envelope["message"]           = result.errorMessage;
    JsonObject data               = envelope.createNestedObject("data");
    fillSetupStatusData(_provisioning, _eth, data, _wizardConfig, _networkSettings,
                        _routerProvisioning, _routerWorker);
    timer.finish();
    WebResponse::serveJsonEnvelope(req, 200, envelope);
  };

  server.on(
      "/api/setup/ethernet", HTTP_POST,
      [this, handleWizardSave](AsyncWebServerRequest *req) {
        if (!_wizardConfig) {
          WebResponse::serveErrorJson(req, 503, "Wizard config unavailable",
                                      "INTERNAL_ERROR");
          return;
        }
        handleWizardSave(req, "SetupServer/api/setup/ethernet",
                         [this](JsonObjectConst body) {
                           return _wizardConfig->saveEthernet(body);
                         });
      },
      nullptr, bodyCollect);

  server.on(
      "/api/setup/guest-wifi", HTTP_POST,
      [this, handleWizardSave](AsyncWebServerRequest *req) {
        if (!_wizardConfig) {
          WebResponse::serveErrorJson(req, 503, "Wizard config unavailable",
                                      "INTERNAL_ERROR");
          return;
        }
        handleWizardSave(req, "SetupServer/api/setup/guest-wifi",
                         [this](JsonObjectConst body) {
                           return _wizardConfig->saveGuestWifi(body);
                         });
      },
      nullptr, bodyCollect);

  server.on(
      "/api/setup/ap-deployment", HTTP_POST,
      [this, handleWizardSave](AsyncWebServerRequest *req) {
        if (!_wizardConfig) {
          WebResponse::serveErrorJson(req, 503, "Wizard config unavailable",
                                      "INTERNAL_ERROR");
          return;
        }
        handleWizardSave(req, "SetupServer/api/setup/ap-deployment",
                         [this](JsonObjectConst body) {
                           return _wizardConfig->saveApDeployment(body);
                         });
      },
      nullptr, bodyCollect);

  server.on(
      "/api/setup/coin", HTTP_POST,
      [this, handleWizardSave](AsyncWebServerRequest *req) {
        if (!_wizardConfig) {
          WebResponse::serveErrorJson(req, 503, "Wizard config unavailable",
                                      "INTERNAL_ERROR");
          return;
        }
        handleWizardSave(req, "SetupServer/api/setup/coin",
                         [this](JsonObjectConst body) {
                           return _wizardConfig->saveCoinSetup(body);
                         });
      },
      nullptr, bodyCollect);

  server.on(
      "/api/setup/operator", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        WebRequestDiagnostics::RequestTimer timer(req, "SetupServer/api/setup/operator");
        if (!HttpPlaneGate::ensureSetupPlane(req)) return;
        if (!ensureSetupWizardEnabled(_installation, _provisioning, req)) return;
        if (!ensureOwnerCreated(_installation, req)) return;
        if (!req->_tempObject || !_provisioning) {
          WebResponse::serveErrorJson(req, 400, "Request body required", "INVALID_JSON");
          return;
        }
        HeapJsonDocument heapDoc(RenzFiConfig::JSON_DOC_SMALL);
        DynamicJsonDocument &body = heapDoc.doc();
        DeserializationError err =
            deserializeJson(body, static_cast<const char *>(req->_tempObject));
        free(req->_tempObject);
        req->_tempObject = nullptr;
        if (err) {
          WebResponse::serveErrorJson(req, 400, "Invalid JSON body", "INVALID_JSON");
          return;
        }
        SetupProvisioningManager::CreateOwnerInput input;
        input.username        = body["username"] | "";
        input.password        = body["password"] | "";
        input.confirmPassword = body["confirmPassword"] | "";
        const auto result = _provisioning->createOperator(input);
        if (!result.success) {
          timer.finish();
          WebResponse::serveErrorJson(req, result.httpStatus, result.errorMessage,
                                      result.errorCode);
          return;
        }
        HeapJsonDocument responseDoc(RenzFiConfig::JSON_DOC_MEDIUM);
        DynamicJsonDocument &envelope = responseDoc.doc();
        envelope["success"]           = true;
        envelope["message"]           = result.errorMessage;
        JsonObject data               = envelope.createNestedObject("data");
        fillSetupStatusData(_provisioning, _eth, data, _wizardConfig, _networkSettings,
                        _routerProvisioning, _routerWorker);
        timer.finish();
        WebResponse::serveJsonEnvelope(req, 200, envelope);
      },
      nullptr, bodyCollect);

  server.on("/api/setup/router-config", HTTP_GET, [this](AsyncWebServerRequest *req) {
    WebRequestDiagnostics::RequestTimer timer(req, "SetupServer/api/setup/router-config");
    if (!HttpPlaneGate::ensureSetupPlane(req)) return;
    if (!ensureSetupWizardEnabled(_installation, _provisioning, req)) return;
    if (!ensureOwnerCreated(_installation, req)) return;

    HeapJsonDocument heapDoc(RenzFiConfig::JSON_DOC_SMALL);
    DynamicJsonDocument &envelope = heapDoc.doc();
    envelope["success"]           = true;
    envelope["message"]           = "Router connection metadata";
    JsonObject data               = envelope.createNestedObject("data");
    if (_routerConnection) {
      _routerConnection->fillSafeConfig(data, true);
    }
    WebResponse::serveJsonEnvelope(req, 200, envelope);
  });

  server.on(
      "/api/setup/router/test", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        handleRouterConnectionPost(req, _installation, _provisioning, _routerConnection,
                                   _routerWorker, RouterPostAction::Test);
      },
      nullptr, routerConnectionBodyCollect);

  server.on(
      "/api/setup/router/save", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        handleRouterConnectionPost(req, _installation, _provisioning, _routerConnection,
                                   _routerWorker, RouterPostAction::Save);
      },
      nullptr, routerConnectionBodyCollect);

#if RENZFI_ROUTER_TCP_DIAGNOSTIC
  server.on(
      "/api/setup/router/tcp-check", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        WebRequestDiagnostics::RequestTimer timer(
            req, "SetupServer/api/setup/router/tcp-check");
        if (!HttpPlaneGate::ensureSetupPlane(req)) return;
        if (!ensureSetupWizardEnabled(_installation, _provisioning, req)) return;
        if (!ensureOwnerCreated(_installation, req)) return;
        if (!_routerWorker) {
          timer.finish();
          WebResponse::serveErrorJson(req, 503, "Router worker unavailable",
                                      "INTERNAL_ERROR");
          return;
        }

        String   host = _routerConnection ? _routerConnection->host() : "";
        uint16_t port = _routerConnection ? _routerConnection->apiPort() : 8728;
        char *ownedBody = SetupRouterOwnedBodyStore::take(req);
        if (ownedBody) {
          OwnedBodyGuard bodyGuard(ownedBody);
          HeapJsonDocument heapDoc(RenzFiConfig::JSON_DOC_SMALL);
          DynamicJsonDocument &doc = heapDoc.doc();
          if (!deserializeJson(doc, bodyGuard.body)) {
            JsonObjectConst body = doc.as<JsonObjectConst>();
            if (body["host"].is<const char *>()) {
              String requestedHost = body["host"] | "";
              requestedHost.trim();
              if (!requestedHost.isEmpty()) host = requestedHost;
            }
            if (body["apiPort"].is<uint16_t>()) {
              const uint16_t requestedPort = body["apiPort"] | 0;
              if (requestedPort > 0) port = requestedPort;
            }
          }
        }
        if (host.isEmpty()) host = "10.10.10.1";
        if (port == 0) port = 8728;

        const auto workerResult = _routerWorker->runTcpDiagnostic(
            host, port, RenzFiConfig::ROUTER_TCP_DIAG_ITERATIONS);
        timer.finish();
        WebResponse::serveJson(req, workerResult.httpStatus, workerResult.body,
                                 CachePolicy::NoCache);
      },
      nullptr, routerConnectionBodyCollect);
#endif

#if RENZFI_ROUTER_API_PROTOCOL_DIAGNOSTIC
  server.on(
      "/api/setup/router/api-check", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        WebRequestDiagnostics::RequestTimer timer(
            req, "SetupServer/api/setup/router/api-check");
        if (!HttpPlaneGate::ensureSetupPlane(req)) return;
        if (!ensureSetupWizardEnabled(_installation, _provisioning, req)) return;
        if (!ensureOwnerCreated(_installation, req)) return;
        if (!_routerWorker) {
          timer.finish();
          WebResponse::serveErrorJson(req, 503, "Router worker unavailable",
                                      "INTERNAL_ERROR");
          return;
        }

        char *ownedBody = SetupRouterOwnedBodyStore::take(req);
        if (!ownedBody || !_routerConnection) {
          timer.finish();
          WebResponse::serveErrorJson(req, 400, "Request body required",
                                      "INVALID_JSON");
          return;
        }
        OwnedBodyGuard bodyGuard(ownedBody);

        SetupRouterConnectionManager::RouterInput input;
        if (!parseRouterInputJson(bodyGuard.body, input)) {
          timer.finish();
          WebResponse::serveErrorJson(req, 400, "Invalid JSON body", "INVALID_JSON");
          return;
        }

        const auto workerResult = _routerWorker->runApiProtocolDiagnostic(input);
        timer.finish();
        WebResponse::serveJson(req, workerResult.httpStatus, workerResult.body,
                                 CachePolicy::NoCache);
      },
      nullptr, routerConnectionBodyCollect);
#endif

  server.on("/api/setup/router-status", HTTP_GET, [this](AsyncWebServerRequest *req) {
    WebRequestDiagnostics::RequestTimer timer(req, "SetupServer/api/setup/router-status");
    if (!HttpPlaneGate::ensureSetupPlane(req)) return;

    HeapJsonDocument heapDoc(RenzFiConfig::JSON_DOC_MEDIUM);
    DynamicJsonDocument &envelope = heapDoc.doc();
    envelope["success"]           = true;
    envelope["message"]           = "Router connection status";
    JsonObject data               = envelope.createNestedObject("data");
    fillRouterStatusPayload(_eth, _installation, data);
    WebResponse::serveJsonEnvelope(req, 200, envelope);
  });

  server.on("/api/setup/router-plan", HTTP_GET, [this](AsyncWebServerRequest *req) {
    WebRequestDiagnostics::RequestTimer timer(req, "SetupServer/api/setup/router-plan");
    if (!HttpPlaneGate::ensureSetupPlane(req)) return;
    if (!ensureSetupWizardEnabled(_installation, _provisioning, req)) return;
    if (!ensureOwnerCreated(_installation, req)) return;
    if (!ensureRouterConfigured(_installation, req)) return;
    if (!_routerProvisioning) {
      WebResponse::serveErrorJson(req, 503, "Router provisioning unavailable",
                                  "INTERNAL_ERROR");
      return;
    }

    HeapJsonDocument responseDoc(RenzFiConfig::JSON_DOC_LARGE);
    DynamicJsonDocument &envelope = responseDoc.doc();
    JsonObject data               = envelope.createNestedObject("data");
    const auto result             = _routerProvisioning->buildLocalPlan(data, JsonObject());
    if (!result.success) {
      timer.finish();
      WebResponse::serveErrorJson(req, result.httpStatus, result.errorMessage,
                                  result.errorCode.c_str());
      return;
    }
    if (_wizardConfig) {
      _wizardConfig->fillReviewSummary(data);
    }

    envelope["success"] = true;
    envelope["message"] = result.errorMessage;
    String body;
    serializeJson(envelope, body);
    timer.finish();
    WebResponse::serveJson(req, 200, body, CachePolicy::NoCache);
  });

  server.on("/api/setup/router-plan", HTTP_POST, [](AsyncWebServerRequest *req) {
    WebResponse::serveErrorJson(
        req, 405,
        "Method not allowed. Use GET /api/setup/router-plan for preview.",
        "METHOD_NOT_ALLOWED");
  });

  server.on(
      "/api/setup/router-apply", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        WebRequestDiagnostics::RequestTimer timer(req, "SetupServer/api/setup/router-apply");
        if (!HttpPlaneGate::ensureSetupPlane(req)) return;
        if (!ensureSetupWizardEnabled(_installation, _provisioning, req)) return;
        if (!ensureOwnerCreated(_installation, req)) return;
        if (!ensureRouterConfigured(_installation, req)) return;

        char *ownedBody = SetupRouterOwnedBodyStore::take(req);
        if (!ownedBody || !_routerWorker) {
          WebResponse::serveErrorJson(req, 400, "Request body required", "INVALID_JSON");
          return;
        }
        OwnedBodyGuard bodyGuard(ownedBody);

        const auto workerResult = _routerWorker->runApply(bodyGuard.body);
        timer.finish();
        WebResponse::serveJson(req, workerResult.httpStatus, workerResult.body,
                               CachePolicy::NoCache);
      },
      nullptr, routerConnectionBodyCollect);

  server.on(
      "/api/setup/router/existing-network/scan", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        WebRequestDiagnostics::RequestTimer timer(req, "SetupServer/existing-network/scan");
        if (!HttpPlaneGate::ensureSetupPlane(req)) return;
        if (!ensureSetupWizardEnabled(_installation, _provisioning, req)) return;
        if (!ensureOwnerCreated(_installation, req)) return;
        if (!ensureSetupOwnerOnly(_auth, req)) return;
        if (!ensureRouterConfigured(_installation, req)) return;
        if (!_routerWorker) {
          WebResponse::serveErrorJson(req, 503, "Router worker unavailable", "INTERNAL_ERROR");
          return;
        }

        // Optional JSON body: {"rescan": true} — the ONLY way this endpoint
        // is allowed to bypass the cache and touch MikroTik again.
        bool forceRescan = false;
        if (req->_tempObject) {
          HeapJsonDocument bodyDoc(RenzFiConfig::JSON_DOC_SMALL);
          DynamicJsonDocument &body = bodyDoc.doc();
          if (!deserializeJson(body, static_cast<const char *>(req->_tempObject))) {
            forceRescan = body["rescan"] | false;
          }
          free(req->_tempObject);
          req->_tempObject = nullptr;
        }

        // Rule #1 (Setup Simplification Pass): router inspection for this
        // step happens ONLY ONCE per Save. Every other view of this panel
        // (Back/Next, page reload, wizard resume) must reuse the cached
        // result instead of re-scanning MikroTik. Only an explicit Rescan
        // (forceRescan) or a fresh Save (ExistingNetworkScanCache::clear()
        // in RouterProvisioningWorker) may trigger a new live scan.
        if (forceRescan) {
          ExistingNetworkScanCache::clear();
        } else {
          int cachedStatus = 0;
          String cachedBody;
          uint32_t cachedAgeMs = 0;
          if (ExistingNetworkScanCache::getAny(cachedStatus, cachedBody, cachedAgeMs)) {
            timer.finish();
            WebResponse::serveJson(req, cachedStatus, cachedBody, CachePolicy::NoCache);
            return;
          }
        }

        const auto outcome = _routerWorker->enqueueExistingNetworkScan();
        timer.finish();
        if (!outcome.accepted) {
          WebResponse::serveJson(req, 503,
                                 buildSetupErrorJsonBody("Router worker is busy",
                                                         "ROUTER_WORKER_BUSY"),
                                 CachePolicy::NoCache);
          return;
        }
        WebResponse::serveJson(req, 202, buildJobQueuedBody(outcome.jobId),
                               CachePolicy::NoCache);
      },
      nullptr, bodyCollect);

  server.on("/api/setup/router/wifi/networks", HTTP_GET, [this](AsyncWebServerRequest *req) {
    WebRequestDiagnostics::RequestTimer timer(req, "SetupServer/wifi/networks");
    if (!HttpPlaneGate::ensureSetupPlane(req)) return;
    if (!ensureSetupWizardEnabled(_installation, _provisioning, req)) return;
    if (!ensureOwnerCreated(_installation, req)) return;
    if (!ensureSetupOwnerOnly(_auth, req)) return;
    if (!ensureRouterConfigured(_installation, req)) return;
    if (!_routerWorker) {
      WebResponse::serveErrorJson(req, 503, "Router worker unavailable", "INTERNAL_ERROR");
      return;
    }

    // Tasks 1/2/3/6/9/13 (Wizard Resume / Single Router Worker / Cache /
    // Rate Limiting / Idempotent Discovery / Stress Testing): reopening,
    // reloading, or backgrounding-then-restoring the Setup Wizard on Step 3
    // must NEVER re-run a live MikroTik discovery more than necessary.
    // Priority order, none of which ever touch MikroTik except the last:
    //   1. Fresh cache (age < TTL)              -> instant, no RouterOS I/O
    //   2. Worker already busy                  -> serve any cached result
    //                                               (stale ok), else HTTP 202
    //   3. Rate-limited (attempt < 5s ago)       -> serve any cached result
    //                                               (stale ok), else HTTP 202
    //   4. Otherwise: this request performs the one real discovery, and the
    //      result is cached for every other request during the next TTL.
    int cachedStatus = 0;
    String cachedBody;
    if (WifiDiscoveryCache::getFresh(RenzFiConfig::WIFI_DISCOVERY_CACHE_TTL_MS,
                                     cachedStatus, cachedBody)) {
      timer.finish();
      WebResponse::serveJson(req, cachedStatus, cachedBody, CachePolicy::NoCache);
      return;
    }

    uint32_t staleAgeMs = 0;
    const bool haveStale =
        WifiDiscoveryCache::getAny(cachedStatus, cachedBody, staleAgeMs);

    if (_routerWorker->isBusy()) {
      timer.finish();
      if (haveStale) {
        WebResponse::serveJson(req, cachedStatus, cachedBody, CachePolicy::NoCache);
      } else {
        WebResponse::serveJson(req, 202, buildWifiDiscoveryBusyBody(),
                               CachePolicy::NoCache);
      }
      return;
    }

    if (!WifiDiscoveryCache::tryBeginAttempt(
            RenzFiConfig::WIFI_DISCOVERY_MIN_INTERVAL_MS)) {
      // Another request already started an attempt within the rate-limit
      // window (or one just finished) — never touch MikroTik again here.
      timer.finish();
      if (haveStale) {
        WebResponse::serveJson(req, cachedStatus, cachedBody, CachePolicy::NoCache);
      } else {
        // Rate-limited: a discovery attempt already started recently — do NOT
        // enqueue another worker job (prevents duplicate discovery storms when
        // the browser polls every 2s while the first job is still running).
        WebResponse::serveJson(req, 202, buildWifiDiscoveryBusyBody(),
                               CachePolicy::NoCache);
      }
      return;
    }

    // Task 1/8 (Wizard Resume Architecture / Async Safety): this request is
    // the one that actually gets to run a live discovery — but it must
    // still never block the AsyncWebServer/AsyncTCP task while MikroTik is
    // probed (that can legitimately take a few seconds across several
    // paced RouterOS commands). Queue the job on the worker task and
    // return immediately; the frontend's existing bounded-retry loop
    // treats HTTP 202 as "still checking" and polls back in ~2s, by which
    // point WifiDiscoveryCache normally already holds the fresh result.
    _routerWorker->tryRefreshWifiNetworksInBackground();
    timer.finish();
    WebResponse::serveJson(req, 202, buildWifiDiscoveryBusyBody(), CachePolicy::NoCache);
  });

  server.on(
      "/api/setup/router/wifi/selection", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        WebRequestDiagnostics::RequestTimer timer(req, "SetupServer/wifi/selection");
        DmaMemoryMonitor::logTrace("wifi-selection-enter");
        if (!HttpPlaneGate::ensureSetupPlane(req)) return;
        if (!ensureSetupWizardEnabled(_installation, _provisioning, req)) return;
        if (!ensureOwnerCreated(_installation, req)) return;
        if (!ensureSetupOwnerOnly(_auth, req)) return;
        if (!ensureRouterConfigured(_installation, req)) return;
        if (!_routerProvisioning || !_provisioning || !_eth) {
          WebResponse::serveErrorJson(req, 503, "Router provisioning unavailable",
                                      "INTERNAL_ERROR");
          return;
        }
        if (!req->_tempObject) {
          WebResponse::serveErrorJson(req, 400, "Request body required", "INVALID_JSON");
          return;
        }

        HeapJsonDocument bodyDoc(RenzFiConfig::JSON_DOC_SMALL);
        DynamicJsonDocument &body = bodyDoc.doc();
        if (deserializeJson(body, static_cast<const char *>(req->_tempObject))) {
          free(req->_tempObject);
          req->_tempObject = nullptr;
          WebResponse::serveErrorJson(req, 400, "Invalid JSON body", "INVALID_JSON");
          return;
        }
        free(req->_tempObject);
        req->_tempObject = nullptr;

        PsramJsonDocument responseDoc;
        JsonDocument &envelope = responseDoc.doc();
        JsonObject data        = envelope.createNestedObject("data");
        const auto result =
            _routerProvisioning->saveWifiSelection(body.as<JsonObjectConst>(), data);
        if (!result.success) {
          timer.finish();
          WebResponse::serveErrorJson(req, result.httpStatus, result.errorMessage,
                                      result.errorCode);
          return;
        }

        _provisioning->fillSetupStatus(
            data, _eth,
            buildSetupStatusContext(_routerProvisioning, _routerWorker));
        if (_routerProvisioning) {
          data["durableCommitStatus"] =
              _routerProvisioning->durableCommitStatus();
          data["wifiSelectionDurablePending"] =
              _routerProvisioning->wifiSelectionDurablePending();
        }
        envelope["success"] = true;
        envelope["message"] = result.errorMessage;
        timer.finish();
        DmaMemoryMonitor::logTrace("wifi-selection-before-response");
        // 202 while durable commit is pending — do not claim SD write done.
        WebResponse::serveJsonEnvelope(req, result.httpStatus, envelope);
      },
      nullptr, bodyCollect);

  server.on(
      "/api/setup/router/existing-network/configure", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        WebRequestDiagnostics::RequestTimer timer(req, "SetupServer/existing-network/configure");
        if (!HttpPlaneGate::ensureSetupPlane(req)) return;
        if (!ensureSetupWizardEnabled(_installation, _provisioning, req)) return;
        if (!ensureOwnerCreated(_installation, req)) return;
        if (!ensureSetupOwnerOnly(_auth, req)) return;
        if (!ensureRouterConfigured(_installation, req)) return;
        if (!_routerWorker) {
          WebResponse::serveErrorJson(req, 503, "Router worker unavailable", "INTERNAL_ERROR");
          return;
        }
        if (!req->_tempObject) {
          WebResponse::serveErrorJson(req, 400, "Request body required", "INVALID_JSON");
          return;
        }

        OwnedBodyGuard bodyGuard(static_cast<char *>(req->_tempObject));
        req->_tempObject = nullptr;

        DmaMemoryMonitor::logTrace("configure-existing-before-enqueue");
        const auto outcome =
            _routerWorker->enqueueConfigureExistingNetwork(bodyGuard.body);
        timer.finish();
        DmaMemoryMonitor::logTrace("configure-existing-after-enqueue");
        if (!outcome.accepted) {
          WebResponse::serveJson(req, 503,
                                 buildSetupErrorJsonBody("Router worker is busy",
                                                         "ROUTER_WORKER_BUSY"),
                                 CachePolicy::NoCache);
          return;
        }
        WebResponse::serveJson(req, 202, buildJobQueuedBody(outcome.jobId),
                               CachePolicy::NoCache);
      },
      nullptr, bodyCollect);

  server.on("/api/setup/router/network-mode", HTTP_GET, [this](AsyncWebServerRequest *req) {
    WebRequestDiagnostics::RequestTimer timer(req, "SetupServer/network-mode GET");
    if (!HttpPlaneGate::ensureSetupPlane(req)) return;
    if (!ensureSetupWizardEnabled(_installation, _provisioning, req)) return;
    if (!ensureOwnerCreated(_installation, req)) return;
    if (!ensureSetupOwnerOnly(_auth, req)) return;
    if (!_routerProvisioning) {
      WebResponse::serveErrorJson(req, 503, "Router provisioning unavailable", "INTERNAL_ERROR");
      return;
    }
    HeapJsonDocument heapDoc(RenzFiConfig::JSON_DOC_MEDIUM);
    DynamicJsonDocument &envelope = heapDoc.doc();
    JsonObject data               = envelope.createNestedObject("data");
    fillNetworkModeResponse(data, _routerProvisioning);
    if (_routerConnection) {
      data["hasSavedRouterConnection"] = _routerConnection->hasVerifiedConnection();
    }
    envelope["success"] = true;
    envelope["message"] = "Network mode status";
    timer.finish();
    WebResponse::serveJsonEnvelope(req, 200, envelope);
  });

  server.on(
      "/api/setup/router/network-mode", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        WebRequestDiagnostics::RequestTimer timer(req, "SetupServer/network-mode POST");
        if (!HttpPlaneGate::ensureSetupPlane(req)) return;
        if (!ensureSetupWizardEnabled(_installation, _provisioning, req)) return;
        if (!ensureOwnerCreated(_installation, req)) return;
        if (!ensureSetupOwnerOnly(_auth, req)) return;
        if (!req->_tempObject || !_routerProvisioning) {
          WebResponse::serveErrorJson(req, 400, "Request body required", "INVALID_JSON");
          return;
        }
        HeapJsonDocument heapDoc(RenzFiConfig::JSON_DOC_SMALL);
        DynamicJsonDocument &body = heapDoc.doc();
        if (deserializeJson(body, static_cast<const char *>(req->_tempObject))) {
          WebResponse::serveErrorJson(req, 400, "Invalid JSON body", "INVALID_JSON");
          free(req->_tempObject);
          req->_tempObject = nullptr;
          return;
        }
        free(req->_tempObject);
        req->_tempObject = nullptr;
        const auto result =
            _routerProvisioning->setNetworkModePreference(body.as<JsonObjectConst>());
        if (!result.success) {
          timer.finish();
          WebResponse::serveErrorJson(req, result.httpStatus, result.errorMessage,
                                      result.errorCode);
          return;
        }
        HeapJsonDocument responseDoc(RenzFiConfig::JSON_DOC_MEDIUM);
        DynamicJsonDocument &envelope = responseDoc.doc();
        JsonObject data               = envelope.createNestedObject("data");
        fillNetworkModeResponse(data, _routerProvisioning);
        envelope["success"] = true;
        envelope["message"] = result.errorMessage;
        String responseBody;
        serializeJson(envelope, responseBody);
        timer.finish();
        WebResponse::serveJson(req, result.httpStatus, responseBody,
                               CachePolicy::NoCache);
      },
      nullptr, bodyCollect);

  server.on("/api/setup/provisioning/portal/*", HTTP_GET,
            [this](AsyncWebServerRequest *req) {
              WebRequestDiagnostics::RequestTimer timer(
                  req, "SetupServer/provisioning/portal");
              if (!HttpPlaneGate::ensureAppliancePlane(req)) return;
              if (!_finishEngine) {
                WebResponse::serveErrorJson(req, 503, "Provisioning unavailable",
                                            "INTERNAL_ERROR");
                return;
              }
              const String token = req->hasParam("token")
                                       ? req->getParam("token")->value()
                                       : "";
              if (!_finishEngine->acceptsPortalFetchToken(token)) {
                WebResponse::serveErrorJson(req, 403, "Portal fetch token invalid",
                                            "FORBIDDEN");
                return;
              }
              String path = req->url();
              const int marker = path.indexOf("/api/setup/provisioning/portal/");
              if (marker < 0) {
                WebResponse::serveErrorJson(req, 404, "Portal asset not found", "NOT_FOUND");
                return;
              }
              String filename = path.substring(marker + 34);
              const int query = filename.indexOf('?');
              if (query >= 0) filename = filename.substring(0, query);
              filename.trim();
              if (filename.isEmpty()) {
                WebResponse::serveErrorJson(req, 404, "Portal asset not found", "NOT_FOUND");
                return;
              }
              FinishTrace::portalHttpGetReceived(filename.c_str());
              String content, contentType, errorOut;
              if (filename == "renzfi-app.js" || filename == "status.html" ||
                  filename == "logout.html") {
                if (!_finishEngine->servePortalAsset(filename, content, contentType,
                                                     errorOut)) {
                  WebResponse::serveErrorJson(req, 404, errorOut, "NOT_FOUND");
                  return;
                }
                FinishTrace::portalHttpResponseCompleted(filename.c_str());
                timer.finish();
                req->send(200, contentType.c_str(), content);
                return;
              }
              const String spiffsPath = String(StoragePaths::Spiffs::Portal) + "/" + filename;
              if (!SPIFFS.exists(spiffsPath)) {
                WebResponse::serveErrorJson(req, 404, "Portal asset not found", "NOT_FOUND");
                return;
              }
              FinishTrace::portalHttpResponseCompleted(filename.c_str());
              timer.finish();
              req->send(SPIFFS, spiffsPath, portalContentTypeForFile(filename));
            });

  server.on(
      "/api/setup/finish", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        WebRequestDiagnostics::RequestTimer timer(req, "SetupServer/api/setup/finish");
        if (!HttpPlaneGate::ensureSetupPlane(req)) return;
        if (!ensureSetupWizardEnabled(_installation, _provisioning, req)) return;
        if (!ensureOwnerCreated(_installation, req)) return;
        if (!ensureSetupOwnerOnly(_auth, req)) return;
        if (!_routerWorker || !_finishEngine) {
          WebResponse::serveErrorJson(req, 503, "Router provisioning unavailable",
                                      "INTERNAL_ERROR");
          return;
        }
        // Authoritative completion: only skip the worker when installation is
        // already provisioned/ready. finishCompleted-in-RAM alone is not enough —
        // that desync previously returned HTTP 200 while state stayed
        // router_configured.
        if (_installation && _installation->isReady()) {
          HeapJsonDocument responseDoc(RenzFiConfig::JSON_DOC_SMALL);
          DynamicJsonDocument &envelope = responseDoc.doc();
          JsonObject data               = envelope.createNestedObject("data");
          data["alreadyCompleted"]      = true;
          if (_provisioning && _eth) {
            fillSetupStatusData(_provisioning, _eth, data, _wizardConfig,
                                _networkSettings, _routerProvisioning, _routerWorker);
          }
          envelope["success"] = true;
          envelope["message"] = "Setup already finished";
          timer.finish();
          WebResponse::serveJsonEnvelope(req, 200, envelope);
          return;
        }
        if (!ensureRouterConfigured(_installation, req)) return;

        String requestJson = "{}";
        if (req->_tempObject) {
          requestJson = static_cast<const char *>(req->_tempObject);
          free(req->_tempObject);
          req->_tempObject = nullptr;
        }
        const auto outcome =
            _routerWorker->enqueueFinishSetup(requestJson.c_str());
        timer.finish();
        if (!outcome.accepted) {
          WebResponse::serveJson(req, 503,
                                 buildSetupErrorJsonBody("Router worker is busy",
                                                         "ROUTER_WORKER_BUSY"),
                                 CachePolicy::NoCache);
          return;
        }
        WebResponse::serveJson(req, 202, buildJobQueuedBody(outcome.jobId),
                               CachePolicy::NoCache);
      },
      nullptr, bodyCollect);

  server.on("/api/setup/complete", HTTP_POST, [this](AsyncWebServerRequest *req) {
    WebRequestDiagnostics::RequestTimer timer(req, "SetupServer/api/setup/complete");
    if (!HttpPlaneGate::ensureSetupPlane(req)) return;
    if (!ensureSetupOwnerOnly(_auth, req)) return;
    if (!_installation || !_installation->isReady()) {
      WebResponse::serveErrorJson(req, 409, "Setup is not complete yet",
                                  "SETUP_NOT_COMPLETE");
      return;
    }
    if (!_finishEngine) {
      WebResponse::serveErrorJson(req, 503, "Router provisioning engine unavailable",
                                  "INTERNAL_ERROR");
      return;
    }
    _finishEngine->completeSetupAfterFinishSuccess();
    if (_provisioning) _provisioning->closeUnlockedSetup();
    HeapJsonDocument responseDoc(RenzFiConfig::JSON_DOC_SMALL);
    DynamicJsonDocument &envelope = responseDoc.doc();
    envelope["success"]           = true;
    envelope["message"]             = "Installation finalized — rebooting";
    JsonObject data                 = envelope.createNestedObject("data");
    data["rebooting"]               = true;
    timer.finish();
    WebResponse::serveJsonEnvelope(req, 200, envelope);
    delay(250);
    ESP.restart();
  });

  server.on("/api/setup/router/jobs/*", HTTP_GET, [this](AsyncWebServerRequest *req) {
    WebRequestDiagnostics::RequestTimer timer(req, "SetupServer/router/jobs");
    if (!HttpPlaneGate::ensureSetupPlane(req)) return;
    serveJobPoll(req, _routerWorker, "/api/setup/router/jobs/",
                /*includeScanProgressAliases=*/false);
    timer.finish();
  });

  server.on("/api/setup/router/existing-network/jobs/*", HTTP_GET,
            [this](AsyncWebServerRequest *req) {
              WebRequestDiagnostics::RequestTimer timer(
                  req, "SetupServer/existing-network/jobs");
              if (!HttpPlaneGate::ensureSetupPlane(req)) return;
              serveJobPoll(req, _routerWorker,
                          "/api/setup/router/existing-network/jobs/",
                          /*includeScanProgressAliases=*/true);
              timer.finish();
            });

  Serial.println("[web] SetupServer routes registered (Management AP setup plane):");
  Serial.println("[web]   GET /admin/setup, /healthz, /favicon.ico");
  Serial.println("[web]   GET /, /login, /dashboard -> WebServerManager (plane-aware)");
  Serial.println("[web]   GET /api/setup/status, POST /api/setup/owner");
  Serial.println("[web]   GET /api/setup/router-status, /api/setup/router-config");
  Serial.println("[web]   POST /api/setup/router/test, /api/setup/router/save (async, 202+jobId)");
  Serial.println("[web]   GET /api/setup/router-plan (POST -> 405)");
  Serial.println("[web]   POST /api/setup/router-apply (sync, legacy), /api/setup/finish (async, 202+jobId), /api/setup/complete (POST)");
  Serial.println("[web]   GET  /api/setup/router/wifi/networks (cache-first, async refresh)");
  Serial.println("[web]   POST /api/setup/router/wifi/selection (defer durable)");
  Serial.println("[web]   POST /api/setup/router/existing-network/scan (async, 202+jobId)");
  Serial.println("[web]   POST /api/setup/router/existing-network/configure (async, 202+jobId)");
  Serial.println("[web]   GET /api/setup/router/jobs/*, /api/setup/router/existing-network/jobs/* (poll)");
  Serial.println("[web]   GET /api/setup/provisioning/portal/* (tokenized fetch)");
}

const char *SetupServer::providerName() const {
  return "SetupServer";
}
