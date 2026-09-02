#include "ApiServer.h"

#include "AuthRole.h"

#include <Update.h>
#include <MD5Builder.h>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <memory>
#include <vector>

#include <SD.h>
#include <SPIFFS.h>

#include "Config.h"
#include "AssetManager.h"
#include "DeviceIdentity.h"
#include "DmaMemoryMonitor.h"
#include "JsonHeap.h"
#include "ManagementApConfig.h"
#include "ManagementApManager.h"
#include "ExternalAccessPointManager.h"
#include "ContentFilterManager.h"
#include "GamingPriorityManager.h"
#include "NetworkDiagnostics.h"
#include "NetworkStatusModel.h"
#include "PortalConfigManager.h"
#include "ProductionHandoff.h"
#include "RouterApiTransportGate.h"
#include "RouterProvisioningApi.h"
#include "SalesTime.h"
#include "SetupProvisioningManager.h"
#include "StoragePaths.h"
#include "web/HttpPlaneGate.h"
#include "web/WebRequestDiagnostics.h"
#include "web/WebResponse.h"
#include "web/WebServerManager.h"

// ── File-scope helpers ────────────────────────────────────────────────────────

namespace {

// CPU-side JSON HTTP bodies must not use Arduino String / default malloc.
// Blocks below CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL land in INTERNAL/DMA SRAM
// (N16R8 remaining-issues forensic). W5500 SPI priv buffers need that pool.
struct PsramJsonBody {
  char *buf = nullptr;
  size_t len = 0;
  bool holdsSlot = false;
  ~PsramJsonBody() {
    if (holdsSlot) DmaMemoryMonitor::releaseHttpSlot();
    if (buf) heap_caps_free(buf);
  }
};

std::shared_ptr<PsramJsonBody> makePsramJsonBody(JsonDocument &doc) {
  auto body = std::make_shared<PsramJsonBody>();
  const size_t n = measureJson(doc);
  body->buf = static_cast<char *>(
      heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!body->buf) {
    body->buf = static_cast<char *>(heap_caps_malloc(n + 1, MALLOC_CAP_8BIT));
  }
  if (!body->buf) return nullptr;
  body->len = serializeJson(doc, body->buf, n + 1);
  return body;
}

void sendEthDmaLowJson(AsyncWebServerRequest *req, const char *detail) {
  if (DmaMemoryMonitor::isEthDmaCritical()) {
    Serial.printf("[http] drop reason=%s (ETH DMA critical, no socket I/O)\n",
                  detail ? detail : "api-json");
    DmaMemoryMonitor::logSnapshot("api-json-drop-critical");
    return;
  }
  Serial.printf("[http] 503 reason=ETH_DMA_LOW detail=%s\n",
                detail ? detail : "");
  DmaMemoryMonitor::logSnapshot("api-json-dma-low");
  AsyncWebServerResponse *res = req->beginResponse(
      503, "application/json",
      "{\"success\":false,\"error\":\"Ethernet DMA temporarily "
      "exhausted\",\"code\":\"ETH_DMA_LOW\"}");
  WebResponse::addCorsHeaders(res);
  res->addHeader("Retry-After", "2");
  req->send(res);
}

void sendJsonResponse(AsyncWebServerRequest *req, int httpStatus,
                      JsonDocument &doc, const String *setCookie = nullptr,
                      bool liveness = false) {
  // /api/health is session liveness — must not compete for paced JSON slots
  // or the 3072 B HTTP-admit floor while Admin/Sales fan-out is in flight.
  if (liveness) {
    if (DmaMemoryMonitor::isEthDmaCritical()) {
      Serial.println("[http] drop reason=api-health-liveness (ETH DMA critical)");
      DmaMemoryMonitor::logSnapshot("api-health-drop-critical");
      return;
    }
    if (!DmaMemoryMonitor::hasEthTransmitHeadroom()) {
      sendEthDmaLowJson(req, "health-liveness");
      return;
    }
  } else {
    if (!WebResponse::ensureEthTransmitHeadroom(req, "api-json-admit")) return;
    if (!DmaMemoryMonitor::tryAcquireHttpSlot()) {
      Serial.println("[http] 503 reason=ETH_DMA_LOW detail=concurrency");
      DmaMemoryMonitor::logSnapshot("api-json-concurrency");
      sendEthDmaLowJson(req, "concurrency");
      return;
    }
  }

  auto body = makePsramJsonBody(doc);
  if (!body) {
    if (!liveness) DmaMemoryMonitor::releaseHttpSlot();
    req->send(500, "application/json",
              "{\"success\":false,\"error\":\"JSON alloc failed\","
              "\"code\":\"JSON_ALLOC_FAILED\"}");
    return;
  }
  body->holdsSlot = !liveness;

  AsyncWebServerResponse *res = req->beginResponse(
      "application/json", body->len,
      [body](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
        if (!buffer || !body->buf || index >= body->len) return 0;
        // Same ETH TX pause as large SPA streams: do not drive W5500
        // setup_dma_priv_buffer when dma_largest is already below one frame.
        if (DmaMemoryMonitor::isEthDmaCritical() ||
            !DmaMemoryMonitor::hasEthTransmitHeadroom()) {
          return RESPONSE_TRY_AGAIN;
        }
        const size_t remain = body->len - index;
        const size_t n = remain < maxLen ? remain : maxLen;
        memcpy(buffer, reinterpret_cast<const uint8_t *>(body->buf) + index, n);
        return n;
      });
  if (!res) return;
  res->setCode(httpStatus);
  WebResponse::addCorsHeaders(res);
  res->addHeader("Cache-Control", "no-store");
  if (setCookie && setCookie->length() > 0) {
    res->addHeader("Set-Cookie", *setCookie);
  }
  req->send(res);
}

String urlDecode(String value) {
  value.replace("%20", " ");
  value.replace("%2F", "/");
  value.replace("%2f", "/");
  value.replace("%3A", ":");
  value.replace("%3a", ":");
  return value;
}

bool parseExternalAccessPointId(const String &path, String &idOut) {
  const String prefix = "/api/access-points/";
  if (!path.startsWith(prefix)) return false;
  const String rest = path.substring(prefix.length());
  if (rest.length() == 0) return false;
  if (rest.indexOf('/') >= 0) return false;
  if (rest == "jobs") return false;
  idOut = urlDecode(rest);
  return idOut.length() > 0;
}

bool parseExternalAccessPointCheckPath(const String &path, String &idOut) {
  const String prefix = "/api/access-points/";
  const String suffix = "/check";
  if (!path.startsWith(prefix) || !path.endsWith(suffix)) return false;
  if (path.length() <= prefix.length() + suffix.length()) return false;
  const String rest =
      path.substring(prefix.length(), path.length() - suffix.length());
  if (rest.length() == 0 || rest.indexOf('/') >= 0) return false;
  if (rest == "jobs") return false;
  idOut = urlDecode(rest);
  return idOut.length() > 0;
}

bool parseExternalAccessPointSyncPath(const String &path, String &idOut) {
  const String prefix = "/api/access-points/";
  const String suffix = "/sync";
  if (!path.startsWith(prefix) || !path.endsWith(suffix)) return false;
  if (path.length() <= prefix.length() + suffix.length()) return false;
  const String rest =
      path.substring(prefix.length(), path.length() - suffix.length());
  if (rest.length() == 0 || rest.indexOf('/') >= 0) return false;
  if (rest == "jobs") return false;
  idOut = urlDecode(rest);
  return idOut.length() > 0;
}

bool parseExternalAccessPointJobPath(const String &path, uint32_t &jobIdOut) {
  const String prefix = "/api/access-points/jobs/";
  if (!path.startsWith(prefix)) return false;
  const String rest = path.substring(prefix.length());
  if (rest.length() == 0 || rest.indexOf('/') >= 0) return false;
  if (rest[0] < '0' || rest[0] > '9') return false;
  jobIdOut = static_cast<uint32_t>(rest.toInt());
  return jobIdOut > 0;
}

bool parseExternalAccessPointDetectJobPath(const String &path, uint32_t &jobIdOut) {
  const String prefix = "/api/access-points/detect/jobs/";
  if (!path.startsWith(prefix)) return false;
  const String rest = path.substring(prefix.length());
  if (rest.length() == 0 || rest.indexOf('/') >= 0) return false;
  if (rest[0] < '0' || rest[0] > '9') return false;
  jobIdOut = static_cast<uint32_t>(rest.toInt());
  return jobIdOut > 0;
}

// Body accumulation handler.  Pass as the ArBodyHandlerFunction (5th arg) to
// server.on() for every route that needs to read a POST/PUT body.
// The body is stored as a null-terminated C-string in req->_tempObject, which
// AsyncWebServerRequest::~AsyncWebServerRequest() frees automatically.
void bodyCollect(AsyncWebServerRequest *req, uint8_t *data, size_t len,
                 size_t index, size_t total) {
  if (total > 8192) return;  // reject oversized bodies
  if (index == 0) {
    if (req->_tempObject) free(req->_tempObject);
    req->_tempObject = malloc(total + 1);
  }
  if (req->_tempObject) {
    memcpy(static_cast<uint8_t *>(req->_tempObject) + index, data, len);
    if (index + len == total)
      static_cast<char *>(req->_tempObject)[total] = '\0';
  }
}

void largeBodyCollect(AsyncWebServerRequest *req, uint8_t *data, size_t len,
                      size_t index, size_t total) {
  if (total > RenzFiConfig::PORTAL_MUSIC_MAX_BYTES) return;
  if (index == 0) {
    if (req->_tempObject) free(req->_tempObject);
    req->_tempObject = malloc(sizeof(size_t) + total);
    if (!req->_tempObject) return;
    *static_cast<size_t *>(req->_tempObject) = total;
  }
  if (!req->_tempObject) return;
  uint8_t *base = static_cast<uint8_t *>(req->_tempObject) + sizeof(size_t);
  memcpy(base + index, data, len);
}

void fillActiveUsers(SessionManager       *sessions,
                     PortalSessionManager *portal,
                     JsonDocument         &data) {
  DynamicJsonDocument seen(RenzFiConfig::JSON_DOC_SMALL);
  JsonArray seenMacs = seen.to<JsonArray>();
  JsonArray out = data.to<JsonArray>();

  if (portal) portal->appendActiveUsers(out, seenMacs);
  if (sessions) sessions->appendActiveUsers(out, seenMacs);
}

void mergedActiveUserStats(SessionManager       *sessions,
                           PortalSessionManager *portal,
                           int                  &count,
                           int                  &paused) {
  DynamicJsonDocument data(RenzFiConfig::JSON_DOC_MEDIUM);
  fillActiveUsers(sessions, portal, data);
  count = 0;
  paused = 0;
  if (!data.is<JsonArray>()) return;
  for (JsonObjectConst row : data.as<JsonArray>()) {
    count++;
    const char *state = row["state"] | "";
    if (strcmp(state, "paused") == 0) paused++;
  }
}

// Safe JSON when ENABLE_COIN_MANAGER=false (_coin == nullptr in ApiServer).
void fillCoinDisabledStatus(JsonObject coinSlot) {
  coinSlot["enabled"]           = false;
  coinSlot["state"]             = "DISABLED";
  coinSlot["hardwareState"]     = "DISABLED";
  coinSlot["lastPulseTimestamp"] = nullptr;
  coinSlot["lastCoinTimestamp"]  = nullptr;
  coinSlot["totalPulseCount"]   = 0;
  coinSlot["totalCoinCount"]    = 0;
  coinSlot["uptimePulseCount"]  = 0;
  coinSlot["uptimeCoinCount"]   = 0;
  coinSlot["ok"]                = false;
  coinSlot["stateLabel"]        = "disabled";
  coinSlot["pulsesToday"]       = 0;
}

void fillCoinDisabledCoinStatus(JsonObject out) {
  out["enabled"] = false;
  out["state"] = "DISABLED";
  out["totalPulseCount"] = 0;
  out["totalCoinCount"] = 0;
  out["uptimePulseCount"] = 0;
  out["uptimeCoinCount"] = 0;
  out["lastPulseTimestamp"] = nullptr;
  out["lastCoinTimestamp"] = nullptr;
}

void fillCoinDisabledSettings(JsonDocument &doc) {
  doc["pulsesPerPeso"]         = "1";
  doc["pesoPerPulse"]          = "1";
  doc["pulse_width_ms"]        = String(RenzFiConfig::COIN_DEBOUNCE_MS);
  doc["calibration"]           = "1";
  doc["timeout_seconds"]       = String(RenzFiConfig::COIN_INSERT_TIMEOUT_SEC);
  doc["defaultMinutesPerPeso"] = "5";
  doc["debounceMs"]            = String(RenzFiConfig::COIN_DEBOUNCE_MS);
  doc["settleMs"]              = String(RenzFiConfig::COIN_SETTLE_MS);
  doc["timeoutSeconds"]        = String(RenzFiConfig::COIN_INSERT_TIMEOUT_SEC);
  doc["enabled"]               = "false";
}

void fillCoinDisabledDiagnostics(JsonDocument &doc) {
  JsonObject stats = doc["stats"].to<JsonObject>();
  stats["last_pulse"]      = "0";
  stats["total_today"]     = "0";
  stats["errors"]          = "0";
  stats["state"]           = "disabled";
  stats["enabled"]         = "false";
  stats["pendingPulses"]   = "0";
  stats["pulsesToday"]     = "0";
  stats["debounceMs"]      = String(RenzFiConfig::COIN_DEBOUNCE_MS);
  stats["settleMs"]        = String(RenzFiConfig::COIN_SETTLE_MS);
  stats["pin"]             = String(RenzFiConfig::PIN_COIN);
  doc["logs"].to<JsonArray>();
}

struct PortalAssetUpload {
  enum class Kind { None, Banner, Music } kind = Kind::None;
  enum class Source { None, Multipart, RawBody, Upload } source = Source::None;
  String filename;
  AsyncWebServerRequest *owner = nullptr;
  bool active = false;
  bool authenticated = false;
  bool authFailed = false;
  bool rejected = false;
  String rejectReason;
  bool streamActive = false;
  bool streamFinished = false;
  bool bodySeen = false;
  uint32_t startedAt = 0;
  size_t expectedTotal = 0;
  size_t bytesReceived = 0;
  size_t lastLoggedBytes = 0;
};

struct FirmwareOtaUpload {
  bool active = false;
  bool rejected = false;
  bool finalized = false;
  String rejectReason;
  String digest;
  size_t total = 0;
  size_t received = 0;
  MD5Builder md5;
};

struct RestoreUpload {
  File file;
  AsyncWebServerRequest *owner = nullptr;
  bool active = false;
  bool authenticated = false;
  bool authFailed = false;
  bool rejected = false;
  String rejectReason;
  size_t received = 0;
  uint32_t startedAt = 0;
};

PortalAssetUpload gPortalUpload;
FirmwareOtaUpload gOtaUpload;
RestoreUpload gRestoreUpload;

bool isBinFirmware(const String &filename) {
  const String lower = filename;
  return lower.endsWith(".bin") && !lower.endsWith(".ino");
}

}  // namespace

// ── ApiServer::begin ──────────────────────────────────────────────────────────

void ApiServer::begin(StorageManager       *storage,
                      AuthManager          *auth,
                      SessionManager       *sessions,
                      PromoManager         *promos,
                      VoucherManager       *vouchers,
                      CoinManager          *coin,
                      RouterPlatform       *router,
                      Logger               *logger,
                      EventBus             *events,
                      EthernetManager      *eth,
                      PortalSessionManager *portalSessions,
                      PortalConfigManager  *portalConfig,
                      AssetManager         *assets,
                      RgbController        *rgb,
                      SystemHealthService  *health,
                      BuildMetadata        *build,
                      InstallationStateManager *installation,
                      ManagementApManager  *mgmtAp,
                      ManagementApLifecycle *mgmtApLifecycle,
                      NetworkSettingsManager *networkSettings,
                      RouterProvisioningWorker *routerWorker,
                      FactoryResetWorker *factoryReset,
                      ExternalAccessPointManager *accessPoints,
                      ContentFilterManager *contentFilter,
                      GamingPriorityManager *gamingPriority) {
  _server         = nullptr;
  _storage        = storage;
  _auth           = auth;
  _sessions       = sessions;
  _promos         = promos;
  _vouchers       = vouchers;
  _coin           = coin;
  _router         = router;
  _logger         = logger;
  _events         = events;
  _eth            = eth;
  _portalSessions = portalSessions;
  _portalConfig   = portalConfig;
  _assets         = assets;
  _rgb            = rgb;
  _health         = health;
  _build          = build;
  _installation   = installation;
  _mgmtAp         = mgmtAp;
  _mgmtApLifecycle = mgmtApLifecycle;
  _networkSettings = networkSettings;
  _routerWorker    = routerWorker;
  _factoryReset    = factoryReset;
  _accessPoints    = accessPoints;
  _contentFilter   = contentFilter;
  _gamingPriority  = gamingPriority;
  _backup.begin(_storage, _logger, _auth, _portalConfig, _assets, _installation);
}

// ── Response helpers ──────────────────────────────────────────────────────────

bool ApiServer::requireAuth(AsyncWebServerRequest *req,
                            AuthRequirement requirement) {
  String cookie = req->hasHeader("Cookie")
                    ? req->getHeader("Cookie")->value()
                    : String("");
  if (!_auth || !_auth->isAuthenticated(cookie)) {
    sendError(req, 401, "Authentication required", "UNAUTHENTICATED");
    return false;
  }
  if (requirement == AuthRequirement::OwnerOnly &&
      !_auth->isAuthenticatedWithRole(cookie, AuthRole::Owner)) {
    sendError(req, 403, "Owner privileges required", "OWNER_REQUIRED");
    return false;
  }
  if (requirement == AuthRequirement::FullAccess &&
      _auth->mustChangePassword()) {
    sendError(req, 403, "Password change required", "PASSWORD_CHANGE_REQUIRED");
    return false;
  }
  return true;
}

bool ApiServer::requireOwnerAuth(AsyncWebServerRequest *req) {
  return requireAuth(req, AuthRequirement::OwnerOnly);
}

void ApiServer::addCorsHeaders(AsyncWebServerResponse *res) {
  WebResponse::addCorsHeaders(res);
}

void ApiServer::sendOk(AsyncWebServerRequest *req, JsonDocument &data,
                       const String &message) {
  sendOk(req, data, 200, message);
}

void ApiServer::sendOk(AsyncWebServerRequest *req, JsonDocument &data, int httpStatus,
                       const String &message) {
  logRequest(req, "api-ok");
  PsramJsonDocument envHeap;
  JsonDocument &env = envHeap.doc();
  env["success"] = true;
  env["data"] = data;
  env["message"] = message;
  sendJsonResponse(req, httpStatus, env);
}

void ApiServer::sendOk(AsyncWebServerRequest *req, const String &message) {
  logRequest(req, "api-ok");
  PsramJsonDocument envHeap;
  JsonDocument &env = envHeap.doc();
  env["success"] = true;
  env["data"]["ok"] = true;
  env["message"] = message;
  sendJsonResponse(req, 200, env);
}

void ApiServer::sendOkLiveness(AsyncWebServerRequest *req, JsonDocument &data) {
  logRequest(req, "api-ok-liveness");
  PsramJsonDocument envHeap;
  JsonDocument &env = envHeap.doc();
  env["success"] = true;
  env["data"] = data;
  env["message"] = "OK";
  sendJsonResponse(req, 200, env, nullptr, true);
}

void ApiServer::sendError(AsyncWebServerRequest *req, int status,
                          const String &error, const String &code) {
  logRequest(req, "api-error");
  PsramJsonDocument envHeap;
  JsonDocument &doc = envHeap.doc();
  doc["success"] = false;
  doc["error"] = error;
  doc["code"] = code;
  sendJsonResponse(req, status, doc);
}

void ApiServer::sendWorkerResult(AsyncWebServerRequest *req,
                                 const RouterProvisioningWorker::Result &result) {
  logRequest(req, result.ok ? "api-ok" : "api-error");
  if (!WebResponse::ensureEthTransmitHeadroom(req, "worker-result")) return;
  AsyncWebServerResponse *res =
      req->beginResponse(result.httpStatus, "application/json", result.body);
  addCorsHeaders(res);
  res->addHeader("Cache-Control", "no-store");
  req->send(res);
}

void ApiServer::sendAdminJobAccepted(AsyncWebServerRequest *req, uint32_t jobId,
                                     const char *typeLabel) {
  PsramJsonDocument envHeap;
  JsonDocument &doc = envHeap.doc();
  doc["success"] = true;
  JsonObject data = doc.createNestedObject("data");
  data["jobId"] = jobId;
  data["state"] = "queued";
  if (typeLabel && typeLabel[0]) data["type"] = typeLabel;
  logRequest(req, "admin-router-job-accepted");
  Serial.printf("[admin-router-api] accepted job=%u type=%s\n",
                static_cast<unsigned>(jobId), typeLabel ? typeLabel : "?");
  sendJsonResponse(req, 202, doc);
}

void ApiServer::enqueueContentFilterSyncOrError(
    AsyncWebServerRequest *req, const char *okMessage,
    ContentFilterManager::SyncEnqueueStatus mutationStatus,
    const char *extraFieldKey, const String *extraFieldValue) {
  if (mutationStatus != ContentFilterManager::SyncEnqueueStatus::Ok) {
    sendError(req, ContentFilterManager::syncEnqueueHttpStatus(mutationStatus),
              ContentFilterManager::syncEnqueueMessage(mutationStatus),
              ContentFilterManager::syncEnqueueCode(mutationStatus));
    return;
  }
  if (!_contentFilter || !_routerWorker) {
    sendError(req, 503, "Content filter unavailable", "INTERNAL_ERROR");
    return;
  }
  HeapJsonDocument payload(RenzFiConfig::JSON_DOC_MEDIUM);
  _contentFilter->buildSyncPayload(payload.doc());
  String requestJson;
  serializeJson(payload.doc(), requestJson);
  const auto outcome = _routerWorker->enqueueContentFilterSync(requestJson);
  if (!outcome.accepted) {
    sendError(req, 503, outcome.rejectMessage ? outcome.rejectMessage
                                              : "Router worker busy",
              outcome.rejectCode ? outcome.rejectCode : "WORKER_BUSY");
    return;
  }
  PsramJsonDocument dataHeap;
  JsonObject data = dataHeap.doc().to<JsonObject>();
  data["jobId"] = outcome.jobId;
  data["state"] = "queued";
  if (extraFieldKey && extraFieldValue) {
    data[extraFieldKey] = *extraFieldValue;
  }
  sendOk(req, dataHeap.doc(), 202, okMessage);
}

void ApiServer::enqueueGamingPrioritySyncOrError(AsyncWebServerRequest *req,
                                                 const char *okMessage) {
  if (!_gamingPriority || !_routerWorker) {
    sendError(req, 503, "Gaming priority unavailable", "INTERNAL_ERROR");
    return;
  }
  HeapJsonDocument payload(RenzFiConfig::JSON_DOC_SMALL);
  _gamingPriority->buildSyncPayload(payload.doc());
  String requestJson;
  serializeJson(payload.doc(), requestJson);
  const auto outcome = _routerWorker->enqueueGamingPrioritySync(requestJson);
  if (!outcome.accepted) {
    sendError(req, 503, outcome.rejectMessage ? outcome.rejectMessage
                                              : "Router worker busy",
              outcome.rejectCode ? outcome.rejectCode : "WORKER_BUSY");
    return;
  }
  PsramJsonDocument dataHeap;
  JsonObject data = dataHeap.doc().to<JsonObject>();
  data["jobId"] = outcome.jobId;
  data["state"] = "queued";
  sendOk(req, dataHeap.doc(), 202, okMessage);
}

bool ApiServer::pollWorkerJobOrError(AsyncWebServerRequest *req,
                                     const char *urlPrefix, const char *opType,
                                     const char *notFoundMessage,
                                     RouterProvisioningWorker::JobRecord &job) {
  if (!_routerWorker) {
    sendError(req, 503, "Router worker unavailable", "INTERNAL_ERROR");
    return false;
  }
  String tail = req->url().substring(strlen(urlPrefix));
  tail.trim();
  const uint32_t jobId = static_cast<uint32_t>(tail.toInt());
  if (jobId == 0 || !_routerWorker->pollJob(jobId, job) ||
      job.opType != opType) {
    sendError(req, 404, notFoundMessage, "NOT_FOUND");
    return false;
  }
  return true;
}

void ApiServer::sendWorkerJobPollOk(
    AsyncWebServerRequest *req,
    const RouterProvisioningWorker::JobRecord &job) {
  PsramJsonDocument dataHeap;
  JsonObject data = dataHeap.doc().to<JsonObject>();
  data["jobId"] = job.jobId;
  data["state"] = RouterProvisioningWorker::jobStateLabel(job.state);
  const bool terminal =
      job.state == RouterProvisioningWorker::JobState::Completed ||
      job.state == RouterProvisioningWorker::JobState::Failed;
  if (terminal) {
    data["ok"] = job.result.ok;
    data["httpStatus"] = job.result.httpStatus;
    if (job.result.body.length() > 0) data["result"] = job.result.body;
  }
  sendOk(req, dataHeap.doc());
}

String ApiServer::getBody(AsyncWebServerRequest *req) {
  if (req->_tempObject) return String(static_cast<char *>(req->_tempObject));
  return "";
}

void ApiServer::logRequest(AsyncWebServerRequest *req, const char *handler) {
  WebRequestDiagnostics::logRequest(req, handler);
}


void ApiServer::appendAssetInfoJson(JsonObject obj,
                                    const AssetInfo &info) const {
  obj["type"] = assetTypeLabel(info.type);
  obj["filename"] = info.filename;
  obj["mimeType"] = info.mimeType;
  obj["size"] = info.size;
  obj["lastModified"] = info.lastModified;
  obj["checksum"] = info.checksum;
  obj["storageLocation"] = assetStorageLocationLabel(info.storageLocation);
  obj["path"] = info.path;
  if (info.slot > 0) obj["slot"] = info.slot;
}

void ApiServer::appendUploadResultJson(
    JsonObject root, const AssetOperationResult &result) const {
  root["revision"] = result.revisionUpdated;
  root["storedPath"] = result.storedPath;
  root["bytesWritten"] = result.bytesWritten;
  if (result.warning.length() > 0) root["warning"] = result.warning;
  if (result.success && result.asset.present()) {
    JsonObject asset = root["asset"].to<JsonObject>();
    appendAssetInfoJson(asset, result.asset);
  }
}

void ApiServer::sendSdFile(AsyncWebServerRequest *req, const char *sdPath,
                           const char *filename) {
  if (!_storage || !_storage->healthy() || !_storage->sdIoAllowed()) {
    sendError(req, 503, "SD storage unavailable", "SD_UNAVAILABLE");
    return;
  }
  if (!SD.exists(sdPath)) {
    sendError(req, 404, "File not found", "FILE_NOT_FOUND");
    return;
  }
  WebResponse::serveDownload(req, SD, sdPath, filename, "application/json");
}


#define RENZFI_APPLIANCE_GATE(req) \
  do { \
    if (!HttpPlaneGate::ensureAppliancePlane((req))) return; \
  } while (0)

// Privileged Admin / system APIs — production plane AND (Management LAN OR
// authenticated Admin/Operator session). Hotspot guests are never trusted by
// subnet alone; AuthManager session is required after login.
#define RENZFI_PROD_GATE(req) \
  do { \
    if (!HttpPlaneGate::ensureAdminAccess((req))) return; \
  } while (0)

// Customer captive portal APIs — any production-plane client (incl. guest).
#define RENZFI_PORTAL_GATE(req) \
  do { \
    if (!HttpPlaneGate::ensureProductionPlane((req))) return; \
  } while (0)

// ── Route registration ────────────────────────────────────────────────────────


void ApiServer::registerSetupRoutes(WebServerManager &web,
                                    SetupProvisioningManager *setupProvisioning,
                                    RouterProvisioningManager *routerProvisioning) {
  _server = &web.routeServer();
  _web = &web;
  _setupProvisioning = setupProvisioning;
  _routerProvisioning = routerProvisioning;
  if (!_server) return;

  Serial.println("[web] ApiServer registering setup-plane routes");

  // ── CORS preflight (OPTIONS /api/) ───────────────────────────────────────
  // Must remain reachable from Hotspot guests (MikroTik-hosted portal origin).
  _server->on("/api/", HTTP_OPTIONS, [this](AsyncWebServerRequest *req) {
    RENZFI_PORTAL_GATE(req);
    AsyncWebServerResponse *res = req->beginResponse(204, "text/plain", "");
    addCorsHeaders(res);
    req->send(res);
  });

  // ── Health ───────────────────────────────────────────────────────────────
  _server->on("/api/health", HTTP_GET, [this](AsyncWebServerRequest *req) {
    RENZFI_APPLIANCE_GATE(req);
    String cookie = req->hasHeader("Cookie")
                      ? req->getHeader("Cookie")->value()
                      : String("");

    // Hotspot guests may bootstrap Admin login (session check + ok) without
    // receiving infrastructure inventory (ethernet/router/coin/storage).
    if (HttpPlaneGate::isCustomerPortalClient(req)) {
      DynamicJsonDocument data(RenzFiConfig::JSON_DOC_SMALL);
      data["ok"] = true;
      data["httpPlane"] = HttpPlaneGate::planeLabel(req);
      data["accessClass"] = HttpPlaneGate::accessClassLabel(req);
      data["session"]["authenticated"] = _auth->isAuthenticated(cookie);
      data["session"]["mustChangePassword"] = _auth->mustChangePassword();
      data["session"]["firstBootCompleted"] = _auth->firstBootCompleted();
      data["session"]["role"] =
          _auth->isAuthenticated(cookie)
              ? authRoleLabel(_auth->sessionRole(cookie))
              : "none";
      if (_auth->isAuthenticated(cookie) &&
          _auth->sessionRole(cookie) == AuthRole::Operator) {
        JsonArray perms = data["session"]["permissions"].to<JsonArray>();
        _auth->fillOperatorPermissions(perms);
      }
      sendOkLiveness(req, data);
      return;
    }

    // Appliance health: PSRAM JSON pool (same DMA rationale as /api/status).
    PsramJsonDocument dataHeap;
    JsonDocument &data = dataHeap.doc();
    data["ok"]                            = true;
    data["httpPlane"]                     = HttpPlaneGate::planeLabel(req);
    data["storage"]["ok"]                 = _storage->healthy() || _storage->usingFallback();
    _storage->fillStorageStatus(data["storage"].to<JsonObject>());
    data["storage"]["spiffsReady"]        = _storage->isSpiffsMounted();

    if (_installation) {
      PsramJsonDocument installHeap;
      JsonDocument &installDoc = installHeap.doc();
      _installation->fillStatus(installDoc);
      data["installation"].set(installDoc.as<JsonObjectConst>());
      data["installationState"] = data["installation"]["state"];
    } else {
      data["installationState"] = nullptr;
    }

    ProductionHandoff::Context handoffCtx;
    handoffCtx.auth               = _auth;
    handoffCtx.setupProvisioning  = _setupProvisioning;
    handoffCtx.installation       = _installation;
    handoffCtx.router             = _router;
    handoffCtx.routerProvisioning = _routerProvisioning;
    handoffCtx.eth                = _eth;
    handoffCtx.storage            = _storage;
    handoffCtx.web                = _web;
    const ProductionHandoff::Status handoff =
        ProductionHandoff::evaluate(handoffCtx);
    ProductionHandoff::fillHealthFields(data.as<JsonObject>(), handoff);

    // Non-secret Ethernet status — no router credentials here. Safe to
    // expose on this public/unauthenticated endpoint so the setup wizard
    // and mobile app can read link/address state before any session exists.
    JsonObject ethObj = data["ethernet"].to<JsonObject>();
    ethObj["driverReady"] = _eth && _eth->driverReady();
    ethObj["link"]        = _eth && _eth->linkUp();
    ethObj["hasIp"]       = _eth && _eth->hasIp();
    ethObj["mode"]        = _eth ? _eth->addressModeLabel() : "dhcp";
    ethObj["ip"]          = _eth ? _eth->ip() : "";
    ethObj["gateway"]     = _eth ? _eth->gateway() : "";
    ethObj["mac"]         = _eth ? _eth->macAddress() : "";

    JsonObject routerObj = data["router"].to<JsonObject>();
    if (_router) {
      _router->fillHealthStatus(routerObj);
    } else {
      routerObj["configured"] = false;
      routerObj["driverId"] = nullptr;
      routerObj["status"] = "unavailable";
    }

    if (_portalConfig) {
      JsonObject portalObj = data["portal"].to<JsonObject>();
      portalObj["revision"]   = _portalConfig->revision();
      portalObj["hasBanner"]  = _portalConfig->hasCustomBanner();
      portalObj["hasMusic"]   = _portalConfig->hasCustomMusic();
      portalObj["assetsReady"] = _assets && _assets->ready();
    }

    JsonObject coinObj = data["coin"].to<JsonObject>();
    if (_coin) {
      coinObj["enabled"] = true;
      _coin->fillCoinStatus(coinObj);
    } else {
      coinObj["enabled"] = false;
    }

    data["uptimeSeconds"] = millis() / 1000U;
    data["serverTimeMs"]  = millis();

    data["session"]["authenticated"]      = _auth->isAuthenticated(cookie);
    data["session"]["mustChangePassword"] = _auth->mustChangePassword();
    data["session"]["firstBootCompleted"] = _auth->firstBootCompleted();
    // Exposes the caller's role ("owner" / "operator") so the admin
    // dashboard can hide Administrator-only menus/routes for Operator
    // sessions (Rule #6/#7). Absent/"none" when not authenticated.
    data["session"]["role"] =
        _auth->isAuthenticated(cookie)
            ? authRoleLabel(_auth->sessionRole(cookie))
            : "none";
    if (_auth->isAuthenticated(cookie) &&
        _auth->sessionRole(cookie) == AuthRole::Operator) {
      JsonArray perms = data["session"]["permissions"].to<JsonArray>();
      _auth->fillOperatorPermissions(perms);
    }
    DeviceIdentity::fillRuntimeProfile(data["device"].to<JsonObject>());
    data["deviceId"]   = data["device"]["deviceId"];
    data["deviceName"] = data["device"]["friendlyName"];
    data["version"]    = RenzFiConfig::FIRMWARE_VERSION;
    if (_build) {
      _build->fillJson(data["build"].to<JsonObject>());
    }

    JsonObject mgmtApObj = data["managementAp"].to<JsonObject>();
    if (_mgmtAp) {
      _mgmtAp->fillStatus(mgmtApObj);
      if (_mgmtApLifecycle) {
        _mgmtApLifecycle->patchStatus(mgmtApObj);
      }
    } else {
      mgmtApObj["enabled"]          = false;
      mgmtApObj["running"]          = false;
      mgmtApObj["mode"]             = "disabled";
      mgmtApObj["ssid"]             = nullptr;
      mgmtApObj["ip"]               = nullptr;
    }

    sendOkLiveness(req, data);
  });

  // ── Auth ─────────────────────────────────────────────────────────────────
  _server->on(
      "/api/auth/login", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_APPLIANCE_GATE(req);
        // CPU JSON — PSRAM. Login previously bypassed sendOk (Set-Cookie) and
        // allocated 3× JSON_DOC_SMALL + Arduino String serialize on INTERNAL
        // (ALWAYSINTERNAL=4096). That overlaps the dashboard fan-out / SPA TX.
        PsramJsonDocument bodyHeap;
        JsonDocument &body = bodyHeap.doc();
        if (req->_tempObject) {
          deserializeJson(body, static_cast<const char *>(req->_tempObject));
        }

        PsramJsonDocument responseHeap;
        JsonDocument &response = responseHeap.doc();
        String              username   = body["username"]   | "";
        String              password   = body["password"]   | "";
        bool                rememberIp = body["rememberIp"] | false;
        String              setCookieVal;

        if (!_auth->login(username, password, rememberIp, response, setCookieVal)) {
          sendError(req, 401, "Invalid password", "INVALID_CREDENTIALS");
          return;
        }
        PsramJsonDocument envelopeHeap;
        JsonDocument &envelope = envelopeHeap.doc();
        envelope["success"] = true;
        envelope["data"].set(response.as<JsonObject>());
        envelope["message"] = "OK";
        sendJsonResponse(req, 200, envelope, &setCookieVal);
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/auth/logout", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_APPLIANCE_GATE(req);
        String cookie = req->hasHeader("Cookie")
                          ? req->getHeader("Cookie")->value()
                          : String("");
        _auth->logout(cookie);
        AsyncWebServerResponse *res = req->beginResponse(
            200, "application/json",
            "{\"success\":true,\"data\":{\"ok\":true},\"message\":\"OK\"}");
        addCorsHeaders(res);
        res->addHeader("Set-Cookie",
                       String(RenzFiConfig::SESSION_COOKIE) +
                           "=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0");
        res->addHeader("Cache-Control", "no-store");
        req->send(res);
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/auth/change-password", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_APPLIANCE_GATE(req);
        if (!requireAuth(req, AuthRequirement::Session)) return;
        DynamicJsonDocument body(256);
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        if (_auth->changePassword(body["oldPassword"] | "",
                                  body["newPassword"] | "")) {
          DynamicJsonDocument data(RenzFiConfig::JSON_DOC_SMALL);
          data["ok"] = true;
          data["mustChangePassword"] = _auth->mustChangePassword();
          data["firstBootCompleted"] = _auth->firstBootCompleted();
          sendOk(req, data);
        } else
          sendError(req, 400, "Unable to change password",
                    "PASSWORD_CHANGE_FAILED");
      },
      nullptr, bodyCollect);

  // Setup Unlock Password — owner-only. Returns decrypted credential from the
  // RAM-resident protected blob. Never returns the hash. Never logs plaintext.
  _server->on("/api/settings/setup-unlock", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_APPLIANCE_GATE(req);
                if (!requireOwnerAuth(req)) return;
                DynamicJsonDocument data(768);
                const bool configured =
                    _setupProvisioning &&
                    _setupProvisioning->setupUnlockConfigured();
                data["configured"] = configured;
                String recovered;
                const bool recoverable =
                    configured &&
                    _setupProvisioning->recoverSetupUnlockPassword(recovered);
                data["recoverable"] = recoverable;
                if (recoverable) {
                  data["password"] = recovered;
                }
                recovered = "";
                sendOk(req, data);
              });

  _server->on(
      "/api/settings/setup-unlock", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_APPLIANCE_GATE(req);
        if (!requireOwnerAuth(req)) return;
        if (!_setupProvisioning) {
          sendError(req, 503, "Setup provisioning unavailable",
                    "SETUP_PROVISIONING_UNAVAILABLE");
          return;
        }
        DynamicJsonDocument body(384);
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        const String currentPassword = body["currentPassword"] | "";
        const String newPassword     = body["newPassword"] | "";
        const String confirmPassword = body["confirmPassword"] | "";
        if (newPassword != confirmPassword) {
          sendError(req, 400, "New password confirmation does not match",
                    "SETUP_UNLOCK_PASSWORD_MISMATCH");
          return;
        }
        String errorCode;
        if (!_setupProvisioning->changeSetupUnlockPassword(
                currentPassword, newPassword, errorCode)) {
          const int status =
              (errorCode == "SETUP_UNLOCK_INVALID") ? 403 : 400;
          const char *message =
              (errorCode == "SETUP_UNLOCK_INVALID")
                  ? "Current Setup Unlock Password is incorrect"
              : (errorCode == "SETUP_UNLOCK_PASSWORD_TOO_SHORT")
                    ? "New Setup Unlock Password must be at least 8 characters"
              : (errorCode == "SETUP_UNLOCK_PASSWORD_UNCHANGED")
                    ? "New password must differ from the current password"
                    : "Unable to change Setup Unlock Password";
          sendError(req, status, message,
                    errorCode.isEmpty() ? "SETUP_UNLOCK_CHANGE_FAILED"
                                        : errorCode.c_str());
          return;
        }
        DynamicJsonDocument data(128);
        data["ok"]         = true;
        data["configured"] = true;
        sendOk(req, data);
      },
      nullptr, bodyCollect);

  // Optional Operator account — same AuthManager NVS store as Setup createOperator.
  _server->on("/api/settings/operator", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_APPLIANCE_GATE(req);
                if (!requireOwnerAuth(req)) return;
                DynamicJsonDocument data(512);
                const bool configured =
                    _auth && _auth->hasOperatorNvsCredentials();
                data["configured"] = configured;
                if (configured) {
                  data["username"] = _auth->operatorUsername();
                  JsonArray perms = data["permissions"].to<JsonArray>();
                  _auth->fillOperatorPermissions(perms);
                }
                sendOk(req, data);
              });

  _server->on(
      "/api/settings/operator", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_APPLIANCE_GATE(req);
        if (!requireOwnerAuth(req)) return;
        if (!_auth) {
          sendError(req, 503, "Authentication service unavailable",
                    "AUTH_UNAVAILABLE");
          return;
        }
        DynamicJsonDocument body(768);
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);

        // Update permissions on an existing operator (no password required).
        if (_auth->hasOperatorNvsCredentials() &&
            body["permissions"].is<JsonArrayConst>() &&
            !body["username"].is<const char *>() &&
            !body["password"].is<const char *>()) {
          String csv;
          for (JsonVariantConst v : body["permissions"].as<JsonArrayConst>()) {
            const char *p = v.as<const char *>();
            if (!p || !p[0]) continue;
            if (!csv.isEmpty()) csv += ',';
            csv += p;
          }
          String errorCode;
          if (!_auth->setOperatorPermissions(csv, errorCode)) {
            sendError(req, 400,
                      errorCode.isEmpty() ? "Unable to update permissions"
                                          : errorCode.c_str(),
                      errorCode.isEmpty() ? "OPERATOR_PERMS_FAILED"
                                          : errorCode.c_str());
            return;
          }
          DynamicJsonDocument data(256);
          data["ok"] = true;
          data["configured"] = true;
          data["username"] = _auth->operatorUsername();
          JsonArray perms = data["permissions"].to<JsonArray>();
          _auth->fillOperatorPermissions(perms);
          sendOk(req, data);
          return;
        }

        if (_auth->hasOperatorNvsCredentials()) {
          sendError(req, 409, "An operator account already exists",
                    "OPERATOR_ALREADY_EXISTS");
          return;
        }
        const String username        = body["username"] | "";
        const String password        = body["password"] | "";
        const String confirmPassword = body["confirmPassword"] | "";
        if (username.length() < 3) {
          sendError(req, 400, "Operator username is required",
                    "USERNAME_INVALID");
          return;
        }
        if (password.length() < 8) {
          sendError(req, 400, "Password must be at least 8 characters",
                    "PASSWORD_TOO_SHORT");
          return;
        }
        if (password != confirmPassword) {
          sendError(req, 400, "Password and confirmation do not match",
                    "PASSWORD_MISMATCH");
          return;
        }
        if (username == _auth->ownerUsername()) {
          sendError(req, 400, "Operator username must differ from the owner",
                    "USERNAME_CONFLICT");
          return;
        }
        String errorCode;
        // Do not invalidate the owner's Admin session when creating operator.
        if (!_auth->provisionOperatorCredentials(username, password, errorCode,
                                                 false)) {
          sendError(req, 400,
                    errorCode.isEmpty() ? "Unable to create operator"
                                        : errorCode.c_str(),
                    errorCode.isEmpty() ? "OPERATOR_CREATE_FAILED"
                                        : errorCode.c_str());
          return;
        }
        if (body["permissions"].is<JsonArrayConst>()) {
          String csv;
          for (JsonVariantConst v : body["permissions"].as<JsonArrayConst>()) {
            const char *p = v.as<const char *>();
            if (!p || !p[0]) continue;
            if (!csv.isEmpty()) csv += ',';
            csv += p;
          }
          String permsError;
          _auth->setOperatorPermissions(csv, permsError);
        }
        DynamicJsonDocument data(256);
        data["ok"]         = true;
        data["configured"] = true;
        data["username"]   = username;
        JsonArray perms = data["permissions"].to<JsonArray>();
        _auth->fillOperatorPermissions(perms);
        sendOk(req, data);
      },
      nullptr, bodyCollect);


  // ── Network status (Management AP + Ethernet) ─────────────────────────────
  auto networkStatus = [this](AsyncWebServerRequest *req) {
    RENZFI_APPLIANCE_GATE(req);
    if (!requireAuth(req)) return;
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_SMALL);
    NetworkStatusModel::fill(data.to<JsonObject>(), _eth, _mgmtAp, _mgmtApLifecycle);
    sendOk(req, data);
  };
  _server->on("/api/system/network", HTTP_GET, networkStatus);
  _server->on("/api/system/wifi",    HTTP_GET, networkStatus);  // backward-compat

  auto mgmtApStart = [this](AsyncWebServerRequest *req) {
    RENZFI_APPLIANCE_GATE(req);
    if (!requireAuth(req)) return;
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_SMALL);
    const bool ok = _mgmtApLifecycle && _mgmtApLifecycle->startMaintenance();
    if (!ok) {
      sendError(req, 400, "Unable to start maintenance access point",
                "MGMT_AP_START_FAILED");
      return;
    }
    if (_mgmtAp) {
      _mgmtAp->fillStatus(data["managementAp"].to<JsonObject>());
      if (_mgmtApLifecycle) {
        _mgmtApLifecycle->patchStatus(data["managementAp"].to<JsonObject>());
      }
    }
    sendOk(req, data, "Maintenance access point started");
  };
  _server->on("/api/system/management-ap/start", HTTP_POST, mgmtApStart);

  auto mgmtApStop = [this](AsyncWebServerRequest *req) {
    RENZFI_APPLIANCE_GATE(req);
    if (!requireAuth(req)) return;
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_SMALL);
    const bool ok = _mgmtApLifecycle && _mgmtApLifecycle->stopMaintenance();
    if (!ok) {
      sendError(req, 400, "Unable to stop maintenance access point",
                "MGMT_AP_STOP_FAILED");
      return;
    }
    if (_mgmtAp) {
      _mgmtAp->fillStatus(data["managementAp"].to<JsonObject>());
      if (_mgmtApLifecycle) {
        _mgmtApLifecycle->patchStatus(data["managementAp"].to<JsonObject>());
      }
    }
    sendOk(req, data, "Maintenance access point stopped");
  };
  _server->on("/api/system/management-ap/stop", HTTP_POST, mgmtApStop);

  _server->on(
      "/api/system/management-ap/temporary", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_APPLIANCE_GATE(req);
        if (!requireAuth(req)) return;
        DynamicJsonDocument body(RenzFiConfig::JSON_DOC_SMALL);
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        const uint32_t durationSeconds =
            body["durationSeconds"] | ManagementApConfig::MAINTENANCE_TIMEOUT_SECONDS;
        DynamicJsonDocument data(RenzFiConfig::JSON_DOC_SMALL);
        if (!_mgmtApLifecycle ||
            !_mgmtApLifecycle->startTemporary(durationSeconds)) {
          sendError(req, 400,
                    "Unable to start temporary maintenance access point",
                    "MGMT_AP_TEMPORARY_FAILED");
          return;
        }
        if (_mgmtAp) {
          _mgmtAp->fillStatus(data["managementAp"].to<JsonObject>());
          if (_mgmtApLifecycle) {
            _mgmtApLifecycle->patchStatus(data["managementAp"].to<JsonObject>());
          }
        }
        data["durationSeconds"] = durationSeconds;
        sendOk(req, data, "Temporary maintenance access point started");
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/system/management-ap/post-setup", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_APPLIANCE_GATE(req);
        if (!requireAuth(req)) return;
        DynamicJsonDocument body(RenzFiConfig::JSON_DOC_SMALL);
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        const bool keepEnabled = body["keepEnabled"] | false;
        DynamicJsonDocument data(RenzFiConfig::JSON_DOC_SMALL);
        if (!_mgmtApLifecycle ||
            !_mgmtApLifecycle->applyPostSetupPreference(keepEnabled)) {
          sendError(req, 400, "Unable to apply Management AP preference",
                    "MGMT_AP_POST_SETUP_FAILED");
          return;
        }
        if (_mgmtAp) {
          _mgmtAp->fillStatus(data["managementAp"].to<JsonObject>());
          if (_mgmtApLifecycle) {
            _mgmtApLifecycle->patchStatus(data["managementAp"].to<JsonObject>());
          }
        }
        data["keepEnabledAfterSetup"] = keepEnabled;
        sendOk(req, data, keepEnabled
                              ? "Management access point kept enabled"
                              : "Management access point disabled");
      },
      nullptr, bodyCollect);


}

void ApiServer::registerProductionRoutes(WebServerManager &web) {
  _server = &web.routeServer();
  if (!_server) return;

  Serial.println("[web] ApiServer registering production-plane routes");

  // ── Dashboard status ──────────────────────────────────────────────────────
  _server->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireAuth(req)) return;
    const uint32_t t0 = millis();
    // PSRAM pool: INTERNAL DynamicJsonDocument(8192) fragments the DMA heap
    // W5500 needs for SPI priv TX/RX (~1490 B). See ADMIN_DASHBOARD_DMA_GURU_FORENSIC.
    PsramJsonDocument dataHeap;
    PsramJsonDocument salesTodayHeap;
    PsramJsonDocument salesWeekHeap;
    PsramJsonDocument salesMonthHeap;
    JsonDocument &data = dataHeap.doc();
    JsonDocument &salesToday = salesTodayHeap.doc();
    JsonDocument &salesWeek = salesWeekHeap.doc();
    JsonDocument &salesMonth = salesMonthHeap.doc();
    _sessions->salesToday(salesToday);
    _sessions->salesWeek(salesWeek);
    _sessions->salesMonth(salesMonth);
    data["server"]["ok"]                 = true;
    data["server"]["uptimeSeconds"]      = millis() / 1000;
    data["database"]["ok"]               = _storage->healthy() || _storage->usingFallback();
    data["database"]["path"]             = _storage->usingFallback()
                                               ? "SPIFFS Fallback"
                                               : "SD";
    data["sales"]["today"]["amount"]     = salesToday["amount"]   | 0;
    data["sales"]["today"]["sessions"]   = salesToday["sessions"] | 0;
    data["sales"]["weekly"]["amount"]    = salesWeek["amount"]    | 0;
    data["sales"]["weekly"]["sessions"]  = salesWeek["sessions"]  | 0;
    data["sales"]["monthly"]["amount"]   = salesMonth["amount"]   | 0;
    data["sales"]["monthly"]["sessions"] = salesMonth["sessions"] | 0;
    int activeCount = 0;
    int pausedCount = 0;
    _sessions->cachedActiveUserStats(activeCount, pausedCount);
    data["activeUsers"]["count"]         = activeCount;
    data["activeUsers"]["paused"]        = pausedCount;
    data["activeUsers"]["idle"]          = 0;

    String routerHost;
    bool routerConfigured = false;
    if (_router) {
      routerConfigured = _router->cachedRouterConfigured();
      routerHost = _router->cachedRouterHost();
    }

    // Configured ≠ Online. Host/credentials present means configured only.
    data["mikrotik"]["configured"] = routerConfigured;
    data["mikrotik"]["ok"]         = routerConfigured;  // legacy: "Configured" UI
    data["mikrotik"]["host"]       = routerHost;
    data["mikrotik"]["latencyMs"]  = 0;
    data["mikrotik"]["connectivity"] = "unknown";
    data["mikrotik"]["lastSuccessfulContactAt"] = "";
    data["mikrotik"]["lastContactError"]        = "";

    if (_router && _router->cachePopulated()) {
      _router->fillHealthStatus(data["mikrotik"].to<JsonObject>());
      // fillHealthStatus may overwrite ok/status — restore configured semantics.
      data["mikrotik"]["configured"] = routerConfigured;
      data["mikrotik"]["ok"]         = routerConfigured;
      data["mikrotik"]["host"]       = routerHost;
    }

    // Observational connectivity / hotspot / WAN from last RouterOS Sync/Test.
    data["hotspot"]["ok"]     = false;
    data["hotspot"]["status"] = "unknown";
    data["internet"]["ok"]               = false;
    data["internet"]["known"]            = false;
    data["internet"]["latencyMs"]        = 0;
    data["wan"]["known"]         = false;
    data["wan"]["interface"]     = "";
    data["wan"]["link"]          = "unknown";
    data["wan"]["dhcp"]          = "unknown";
    data["wan"]["ip"]            = "";
    data["wan"]["gateway"]       = "";
    data["wan"]["defaultRoute"]  = "unknown";
    data["wan"]["internet"]      = "unknown";
    data["wan"]["dns"]           = "unknown";
    data["wan"]["note"]          = "";
    if (_router) {
      PsramJsonDocument cacheStatusHeap;
      JsonDocument &cacheStatus = cacheStatusHeap.doc();
      JsonObject statusObj = cacheStatus.to<JsonObject>();
      _router->fillRouterCacheStatus(statusObj);
      if (statusObj["observation"].is<JsonObjectConst>()) {
        JsonObjectConst obs = statusObj["observation"].as<JsonObjectConst>();
        const char *connectivity = obs["connectivity"] | "unknown";
        data["mikrotik"]["connectivity"] = connectivity;
        data["mikrotik"]["lastSuccessfulContactAt"] =
            obs["lastSuccessfulContactAt"] | "";
        data["mikrotik"]["lastContactError"] = obs["lastContactError"] | "";

        const char *hsStatus = obs["hotspotStatus"] | "unknown";
        data["hotspot"]["status"]    = hsStatus;
        data["hotspot"]["ok"]        = (strcmp(hsStatus, "available") == 0);
        data["hotspot"]["server"]    = obs["hotspotServer"] | "";
        data["hotspot"]["interface"] = obs["hotspotInterface"] | "";

        if (obs["wan"].is<JsonObjectConst>()) {
          JsonObjectConst wanObs = obs["wan"].as<JsonObjectConst>();
          const bool wanKnown = wanObs["known"] | false;
          data["wan"]["known"]        = wanKnown;
          data["wan"]["interface"]    = wanObs["interface"] | "";
          data["wan"]["link"]         = wanObs["link"] | "unknown";
          data["wan"]["dhcp"]         = wanObs["dhcp"] | "unknown";
          data["wan"]["ip"]           = wanObs["ip"] | "";
          data["wan"]["gateway"]      = wanObs["gateway"] | "";
          data["wan"]["defaultRoute"] = wanObs["defaultRoute"] | "unknown";
          data["wan"]["internet"]     = wanObs["internet"] | "unknown";
          data["wan"]["dns"]          = wanObs["dns"] | "unknown";
          data["wan"]["note"]         = wanObs["note"] | "";
          if (wanKnown) {
            const char *inet = wanObs["internet"] | "unknown";
            const bool inetKnown = (strcmp(inet, "unknown") != 0);
            data["internet"]["known"] = inetKnown;
            data["internet"]["ok"]    = (strcmp(inet, "online") == 0);
            // Explicit Sync/Test probe — not a continuous latency sample.
            data["internet"]["latencyMs"] =
                (strcmp(inet, "online") == 0) ? 1 : 0;
          }
        }
      }
    }

    if (_coin) {
      _coin->fillStatus(data["coinSlot"].to<JsonObject>());
    } else {
      fillCoinDisabledStatus(data["coinSlot"].to<JsonObject>());
    }

    if (_router) {
      _router->fillRouterCacheStatus(data["routerCache"].to<JsonObject>());
    } else {
      data["routerCache"]["populated"] = false;
    }

    if (_routerProvisioning) {
      routerProvisioningFillNetworkModeStatus(_routerProvisioning,
                                            data.as<JsonObject>());
      if (_router) {
        DynamicJsonDocument cacheDoc(RenzFiConfig::JSON_DOC_MEDIUM);
        if (_router->fillRouterCache(cacheDoc)) {
          String boardLower =
              String(cacheDoc["routerOs"]["boardName"] | "") + " " +
              String(cacheDoc["identity"] | "");
          boardLower.toLowerCase();
          if (boardLower.indexOf("hex") >= 0 ||
              boardLower.indexOf("rb750") >= 0 ||
              boardLower.indexOf("rb760") >= 0) {
            JsonObject network = data["networkProvisioning"].to<JsonObject>();
            network["externalApOnly"] = true;
            network["noWirelessCapabilityDetected"] = true;
          }
        }
      }
    }

    data["esp32"]["uptime"]              = String(millis() / 1000) + "s";
    data["esp32"]["lastSeen"]            = nullptr;
    {
      const uint32_t internalTotal = ESP.getHeapSize();
      const uint32_t internalFree  = ESP.getFreeHeap();
      data["storage"]["ramUsedKb"] =
          (internalTotal > internalFree ? (internalTotal - internalFree) : 0) /
          1024;
      data["storage"]["ramTotalKb"] = internalTotal / 1024;
      data["storage"]["ramLabel"] = "internal-heap";

      JsonObject internalHeap = data["storage"]["internalHeap"].to<JsonObject>();
      internalHeap["totalKb"] = internalTotal / 1024;
      internalHeap["freeKb"]  = internalFree / 1024;
      internalHeap["usedKb"] =
          (internalTotal > internalFree ? (internalTotal - internalFree) : 0) /
          1024;
      internalHeap["minFreeKb"] = ESP.getMinFreeHeap() / 1024;
      internalHeap["largestKb"] = ESP.getMaxAllocHeap() / 1024;

      JsonObject psram = data["storage"]["psram"].to<JsonObject>();
#if defined(BOARD_HAS_PSRAM)
      const uint32_t psramTotal = ESP.getPsramSize();
      const uint32_t psramFree  = ESP.getFreePsram();
      psram["present"] = psramTotal > 0;
      psram["totalKb"] = psramTotal / 1024;
      psram["freeKb"]  = psramFree / 1024;
      psram["usedKb"] =
          (psramTotal > psramFree ? (psramTotal - psramFree) : 0) / 1024;
      psram["minFreeKb"] = ESP.getMinFreePsram() / 1024;
#else
      psram["present"] = false;
      psram["totalKb"] = 0;
      psram["freeKb"]  = 0;
      psram["usedKb"]  = 0;
#endif

      JsonObject dma = data["storage"]["dma"].to<JsonObject>();
      const size_t dmaFree = heap_caps_get_free_size(MALLOC_CAP_DMA);
      const size_t dmaLargest =
          heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
      dma["freeKb"]     = dmaFree / 1024;
      dma["largestKb"]  = dmaLargest / 1024;
      dma["minimumKb"]  = heap_caps_get_minimum_free_size(MALLOC_CAP_DMA) / 1024;

      _storage->fillDashboardStatus(data["storage"].to<JsonObject>(),
                                    data["storage"]["sd"].to<JsonObject>(),
                                    data["storageStatus"].to<JsonObject>());
    }
    data["sync"]["pending"]              = 0;
    data["sync"]["lastSyncAt"]           = nullptr;
    sendOk(req, data);
    const uint32_t elapsedMs = millis() - t0;
    if (elapsedMs >= 50U) {
      static uint32_t s_lastStatusSlowLogMs = 0;
      const uint32_t now = millis();
      if (s_lastStatusSlowLogMs == 0 || (now - s_lastStatusSlowLogMs) >= 5000U) {
        s_lastStatusSlowLogMs = now;
        Serial.printf(
            "[http-status] elapsedMs=%u storageIo=0 routerIo=0 snapshotAgeMs=%u\n",
            static_cast<unsigned>(elapsedMs),
            static_cast<unsigned>(_storage->dashboardSnapshotAgeMs()));
      }
    }
  });

  // ── System ────────────────────────────────────────────────────────────────
  _server->on("/api/storage/retry-sd", HTTP_POST,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
                if (!requireAuth(req)) return;
                logRequest(req, "storage.retry-sd");
                _storage->retrySd();
                DynamicJsonDocument data(128);
                data["healthy"]  = _storage->healthy();
                data["fallback"] = _storage->usingFallback();
                data["recoveryQueued"] = true;
                sendOk(req, data);
              });

  _server->on("/api/storage/status", HTTP_GET, [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireAuth(req)) return;
    // Dashboard StorageHealthCard polls this on the same cadence as status.
    // JSON_DOC_MEDIUM on default heap was the N16R8 INTERNAL shredder class.
    PsramJsonDocument dataHeap;
    JsonDocument &data = dataHeap.doc();
    _storage->fillStorageStatus(data.to<JsonObject>());
    Serial.printf(
        "[storage-api] status snapshot state=%s recoveryInProgress=%s\n",
        data["sdLifecycle"] | "unknown",
        (data["recoveryInProgress"] | false) ? "yes" : "no");
    if (_backup.lastSuccessfulBackup().isEmpty())
      data["lastSuccessfulBackup"] = nullptr;
    else
      data["lastSuccessfulBackup"] = _backup.lastSuccessfulBackup();
    if (_backup.hasSuccessfulBackup())
      data["lastSuccessfulBackupAgeSeconds"] =
          _backup.lastSuccessfulBackupAgeSeconds();
    else
      data["lastSuccessfulBackupAgeSeconds"] = nullptr;
    sendOk(req, data);
  });

  _server->on("/api/system/health", HTTP_GET, [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireAuth(req)) return;
    // N16R8 remaining-issues: JSON_DOC_MEDIUM (8192) on default heap is INTERNAL
    // and shreds W5500 DMA. Dashboard polls this every fallbackPollMs.
    PsramJsonDocument dataHeap;
    JsonDocument &data = dataHeap.doc();
    if (_health) {
      _health->fillHealth(data.to<JsonObject>());
    }
    sendOk(req, data);
  });

  _server->on("/api/system/build", HTTP_GET, [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    PsramJsonDocument dataHeap;
    JsonDocument &data = dataHeap.doc();
    data["runningFirmwareVersion"] = RenzFiConfig::FIRMWARE_VERSION;
    if (_build) {
      _build->fillJson(data["staged"].to<JsonObject>());
    }
    sendOk(req, data);
  });

  _server->on("/api/system/coin", HTTP_GET, [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireAuth(req)) return;
    PsramJsonDocument dataHeap;
    JsonDocument &data = dataHeap.doc();
    if (_coin) {
      _coin->fillCoinStatus(data.to<JsonObject>());
    } else {
      fillCoinDisabledCoinStatus(data.to<JsonObject>());
    }
    sendOk(req, data);
  });

  auto rgbSystemGet = [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireAuth(req)) return;
    PsramJsonDocument dataHeap;
    JsonDocument &data = dataHeap.doc();
    if (_rgb) {
      // Return full status (mode, color, signal, brightness, enabled)
      _rgb->fillStatus(data.to<JsonObject>());
    } else {
      data["enabled"]    = false;
      data["brightness"] = 0;
      data["mode"]       = "SYSTEM_STATUS";
      data["state"]      = "OFF";
      data["colorName"]  = "OFF";
      data["color"]["red"]   = 0;
      data["color"]["green"] = 0;
      data["color"]["blue"]  = 0;
    }
    sendOk(req, data);
  };
  _server->on("/api/system/rgb", HTTP_GET, rgbSystemGet);

  // PUT /api/system/rgb — full manual control:
  // { enabled, brightness, mode, red, green, blue }
  // mode values: "AUTOMATIC" (= SYSTEM_STATUS), "OFF", "SOLID", "BREATHING", "RAINBOW"
  auto rgbSystemPut = [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireAuth(req)) return;
    if (!_rgb) {
      sendError(req, 503, "RGB controller unavailable", "RGB_DISABLED");
      return;
    }
    DynamicJsonDocument body(512);
    String raw = getBody(req);
    if (raw.length() > 0 && deserializeJson(body, raw)) {
      sendError(req, 400, "Invalid JSON body", "BAD_REQUEST");
      return;
    }
    JsonObjectConst root = body.as<JsonObjectConst>();
    RgbSettings cur = _rgb->settings();
    bool enabled = cur.enabled;
    uint8_t brightness = cur.brightness;
    bool changed = false;

    if (root["enabled"].is<bool>()) {
      enabled = root["enabled"].as<bool>();
    } else if (root["enabled"].is<const char *>()) {
      enabled = String(root["enabled"].as<const char *>()) == "true";
    }
    if (root["brightness"].is<int>()) {
      brightness = static_cast<uint8_t>(root["brightness"].as<int>());
    }

    // Apply enabled + brightness together
    changed |= _rgb->applySettings(enabled, brightness);

    // Mode: accept "AUTOMATIC" as alias for SYSTEM_STATUS
    if (root["mode"].is<const char *>()) {
      const char *modeStr = root["mode"].as<const char *>();
      RgbMode mode = RgbMode::SystemStatus;
      if (strcmp(modeStr, "OFF") == 0)             mode = RgbMode::Off;
      else if (strcmp(modeStr, "SOLID") == 0)      mode = RgbMode::Solid;
      else if (strcmp(modeStr, "BREATHING") == 0)  mode = RgbMode::Breathing;
      else if (strcmp(modeStr, "RAINBOW") == 0)    mode = RgbMode::Rainbow;
      // "AUTOMATIC" or "SYSTEM_STATUS" → RgbMode::SystemStatus (default)
      changed |= _rgb->setMode(mode);
    }

    // Color (used when mode is SOLID/BREATHING/RAINBOW)
    if (root["red"].is<int>() || root["green"].is<int>() || root["blue"].is<int>()) {
      uint8_t r = root["red"].is<int>()   ? static_cast<uint8_t>(root["red"].as<int>())   : cur.red;
      uint8_t g = root["green"].is<int>() ? static_cast<uint8_t>(root["green"].as<int>()) : cur.green;
      uint8_t b = root["blue"].is<int>()  ? static_cast<uint8_t>(root["blue"].as<int>())  : cur.blue;
      changed |= _rgb->setColor(r, g, b);
    }

    if (changed) {
      DynamicJsonDocument data(512);
      _rgb->fillStatus(data.to<JsonObject>());
      sendOk(req, data, "RGB settings saved");
    } else {
      sendError(req, 500, "Failed to save RGB settings", "RGB_SAVE_FAILED");
    }
  };
  _server->on("/api/system/rgb", HTTP_PUT, rgbSystemPut, nullptr, bodyCollect);

  _server->on("/api/rgb/status", HTTP_GET, [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireAuth(req)) return;
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_SMALL);
    if (_rgb) {
      _rgb->fillStatus(data.to<JsonObject>());
    }
    sendOk(req, data);
  });

  _server->on(
      "/api/rgb/mode", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
        if (!requireAuth(req)) return;
        if (!_rgb) {
          sendError(req, 503, "RGB controller unavailable", "RGB_DISABLED");
          return;
        }
        DynamicJsonDocument body(RenzFiConfig::JSON_DOC_SMALL);
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        const char *modeStr = body["mode"] | "SYSTEM_STATUS";
        RgbMode mode = RgbMode::SystemStatus;
        if (strcmp(modeStr, "OFF") == 0) mode = RgbMode::Off;
        else if (strcmp(modeStr, "SOLID") == 0) mode = RgbMode::Solid;
        else if (strcmp(modeStr, "BREATHING") == 0) mode = RgbMode::Breathing;
        else if (strcmp(modeStr, "RAINBOW") == 0) mode = RgbMode::Rainbow;
        if (_rgb->setMode(mode)) sendOk(req);
        else sendError(req, 500, "Unable to save RGB mode", "RGB_MODE_FAILED");
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/rgb/color", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
        if (!requireAuth(req)) return;
        if (!_rgb) {
          sendError(req, 503, "RGB controller unavailable", "RGB_DISABLED");
          return;
        }
        DynamicJsonDocument body(RenzFiConfig::JSON_DOC_SMALL);
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        const uint8_t red = body["red"] | 0;
        const uint8_t green = body["green"] | 0;
        const uint8_t blue = body["blue"] | 255;
        if (_rgb->setColor(red, green, blue)) sendOk(req);
        else sendError(req, 500, "Unable to save RGB color", "RGB_COLOR_FAILED");
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/rgb/brightness", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
        if (!requireAuth(req)) return;
        if (!_rgb) {
          sendError(req, 503, "RGB controller unavailable", "RGB_DISABLED");
          return;
        }
        DynamicJsonDocument body(RenzFiConfig::JSON_DOC_SMALL);
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        const uint8_t brightness = body["brightness"] | 80;
        if (_rgb->setBrightness(brightness)) sendOk(req);
        else
          sendError(req, 500, "Unable to save RGB brightness",
                    "RGB_BRIGHTNESS_FAILED");
      },
      nullptr, bodyCollect);

  _server->on("/api/system/reboot", HTTP_POST,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
                if (!requireOwnerAuth(req)) return;
                sendOk(req, "Rebooting");
                delay(250);
                ESP.restart();
              });

  _server->on(
      "/api/system/factory-reset", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
        if (!requireOwnerAuth(req)) return;
        if (!_factoryReset) {
          sendError(req, 503, "Factory reset unavailable",
                    "FACTORY_RESET_UNAVAILABLE");
          return;
        }
        uint32_t jobId = _factoryReset->enqueue();
        if (jobId == 0) {
          sendError(req, 409, "Factory reset already in progress",
                    "FACTORY_RESET_IN_PROGRESS");
          return;
        }
        // Drop SSE immediately so Admin EventSource cannot keep allocating
        // W5500 TX while FactoryResetWorker performs storage teardown.
        if (_events) _events->closeAllClients();
        Serial.println("[http-quiesce] factory-reset busy — communication quiesced");
        DynamicJsonDocument envelope(256);
        envelope["success"] = true;
        JsonObject data     = envelope.createNestedObject("data");
        data["ok"]          = true;
        data["jobId"]       = jobId;
        data["status"]      = "queued";
        data["state"]       = "queued";
        envelope["message"] = "Factory reset queued";
        String body;
        serializeJson(envelope, body);
        logRequest(req, "factory-reset-accepted");
        AsyncWebServerResponse *res =
            req->beginResponse(202, "application/json", body);
        addCorsHeaders(res);
        res->addHeader("Cache-Control", "no-store");
        req->send(res);
      });

  _server->on("/api/system/factory-reset/status", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
                if (!requireOwnerAuth(req)) return;
                if (!_factoryReset) {
                  sendError(req, 503, "Factory reset unavailable",
                            "FACTORY_RESET_UNAVAILABLE");
                  return;
                }
                FactoryResetWorker::Snapshot snap;
                if (req->hasParam("jobId")) {
                  const uint32_t jobId =
                      static_cast<uint32_t>(req->getParam("jobId")->value().toInt());
                  if (!_factoryReset->poll(jobId, snap)) {
                    sendError(req, 404, "Factory reset job not found",
                              "FACTORY_RESET_JOB_NOT_FOUND");
                    return;
                  }
                } else {
                  _factoryReset->fillSnapshot(snap);
                }
                DynamicJsonDocument data(256);
                data["jobId"]     = snap.jobId;
                data["status"]    = snap.status;
                data["state"]     = snap.status;
                data["phase"]     = snap.phase;
                data["progress"]  = snap.progress;
                data["rebooting"] = snap.rebooting;
                if (snap.error && snap.error[0]) data["error"] = snap.error;
                sendOk(req, data);
              });

  _server->on(
      "/api/system/reconfigure", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        RENZFI_PROD_GATE(req);
        if (!requireOwnerAuth(req)) return;
        if (!_installation || !_installation->isReady()) {
          sendError(req, 409, "Device is not in production mode", "NOT_PRODUCTION");
          return;
        }
        if (!_installation->reopenSetupWizard()) {
          sendError(req, 500, "Unable to reopen setup wizard", "RECONFIGURE_FAILED");
          return;
        }
        if (_storage) {
          _storage->removeBinary(StoragePaths::NetworkAdoptionWorkflowFile, nullptr);
          _storage->removeBinary(StoragePaths::ExistingNetworkScanFile, nullptr);
        }
        if (_mgmtApLifecycle) {
          _mgmtApLifecycle->startMaintenance();
        }
        DynamicJsonDocument data(192);
        data["setupWizardEnabled"] = true;
        // Reconfigure reopens at Wi-Fi review: skip existing-network "complete"
        // shortcut so the installer lands on review, not the operator step.
        data["wizardStep"] =
            _setupProvisioning
                ? _setupProvisioning->wizardStepForPhase(false, false, true)
                : "review";
        data["managementAp"]       = true;
        sendOk(req, data, "Setup wizard reopened — connect to Management Wi-Fi");
      });

  // ── Promos ────────────────────────────────────────────────────────────────
  _server->on("/api/promos", HTTP_GET, [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireAuth(req)) return;
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_MEDIUM);
    if (_promos->list(data)) sendOk(req, data);
    else sendError(req, 500, "Unable to load promos", "STORAGE_ERROR");
  });

  _server->on(
      "/api/promos", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
        if (!requireAuth(req)) return;
        DynamicJsonDocument body(RenzFiConfig::JSON_DOC_MEDIUM);
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        DynamicJsonDocument data(128);
        int id = _promos->create(body.as<JsonObjectConst>());
        if (id < 0) sendError(req, 500, "Unable to create promo",
                              "PROMO_CREATE_FAILED");
        else { data["id"] = id; sendOk(req, data); }
      },
      nullptr, bodyCollect);

  // Exact match required: default BackwardCompatible URI matching would also
  // capture /api/vouchers/bulk-delete and /api/vouchers/jobs/* (Generate
  // validation + list responses stealing job polls).
  _server->on(AsyncURIMatcher::exact("/api/vouchers"), HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireAuth(req)) return;
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_LARGE);
    if (_vouchers->list(data)) sendOk(req, data);
    else sendError(req, 500, "Unable to load vouchers", "STORAGE_ERROR");
  });

  _server->on(
      AsyncURIMatcher::exact("/api/vouchers"), HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        RENZFI_PROD_GATE(req);
        if (!requireOwnerAuth(req)) return;
        if (!_vouchers) {
          sendError(req, 503, "Voucher service unavailable", "INTERNAL_ERROR");
          return;
        }
        DynamicJsonDocument body(RenzFiConfig::JSON_DOC_MEDIUM);
        String raw = getBody(req);
        if (raw.length() == 0) {
          sendError(req, 400, "Request body required", "INVALID_REQUEST");
          return;
        }
        if (deserializeJson(body, raw)) {
          sendError(req, 400, "Invalid JSON body", "INVALID_REQUEST");
          return;
        }
        if (body["count"].isNull() || body["amount"].isNull() ||
            body["minutes"].isNull()) {
          sendError(req, 400,
                    "count, amount, and minutes are required",
                    "INVALID_REQUEST");
          return;
        }
        const int count = body["count"] | 0;
        const int amount = body["amount"] | -1;
        const int minutes = body["minutes"] | 0;
        if (count < 1 || count > 20 || amount < 0 || minutes <= 0 ||
            minutes > 525600) {
          sendError(req, 400,
                    "count must be 1–20, amount ≥ 0, minutes 1–525600",
                    "INVALID_REQUEST");
          return;
        }
        bool alreadyRunning = false;
        const uint32_t jobId =
            _vouchers->enqueueGenerate(body.as<JsonObjectConst>(), alreadyRunning);
        if (jobId == 0) {
          sendError(req, 500, "Unable to enqueue voucher generation",
                    "VOUCHER_CREATE_FAILED");
          return;
        }
        DynamicJsonDocument doc(256);
        doc["success"] = true;
        JsonObject data = doc.createNestedObject("data");
        data["jobId"] = jobId;
        data["state"] = alreadyRunning ? "running" : "queued";
        data["type"] = "voucher-generate";
        if (alreadyRunning) {
          data["duplicate"] = true;
          doc["message"] = "Generation already running";
        }
        String out;
        serializeJson(doc, out);
        AsyncWebServerResponse *res =
            req->beginResponse(202, "application/json", out);
        addCorsHeaders(res);
        res->addHeader("Cache-Control", "no-store");
        req->send(res);
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/vouchers/bulk-delete", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        RENZFI_PROD_GATE(req);
        if (!requireOwnerAuth(req)) return;
        if (!_vouchers) {
          sendError(req, 503, "Voucher service unavailable", "INTERNAL_ERROR");
          return;
        }
        DynamicJsonDocument body(RenzFiConfig::JSON_DOC_MEDIUM);
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        JsonArrayConst codes = body["codes"].as<JsonArrayConst>();
        if (codes.isNull() || codes.size() == 0 || codes.size() > 20) {
          sendError(req, 400, "codes must be 1–20 voucher codes",
                    "INVALID_REQUEST");
          return;
        }
        bool alreadyRunning = false;
        const uint32_t jobId =
            _vouchers->enqueueBulkDelete(codes, alreadyRunning);
        if (jobId == 0) {
          sendError(req, 500, "Unable to enqueue voucher delete",
                    "VOUCHER_DELETE_FAILED");
          return;
        }
        DynamicJsonDocument doc(256);
        doc["success"] = true;
        JsonObject data = doc.createNestedObject("data");
        data["jobId"] = jobId;
        data["state"] = alreadyRunning ? "running" : "queued";
        data["type"] = "voucher-bulk-delete";
        if (alreadyRunning) {
          data["duplicate"] = true;
          doc["message"] = "Voucher job already running";
        }
        String out;
        serializeJson(doc, out);
        AsyncWebServerResponse *res =
            req->beginResponse(202, "application/json", out);
        addCorsHeaders(res);
        res->addHeader("Cache-Control", "no-store");
        req->send(res);
      },
      nullptr, bodyCollect);

  _server->on("/api/vouchers/jobs/*", HTTP_GET, [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireOwnerAuth(req)) return;
    if (!_vouchers) {
      sendError(req, 503, "Voucher service unavailable", "INTERNAL_ERROR");
      return;
    }
    const String url = req->url();
    const int marker = url.lastIndexOf('/');
    if (marker < 0 || marker + 1 >= static_cast<int>(url.length())) {
      sendError(req, 400, "Job id required", "INVALID_JOB_ID");
      return;
    }
    const uint32_t jobId =
        static_cast<uint32_t>(url.substring(marker + 1).toInt());
    VoucherManager::GenerateJobSnapshot snap;
    if (!_vouchers->pollGenerateJob(jobId, snap)) {
      sendError(req, 404, "Voucher job not found", "NOT_FOUND");
      return;
    }
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_MEDIUM);
    data["jobId"] = snap.jobId;
    data["state"] = snap.state;
    data["status"] = snap.state;  // alias for Admin clients
    data["type"] = snap.type;
    data["ok"] = snap.ok;
    if (snap.count > 0) data["count"] = snap.count;
    if (snap.error.length() > 0) data["error"] = snap.error;
    if (strcmp(snap.state, "completed") == 0 && snap.resultJson.length() > 0) {
      DynamicJsonDocument parsed(RenzFiConfig::JSON_DOC_MEDIUM);
      if (!deserializeJson(parsed, snap.resultJson)) {
        data["result"] = parsed.as<JsonVariant>();
      }
    }
    sendOk(req, data, "Voucher job");
  });

  // ── Users ─────────────────────────────────────────────────────────────────
  // Rule #7 (Setup Simplification Pass): Active Users is Administrator-only.
  _server->on("/api/users", HTTP_GET, [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireAuth(req)) return;
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_LARGE);
    fillActiveUsers(_sessions, _portalSessions, data);
    sendOk(req, data);
  });

  _server->on(
      "/api/users/disconnect", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
        if (!requireOwnerAuth(req)) return;
        HeapJsonDocument bodyHeap(256);
        DynamicJsonDocument &body = bodyHeap.doc();
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        String mac = body["mac"] | "";
        if (mac.isEmpty()) {
          sendError(req, 400, "mac field required", "MISSING_MAC");
          return;
        }
        String errorCode;
        if (_portalSessions &&
            _portalSessions->suspendInternet(mac, &errorCode)) {
          sendOk(req, "Internet suspended — time preserved");
          return;
        }
        sendError(req, 404,
                  errorCode.length() > 0 ? errorCode : "Active user not found",
                  errorCode.length() > 0 ? errorCode : "USER_NOT_FOUND");
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/users/reconnect", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
        if (!requireOwnerAuth(req)) return;
        DynamicJsonDocument body(256);
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        String mac = body["mac"] | "";
        if (mac.isEmpty()) {
          sendError(req, 400, "mac field required", "MISSING_MAC");
          return;
        }
        String errorCode;
        if (_portalSessions &&
            _portalSessions->reconnectInternet(mac, &errorCode)) {
          sendOk(req, "Internet reconnect queued");
          return;
        }
        sendError(req, 404,
                  errorCode.length() > 0 ? errorCode : "Active user not found",
                  errorCode.length() > 0 ? errorCode : "USER_NOT_FOUND");
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/users/terminate", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
        if (!requireOwnerAuth(req)) return;
        DynamicJsonDocument body(256);
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        String mac = body["mac"] | "";
        if (mac.isEmpty()) {
          sendError(req, 400, "mac field required", "MISSING_MAC");
          return;
        }
        String errorCode;
        if (_portalSessions &&
            _portalSessions->ownerTerminateSession(mac, &errorCode)) {
          sendOk(req, "Session terminated by owner");
          return;
        }
        sendError(req, 404,
                  errorCode.length() > 0 ? errorCode : "Active user not found",
                  errorCode.length() > 0 ? errorCode : "USER_NOT_FOUND");
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/users/pause", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
        if (!requireOwnerAuth(req)) return;
        DynamicJsonDocument body(256);
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        String mac = body["mac"] | "";
        if (mac.isEmpty()) {
          sendError(req, 400, "mac field required", "MISSING_MAC");
          return;
        }
        if (_portalSessions && _portalSessions->hasSession(mac)) {
          String errorCode;
          if (_portalSessions->pause(mac, &errorCode))
            sendOk(req, "Session paused");
          else if (errorCode == "PAUSE_LIMIT_REACHED")
            sendError(req, 409, "Pause limit reached for this session",
                      "PAUSE_LIMIT_REACHED");
          else
            sendError(req, 400, "Session cannot be paused",
                      errorCode.isEmpty() ? "INVALID_STATE" : errorCode.c_str());
          return;
        }
        if (_sessions && _sessions->pause(mac)) sendOk(req, "Session paused");
        else sendError(req, 404, "Active user not found", "USER_NOT_FOUND");
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/users/resume", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
        if (!requireOwnerAuth(req)) return;
        DynamicJsonDocument body(256);
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        String mac = body["mac"] | "";
        if (mac.isEmpty()) {
          sendError(req, 400, "mac field required", "MISSING_MAC");
          return;
        }
        if (_portalSessions && _portalSessions->hasSession(mac)) {
          if (_portalSessions->resume(mac)) sendOk(req, "Session resumed");
          else
            sendError(req, 400, "Session cannot be resumed", "INVALID_STATE");
          return;
        }
        if (_sessions && _sessions->resume(mac)) sendOk(req, "Session resumed");
        else sendError(req, 404, "Active user not found", "USER_NOT_FOUND");
      },
      nullptr, bodyCollect);

  // ── Sales ─────────────────────────────────────────────────────────────────
  // Rule #7 (Setup Simplification Pass): Reports/Sales is Administrator-only.
  auto registerHistoryDownload =
      [this](const char *route, NdjsonLedger::Kind kind) {
        _server->on(route, HTTP_GET,
                    [this, kind](AsyncWebServerRequest *req) {
          RENZFI_PROD_GATE(req);
          if (!requireOwnerAuth(req)) return;
          if (!req->hasParam("month")) {
            sendError(req, 400, "month must be YYYY-MM or undated",
                      "INVALID_HISTORY_MONTH");
            return;
          }
          const String month = req->getParam("month")->value();
          String path;
          if (!_storage->historyPath(kind, month, path)) {
            sendError(req, 400, "month must be YYYY-MM or undated",
                      "INVALID_HISTORY_MONTH");
            return;
          }
          if (!_storage->isSdReadable() || !_storage->exists(path.c_str())) {
            sendError(req, 404, "History file not found", "HISTORY_NOT_FOUND");
            return;
          }
          if (!WebResponse::ensureEthTransmitHeadroom(req, "history-download")) return;
          AsyncWebServerResponse *res =
              req->beginResponse(SD, path.c_str(), "application/x-ndjson");
          res->addHeader(
              "Content-Disposition",
              String("attachment; filename=\"") +
                  NdjsonLedger::kindName(kind) + "-" + month + ".ndjson\"");
          res->addHeader("Cache-Control", "no-store");
          addCorsHeaders(res);
          req->send(res);
        });
      };
  registerHistoryDownload("/api/history/sales/download",
                          NdjsonLedger::Kind::Sales);
  registerHistoryDownload("/api/history/sessions/download",
                          NdjsonLedger::Kind::Sessions);
  registerHistoryDownload("/api/history/vouchers/download",
                          NdjsonLedger::Kind::Vouchers);
  registerHistoryDownload("/api/history/logs/download",
                          NdjsonLedger::Kind::Logs);

  _server->on("/api/sales/today", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
                if (!requireAuth(req)) return;
                DynamicJsonDocument data(256);
                _sessions->salesToday(data);
                sendOk(req, data);
              });

  _server->on("/api/sales/weekly", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
                if (!requireAuth(req)) return;
                DynamicJsonDocument data(256);
                _sessions->salesWeek(data);
                sendOk(req, data);
              });

  _server->on("/api/sales/monthly", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
                if (!requireAuth(req)) return;
                DynamicJsonDocument data(256);
                _sessions->salesMonth(data);
                sendOk(req, data);
              });

  _server->on("/api/sales/history", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
                if (!requireAuth(req)) return;
                PsramJsonDocument dataHeap;
                _sessions->salesHistory(dataHeap.doc());
                sendOk(req, dataHeap.doc());
              });

  _server->on("/api/sales/records", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
                RENZFI_PROD_GATE(req);
                if (!requireAuth(req)) return;
                size_t limit = req->hasArg("limit")
                                   ? static_cast<size_t>(req->arg("limit").toInt())
                                   : 20U;
                PsramJsonDocument dataHeap;
                if (_sessions->listSalesRecords(dataHeap.doc(), limit)) {
                  sendOk(req, dataHeap.doc());
                } else {
                  sendError(req, 500, "Unable to load sale records",
                            "SALES_RECORDS_ERROR");
                }
              });

  _server->on("/api/sales/export", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
                if (!requireOwnerAuth(req)) return;
                if (!WebResponse::ensureEthTransmitHeadroom(req, "sales-export")) return;
                String csv;
                String filename;
                _sessions->buildSalesCsv(csv, filename);
                AsyncWebServerResponse *res =
                    req->beginResponse(200, "text/csv; charset=utf-8", csv);
                addCorsHeaders(res);
                res->addHeader(
                    "Content-Disposition",
                    String("attachment; filename=\"") + filename + "\"");
                res->addHeader("Cache-Control", "no-store");
                req->send(res);
              });

  _server->on("/api/sales/reset", HTTP_POST,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
                if (!requireOwnerAuth(req)) return;
                if (!_sessions || !_sessions->resetSales()) {
                  sendError(req, 500, "Unable to reset sales",
                            "SALES_RESET_FAILED");
                  return;
                }
                DynamicJsonDocument data(64);
                data["ok"] = true;
                sendOk(req, data, "Sales reset");
              });

  auto registerSalesChartRoute = [this](const char *path, int days) {
    _server->on(path, HTTP_GET, [this, days](AsyncWebServerRequest *req) {
      RENZFI_PROD_GATE(req);
      if (!requireAuth(req)) return;
      // Monthly (180d) labels+data need ~6–8 KB; 2048 overflowed and forced
      // extra internal realloc pressure beside W5500 DMA demand.
      if (!DmaMemoryMonitor::hasEthTransmitHeadroom()) {
        DmaMemoryMonitor::logSnapshot("sales-chart-http-dma-low");
        sendError(req, 503, "Sales chart deferred — SPI DMA memory low",
                  "SPI_DMA_LOW");
        return;
      }
      PsramJsonDocument dataHeap;
      if (_sessions->salesChart(dataHeap.doc(), days)) {
        if (!DmaMemoryMonitor::hasEthTransmitHeadroom()) {
          DmaMemoryMonitor::logSnapshot("sales-chart-post-dma-low");
          sendError(req, 503, "Sales chart deferred — SPI DMA memory low",
                    "SPI_DMA_LOW");
          return;
        }
        sendOk(req, dataHeap.doc());
      } else {
        sendError(req, 500, "Unable to build sales chart", "SALES_CHART_ERROR");
      }
    });
  };
  registerSalesChartRoute("/api/sales/chart/daily", 7);
  registerSalesChartRoute("/api/sales/chart/weekly", 28);
  registerSalesChartRoute("/api/sales/chart/monthly", 180);

  // ── Captive portal branding (admin) — register BEFORE /api/settings ───────
  _server->on("/api/settings/portal", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
                if (!requireAuth(req)) return;
                if (!_portalConfig) {
                  sendError(req, 500, "Portal config unavailable", "NOT_READY");
                  return;
                }
                JsonDocument data;
                JsonObject root = data.to<JsonObject>();
                const String base = WebRequestDiagnostics::requestBaseUrl(req, _eth);
                _portalConfig->fillSettingsJson(root, base);
                Serial.printf(
                    "[portal] settings has_banner=%s bannerConfigured=%s "
                    "has_music=%s musicConfigured=%s bannerUrl=%s musicUrl=%s\n",
                    root["has_banner"].as<bool>() ? "yes" : "no",
                    root["bannerConfigured"].as<bool>() ? "yes" : "no",
                    root["has_music"].as<bool>() ? "yes" : "no",
                    root["musicConfigured"].as<bool>() ? "yes" : "no",
                    root["bannerUrl"].as<const char *>(),
                    root["musicUrl"].as<const char *>());
                sendOk(req, data);
              });

  auto portalHandleChunk =
      [this](AsyncWebServerRequest *req, bool isBanner, const uint8_t *data,
             size_t len, size_t index, size_t total, bool final,
             const char *via, const String &filename) {
        const size_t maxBytes =
            isBanner ? RenzFiConfig::PORTAL_BANNER_MAX_BYTES
                     : RenzFiConfig::PORTAL_MUSIC_MAX_BYTES;

        // Per-chunk Serial on a ~4 MiB upload is thousands of lines and stalls
        // async_tcp / SD writes. Log start, ~256 KiB progress, errors, and end.
        const bool logChunk =
            index == 0 || final ||
            (gPortalUpload.bytesReceived >=
             gPortalUpload.lastLoggedBytes + (256U * 1024U));
        if (logChunk && !gPortalUpload.rejected) {
          Serial.printf(
              "[portal-upload] %s kind=%s index=%u len=%u total=%u final=%s "
              "filename=%s contentLength=%u written=%u\n",
              via, isBanner ? "banner" : "music", (unsigned)index,
              (unsigned)len, (unsigned)total, final ? "yes" : "no",
              filename.c_str(), (unsigned)req->contentLength(),
              (unsigned)gPortalUpload.bytesReceived);
          gPortalUpload.lastLoggedBytes = gPortalUpload.bytesReceived;
        }

        if (index == 0) {
          if (gPortalUpload.active && gPortalUpload.owner != req) {
            if (millis() - gPortalUpload.startedAt <= 120000U) return;
            if (_assets) _assets->abortSaveAsset();
            gPortalUpload = PortalAssetUpload{};
          }
          gPortalUpload = PortalAssetUpload{};
          gPortalUpload.active = true;
          gPortalUpload.owner = req;
          gPortalUpload.startedAt = millis();
          gPortalUpload.source =
              strcmp(via, "UPLOAD") == 0
                  ? PortalAssetUpload::Source::Upload
                  : PortalAssetUpload::Source::RawBody;
          if (!requireAuth(req)) {
            gPortalUpload.authFailed = true;
            return;
          }
          gPortalUpload.authenticated = true;
          gPortalUpload.bodySeen = true;
          gPortalUpload.kind =
              isBanner ? PortalAssetUpload::Kind::Banner
                       : PortalAssetUpload::Kind::Music;
          gPortalUpload.filename =
              filename.length() > 0
                  ? filename
                  : (isBanner ? "raw-body.webp" : "raw-body.mp3");
          gPortalUpload.rejected = false;
          gPortalUpload.rejectReason = "";
          gPortalUpload.streamActive = false;
          gPortalUpload.streamFinished = false;
          // Multipart file callbacks often pass total=0 until the final chunk.
          // Prefer known total; otherwise leave 0 and finalize size on last chunk.
          gPortalUpload.expectedTotal = total;
          gPortalUpload.bytesReceived = 0;
          gPortalUpload.lastLoggedBytes = 0;

          if (total > maxBytes) {
            gPortalUpload.rejected = true;
            gPortalUpload.rejectReason =
                isBanner ? "Banner exceeds 4 MB limit"
                         : "Music exceeds 4 MB limit";
            Serial.printf("[portal-upload] REJECT begin size total=%u max=%u\n",
                          (unsigned)total, (unsigned)maxBytes);
            return;
          }

          if (isBanner && filename.length() > 0) {
            String lower = filename;
            lower.toLowerCase();
            if (!lower.endsWith(".png") && !lower.endsWith(".jpg") &&
                !lower.endsWith(".jpeg") && !lower.endsWith(".mp4")) {
              gPortalUpload.rejected = true;
              gPortalUpload.rejectReason =
                  "Only PNG, JPEG, and MP4 banners are allowed";
              Serial.println("[portal-upload] REJECT begin extension");
              return;
            }
          }

          if (!isBanner && filename.length() > 0 &&
              !filename.endsWith(".mp3") && !filename.endsWith(".MP3")) {
            gPortalUpload.rejected = true;
            gPortalUpload.rejectReason = "Only MP3 files are allowed";
            Serial.println("[portal-upload] REJECT begin extension");
            return;
          }

          if (_assets) {
            const AssetType assetType =
                isBanner ? AssetType::Banner : AssetType::Music;
            const AssetOperationResult began = _assets->beginSaveAsset(
                assetType, total, gPortalUpload.filename);
            gPortalUpload.streamActive = began.success;
            if (!began.success) {
              gPortalUpload.rejected = true;
              gPortalUpload.rejectReason =
                  began.errorMessage.length() > 0
                      ? began.errorMessage
                      : String("Unable to begin upload");
              Serial.printf("[portal-upload] REJECT beginSave: %s\n",
                            gPortalUpload.rejectReason.c_str());
            }
          }
        }

        if (gPortalUpload.owner != req || gPortalUpload.authFailed ||
            gPortalUpload.rejected) {
          return;
        }

        const bool isFinal = final || (total > 0 && index + len >= total);

        if (gPortalUpload.streamActive && _assets && len > 0) {
          const AssetOperationResult chunk = _assets->appendSaveChunk(
              data, len, index, isFinal);
          if (!chunk.success) {
            gPortalUpload.rejected = true;
            gPortalUpload.rejectReason =
                chunk.errorMessage.length() > 0
                    ? chunk.errorMessage
                    : String("Unable to write upload chunk");
            Serial.printf(
                "[portal-upload] REJECT append at written=%u index=%u: %s\n",
                (unsigned)gPortalUpload.bytesReceived, (unsigned)index,
                gPortalUpload.rejectReason.c_str());
            _assets->abortSaveAsset();
            return;
          }
          gPortalUpload.bytesReceived += len;
        } else if (len > 0) {
          gPortalUpload.rejected = true;
          gPortalUpload.rejectReason = "Asset manager unavailable";
          Serial.println("[portal-upload] REJECT stream inactive");
        }

        if (isFinal) gPortalUpload.streamFinished = true;
      };

  auto portalBannerBodyCollect =
      [this, portalHandleChunk](AsyncWebServerRequest *req, uint8_t *data,
                                size_t len, size_t index, size_t total) {
        if (gPortalUpload.source == PortalAssetUpload::Source::Upload &&
            gPortalUpload.bodySeen) {
          return;
        }
        if (index == 0 && !gPortalUpload.active)
          gPortalUpload.source = PortalAssetUpload::Source::RawBody;
        portalHandleChunk(req, true, data, len, index, total,
                          total > 0 && index + len >= total, "BODY",
                          gPortalUpload.filename);
      };

  auto portalMusicBodyCollect =
      [this, portalHandleChunk](AsyncWebServerRequest *req, uint8_t *data,
                                size_t len, size_t index, size_t total) {
        if (gPortalUpload.source == PortalAssetUpload::Source::Upload &&
            gPortalUpload.bodySeen) {
          return;
        }
        if (index == 0 && !gPortalUpload.active)
          gPortalUpload.source = PortalAssetUpload::Source::RawBody;
        portalHandleChunk(req, false, data, len, index, total,
                          total > 0 && index + len >= total, "BODY",
                          gPortalUpload.filename);
      };

  auto portalBannerUploadHandler =
      [this, portalHandleChunk](AsyncWebServerRequest *req, String filename,
                                size_t index, uint8_t *data, size_t len,
                                bool final) {
        if (gPortalUpload.source == PortalAssetUpload::Source::RawBody &&
            gPortalUpload.bodySeen) {
          return;
        }
        if (index == 0 && !gPortalUpload.active)
          gPortalUpload.source = PortalAssetUpload::Source::Upload;
        size_t total = final ? index + len : 0;
        portalHandleChunk(req, true, data, len, index, total, final, "UPLOAD",
                          filename);
      };

  auto portalMusicUploadHandler =
      [this, portalHandleChunk](AsyncWebServerRequest *req, String filename,
                                size_t index, uint8_t *data, size_t len,
                                bool final) {
        if (gPortalUpload.source == PortalAssetUpload::Source::RawBody &&
            gPortalUpload.bodySeen) {
          return;
        }
        if (index == 0 && !gPortalUpload.active)
          gPortalUpload.source = PortalAssetUpload::Source::Upload;
        size_t total = final ? index + len : 0;
        portalHandleChunk(req, false, data, len, index, total, final, "UPLOAD",
                          filename);
      };

  auto portalBannerComplete = [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    Serial.printf(
        "[portal-upload] COMPLETE banner contentLength=%u contentType=%s "
        "bodySeen=%s bytes=%u streamActive=%s streamFinished=%s\n",
        (unsigned)req->contentLength(), req->contentType().c_str(),
        gPortalUpload.bodySeen ? "yes" : "no", (unsigned)gPortalUpload.bytesReceived,
        gPortalUpload.streamActive ? "yes" : "no",
        gPortalUpload.streamFinished ? "yes" : "no");

    if (gPortalUpload.owner != req) {
      sendError(req, 409, "Another asset upload is active", "UPLOAD_BUSY");
      return;
    }
    if (gPortalUpload.authFailed) {
      gPortalUpload = PortalAssetUpload{};
      return;
    }
    if (!gPortalUpload.authenticated && !requireAuth(req)) return;
    if (!_assets) {
      sendError(req, 500, "Asset manager unavailable", "NOT_READY");
      return;
    }

    if (gPortalUpload.rejected) {
      sendError(req, 400, gPortalUpload.rejectReason, "INVALID_UPLOAD");
      _assets->abortSaveAsset();
      gPortalUpload = PortalAssetUpload{};
      return;
    }

    if (!gPortalUpload.bodySeen || gPortalUpload.bytesReceived == 0) {
      sendError(req, 400,
                "No upload body received (body/upload callback did not run)",
                "INVALID_UPLOAD");
      _assets->abortSaveAsset();
      gPortalUpload = PortalAssetUpload{};
      return;
    }

    AssetOperationResult result;
    if (gPortalUpload.streamActive) {
      result = _assets->finishSaveAsset();
    } else {
      sendError(req, 400, "Empty banner upload", "INVALID_UPLOAD");
      gPortalUpload = PortalAssetUpload{};
      return;
    }

    if (result.success) {
      if (_portalConfig) _portalConfig->loadMeta();
      JsonDocument payload;
      JsonObject root = payload.to<JsonObject>();
      const String base = WebRequestDiagnostics::requestBaseUrl(req, _eth);
      if (_portalConfig) _portalConfig->fillSettingsJson(root, base);
      appendUploadResultJson(root, result);
      sendOk(req, payload, "Banner uploaded");
    } else {
      const int status =
          (result.errorCode == AssetErrorCode::InvalidUpload ||
           result.errorCode == AssetErrorCode::InvalidType ||
           result.errorCode == AssetErrorCode::SizeExceeded ||
           result.errorCode == AssetErrorCode::TranscodeFailed)
              ? 400
              : 500;
      sendError(req, status,
                result.errorMessage.length() > 0 ? result.errorMessage
                                                 : String("Unable to save banner"),
                assetErrorCodeLabel(result.errorCode));
    }
    gPortalUpload = PortalAssetUpload{};
  };

  auto portalMusicComplete = [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    Serial.printf(
        "[portal-upload] COMPLETE music contentLength=%u contentType=%s "
        "bodySeen=%s bytes=%u streamActive=%s streamFinished=%s\n",
        (unsigned)req->contentLength(), req->contentType().c_str(),
        gPortalUpload.bodySeen ? "yes" : "no", (unsigned)gPortalUpload.bytesReceived,
        gPortalUpload.streamActive ? "yes" : "no",
        gPortalUpload.streamFinished ? "yes" : "no");

    if (gPortalUpload.owner != req) {
      sendError(req, 409, "Another asset upload is active", "UPLOAD_BUSY");
      return;
    }
    if (gPortalUpload.authFailed) {
      gPortalUpload = PortalAssetUpload{};
      return;
    }
    if (!gPortalUpload.authenticated && !requireAuth(req)) return;
    if (!_assets) {
      sendError(req, 500, "Asset manager unavailable", "NOT_READY");
      return;
    }

    if (gPortalUpload.rejected) {
      sendError(req, 400, gPortalUpload.rejectReason, "INVALID_UPLOAD");
      _assets->abortSaveAsset();
      gPortalUpload = PortalAssetUpload{};
      return;
    }

    if (!gPortalUpload.bodySeen || gPortalUpload.bytesReceived == 0) {
      sendError(req, 400,
                "No upload body received (body/upload callback did not run)",
                "INVALID_UPLOAD");
      _assets->abortSaveAsset();
      gPortalUpload = PortalAssetUpload{};
      return;
    }

    AssetOperationResult result;
    if (gPortalUpload.streamActive) {
      result = _assets->finishSaveAsset();
    } else {
      sendError(req, 400, "Empty music upload", "INVALID_UPLOAD");
      gPortalUpload = PortalAssetUpload{};
      return;
    }

    if (result.success) {
      if (_portalConfig) _portalConfig->loadMeta();
      JsonDocument payload;
      JsonObject root = payload.to<JsonObject>();
      const String base = WebRequestDiagnostics::requestBaseUrl(req, _eth);
      if (_portalConfig) _portalConfig->fillSettingsJson(root, base);
      appendUploadResultJson(root, result);
      sendOk(req, payload, "Music uploaded");
    } else {
      const int status =
          (result.errorCode == AssetErrorCode::InvalidUpload ||
           result.errorCode == AssetErrorCode::InvalidType ||
           result.errorCode == AssetErrorCode::SizeExceeded)
              ? 400
              : 500;
      sendError(req, status,
                result.errorMessage.length() > 0 ? result.errorMessage
                                                 : String("Unable to save music"),
                assetErrorCodeLabel(result.errorCode));
    }
    gPortalUpload = PortalAssetUpload{};
  };

  _server->on(
      "/api/settings/portal/banner", HTTP_POST,
      [portalBannerComplete](AsyncWebServerRequest *req) {
        portalBannerComplete(req);
      },
      portalBannerUploadHandler, portalBannerBodyCollect);

  _server->on(
      "/api/settings/portal/music", HTTP_POST,
      [portalMusicComplete](AsyncWebServerRequest *req) {
        portalMusicComplete(req);
      },
      portalMusicUploadHandler, portalMusicBodyCollect);

  _server->on("/api/settings/portal/banner", HTTP_DELETE,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
                if (!requireAuth(req)) return;
                if (!_assets) {
                  sendError(req, 500, "Asset manager unavailable", "NOT_READY");
                  return;
                }
                const AssetOperationResult result =
                    _assets->deleteAsset(AssetType::Banner);
                if (result.success) {
                  if (_portalConfig) _portalConfig->loadMeta();
                  sendOk(req);
                } else {
                  sendError(req, 500, "Unable to remove banner", "STORAGE_ERROR");
                }
              });

  _server->on("/api/settings/portal/music", HTTP_DELETE,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
                if (!requireAuth(req)) return;
                if (!_assets) {
                  sendError(req, 500, "Asset manager unavailable", "NOT_READY");
                  return;
                }
                const AssetOperationResult result =
                    _assets->deleteAsset(AssetType::Music);
                if (result.success) {
                  if (_portalConfig) _portalConfig->loadMeta();
                  sendOk(req);
                } else {
                  sendError(req, 500, "Unable to remove music", "STORAGE_ERROR");
                }
              });

  // ── Settings ──────────────────────────────────────────────────────────────
  _server->on("/api/settings", HTTP_GET, [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireAuth(req)) return;
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_MEDIUM);
    _storage->readJson(RenzFiConfig::SETTINGS_FILE, data);
    sendOk(req, data);
  });

  auto settingsSave = [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireAuth(req)) return;
    DynamicJsonDocument body(RenzFiConfig::JSON_DOC_MEDIUM);
    String raw = getBody(req);
    if (raw.length() > 0) deserializeJson(body, raw);
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_MEDIUM);
    data.set(body);
    if (_storage->writeJson(RenzFiConfig::SETTINGS_FILE, data)) {
      DeviceIdentity::invalidateRuntimeProfile();
      sendOk(req);
    } else sendError(req, 500, "Unable to save settings", "STORAGE_ERROR");
  };
  _server->on("/api/settings", HTTP_POST, settingsSave, nullptr, bodyCollect);
  _server->on("/api/settings", HTTP_PUT,  settingsSave, nullptr, bodyCollect);

  _server->on("/api/settings/admin", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
                if (!requireAuth(req)) return;
                DynamicJsonDocument data(128);
                if (_eth) data["adminIp"] = _eth->ip();
                sendOk(req, data);
              });

  _server->on("/api/settings/admin", HTTP_PUT,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
                if (!requireAuth(req)) return;
                sendOk(req);
              });

  _server->on("/api/settings/backup", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
                if (!requireOwnerAuth(req)) return;
                if (!_backup.isSdAvailable()) {
                  sendError(req, 503, "SD Card is not available",
                            "SD_UNAVAILABLE");
                  return;
                }
                if (_logger) _logger->infoLocal("backup", "export started");
                Serial.println("[backup] export started");

                String outPath;
                bool useZip = true;
                String error;
                if (!_backup.createBackup(outPath, useZip, error)) {
                  sendError(req, 500, error.isEmpty() ? "Backup failed" : error,
                            "BACKUP_FAILED");
                  return;
                }

                if (!SD.exists(outPath.c_str())) {
                  sendError(req, 500, "Backup file missing", "BACKUP_FAILED");
                  return;
                }
                if (!WebResponse::ensureEthTransmitHeadroom(req, "backup-download")) return;

                const char *mime =
                    useZip ? "application/zip" : "application/json";
                const char *downloadName =
                    useZip ? "renzfi-backup.zip" : "renzfi-backup.json";
                AsyncWebServerResponse *res =
                    req->beginResponse(SD, outPath.c_str(), mime);
                res->addHeader("Content-Disposition",
                               String("attachment; filename=\"") +
                                   downloadName + "\"");
                res->addHeader("Cache-Control", "no-store");
                addCorsHeaders(res);
                req->send(res);

                if (_logger) _logger->infoLocal("backup", "export completed");
                Serial.println("[backup] export completed");
              });

  auto restoreUploadHandler = [this](AsyncWebServerRequest *req, String filename,
                                     size_t index, uint8_t *data, size_t len,
                                     bool final) {
    (void)filename;
    if (index == 0) {
      if (gRestoreUpload.active && gRestoreUpload.owner != req) {
        if (millis() - gRestoreUpload.startedAt <= 120000U) return;
        if (gRestoreUpload.file) gRestoreUpload.file.close();
        SD.remove(BackupManager::TEMP_RESTORE_PATH);
        gRestoreUpload = RestoreUpload{};
      }
      gRestoreUpload = RestoreUpload{};
      gRestoreUpload.active = true;
      gRestoreUpload.owner = req;
      gRestoreUpload.startedAt = millis();
      if (!requireOwnerAuth(req)) {
        gRestoreUpload.authFailed = true;
        return;
      }
      gRestoreUpload.authenticated = true;
      if (!_backup.isSdAvailable()) {
        gRestoreUpload.rejected = true;
        gRestoreUpload.rejectReason = "SD Card is not available";
        return;
      }
      if (!SD.exists("/backup")) SD.mkdir("/backup");
      if (SD.exists(BackupManager::TEMP_RESTORE_PATH)) {
        SD.remove(BackupManager::TEMP_RESTORE_PATH);
      }
      gRestoreUpload.file =
          SD.open(BackupManager::TEMP_RESTORE_PATH, FILE_WRITE);
      if (!gRestoreUpload.file) {
        gRestoreUpload.rejected = true;
        gRestoreUpload.rejectReason = "Unable to store restore upload";
        return;
      }
      if (_logger) _logger->infoLocal("restore", "restore started");
      Serial.println("[restore] restore started");
    }

    if (gRestoreUpload.owner != req || gRestoreUpload.authFailed ||
        gRestoreUpload.rejected || !gRestoreUpload.file) {
      return;
    }
    if (len > 0) {
      if (gRestoreUpload.file.write(data, len) != len) {
        gRestoreUpload.rejected = true;
        gRestoreUpload.rejectReason = "Unable to store restore upload";
        gRestoreUpload.file.close();
        SD.remove(BackupManager::TEMP_RESTORE_PATH);
        return;
      }
      gRestoreUpload.received += len;
      if (gRestoreUpload.received > 3 * 1024 * 1024) {
        gRestoreUpload.rejected = true;
        gRestoreUpload.rejectReason = "Backup file too large";
        gRestoreUpload.file.close();
        SD.remove(BackupManager::TEMP_RESTORE_PATH);
      }
    }
    if (final && gRestoreUpload.file) gRestoreUpload.file.close();
  };

  _server->on(
      "/api/settings/restore", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
        if (gRestoreUpload.active && gRestoreUpload.owner != req) {
          sendError(req, 409, "Another restore upload is active",
                    "RESTORE_BUSY");
          return;
        }
        if (gRestoreUpload.authFailed) {
          gRestoreUpload = RestoreUpload{};
          return;
        }
        if (!gRestoreUpload.authenticated && !requireOwnerAuth(req)) return;

        String error;
        bool restored = false;

        if (gRestoreUpload.active && gRestoreUpload.received > 0) {
          if (gRestoreUpload.rejected) {
            sendError(req, 400, gRestoreUpload.rejectReason, "RESTORE_FAILED");
            gRestoreUpload = RestoreUpload{};
            return;
          }
          restored = _backup.restoreFromFile(BackupManager::TEMP_RESTORE_PATH,
                                              error);
          SD.remove(BackupManager::TEMP_RESTORE_PATH);
          gRestoreUpload = RestoreUpload{};
        } else {
          DynamicJsonDocument body(RenzFiConfig::JSON_DOC_LARGE);
          String raw = getBody(req);
          if (raw.length() == 0) {
            sendError(req, 400, "Missing backup payload", "RESTORE_FAILED");
            return;
          }
          if (!SD.exists("/backup")) SD.mkdir("/backup");
          if (!_storage->writeSdText(BackupManager::TEMP_RESTORE_PATH, raw)) {
            sendError(req, 500, "Unable to stage restore payload",
                      "RESTORE_FAILED");
            return;
          }
          restored = _backup.restoreFromFile(BackupManager::TEMP_RESTORE_PATH,
                                             error);
          SD.remove(BackupManager::TEMP_RESTORE_PATH);
        }

        if (!restored) {
          sendError(req, 400, error.isEmpty() ? "Invalid backup file" : error,
                    "RESTORE_FAILED");
          return;
        }

        if (_portalConfig) _portalConfig->loadMeta();
        if (_assets) _assets->loadMetadata();
        if (_logger) _logger->infoLocal("restore", "restore completed");
        Serial.println("[restore] restore completed");

        DynamicJsonDocument data(128);
        data["rebooting"] = true;
        sendOk(req, data, "Restore complete — rebooting");
        delay(500);
        ESP.restart();
      },
      restoreUploadHandler, bodyCollect);

  // ── Firmware OTA ────────────────────────────────────────────────────────────
  // Rule #7 (Setup Simplification Pass): Firmware is Administrator-only.
  _server->on("/api/system/firmware", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
                if (!requireOwnerAuth(req)) return;
                DynamicJsonDocument data(512);
                data["version"] = RenzFiConfig::FIRMWARE_VERSION;
                data["build"] = __DATE__;
                data["sizeMb"] =
                    round((ESP.getSketchSize() / 1024.0 / 1024.0) * 10.0) / 10.0;
                {
                  const esp_partition_t *running = esp_ota_get_running_partition();
                  data["partition"] = running ? running->label : "unknown";
                }
                sendOk(req, data);
              });

  auto otaUploadHandler = [this](AsyncWebServerRequest *req, String filename,
                                 size_t index, uint8_t *data, size_t len,
                                 bool final) {
    if (index == 0) {
      gOtaUpload = FirmwareOtaUpload{};
      gOtaUpload.active = true;
      gOtaUpload.md5.begin();

      if (filename.endsWith(".ino")) {
        gOtaUpload.rejected = true;
        gOtaUpload.rejectReason = ".ino files are not accepted — upload .bin only";
        return;
      }
      if (!isBinFirmware(filename)) {
        gOtaUpload.rejected = true;
        gOtaUpload.rejectReason = "Only .bin firmware images are accepted";
        return;
      }

      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        gOtaUpload.rejected = true;
        gOtaUpload.rejectReason = "Unable to begin OTA update";
        if (_logger) _logger->errorLocal("firmware", gOtaUpload.rejectReason);
        return;
      }
      if (_logger) _logger->infoLocal("firmware", "OTA upload started: " + filename);
    }

    if (gOtaUpload.rejected) return;

    if (len > 0) {
      gOtaUpload.md5.add(data, len);
      if (Update.write(data, len) != len) {
        Update.abort();
        gOtaUpload.rejected = true;
        gOtaUpload.rejectReason = "Flash write failed during OTA";
        if (_logger) _logger->errorLocal("firmware", gOtaUpload.rejectReason);
        return;
      }
      gOtaUpload.received += len;
      if (_events) {
        DynamicJsonDocument progress(128);
        progress["phase"] = "upload";
        progress["received"] = gOtaUpload.received;
        String payload;
        serializeJson(progress, payload);
        _events->emit("firmware.progress", payload);
      }
    }

    if (final && !gOtaUpload.rejected && gOtaUpload.received > 0) {
      Serial.printf("[firmware] multipart final received=%u\n",
                    static_cast<unsigned>(gOtaUpload.received));
      gOtaUpload.md5.calculate();
      gOtaUpload.digest = gOtaUpload.md5.toString();

      if (_events) {
        DynamicJsonDocument progress(192);
        progress["phase"] = "verify";
        progress["md5"] = gOtaUpload.digest;
        String payload;
        serializeJson(progress, payload);
        _events->emit("firmware.progress", payload);
      }

      if (!Update.end(true)) {
        gOtaUpload.rejected = true;
        gOtaUpload.rejectReason = String("OTA verify failed: ") + Update.errorString();
        if (_logger) _logger->errorLocal("firmware", gOtaUpload.rejectReason);
        return;
      }
      gOtaUpload.finalized = true;

      if (_logger) {
        _logger->infoLocal("firmware",
                      "OTA complete md5=" + gOtaUpload.digest + " bytes=" +
                          String(gOtaUpload.received));
      }
      if (_events) {
        DynamicJsonDocument progress(128);
        progress["phase"] = "complete";
        progress["md5"] = gOtaUpload.digest;
        String payload;
        serializeJson(progress, payload);
        _events->emit("firmware.progress", payload);
      }
    }

    (void)req;
  };

  auto otaRawBodyCollect = [this](AsyncWebServerRequest *req, uint8_t *data,
                                  size_t len, size_t index, size_t total) {
    (void)req;
    if (total > 2800 * 1024) {
      gOtaUpload.rejected = true;
      gOtaUpload.rejectReason = "Firmware image too large";
      return;
    }
    if (index == 0) {
      gOtaUpload = FirmwareOtaUpload{};
      gOtaUpload.active = true;
      gOtaUpload.md5.begin();
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        gOtaUpload.rejected = true;
        gOtaUpload.rejectReason = "Unable to begin OTA update";
        if (_logger) _logger->errorLocal("firmware", gOtaUpload.rejectReason);
        return;
      }
      if (_logger) _logger->infoLocal("firmware", "OTA raw upload started");
    }
    if (gOtaUpload.rejected || len == 0) return;

    gOtaUpload.md5.add(data, len);
    if (Update.write(data, len) != len) {
      Update.abort();
      gOtaUpload.rejected = true;
      gOtaUpload.rejectReason = "Flash write failed during OTA";
      if (_logger) _logger->errorLocal("firmware", gOtaUpload.rejectReason);
      return;
    }
    gOtaUpload.received += len;
    if (_events) {
      DynamicJsonDocument progress(160);
      progress["phase"] = "upload";
      progress["received"] = gOtaUpload.received;
      progress["total"] = total;
      String payload;
      serializeJson(progress, payload);
      _events->emit("firmware.progress", payload);
    }
  };

  _server->on(
      "/api/system/firmware", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
        if (!requireOwnerAuth(req)) return;

        Serial.printf(
            "[firmware] POST complete active=%d finalized=%d rejected=%d "
            "received=%u\n",
            gOtaUpload.active, gOtaUpload.finalized, gOtaUpload.rejected,
            static_cast<unsigned>(gOtaUpload.received));

        if (gOtaUpload.rejected) {
          sendError(req, 400, gOtaUpload.rejectReason, "OTA_REJECTED");
          gOtaUpload = FirmwareOtaUpload{};
          return;
        }

        if (!gOtaUpload.active || gOtaUpload.received == 0) {
          sendError(req, 400, "No firmware binary received", "OTA_EMPTY");
          gOtaUpload = FirmwareOtaUpload{};
          return;
        }

        if (!gOtaUpload.finalized) {
          Serial.println("[firmware] POST finalize: md5.calculate()");
          gOtaUpload.md5.calculate();
          gOtaUpload.digest = gOtaUpload.md5.toString();
          if (_events) {
            DynamicJsonDocument progress(192);
            progress["phase"] = "verify";
            progress["md5"] = gOtaUpload.digest;
            String payload;
            serializeJson(progress, payload);
            _events->emit("firmware.progress", payload);
          }
          Serial.println("[firmware] POST finalize: Update.end()");
          if (!Update.end(true)) {
            gOtaUpload.rejected = true;
            gOtaUpload.rejectReason =
                String("OTA verify failed: ") + Update.errorString();
            if (_logger) _logger->errorLocal("firmware", gOtaUpload.rejectReason);
            sendError(req, 400, gOtaUpload.rejectReason, "OTA_REJECTED");
            gOtaUpload = FirmwareOtaUpload{};
            return;
          }
          gOtaUpload.finalized = true;
          if (_logger) {
            _logger->infoLocal("firmware",
                          "OTA complete md5=" + gOtaUpload.digest + " bytes=" +
                              String(gOtaUpload.received));
          }
          if (_events) {
            DynamicJsonDocument progress(128);
            progress["phase"] = "complete";
            progress["md5"] = gOtaUpload.digest;
            String payload;
            serializeJson(progress, payload);
            _events->emit("firmware.progress", payload);
          }
        }

        DynamicJsonDocument data(256);
        data["ok"] = true;
        data["bytes"] = gOtaUpload.received;
        data["md5"] = gOtaUpload.digest;
        data["rebooting"] = true;
        sendOk(req, data, "Firmware updated — rebooting");

        gOtaUpload = FirmwareOtaUpload{};
        delay(500);
        ESP.restart();
      },
      otaUploadHandler,
      otaRawBodyCollect);

  // ── Logs ──────────────────────────────────────────────────────────────────
  // Rule #7 (Setup Simplification Pass): Logs is Administrator-only.
  _server->on("/api/logs", HTTP_GET, [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireAuth(req)) return;
    String q = req->hasArg("q") ? req->arg("q") : "";
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_LARGE);
    if (_logger->list(data, q)) sendOk(req, data);
    else sendError(req, 500, "Unable to load logs", "STORAGE_ERROR");
  });

  _server->on("/api/logs", HTTP_DELETE, [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireOwnerAuth(req)) return;
    if (_logger->clear()) sendOk(req);
    else sendError(req, 500, "Unable to clear logs", "STORAGE_ERROR");
  });

  _server->on("/api/logs/export", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
                if (!requireOwnerAuth(req)) return;
                DynamicJsonDocument data(RenzFiConfig::JSON_DOC_LARGE);
                if (!_logger->exportRam(data)) {
                  sendError(req, 500, "Unable to export logs", "STORAGE_ERROR");
                  return;
                }
                String payload;
                serializeJson(data, payload);
                AsyncWebServerResponse *res =
                    req->beginResponse(200, "application/json", payload);
                addCorsHeaders(res);
                res->addHeader("Content-Disposition",
                               "attachment; filename=\"renz-fi-logs.json\"");
                req->send(res);
              });

  // ── Coin slot ─────────────────────────────────────────────────────────────
  _server->on("/api/coin/settings", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
                if (!requireAuth(req)) return;
                DynamicJsonDocument data(512);
                if (_coin) {
                  _coin->settings(data);
                } else {
                  fillCoinDisabledSettings(data);
                }
                sendOk(req, data);
              });

  auto coinSave = [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireAuth(req)) return;
    if (!_coin) {
      sendError(req, 503, "Coin slot hardware is disabled", "COIN_DISABLED");
      return;
    }
    DynamicJsonDocument body(512);
    String raw = getBody(req);
    if (raw.length() > 0) deserializeJson(body, raw);
    if (_coin->saveSettings(body.as<JsonObjectConst>())) sendOk(req);
    else sendError(req, 500, "Unable to save coin settings", "STORAGE_ERROR");
  };
  _server->on("/api/coin/settings", HTTP_POST, coinSave, nullptr, bodyCollect);
  _server->on("/api/coin/settings", HTTP_PUT,  coinSave, nullptr, bodyCollect);

  _server->on("/api/coin/diagnostics", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
                if (!requireAuth(req)) return;
                // Dashboard polls this every fallbackPollMs. JSON_DOC_SMALL
                // (2048) is below ALWAYSINTERNAL and lands in DMA SRAM.
                PsramJsonDocument dataHeap;
                JsonDocument &data = dataHeap.doc();
                if (_coin) {
                  _coin->diagnostics(data);
                } else {
                  fillCoinDisabledDiagnostics(data);
                }
                sendOk(req, data);
              });

  _server->on("/api/coin/test", HTTP_POST, [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireAuth(req)) return;
    if (!_coin) {
      sendError(req, 503, "Coin slot hardware is disabled", "COIN_DISABLED");
      return;
    }
    _sessions->grantCoinSession(1, _promos->minutesForAmount(1));
    sendOk(req);
  });

  _server->on("/api/coin/reset", HTTP_POST, [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireAuth(req)) return;
    if (!_coin) {
      sendError(req, 503, "Coin slot hardware is disabled", "COIN_DISABLED");
      return;
    }
    _coin->resetCounters();
    sendOk(req);
  });

  // ── Router / MikroTik ─────────────────────────────────────────────────────
  auto sendAdminEnqueueReject =
      [this](AsyncWebServerRequest *req,
             const RouterProvisioningWorker::EnqueueOutcome &outcome) {
        const char *code =
            (outcome.rejectCode && outcome.rejectCode[0])
                ? outcome.rejectCode
                : "ROUTER_WORKER_BUSY";
        const char *msg =
            (outcome.rejectMessage && outcome.rejectMessage[0])
                ? outcome.rejectMessage
                : "Router worker is busy";
        sendError(req, 503, msg, code);
      };

  _server->on("/api/router/settings", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
                if (!requireOwnerAuth(req)) return;
                DynamicJsonDocument data(RenzFiConfig::JSON_DOC_SMALL);
                if (_router->fillPublicSettings(data)) sendOk(req, data);
                else sendError(req, 500, "Unable to load router settings", "STORAGE_ERROR");
              });

  auto routerSave = [this, sendAdminEnqueueReject](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireOwnerAuth(req)) return;
    if (!_routerWorker) {
      sendError(req, 503, "Router worker unavailable", "INTERNAL_ERROR");
      return;
    }
    const auto outcome =
        _routerWorker->enqueueAdminSaveSettings(getBody(req));
    if (!outcome.accepted) {
      sendAdminEnqueueReject(req, outcome);
      return;
    }
    sendAdminJobAccepted(req, outcome.jobId, "admin-save-settings");
  };
  _server->on("/api/router/settings", HTTP_POST, routerSave, nullptr,
              bodyCollect);
  _server->on("/api/router/settings", HTTP_PUT, routerSave, nullptr,
              bodyCollect);

  _server->on("/api/router/profiles", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
                // Part 5 (UX Simplification Pass): Router is Administrator-only.
                if (!requireOwnerAuth(req)) return;
                DynamicJsonDocument result(RenzFiConfig::JSON_DOC_MEDIUM);
                if (_router && _router->listProfiles(result)) {
                  sendOk(req, result, "Profiles loaded from cache");
                } else {
                  const char *message = result["error"] | "Failed to load profiles";
                  sendOk(req, result, message);
                }
              });

  // One-shot profile refresh / rate-limit set / managed speed ensure via worker.
  auto routerProfilesOp = [this, sendAdminEnqueueReject](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireOwnerAuth(req)) return;
    if (!_routerWorker) {
      sendError(req, 503, "Router worker unavailable", "INTERNAL_ERROR");
      return;
    }
    String body = getBody(req);
    if (req->url() == "/api/router/profiles/refresh") {
      body = "{\"action\":\"refresh\"}";
    }
    const auto outcome = _routerWorker->enqueueAdminUserProfileOp(body);
    if (!outcome.accepted) {
      sendAdminEnqueueReject(req, outcome);
      return;
    }
    sendAdminJobAccepted(req, outcome.jobId, "admin-user-profile");
  };
  _server->on("/api/router/profiles/refresh", HTTP_POST, routerProfilesOp,
              nullptr, bodyCollect);
  _server->on("/api/router/profiles/op", HTTP_POST, routerProfilesOp, nullptr,
              bodyCollect);

  _server->on(
      "/api/router/test", HTTP_POST,
      [this, sendAdminEnqueueReject](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
        if (!requireOwnerAuth(req)) return;
        if (!_routerWorker) {
          sendError(req, 503, "Router worker unavailable", "INTERNAL_ERROR");
          return;
        }
        const auto outcome = _routerWorker->enqueueAdminTest(getBody(req));
        if (!outcome.accepted) {
          sendAdminEnqueueReject(req, outcome);
          return;
        }
        sendAdminJobAccepted(req, outcome.jobId, "admin-test");
      },
      nullptr, bodyCollect);

  // Cached RouterOS metadata — no live API on GET.
  _server->on("/api/router/wireless", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
                // Part 5 (UX Simplification Pass): Router is Administrator-only.
                if (!requireOwnerAuth(req)) return;
                DynamicJsonDocument result(RenzFiConfig::JSON_DOC_SMALL);
                const bool ok = _router && _router->fillWireless(result);
                if (_routerProvisioning) {
                  routerProvisioningFillWirelessStatus(
                      _routerProvisioning, result.as<JsonObject>());
                }
                if (_router) {
                  DynamicJsonDocument cacheDoc(RenzFiConfig::JSON_DOC_MEDIUM);
                  if (_router->fillRouterCache(cacheDoc)) {
                    String boardLower =
                        String(cacheDoc["routerOs"]["boardName"] | "") + " " +
                        String(cacheDoc["identity"] | "");
                    boardLower.toLowerCase();
                    if (boardLower.indexOf("hex") >= 0 ||
                        boardLower.indexOf("rb750") >= 0 ||
                        boardLower.indexOf("rb760") >= 0) {
                      result["externalApOnly"] = true;
                      result["noWirelessCapabilityDetected"] = true;
                      result["configured"] = false;
                    }
                  }
                }
                if (result["externalApOnly"] | false) {
                  result.remove("interface");
                  result.remove("interfaceId");
                  result.remove("band");
                  result["guestTopologyMode"] = "external_access_point";
                }
                const char *message =
                    ok ? "Wireless settings loaded from cache"
                       : (result["error"] | "Failed to load wireless settings");
                sendOk(req, result, message);
              });

  auto routerWirelessSave = [this, sendAdminEnqueueReject](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireOwnerAuth(req)) return;
    if (!_routerWorker) {
      sendError(req, 503, "Router worker unavailable", "INTERNAL_ERROR");
      return;
    }
    const auto outcome =
        _routerWorker->enqueueAdminSaveWireless(getBody(req));
    if (!outcome.accepted) {
      sendAdminEnqueueReject(req, outcome);
      return;
    }
    sendAdminJobAccepted(req, outcome.jobId, "admin-save-wireless");
  };
  _server->on("/api/router/wireless", HTTP_POST, routerWirelessSave, nullptr, bodyCollect);
  _server->on("/api/router/wireless", HTTP_PUT,  routerWirelessSave, nullptr, bodyCollect);

  _server->on("/api/router/cache", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
                RENZFI_PROD_GATE(req);
                // Part 5 (UX Simplification Pass): Router is Administrator-only.
                if (!requireOwnerAuth(req)) return;
                DynamicJsonDocument result(RenzFiConfig::JSON_DOC_MEDIUM);
                if (_router && _router->fillRouterCache(result)) {
                  sendOk(req, result, "Router cache loaded");
                } else {
                  sendOk(req, result, result["error"] | "Router cache unavailable");
                }
              });

  auto routerCacheEnqueue = [this, sendAdminEnqueueReject](
                                   AsyncWebServerRequest *req,
                                   bool telemetryRefresh,
                                   const char *successMessage,
                                   const char *jobTypeLabel) {
    RENZFI_PROD_GATE(req);
    if (!requireOwnerAuth(req)) return;
    if (!_routerWorker) {
      sendError(req, 503, "Router worker unavailable", "INTERNAL_ERROR");
      return;
    }
    const auto outcome =
        telemetryRefresh
            ? _routerWorker->enqueueAdminRefreshCache(successMessage)
            : _routerWorker->enqueueAdminSyncCache(successMessage);
    if (!outcome.accepted) {
      sendAdminEnqueueReject(req, outcome);
      return;
    }
    sendAdminJobAccepted(req, outcome.jobId, jobTypeLabel);
  };

  _server->on(
      "/api/router/cache/sync", HTTP_POST,
      [this, routerCacheEnqueue](AsyncWebServerRequest *req) {
        routerCacheEnqueue(req, false, "Router configuration synchronized",
                           "admin-sync-cache");
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/router/cache/refresh", HTTP_POST,
      [this, routerCacheEnqueue](AsyncWebServerRequest *req) {
        routerCacheEnqueue(req, true, "Router information refreshed",
                           "admin-refresh-cache");
      },
      nullptr, bodyCollect);

  _server->on("/api/router/jobs/*", HTTP_GET, [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireOwnerAuth(req)) return;
    if (!_routerWorker) {
      sendError(req, 503, "Router worker unavailable", "INTERNAL_ERROR");
      return;
    }
    const String url = req->url();
    const int marker = url.lastIndexOf('/');
    if (marker < 0 || marker + 1 >= static_cast<int>(url.length())) {
      sendError(req, 400, "Job id required", "INVALID_JOB_ID");
      return;
    }
    const uint32_t jobId =
        static_cast<uint32_t>(strtoul(url.c_str() + marker + 1, nullptr, 10));
    if (jobId == 0) {
      sendError(req, 400, "Job id required", "INVALID_JOB_ID");
      return;
    }
    RouterProvisioningWorker::JobRecord job;
    if (!_routerWorker->pollJob(jobId, job)) {
      sendError(req, 404, "Job not found", "JOB_NOT_FOUND");
      return;
    }
    // Admin Test/Sync envelopes include profile inventories — must fit the
    // nested result JSON. A 384-byte doc truncates/drops result and the UI
    // falsely shows "API unreachable / login failed" despite worker success.
    const size_t capacity =
        RenzFiConfig::JSON_DOC_MEDIUM +
        (job.result.body.length() > 0 ? job.result.body.length() : 0);
    DynamicJsonDocument doc(capacity);
    doc["success"] = true;
    JsonObject data = doc["data"].to<JsonObject>();
    data["jobId"] = job.jobId;
    data["state"] = RouterProvisioningWorker::jobStateLabel(job.state);
    const bool terminal =
        job.state == RouterProvisioningWorker::JobState::Completed ||
        job.state == RouterProvisioningWorker::JobState::Failed;
    if (terminal) {
      data["httpStatus"] = job.result.httpStatus;
      if (!job.result.body.isEmpty()) {
        data["result"] = serialized(job.result.body);
      }
    }
    if (!job.stageId.isEmpty()) data["stage"] = job.stageId;
    if (!job.stageLabel.isEmpty()) data["label"] = job.stageLabel;
    String body;
    serializeJson(doc, body);
    logRequest(req, "admin-router-job-poll");
    AsyncWebServerResponse *res =
        req->beginResponse(200, "application/json", body);
    addCorsHeaders(res);
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
  });

  // ── Ethernet network config (DHCP-first, optional static) ────────────────
  // GET is intentionally readable pre-FullAccess (Session only) so the
  // first-run setup wizard can show current mode/MAC before a password has
  // been chosen; POST/PUT require FullAccess like other provisioning writes.
  _server->on("/api/system/wifi/config", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
                if (!requireAuth(req, AuthRequirement::Session)) return;
                const NetworkSettings settings = _networkSettings
                    ? _networkSettings->settings()
                    : NetworkSettingsManager::factoryDefaults();

                DynamicJsonDocument data(RenzFiConfig::JSON_DOC_SMALL);
                data["addressMode"] = ethernetAddressModeLabel(settings.addressMode);
                data["provisioned"] = settings.provisioned;
                data["staticIp"]           = settings.staticIp;
                data["staticGateway"]      = settings.staticGateway;
                data["staticSubnetMask"]   = settings.staticSubnetMask;
                data["staticDnsPrimary"]   = settings.staticDnsPrimary;
                data["staticDnsSecondary"] = settings.staticDnsSecondary;

                // Live driver state — reflects what's actually running now,
                // which may differ from the saved config until next reboot.
                data["current"]["mode"]    = _eth ? _eth->addressModeLabel() : "dhcp";
                data["current"]["ip"]      = _eth ? _eth->ip() : "";
                data["current"]["gateway"] = _eth ? _eth->gateway() : "";
                data["current"]["netmask"] = _eth ? _eth->subnet() : "";
                data["current"]["dns"]     = _eth ? _eth->dns() : "";
                data["current"]["mac"]     = _eth ? _eth->macAddress() : "";

                // DHCP-reservation guidance for the setup wizard (MikroTik).
                const String mac = _eth ? _eth->macAddress() : "";
                data["dhcpReservation"]["mac"] = mac;
                data["dhcpReservation"]["routerOsExample"] =
                    mac.length() > 0
                        ? "/ip dhcp-server lease add mac-address=" + mac +
                              " address=<desired-ip> server=<dhcp-server-name>"
                        : "";

                sendOk(req, data);
              });

  auto wifiConfigWrite = [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireOwnerAuth(req)) return;

    DynamicJsonDocument body(RenzFiConfig::JSON_DOC_SMALL);
    String raw = getBody(req);
    if (raw.length() > 0) deserializeJson(body, raw);

    if (!_networkSettings) {
      sendError(req, 500, "Network settings unavailable", "NETWORK_SETTINGS_UNAVAILABLE");
      return;
    }

    NetworkSettings settings = _networkSettings->settings();

    const char *modeStr = body["addressMode"] | ethernetAddressModeLabel(settings.addressMode);
    settings.addressMode = parseEthernetAddressMode(modeStr);
    if (!body["staticIp"].isNull())           settings.staticIp = body["staticIp"].as<const char *>();
    if (!body["staticGateway"].isNull())      settings.staticGateway = body["staticGateway"].as<const char *>();
    if (!body["staticSubnetMask"].isNull())   settings.staticSubnetMask = body["staticSubnetMask"].as<const char *>();
    if (!body["staticDnsPrimary"].isNull())   settings.staticDnsPrimary = body["staticDnsPrimary"].as<const char *>();
    if (!body["staticDnsSecondary"].isNull()) settings.staticDnsSecondary = body["staticDnsSecondary"].as<const char *>();
    settings.provisioned = true;

    String validationError;
    if (!_networkSettings->save(settings, &validationError)) {
      sendError(req, 400,
                validationError.length() > 0 ? validationError : "Invalid network configuration",
                "INVALID_NETWORK_CONFIG");
      return;
    }

    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_SMALL);
    data["addressMode"] = ethernetAddressModeLabel(settings.addressMode);
    data["provisioned"] = settings.provisioned;
    data["rebootRequired"] = true;
    sendOk(req, data,
          "Network settings saved. Reboot the appliance to apply the new Ethernet configuration.");
  };
  _server->on("/api/system/wifi/config", HTTP_POST, wifiConfigWrite, nullptr, bodyCollect);
  _server->on("/api/system/wifi/config", HTTP_PUT,  wifiConfigWrite, nullptr, bodyCollect);

  // External Access Point registry (Stage B CRUD) + Stage C check jobs.
  // Exact collection match so POST /api/access-points/{id}/check cannot hit create.
  // Job GET is registered before /api/access-points/* so jobs/{id} is not stolen.
  _server->on(AsyncURIMatcher::exact("/api/access-points"), HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireOwnerAuth(req)) return;
    if (!_accessPoints) {
      sendError(req, 503, "Access point registry unavailable", "INTERNAL_ERROR");
      return;
    }
    PsramJsonDocument heap;
    _accessPoints->fillList(heap.doc());
    sendOk(req, heap.doc());
  });

  _server->on(
      AsyncURIMatcher::exact("/api/access-points"), HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        RENZFI_PROD_GATE(req);
        if (!requireOwnerAuth(req)) return;
        if (!_accessPoints) {
          sendError(req, 503, "Access point registry unavailable", "INTERNAL_ERROR");
          return;
        }
        PsramJsonDocument bodyHeap;
        const String raw = getBody(req);
        if (raw.length() == 0) {
          sendError(req, 400, "Request body required", "INVALID_REQUEST");
          return;
        }
        if (deserializeJson(bodyHeap.doc(), raw)) {
          sendError(req, 400, "Invalid JSON body", "INVALID_REQUEST");
          return;
        }
        PsramJsonDocument outHeap;
        const ExternalAccessPoint::CrudStatus status =
            _accessPoints->create(bodyHeap.doc().as<JsonObjectConst>(),
                                  outHeap.doc());
        if (status != ExternalAccessPoint::CrudStatus::Ok) {
          sendError(req, ExternalAccessPoint::crudHttpStatus(status),
                    ExternalAccessPoint::crudMessage(status),
                    ExternalAccessPoint::crudCode(status));
          return;
        }
        sendOk(req, outHeap.doc(), 201, "Access point created");
      },
      nullptr, bodyCollect);

  _server->on("/api/access-points/jobs/*", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireOwnerAuth(req)) return;
    uint32_t jobId = 0;
    if (!parseExternalAccessPointJobPath(req->url(), jobId)) {
      sendError(req, 404, "Access point job not found", "NOT_FOUND");
      return;
    }

    // Prefer MikroTik-backed Check jobs (Router Worker), then legacy Sync jobs.
    if (_routerWorker) {
      RouterProvisioningWorker::JobRecord job;
      if (_routerWorker->pollJob(jobId, job) &&
          job.opType == "access-point-check") {
        PsramJsonDocument dataHeap;
        JsonObject data = dataHeap.doc().to<JsonObject>();
        data["jobId"] = job.jobId;
        data["state"] = RouterProvisioningWorker::jobStateLabel(job.state);
        const bool terminal =
            job.state == RouterProvisioningWorker::JobState::Completed ||
            job.state == RouterProvisioningWorker::JobState::Failed;
        if (terminal) {
          data["ok"] = job.result.ok;
          data["httpStatus"] = job.result.httpStatus;
          bool online = false;
          String accessPointId;
          String ipAddress;
          String method = "arp_ping";
          String status = "unknown";
          String message;
          String errorCode;
          if (!job.result.body.isEmpty()) {
            HeapJsonDocument parsed(RenzFiConfig::JSON_DOC_MEDIUM);
            if (!deserializeJson(parsed.doc(), job.result.body)) {
              JsonObjectConst root = parsed.doc().as<JsonObjectConst>();
              JsonObjectConst payload =
                  root["data"].is<JsonObjectConst>()
                      ? root["data"].as<JsonObjectConst>()
                      : root;
              online = payload["online"] | false;
              accessPointId = payload["accessPointId"] | "";
              ipAddress = payload["ipAddress"] | payload["managementIp"] | "";
              method = payload["method"] | "arp_ping";
              status = payload["status"] | (online ? "online" : "unreachable");
              message = root["message"] | "";
              errorCode = payload["errorCode"] | root["code"] | "";
              if (message.isEmpty()) {
                message = online ? "Online" : "Offline";
              }
            } else if (!job.result.ok) {
              status = "unknown";
              message = "Access point check failed";
              errorCode = "AP_CHECK_FAILED";
            }
          } else if (!job.result.ok) {
            status = "unknown";
            message = "Access point check failed";
            errorCode = "AP_CHECK_FAILED";
          }

          if (accessPointId.length() > 0) data["accessPointId"] = accessPointId;
          if (ipAddress.length() > 0) {
            data["ipAddress"] = ipAddress;
            data["managementIp"] = ipAddress;
          }
          data["online"] = online;
          data["method"] = method;
          data["status"] = status;
          data["latencyMs"] = nullptr;
          if (errorCode.length() > 0) data["errorCode"] = errorCode;
          if (message.length() > 0) data["message"] = message;

          if (job.state == RouterProvisioningWorker::JobState::Completed &&
              _accessPoints && accessPointId.length() > 0 &&
              (status == "online" || status == "unreachable")) {
            _accessPoints->applyMikroTikCheckResult(accessPointId, online,
                                                    method.c_str(), 0);
          }
        }
        sendOk(req, dataHeap.doc(), "Access point job");
        return;
      }
    }

    if (!_accessPoints) {
      sendError(req, 503, "Access point registry unavailable", "INTERNAL_ERROR");
      return;
    }
    ExternalAccessPointManager::CheckJobSnapshot snap;
    if (!_accessPoints->pollCheckJob(jobId, snap)) {
      sendError(req, 404, "Access point job not found", "NOT_FOUND");
      return;
    }
    PsramJsonDocument dataHeap;
    JsonObject data = dataHeap.doc().to<JsonObject>();
    data["jobId"] = snap.jobId;
    data["accessPointId"] = snap.accessPointId;
    data["state"] = snap.state;
    data["ok"] = snap.ok;
    data["status"] = snap.status;
    data["online"] =
        snap.ok && snap.status &&
        (strcmp(snap.status, "online") == 0 ||
         strcmp(snap.status, "network_reachable") == 0 ||
         strcmp(snap.status, "management_reachable") == 0);
    if (snap.latencyValid) {
      data["latencyMs"] = snap.latencyMs;
    } else {
      data["latencyMs"] = nullptr;
    }
    data["startedAt"] = snap.startedAt;
    data["completedAt"] = snap.completedAt;
    if (snap.errorCode) data["errorCode"] = snap.errorCode;
    if (snap.message[0] != '\0') data["message"] = snap.message;
    sendOk(req, dataHeap.doc(), "Access point job");
  });

  _server->on(AsyncURIMatcher::exact("/api/access-points/detect"), HTTP_POST,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireOwnerAuth(req)) return;
    if (!_routerWorker || !_accessPoints) {
      sendError(req, 503, "Access point detection unavailable", "INTERNAL_ERROR");
      return;
    }

    PsramJsonDocument listHeap;
    _accessPoints->fillList(listHeap.doc());
    PsramJsonDocument detectReq;
    JsonArray registered = detectReq.doc()["registeredIps"].to<JsonArray>();
    if (listHeap.doc()["accessPoints"].is<JsonArrayConst>()) {
      for (JsonObjectConst row : listHeap.doc()["accessPoints"].as<JsonArrayConst>()) {
        const String ip = row["managementIp"] | "";
        if (ip.length() > 0) registered.add(ip);
      }
    }
    String requestJson;
    serializeJson(detectReq.doc(), requestJson);

    const auto outcome = _routerWorker->enqueueAccessPointDetect(requestJson);
    if (!outcome.accepted) {
      const char *code = outcome.rejectCode ? outcome.rejectCode : "ROUTER_WORKER_BUSY";
      const char *message = outcome.rejectMessage ? outcome.rejectMessage
                                                  : "Router worker is busy";
      sendError(req, 503, message, code);
      return;
    }

    PsramJsonDocument dataHeap;
    JsonObject data = dataHeap.doc().to<JsonObject>();
    data["jobId"] = outcome.jobId;
    data["state"] = "queued";
    sendOk(req, dataHeap.doc(), 202, "Access point detect queued");
  }, nullptr, bodyCollect);

  _server->on("/api/access-points/detect/jobs/*", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireOwnerAuth(req)) return;
    if (!_routerWorker) {
      sendError(req, 503, "Router worker unavailable", "INTERNAL_ERROR");
      return;
    }
    uint32_t jobId = 0;
    if (!parseExternalAccessPointDetectJobPath(req->url(), jobId)) {
      sendError(req, 404, "Access point detect job not found", "NOT_FOUND");
      return;
    }
    RouterProvisioningWorker::JobRecord job;
    if (!_routerWorker->pollJob(jobId, job)) {
      sendError(req, 404, "Access point detect job not found", "NOT_FOUND");
      return;
    }
    PsramJsonDocument dataHeap;
    JsonObject data = dataHeap.doc().to<JsonObject>();
    data["jobId"] = job.jobId;
    data["state"] = RouterProvisioningWorker::jobStateLabel(job.state);
    const bool terminal =
        job.state == RouterProvisioningWorker::JobState::Completed ||
        job.state == RouterProvisioningWorker::JobState::Failed;
    if (terminal) {
      data["ok"] = job.result.ok;
      if (!job.result.body.isEmpty()) data["result"] = serialized(job.result.body);
      data["httpStatus"] = job.result.httpStatus;
    }
    sendOk(req, dataHeap.doc(), "Access point detect job");
  });

  _server->on("/api/access-points/*", HTTP_POST,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireOwnerAuth(req)) return;
    if (!_accessPoints) {
      sendError(req, 503, "Access point registry unavailable", "INTERNAL_ERROR");
      return;
    }
    String syncId;
    if (parseExternalAccessPointSyncPath(req->url(), syncId)) {
      uint32_t jobId = 0;
      const ExternalAccessPoint::CheckEnqueueStatus status =
          _accessPoints->enqueueSync(syncId, jobId);
      if (status != ExternalAccessPoint::CheckEnqueueStatus::Ok) {
        sendError(req, ExternalAccessPoint::checkEnqueueHttpStatus(status),
                  ExternalAccessPoint::checkEnqueueMessage(status),
                  ExternalAccessPoint::checkEnqueueCode(status));
        return;
      }
      PsramJsonDocument dataHeap;
      JsonObject data = dataHeap.doc().to<JsonObject>();
      data["jobId"] = jobId;
      data["accessPointId"] = syncId;
      data["state"] = "queued";
      sendOk(req, dataHeap.doc(), 202, "Access point sync queued");
      return;
    }
    String id;
    if (!parseExternalAccessPointCheckPath(req->url(), id)) {
      sendError(req, 404, "Access point not found", "NOT_FOUND");
      return;
    }
    if (!_routerWorker || !_accessPoints) {
      sendError(req, 503, "Access point check unavailable", "INTERNAL_ERROR");
      return;
    }

    PsramJsonDocument apHeap;
    const ExternalAccessPoint::CrudStatus getStatus =
        _accessPoints->getById(id, apHeap.doc());
    if (getStatus != ExternalAccessPoint::CrudStatus::Ok) {
      sendError(req, ExternalAccessPoint::crudHttpStatus(getStatus),
                ExternalAccessPoint::crudMessage(getStatus),
                ExternalAccessPoint::crudCode(getStatus));
      return;
    }
    const String managementIp = apHeap.doc()["managementIp"] | "";
    const bool enabled = apHeap.doc()["enabled"] | false;
    if (!enabled) {
      sendError(req, 400, "Access point is disabled", "ACCESS_POINT_DISABLED");
      return;
    }
    if (managementIp.length() == 0) {
      sendError(req, 400, "Access point management IP missing", "INVALID_IP");
      return;
    }

    DynamicJsonDocument checkReq(RenzFiConfig::JSON_DOC_SMALL);
    checkReq["accessPointId"] = id;
    checkReq["managementIp"] = managementIp;
    String requestJson;
    serializeJson(checkReq, requestJson);

    const auto outcome = _routerWorker->enqueueAccessPointCheck(requestJson);
    if (!outcome.accepted) {
      const char *code = outcome.rejectCode ? outcome.rejectCode : "ROUTER_WORKER_BUSY";
      const char *message = outcome.rejectMessage ? outcome.rejectMessage
                                                  : "Router worker is busy";
      sendError(req, 503, message, code);
      return;
    }

    PsramJsonDocument dataHeap;
    JsonObject data = dataHeap.doc().to<JsonObject>();
    data["jobId"] = outcome.jobId;
    data["accessPointId"] = id;
    data["state"] = "queued";
    sendOk(req, dataHeap.doc(), 202, "Access point check queued");
  });

  auto accessPointItem = [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireOwnerAuth(req)) return;
    if (!_accessPoints) {
      sendError(req, 503, "Access point registry unavailable", "INTERNAL_ERROR");
      return;
    }
    String id;
    if (!parseExternalAccessPointId(req->url(), id)) {
      sendError(req, 404, "Access point not found", "NOT_FOUND");
      return;
    }
    const int method = req->method();
    if (method == HTTP_GET) {
      PsramJsonDocument outHeap;
      const ExternalAccessPoint::CrudStatus status =
          _accessPoints->getById(id, outHeap.doc());
      if (status != ExternalAccessPoint::CrudStatus::Ok) {
        sendError(req, ExternalAccessPoint::crudHttpStatus(status),
                  ExternalAccessPoint::crudMessage(status),
                  ExternalAccessPoint::crudCode(status));
        return;
      }
      sendOk(req, outHeap.doc());
      return;
    }
    if (method == HTTP_PUT) {
      PsramJsonDocument bodyHeap;
      const String raw = getBody(req);
      if (raw.length() == 0) {
        sendError(req, 400, "Request body required", "INVALID_REQUEST");
        return;
      }
      if (deserializeJson(bodyHeap.doc(), raw)) {
        sendError(req, 400, "Invalid JSON body", "INVALID_REQUEST");
        return;
      }
      PsramJsonDocument outHeap;
      const ExternalAccessPoint::CrudStatus status =
          _accessPoints->update(id, bodyHeap.doc().as<JsonObjectConst>(),
                                outHeap.doc());
      if (status != ExternalAccessPoint::CrudStatus::Ok) {
        sendError(req, ExternalAccessPoint::crudHttpStatus(status),
                  ExternalAccessPoint::crudMessage(status),
                  ExternalAccessPoint::crudCode(status));
        return;
      }
      sendOk(req, outHeap.doc(), "Access point updated");
      return;
    }
    if (method == HTTP_DELETE) {
      const ExternalAccessPoint::CrudStatus status = _accessPoints->remove(id);
      if (status != ExternalAccessPoint::CrudStatus::Ok) {
        sendError(req, ExternalAccessPoint::crudHttpStatus(status),
                  ExternalAccessPoint::crudMessage(status),
                  ExternalAccessPoint::crudCode(status));
        return;
      }
      PsramJsonDocument outHeap;
      JsonObject data = outHeap.doc().to<JsonObject>();
      data["ok"] = true;
      data["id"] = id;
      sendOk(req, outHeap.doc(), "Access point removed");
      return;
    }
    sendError(req, 405, "Method not allowed", "METHOD_NOT_ALLOWED");
  };
  _server->on("/api/access-points/*", HTTP_GET, accessPointItem);
  _server->on("/api/access-points/*", HTTP_PUT, accessPointItem, nullptr, bodyCollect);
  _server->on("/api/access-points/*", HTTP_DELETE, accessPointItem);

  // Content filtering — guest-network domain blocking (owner, Router Worker apply).
  _server->on(AsyncURIMatcher::exact("/api/content-filter"), HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireOwnerAuth(req)) return;
    if (!_contentFilter) {
      sendError(req, 503, "Content filter unavailable", "INTERNAL_ERROR");
      return;
    }
    PsramJsonDocument heap;
    _contentFilter->fillList(heap.doc());
    sendOk(req, heap.doc());
  });

  _server->on(
      AsyncURIMatcher::exact("/api/content-filter"), HTTP_PUT,
      [this](AsyncWebServerRequest *req) {
        RENZFI_PROD_GATE(req);
        if (!requireOwnerAuth(req)) return;
        if (!_contentFilter || !_routerWorker) {
          sendError(req, 503, "Content filter unavailable", "INTERNAL_ERROR");
          return;
        }
        PsramJsonDocument bodyHeap;
        const String raw = getBody(req);
        if (raw.length() == 0 ||
            deserializeJson(bodyHeap.doc(), raw) ||
            bodyHeap.doc()["enabled"].isNull()) {
          sendError(req, 400, "Invalid JSON body", "INVALID_REQUEST");
          return;
        }
        const bool enabled = bodyHeap.doc()["enabled"].as<bool>();
        const auto status = _contentFilter->setEnabled(enabled);
        enqueueContentFilterSyncOrError(req, "Content filter sync queued", status);
      },
      nullptr, bodyCollect);

  _server->on(
      AsyncURIMatcher::exact("/api/content-filter/domains"), HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        RENZFI_PROD_GATE(req);
        if (!requireOwnerAuth(req)) return;
        if (!_contentFilter || !_routerWorker) {
          sendError(req, 503, "Content filter unavailable", "INTERNAL_ERROR");
          return;
        }
        PsramJsonDocument bodyHeap;
        const String raw = getBody(req);
        if (raw.length() == 0 || deserializeJson(bodyHeap.doc(), raw)) {
          sendError(req, 400, "Invalid JSON body", "INVALID_REQUEST");
          return;
        }
        String normalized;
        const auto status =
            _contentFilter->addDomain(bodyHeap.doc()["domain"] | "", normalized);
        enqueueContentFilterSyncOrError(req, "Blocked domain queued for MikroTik apply",
                                        status, "domain", &normalized);
      },
      nullptr, bodyCollect);

  _server->on(
      AsyncURIMatcher::exact("/api/content-filter/sync"), HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        RENZFI_PROD_GATE(req);
        if (!requireOwnerAuth(req)) return;
        enqueueContentFilterSyncOrError(req, "Content filter sync queued");
      });

  _server->on("/api/content-filter/jobs/*", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireOwnerAuth(req)) return;
    RouterProvisioningWorker::JobRecord job;
    if (!pollWorkerJobOrError(req, "/api/content-filter/jobs/",
                              "content-filter-sync",
                              "Content filter job not found", job)) {
      return;
    }
    const bool terminal =
        job.state == RouterProvisioningWorker::JobState::Completed ||
        job.state == RouterProvisioningWorker::JobState::Failed;
    if (terminal && _contentFilter && job.result.body.length() > 0) {
      HeapJsonDocument resultDoc(RenzFiConfig::JSON_DOC_SMALL);
      if (!deserializeJson(resultDoc.doc(), job.result.body)) {
        JsonObjectConst payload = resultDoc.doc().as<JsonObjectConst>();
        JsonObjectConst inner = payload["data"].as<JsonObjectConst>();
        const char *message = payload["message"] | "";
        _contentFilter->applySyncResult(inner, job.result.ok, String(message));
      }
    }
    sendWorkerJobPollOk(req, job);
  });

  _server->on("/api/content-filter/domains/*", HTTP_DELETE,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireOwnerAuth(req)) return;
    if (!_contentFilter || !_routerWorker) {
      sendError(req, 503, "Content filter unavailable", "INTERNAL_ERROR");
      return;
    }
    const String prefix = "/api/content-filter/domains/";
    String domain = req->url().substring(prefix.length());
    domain.trim();
    if (domain.isEmpty()) {
      sendError(req, 400, "Domain is required", "INVALID_REQUEST");
      return;
    }
    domain.replace("%2E", ".");
    domain.replace("%2e", ".");
    const auto status = _contentFilter->removeDomain(domain);
    enqueueContentFilterSyncOrError(req, "Blocked domain removal queued", status);
  });

  // Gaming priority — guest-network QoS pilot (owner, manual Router Worker apply).
  _server->on(AsyncURIMatcher::exact("/api/gaming-priority"), HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireOwnerAuth(req)) return;
    if (!_gamingPriority) {
      sendError(req, 503, "Gaming priority unavailable", "INTERNAL_ERROR");
      return;
    }
    PsramJsonDocument heap;
    _gamingPriority->fillJson(heap.doc());
    sendOk(req, heap.doc());
  });

  _server->on(
      AsyncURIMatcher::exact("/api/gaming-priority"), HTTP_PUT,
      [this](AsyncWebServerRequest *req) {
        RENZFI_PROD_GATE(req);
        if (!requireOwnerAuth(req)) return;
        if (!_gamingPriority) {
          sendError(req, 503, "Gaming priority unavailable", "INTERNAL_ERROR");
          return;
        }
        PsramJsonDocument bodyHeap;
        const String raw = getBody(req);
        if (raw.length() == 0 || deserializeJson(bodyHeap.doc(), raw)) {
          sendError(req, 400, "Invalid JSON body", "INVALID_REQUEST");
          return;
        }
        String error;
        if (!_gamingPriority->updateFromJson(bodyHeap.doc().as<JsonObjectConst>(),
                                             error)) {
          sendError(req, 400, error, "INVALID_GAMING_PRIORITY");
          return;
        }
        PsramJsonDocument outHeap;
        _gamingPriority->fillJson(outHeap.doc());
        sendOk(req, outHeap.doc(), 200, "Gaming priority configuration saved");
      },
      nullptr, bodyCollect);

  _server->on(
      AsyncURIMatcher::exact("/api/gaming-priority/apply"), HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        RENZFI_PROD_GATE(req);
        if (!requireOwnerAuth(req)) return;
        enqueueGamingPrioritySyncOrError(req, "Gaming priority apply queued");
      });

  _server->on("/api/gaming-priority/jobs/*", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PROD_GATE(req);
    if (!requireOwnerAuth(req)) return;
    RouterProvisioningWorker::JobRecord job;
    if (!pollWorkerJobOrError(req, "/api/gaming-priority/jobs/",
                              "gaming-priority-sync",
                              "Gaming priority job not found", job)) {
      return;
    }
    const bool terminal =
        job.state == RouterProvisioningWorker::JobState::Completed ||
        job.state == RouterProvisioningWorker::JobState::Failed;
    if (terminal && _gamingPriority && job.result.body.length() > 0) {
      HeapJsonDocument resultDoc(RenzFiConfig::JSON_DOC_SMALL);
      if (!deserializeJson(resultDoc.doc(), job.result.body)) {
        const char *message = resultDoc.doc()["message"] | "";
        _gamingPriority->applySyncResult(job.result.ok, String(message));
      }
    }
    sendWorkerJobPollOk(req, job);
  });

  // ── Portal session API (/api/portal/*) ────────────────────────────────────
  // These endpoints are intentionally open (no requireAuth) — called by the
  // MikroTik-hosted captive portal JS from the customer's device.
  // Use RENZFI_PORTAL_GATE (not management) so Hotspot guests may call them.

  // Explicit CORS preflight for cross-origin JSON POSTs from the MikroTik
  // Hotspot origin. Prefix match (/api/portal/*) so OPTIONS for terminate,
  // heartbeat, etc. is handled here — not only via onNotFound fallback.
  // Policy is identical to WebResponse::serveOptions / addCorsHeaders.
  _server->on("/api/portal/*", HTTP_OPTIONS, [this](AsyncWebServerRequest *req) {
    RENZFI_PORTAL_GATE(req);
    WebResponse::serveOptions(req);
  });

  _server->on("/api/portal/branding", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PORTAL_GATE(req);
                NetworkDiagnostics::logPortalApiDebug(req, "/api/portal/branding");
                if (!_portalConfig) {
                  sendError(req, 500, "Portal config unavailable", "NOT_READY");
                  return;
                }
                HeapJsonDocument dataHeap(512);
                String base = (_eth && _eth->hasIp())
                                  ? ("http://" + _eth->ip())
                                  : String(ManagementApConfig::PORTAL_URL);
                _portalConfig->fillBrandingJson(dataHeap.doc().to<JsonObject>(), base);
                sendOk(req, dataHeap.doc());
              });

  _server->on("/api/portal/session", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PORTAL_GATE(req);
                NetworkDiagnostics::logPortalApiDebug(req, "/api/portal/session");
                String mac = req->hasArg("mac") ? req->arg("mac") : "";
                String ip  = req->hasArg("ip")  ? req->arg("ip")  : "";
                if (mac.isEmpty()) {
                  sendError(req, 400, "mac parameter required", "MISSING_MAC");
                  return;
                }
                PsramJsonDocument docHeap;
                if (_portalSessions &&
                    _portalSessions->getSession(mac, ip, docHeap.doc()))
                  sendOk(req, docHeap.doc());
                else
                  sendError(req, 500, "Failed to load session", "SESSION_ERROR");
              });

  _server->on(
      "/api/portal/start-coin-session", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_PORTAL_GATE(req);
        NetworkDiagnostics::logPortalApiDebug(req, "/api/portal/start-coin-session");
        HeapJsonDocument bodyHeap(512);
        DynamicJsonDocument &body = bodyHeap.doc();
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        String mac = body["mac"] | "";
        String ip  = body["ip"]  | "";
        if (mac.isEmpty()) {
          sendError(req, 400, "mac field required", "MISSING_MAC");
          return;
        }
        if (!_coin) {
          sendError(req, 503, "Coin slot hardware is disabled", "COIN_DISABLED");
          return;
        }
        if (_portalSessions && _portalSessions->startCoinWindow(mac, ip)) {
          PsramJsonDocument outHeap;
          _portalSessions->getSession(mac, ip, outHeap.doc());
          sendOk(req, outHeap.doc(), "Coin window opened");
        } else {
          sendError(req, 500, "Failed to open coin window", "SESSION_ERROR");
        }
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/portal/done-paying", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_PORTAL_GATE(req);
        logRequest(req, "portal.done-paying");
        Serial.printf("[portal] heap before done-paying free=%u min=%u\n",
                      (unsigned)ESP.getFreeHeap(),
                      (unsigned)ESP.getMinFreeHeap());
        HeapJsonDocument bodyHeap(512);
        DynamicJsonDocument &body = bodyHeap.doc();
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        String mac = body["mac"] | "";
        String ip  = body["ip"] | "";
        if (mac.isEmpty()) {
          sendError(req, 400, "mac field required", "MISSING_MAC");
          return;
        }
        if (!_portalSessions) {
          sendError(req, 500, "Portal session manager not ready", "NOT_READY");
          return;
        }
        String errorCode;
        if (_portalSessions->donePaying(mac, errorCode, ip)) {
          Serial.println("[portal] done-paying before response json");
          PsramJsonDocument heapOut;
          _portalSessions->getSession(mac, ip, heapOut.doc());
          Serial.println("[portal] done-paying after response json");
          Serial.println("[portal] done-paying before send");
          sendOk(req, heapOut.doc(), "Session activating");
          Serial.printf("[portal] heap after done-paying free=%u min=%u\n",
                        (unsigned)ESP.getFreeHeap(),
                        (unsigned)ESP.getMinFreeHeap());
          Serial.println("[portal] done-paying after send");
        } else if (errorCode == "NO_CREDITS") {
          sendError(req, 400,
                    "No credits to convert — insert coins first", "NO_CREDITS");
        } else if (errorCode == "NO_MINUTES") {
          sendError(req, 400, "No purchased minutes available", "NO_MINUTES");
        } else if (errorCode == "ACTIVATION_QUEUE_FULL") {
          sendError(req, 503, "Activation queue full — credits preserved",
                    "ACTIVATION_QUEUE_FULL");
        } else if (errorCode == "SESSION_NOT_FOUND") {
          sendError(req, 404, "Portal session not found", "SESSION_NOT_FOUND");
        } else if (errorCode == "VOUCHER_SESSION") {
          sendError(req, 409, "Use voucher flow for voucher sessions",
                    "VOUCHER_SESSION");
        } else {
          sendError(req, 500,
                    errorCode.isEmpty() ? "Done paying failed" : errorCode.c_str(),
                    errorCode.isEmpty() ? "SESSION_ERROR" : errorCode.c_str());
        }
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/portal/voucher/redeem", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        RENZFI_PORTAL_GATE(req);
        HeapJsonDocument bodyHeap(512);
        DynamicJsonDocument &body = bodyHeap.doc();
        String raw = getBody(req);
        if (!raw.isEmpty()) deserializeJson(body, raw);
        const String code = body["code"] | "";
        const String mac = body["mac"] | "";
        const String ip = body["ip"] | "";
        if (code.isEmpty() || mac.isEmpty()) {
          sendError(req, 400, "Voucher code and MAC are required",
                    "INVALID_VOUCHER");
          return;
        }
        HeapJsonDocument outHeap(RenzFiConfig::JSON_DOC_SMALL);
        String errorCode;
        if (_portalSessions &&
            _portalSessions->redeemVoucher(code, mac, ip, outHeap.doc(),
                                            errorCode)) {
          sendOk(req, outHeap.doc(), "Voucher activating");
          return;
        }
        Serial.printf("[voucher-redeem] fail mac=%s code=%s\n", mac.c_str(),
                      errorCode.isEmpty() ? "(none)" : errorCode.c_str());
        int status = 409;
        const char *message = "Voucher redemption failed";
        if (errorCode == "VOUCHER_NOT_FOUND") status = 404;
        else if (errorCode == "CLOCK_NOT_READY") {
          status = 503;
          message =
              "Device clock not ready — check WAN/DNS, then try again";
        } else if (errorCode == "STORAGE_ERROR" ||
                   errorCode == "ACTIVATION_QUEUE_FULL") {
          status = 503;
        }
        sendError(req, status, message,
                  errorCode.isEmpty() ? "VOUCHER_UNAVAILABLE" : errorCode);
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/portal/voucher/reconnect", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        RENZFI_PORTAL_GATE(req);
        HeapJsonDocument bodyHeap(256);
        DynamicJsonDocument &body = bodyHeap.doc();
        String raw = getBody(req);
        if (!raw.isEmpty()) deserializeJson(body, raw);
        const String mac = body["mac"] | "";
        const String ip = body["ip"] | "";
        if (mac.isEmpty()) {
          sendError(req, 400, "MAC is required", "MISSING_MAC");
          return;
        }
        HeapJsonDocument outHeap(RenzFiConfig::JSON_DOC_SMALL);
        String errorCode;
        if (_portalSessions &&
            _portalSessions->reconnectVoucher(mac, ip, outHeap.doc(),
                                               errorCode)) {
          sendOk(req, outHeap.doc(), "Voucher reconnect activating");
          return;
        }
        int status = errorCode == "VOUCHER_EXPIRED" ? 410 : 409;
        if (errorCode == "CLOCK_NOT_READY" ||
            errorCode == "ACTIVATION_QUEUE_FULL")
          status = 503;
        sendError(req, status,
                  "Voucher reconnect failed",
                  errorCode.isEmpty() ? "VOUCHER_UNAVAILABLE" : errorCode);
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/portal/pause", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_PORTAL_GATE(req);
        logRequest(req, "portal.pause");
        HeapJsonDocument bodyHeap(256);
        DynamicJsonDocument &body = bodyHeap.doc();
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        String mac = body["mac"] | "";
        String ip  = body["ip"]  | "";
        if (mac.isEmpty()) {
          sendError(req, 400, "mac field required", "MISSING_MAC");
          return;
        }
        if (!_portalSessions) {
          sendError(req, 500, "Portal session manager not ready", "NOT_READY");
          return;
        }
        String errorCode;
        if (_portalSessions->pause(mac, &errorCode)) {
          PsramJsonDocument outHeap;
          _portalSessions->getSession(mac, ip, outHeap.doc());
          sendOk(req, outHeap.doc(), "Session paused");
        } else if (errorCode == "SESSION_NOT_FOUND") {
          sendError(req, 404, "Session not found", "SESSION_NOT_FOUND");
        } else if (errorCode == "PAUSE_LIMIT_REACHED") {
          sendError(req, 409,
                    "Pause limit reached for this session", "PAUSE_LIMIT_REACHED");
        } else {
          sendError(req, 409, "Session cannot be paused right now",
                    errorCode.isEmpty() ? "PAUSE_NOT_ALLOWED" : errorCode.c_str());
        }
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/portal/resume", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_PORTAL_GATE(req);
        logRequest(req, "portal.resume");
        HeapJsonDocument bodyHeap(256);
        DynamicJsonDocument &body = bodyHeap.doc();
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        String mac = body["mac"] | "";
        String ip  = body["ip"]  | "";
        if (mac.isEmpty()) {
          sendError(req, 400, "mac field required", "MISSING_MAC");
          return;
        }
        if (!_portalSessions) {
          sendError(req, 500, "Portal session manager not ready", "NOT_READY");
          return;
        }
        String errorCode;
        if (_portalSessions->resume(mac, &errorCode)) {
          PsramJsonDocument outHeap;
          _portalSessions->getSession(mac, ip, outHeap.doc());
          sendOk(req, outHeap.doc(), "Session resumed");
        } else if (errorCode == "SESSION_NOT_FOUND") {
          sendError(req, 404, "Session not found", "SESSION_NOT_FOUND");
        } else if (errorCode == "ACTIVATION_QUEUE_FULL") {
          sendError(req, 503, "Activation queue full — purchased time preserved",
                    "ACTIVATION_QUEUE_FULL");
        } else {
          sendError(req, 409, "Session cannot be resumed right now",
                    errorCode.isEmpty() ? "RESUME_NOT_ALLOWED" : errorCode.c_str());
        }
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/portal/cancel-modal", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_PORTAL_GATE(req);
        logRequest(req, "portal.cancel-modal");
        HeapJsonDocument bodyHeap(256);
        DynamicJsonDocument &body = bodyHeap.doc();
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        String mac = body["mac"] | "";
        if (mac.isEmpty()) {
          sendError(req, 400, "mac field required", "MISSING_MAC");
          return;
        }
        HeapJsonDocument outHeap(RenzFiConfig::JSON_DOC_SMALL);
        if (_portalSessions &&
            _portalSessions->cancelModal(mac, outHeap.doc()))
          sendOk(req, outHeap.doc(), "Modal closed; credits preserved");
        else
          sendError(req, 500, "Cancel modal failed", "SESSION_ERROR");
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/portal/reset", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_PORTAL_GATE(req);
        logRequest(req, "portal.reset");
        HeapJsonDocument bodyHeap(256);
        DynamicJsonDocument &body = bodyHeap.doc();
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        String mac = body["mac"] | "";
        if (mac.isEmpty()) {
          sendError(req, 400, "mac field required", "MISSING_MAC");
          return;
        }
        if (_portalSessions && _portalSessions->reset(mac))
          sendOk(req, "Session reset");
        else
          sendError(req, 404, "Session not found", "NOT_FOUND");
      },
      nullptr, bodyCollect);

  // POST /api/portal/terminate — user-initiated session termination.
  // Destroys active internet session and disconnects from RouterOS.
  // Sales records are preserved.
  _server->on(
      "/api/portal/terminate", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_PORTAL_GATE(req);
        logRequest(req, "portal.terminate");
        Serial.printf("[portal] heap before terminate free=%u min=%u\n",
                      (unsigned)ESP.getFreeHeap(),
                      (unsigned)ESP.getMinFreeHeap());
        HeapJsonDocument bodyHeap(256);
        DynamicJsonDocument &body = bodyHeap.doc();
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        String mac = body["mac"] | "";
        String ip  = body["ip"]  | "";
        if (mac.isEmpty()) {
          sendError(req, 400, "mac field required", "MISSING_MAC");
          return;
        }
        if (!_portalSessions) {
          sendError(req, 500, "Portal session manager not ready", "NOT_READY");
          return;
        }
        if (_portalSessions->terminateSession(mac)) {
          Serial.println("[portal] terminate before response json");
          HeapJsonDocument heapOut(RenzFiConfig::JSON_DOC_SMALL);
          _portalSessions->getSession(mac, ip, heapOut.doc());
          Serial.println("[portal] terminate after response json");
          Serial.println("[portal] terminate before send");
          sendOk(req, heapOut.doc(), "Session terminated");
          Serial.printf("[portal] heap after terminate free=%u min=%u\n",
                        (unsigned)ESP.getFreeHeap(),
                        (unsigned)ESP.getMinFreeHeap());
          Serial.println("[portal] terminate after send");
        } else {
          sendError(req, 404, "No active session to terminate", "NO_SESSION");
        }
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/portal/heartbeat", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
    RENZFI_PORTAL_GATE(req);
        HeapJsonDocument bodyHeap(256);
        DynamicJsonDocument &body = bodyHeap.doc();
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        String mac = body["mac"] | "";
        String ip  = body["ip"]  | "";
        if (_portalSessions && !mac.isEmpty())
          _portalSessions->heartbeat(mac, ip);
        sendOk(req, "ok");
      },
      nullptr, bodyCollect);

  _server->on("/api/portal/rates", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
    RENZFI_PORTAL_GATE(req);
                logRequest(req, "portal.rates");
                if (!_portalSessions) {
                  sendError(req, 500, "Failed to load rates", "PROMO_ERROR");
                  return;
                }
                HeapJsonDocument docHeap(RenzFiConfig::JSON_DOC_MEDIUM);
                if (_portalSessions->getRates(docHeap.doc())) {
                  sendOk(req, docHeap.doc());
                } else {
                  sendError(req, 500, "Failed to load rates", "PROMO_ERROR");
                }
              });

  Serial.println("[boot] Portal session API routes registered:");
  Serial.println("[boot]   OPTIONS /api/portal/*");
  Serial.println("[boot]   GET  /api/portal/branding");
  Serial.println("[boot]   GET  /api/portal/session");
  Serial.println("[boot]   POST /api/portal/start-coin-session");
  Serial.println("[boot]   POST /api/portal/done-paying");
  Serial.println("[boot]   POST /api/portal/voucher/redeem");
  Serial.println("[boot]   POST /api/portal/voucher/reconnect");
  Serial.println("[boot]   POST /api/portal/pause");
  Serial.println("[boot]   POST /api/portal/resume");
  Serial.println("[boot]   POST /api/portal/cancel-modal");
  Serial.println("[boot]   POST /api/portal/reset");
  Serial.println("[boot]   POST /api/portal/terminate");
  Serial.println("[boot]   POST /api/portal/heartbeat");
  Serial.println("[boot]   GET  /api/portal/rates");
}

const char *ApiServer::providerName() const {
  return "ApiServer";
}

int ApiServer::notFoundPriority() const {
  return 20;
}

bool ApiServer::handleNotFound(AsyncWebServerRequest *req) {
  if (!HttpPlaneGate::ensureProductionPlane(req)) return true;
  const String path   = req->url();
  const int    method = req->method();

  // /api/promos/{id}  PUT | DELETE
  if (path.startsWith("/api/promos/")) {
    if (!requireAuth(req)) return true;
    int id = path.substring(12).toInt();
    if (method == HTTP_PUT) {
      DynamicJsonDocument body(RenzFiConfig::JSON_DOC_MEDIUM);
      String raw = getBody(req);
      if (raw.length() > 0) deserializeJson(body, raw);
      if (_promos->update(id, body.as<JsonObjectConst>())) sendOk(req);
      else sendError(req, 404, "Promo not found", "PROMO_NOT_FOUND");
      return true;
    }
    if (method == HTTP_DELETE) {
      if (_promos->remove(id)) sendOk(req);
      else sendError(req, 404, "Promo not found", "PROMO_NOT_FOUND");
      return true;
    }
    sendError(req, 405, "Method not allowed", "METHOD_NOT_ALLOWED");
    return true;
  }

  // /api/vouchers/{code} and owner lifecycle actions.
  // Rule #7 (Setup Simplification Pass): Vouchers is Administrator-only.
  if (path.startsWith("/api/vouchers/")) {
    if (path.startsWith("/api/vouchers/jobs/") ||
        path == "/api/vouchers/bulk-delete") {
      return false;
    }
    if (!requireOwnerAuth(req)) return true;
    String voucherPath = path.substring(14);
    String action;
    const int slash = voucherPath.lastIndexOf('/');
    if (slash > 0) {
      action = voucherPath.substring(slash + 1);
      voucherPath = voucherPath.substring(0, slash);
    }
    String code = urlDecode(voucherPath);
    if (method == HTTP_POST && !action.isEmpty()) {
      if (action != "terminate" && action != "expire" &&
          action != "disable" && action != "archive") {
        sendError(req, 404, "Voucher action not found", "NOT_FOUND");
        return true;
      }
      HeapJsonDocument bodyHeap(256);
      DynamicJsonDocument &body = bodyHeap.doc();
      const String raw = getBody(req);
      if (!raw.isEmpty()) deserializeJson(body, raw);
      String reason = body["reason"] | "";
      if (reason.isEmpty()) reason = String("owner_") + action;
      String errorCode;
      if (_portalSessions &&
          _portalSessions->administerVoucher(code, action, reason,
                                              errorCode)) {
        HeapJsonDocument data(RenzFiConfig::JSON_DOC_SMALL);
        if (_vouchers->find(code, data)) sendOk(req, data);
        else sendOk(req, "Voucher updated");
      } else {
        sendError(req, errorCode == "VOUCHER_NOT_FOUND" ? 404 : 409,
                  "Voucher action rejected",
                  errorCode.isEmpty() ? "INVALID_VOUCHER_STATE" : errorCode);
      }
      return true;
    }
    if (method == HTTP_GET) {
      DynamicJsonDocument data(RenzFiConfig::JSON_DOC_SMALL);
      if (_vouchers->find(code, data)) sendOk(req, data);
      else sendError(req, 404, "Voucher not found", "VOUCHER_NOT_FOUND");
      return true;
    }
    if (method == HTTP_DELETE) {
      if (_vouchers->remove(code)) sendOk(req);
      else sendError(req, 404, "Voucher not found", "VOUCHER_NOT_FOUND");
      return true;
    }
    sendError(req, 405, "Method not allowed", "METHOD_NOT_ALLOWED");
    return true;
  }

  // /api/* — 404 JSON
  if (path.startsWith("/api/")) {
    WebRequestDiagnostics::logRequest(req, "api-404");
    sendError(req, 404, "API endpoint not found", "NOT_FOUND");
    return true;
  }

  return false;
}

void ApiServer::registerRoutes(WebServerManager &web) {
  registerProductionRoutes(web);
}
