#include "ApiServer.h"

#include <Update.h>
#include <MD5Builder.h>
#include <esp_ota_ops.h>
#include <vector>

#include <SD.h>
#include <SPIFFS.h>

#include "Config.h"
#include "PortalConfigManager.h"
#include "RenzFiPortalRoutes.h"
#include "SalesTime.h"
#include "SpiffsHost.h"
#include "W5500Config.h"

// ── File-scope helpers ────────────────────────────────────────────────────────

namespace {

// Inline fallback page served when SPIFFS is not mounted or /index.html missing.
const char SPIFFS_FALLBACK_PAGE[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Renz-Fi Admin Dashboard</title>
  <style>
    body{font-family:Arial,sans-serif;margin:0;background:#0f172a;color:#e2e8f0;display:grid;min-height:100vh;place-items:center}
    main{max-width:640px;padding:32px}
    a{color:#38bdf8}
    code{background:#1e293b;padding:2px 6px;border-radius:4px}
  </style>
</head>
<body>
  <main>
    <h1>Renz-Fi Admin Dashboard</h1>
    <p>The ESP32-S3 hotspot and backend are running in degraded mode, but the SPIFFS frontend image is not available.</p>
    <p>Build the React app, copy <code>dist/*</code> into <code>ESP32_S3_Firmware/data/</code>, upload the SPIFFS image, then reopen the admin dashboard at <code>/admin</code> on the ESP32 STA address or fallback setup AP.</p>
    <p>Backend health: <a href="/api/health">/api/health</a></p>
  </main>
</body>
</html>)rawliteral";

String urlDecode(String value) {
  value.replace("%20", " ");
  value.replace("%2F", "/");
  value.replace("%2f", "/");
  value.replace("%3A", ":");
  value.replace("%3a", ":");
  return value;
}

const char *methodStr(WebRequestMethodComposite method) {
  switch (method) {
    case HTTP_GET:     return "GET";
    case HTTP_POST:    return "POST";
    case HTTP_PUT:     return "PUT";
    case HTTP_DELETE:  return "DELETE";
    case HTTP_OPTIONS: return "OPTIONS";
    default:           return "HTTP";
  }
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

struct PortalAssetUpload {
  enum class Kind { None, Banner, Music } kind = Kind::None;
  enum class Source { None, Multipart, RawBody, Upload } source = Source::None;
  String filename;
  std::vector<uint8_t> buffer;
  bool rejected = false;
  String rejectReason;
  bool streamActive = false;
  bool streamFinished = false;
  bool bodySeen = false;
  size_t expectedTotal = 0;
  size_t bytesReceived = 0;
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
  bool active = false;
  bool rejected = false;
  String rejectReason;
  size_t received = 0;
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

void ApiServer::begin(AsyncWebServer      *server,
                      StorageManager       *storage,
                      AuthManager          *auth,
                      SessionManager       *sessions,
                      PromoManager         *promos,
                      VoucherManager       *vouchers,
                      CoinManager          *coin,
                      MikroTikManager      *mikrotik,
                      Logger               *logger,
                      EventBus             *events,
                      EthernetManager      *eth,
                      PortalSessionManager *portalSessions,
                      PortalConfigManager  *portalConfig) {
  _server         = server;
  _storage        = storage;
  _auth           = auth;
  _sessions       = sessions;
  _promos         = promos;
  _vouchers       = vouchers;
  _coin           = coin;
  _mikrotik       = mikrotik;
  _logger         = logger;
  _events         = events;
  _eth            = eth;
  _portalSessions = portalSessions;
  _portalConfig   = portalConfig;
  _backup.begin(_storage, _logger, _auth, _portalConfig);

  registerRoutes();
}

// ── Response helpers ──────────────────────────────────────────────────────────

bool ApiServer::requireAuth(AsyncWebServerRequest *req) {
  String cookie = req->hasHeader("Cookie")
                    ? req->getHeader("Cookie")->value()
                    : String("");
  if (_auth && _auth->isAuthenticated(cookie)) return true;
  sendError(req, 401, "Authentication required", "UNAUTHENTICATED");
  return false;
}

void ApiServer::addCorsHeaders(AsyncWebServerResponse *res) {
  res->addHeader("Access-Control-Allow-Origin", "*");
  res->addHeader("Access-Control-Allow-Methods",
                 "GET, POST, PUT, DELETE, OPTIONS");
  res->addHeader("Access-Control-Allow-Headers",
                 "Content-Type, Authorization");
  res->addHeader("X-Content-Type-Options", "nosniff");
  res->addHeader("X-Frame-Options", "SAMEORIGIN");
  res->addHeader("Referrer-Policy", "same-origin");
}

void ApiServer::sendOk(AsyncWebServerRequest *req, JsonDocument &data,
                       const String &message) {
  logRequest(req, "api-ok");
  String dataBody;
  serializeJson(data, dataBody);
  String body = "{\"success\":true,\"data\":";
  body += dataBody;
  body += ",\"message\":\"";
  body += message;
  body += "\"}";
  AsyncWebServerResponse *res =
      req->beginResponse(200, "application/json", body);
  addCorsHeaders(res);
  res->addHeader("Cache-Control", "no-store");
  req->send(res);
}

void ApiServer::sendOk(AsyncWebServerRequest *req, const String &message) {
  logRequest(req, "api-ok");
  DynamicJsonDocument envelope(256);
  envelope["success"]    = true;
  envelope["data"]["ok"] = true;
  envelope["message"]    = message;
  String body;
  serializeJson(envelope, body);
  AsyncWebServerResponse *res =
      req->beginResponse(200, "application/json", body);
  addCorsHeaders(res);
  res->addHeader("Cache-Control", "no-store");
  req->send(res);
}

void ApiServer::sendError(AsyncWebServerRequest *req, int status,
                          const String &error, const String &code) {
  logRequest(req, "api-error");
  DynamicJsonDocument doc(256);
  doc["success"] = false;
  doc["error"]   = error;
  doc["code"]    = code;
  String body;
  serializeJson(doc, body);
  AsyncWebServerResponse *res =
      req->beginResponse(status, "application/json", body);
  addCorsHeaders(res);
  res->addHeader("Cache-Control", "no-store");
  req->send(res);
}

String ApiServer::getBody(AsyncWebServerRequest *req) {
  if (req->_tempObject) return String(static_cast<char *>(req->_tempObject));
  return "";
}

void ApiServer::logRequest(AsyncWebServerRequest *req, const char *handler) {
  IPAddress remoteIp = req->client()->remoteIP();
  Serial.printf("[tcp] %s %s from=%s via=ETH local=%s host=%s handler=%s\n",
                methodStr(req->method()),
                req->url().c_str(),
                remoteIp.toString().c_str(),
                W5500Config::IP.toString().c_str(),
                req->host().c_str(),
                handler);
}

// ── Static / SPA helpers ──────────────────────────────────────────────────────

void ApiServer::sendStaticOrIndex(AsyncWebServerRequest *req) {
  String path = req->url();
  const int query = path.indexOf('?');
  if (query >= 0) path = path.substring(0, query);
  logRequest(req, "static");

  bool         gzip      = false;
  const String spiffsPath = resolveSpiffsServePath(path, &gzip);

  if (spiffsPath.isEmpty()) {
    if (path.startsWith("/assets/") || path.startsWith("/portal/") ||
        path == "/manifest.webmanifest" || path == "/sw.js" ||
        path == "/favicon.svg"          || path == "/favicon.ico") {
      Serial.printf("[http] 404 path=%s reason=missing-static-asset\n",
                    path.c_str());
      AsyncWebServerResponse *res =
          req->beginResponse(404, "text/plain", "Not Found");
      addCorsHeaders(res);
      res->addHeader("Cache-Control", "no-store");
      req->send(res);
      return;
    }
    Serial.printf("[http] 200 path=%s reason=spa-fallback-page\n", path.c_str());
    AsyncWebServerResponse *res = req->beginResponse(
        200, "text/html; charset=utf-8", String(FPSTR(SPIFFS_FALLBACK_PAGE)));
    addCorsHeaders(res);
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
    return;
  }

  String typePath = path;
  if (spiffsPath.endsWith("index.html") ||
      spiffsPath.endsWith("index.html.gz")) {
    typePath = "/index.html";
  } else if (spiffsPath.endsWith(".gz")) {
    typePath = spiffsPath.substring(0, spiffsPath.length() - 3);
  }

  if (!SPIFFS.exists(spiffsPath)) {
    Serial.printf("[http] 404 path=%s spiffs=%s reason=open-failed\n",
                  path.c_str(), spiffsPath.c_str());
    AsyncWebServerResponse *res = req->beginResponse(
        200, "text/html; charset=utf-8", String(FPSTR(SPIFFS_FALLBACK_PAGE)));
    addCorsHeaders(res);
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
    return;
  }

  const String cacheControl = path.startsWith("/assets/")
                                  ? "public, max-age=31536000, immutable"
                                  : "no-cache";

  Serial.printf("[http] 200 path=%s spiffs=%s gzip=%s contentType=%s\n",
                path.c_str(), spiffsPath.c_str(), gzip ? "yes" : "no",
                _storage->contentType(typePath).c_str());

  AsyncWebServerResponse *res = req->beginResponse(
      SPIFFS, spiffsPath, _storage->contentType(typePath));
  if (gzip) res->addHeader("Content-Encoding", "gzip");
  addCorsHeaders(res);
  res->addHeader("Cache-Control", cacheControl);
  req->send(res);
}

void ApiServer::sendSdFile(AsyncWebServerRequest *req, const char *sdPath,
                           const char *filename) {
  if (!SD.exists(sdPath)) {
    sendError(req, 404, "File not found", "FILE_NOT_FOUND");
    return;
  }
  AsyncWebServerResponse *res =
      req->beginResponse(SD, sdPath, "application/json");
  res->addHeader("Content-Disposition",
                 String("attachment; filename=\"") + filename + "\"");
  res->addHeader("Cache-Control", "no-store");
  addCorsHeaders(res);
  req->send(res);
}

// ── Route registration ────────────────────────────────────────────────────────

void ApiServer::registerRoutes() {
  // ── Static assets (served before API or SPA handlers) ────────────────────
  _server->serveStatic("/assets/", SPIFFS, "/assets/")
         .setCacheControl("public, max-age=31536000, immutable");

  // ── CORS preflight (OPTIONS /api/) ───────────────────────────────────────
  _server->on("/api/", HTTP_OPTIONS, [this](AsyncWebServerRequest *req) {
    AsyncWebServerResponse *res = req->beginResponse(204, "text/plain", "");
    addCorsHeaders(res);
    req->send(res);
  });

  // ── Health ───────────────────────────────────────────────────────────────
  _server->on("/api/health", HTTP_GET, [this](AsyncWebServerRequest *req) {
    String cookie = req->hasHeader("Cookie")
                      ? req->getHeader("Cookie")->value()
                      : String("");
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_SMALL);
    data["ok"]                            = true;
    data["storage"]["ok"]                 = _storage->healthy();
    data["session"]["authenticated"]      = _auth->isAuthenticated(cookie);
    data["session"]["mustChangePassword"] = _auth->mustChangePassword();
    sendOk(req, data);
  });

  // ── Auth ─────────────────────────────────────────────────────────────────
  _server->on(
      "/api/auth/login", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        DynamicJsonDocument body(RenzFiConfig::JSON_DOC_SMALL);
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);

        DynamicJsonDocument response(RenzFiConfig::JSON_DOC_SMALL);
        String              password   = body["password"]   | "";
        bool                rememberIp = body["rememberIp"] | false;
        String              setCookieVal;

        if (!_auth->login(password, rememberIp, response, setCookieVal)) {
          sendError(req, 401, "Invalid password", "INVALID_CREDENTIALS");
          return;
        }
        DynamicJsonDocument envelope(RenzFiConfig::JSON_DOC_SMALL);
        envelope["success"] = true;
        envelope["data"].set(response.as<JsonObject>());
        envelope["message"] = "OK";
        String payload;
        serializeJson(envelope, payload);
        AsyncWebServerResponse *res =
            req->beginResponse(200, "application/json", payload);
        addCorsHeaders(res);
        res->addHeader("Set-Cookie", setCookieVal);
        res->addHeader("Cache-Control", "no-store");
        req->send(res);
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/auth/logout", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
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
        if (!requireAuth(req)) return;
        DynamicJsonDocument body(256);
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        if (_auth->changePassword(body["oldPassword"] | "",
                                  body["newPassword"] | ""))
          sendOk(req);
        else
          sendError(req, 400, "Unable to change password",
                    "PASSWORD_CHANGE_FAILED");
      },
      nullptr, bodyCollect);

  // ── Dashboard status ──────────────────────────────────────────────────────
  _server->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *req) {
    if (!requireAuth(req)) return;
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_MEDIUM);
    DynamicJsonDocument salesToday(256);
    DynamicJsonDocument salesWeek(256);
    DynamicJsonDocument salesMonth(256);
    _sessions->salesToday(salesToday);
    _sessions->salesWeek(salesWeek);
    _sessions->salesMonth(salesMonth);
    data["server"]["ok"]                 = true;
    data["server"]["uptimeSeconds"]      = millis() / 1000;
    data["database"]["ok"]               = _storage->healthy();
    data["database"]["path"]             = _storage->usingFallback()
                                               ? "SPIFFS Fallback"
                                               : "SD";
    data["sales"]["today"]["amount"]     = salesToday["amount"]   | 0;
    data["sales"]["today"]["sessions"]   = salesToday["sessions"] | 0;
    data["sales"]["weekly"]["amount"]    = salesWeek["amount"]    | 0;
    data["sales"]["weekly"]["sessions"]  = salesWeek["sessions"]  | 0;
    data["sales"]["monthly"]["amount"]   = salesMonth["amount"]   | 0;
    data["sales"]["monthly"]["sessions"] = salesMonth["sessions"] | 0;
    salesLogDiagnostics(salesToday["amount"] | 0, salesWeek["amount"] | 0,
                        salesMonth["amount"] | 0);
    int activeCount = 0;
    int pausedCount = 0;
    mergedActiveUserStats(_sessions, _portalSessions, activeCount, pausedCount);
    data["activeUsers"]["count"]         = activeCount;
    data["activeUsers"]["paused"]        = pausedCount;
    data["activeUsers"]["idle"]          = 0;

    DynamicJsonDocument routerDoc(RenzFiConfig::JSON_DOC_SMALL);
    String routerHost;
    String routerSsid;
    bool routerConfigured = false;
    if (_mikrotik && _mikrotik->load(routerDoc)) {
      routerHost = routerDoc["host"] | "";
      routerSsid = routerDoc["ssid"] | "";
      routerConfigured = routerHost.length() > 0;
    }
    data["mikrotik"]["ok"]               = routerConfigured;
    data["mikrotik"]["host"]             = routerHost;
    data["mikrotik"]["latencyMs"]        = 0;

    data["internet"]["ok"]               = false;
    data["internet"]["known"]            = false;
    data["internet"]["latencyMs"]        = 0;

    if (_coin) {
      _coin->fillStatus(data["coinSlot"].to<JsonObject>());
    } else {
      data["coinSlot"]["ok"]             = false;
      data["coinSlot"]["state"]          = "Unavailable";
      data["coinSlot"]["pulsesToday"]    = 0;
    }

    data["hotspot"]["ok"]                = routerConfigured && routerSsid.length() > 0;
    data["hotspot"]["ssid"]              = routerSsid;

    data["esp32"]["uptime"]              = String(millis() / 1000) + "s";
    data["esp32"]["lastSeen"]            = nullptr;
    {
      const uint64_t spiffsUsed = _storage->getSpiffsUsedBytes();
      const uint64_t spiffsTotal = _storage->getSpiffsTotalBytes();
      data["storage"]["flashUsedMb"] =
          round((spiffsUsed / 1024.0 / 1024.0) * 10.0) / 10.0;
      data["storage"]["flashTotalMb"] =
          round((spiffsTotal / 1024.0 / 1024.0) * 10.0) / 10.0;
      data["storage"]["ramUsedKb"] =
          (ESP.getHeapSize() - ESP.getFreeHeap()) / 1024;
      data["storage"]["ramTotalKb"] = ESP.getHeapSize() / 1024;
      size_t logsBytes = _storage->fileSizeBytes(RenzFiConfig::LOGS_FILE);
      data["storage"]["logsUsedKb"] = (logsBytes + 1023) / 1024;
      data["storage"]["logsTotalKb"] = RenzFiConfig::LOGS_QUOTA_KB;
      _storage->fillSdStatus(data["storage"]["sd"].to<JsonObject>());
    }
    data["sync"]["pending"]              = 0;
    data["sync"]["lastSyncAt"]           = nullptr;
    sendOk(req, data);
  });

  // ── System ────────────────────────────────────────────────────────────────
  _server->on("/api/storage/retry-sd", HTTP_POST,
              [this](AsyncWebServerRequest *req) {
                if (!requireAuth(req)) return;
                logRequest(req, "storage.retry-sd");
                if (_storage->retrySd()) {
                  DynamicJsonDocument data(128);
                  data["healthy"]  = _storage->healthy();
                  data["fallback"] = _storage->usingFallback();
                  sendOk(req, data);
                } else {
                  sendError(req, 500, "SD mount failed", "SD_MOUNT_FAILED");
                }
              });

  _server->on("/api/system/reboot", HTTP_POST,
              [this](AsyncWebServerRequest *req) {
                if (!requireAuth(req)) return;
                sendOk(req, "Rebooting");
                delay(250);
                ESP.restart();
              });

  _server->on(
      "/api/system/factory-reset", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        if (!requireAuth(req)) return;
        String error;
        if (!_backup.performFactoryReset(error)) {
          sendError(req, 500, error.isEmpty() ? "Factory reset failed"
                                            : error,
                    "FACTORY_RESET_FAILED");
          return;
        }
        DynamicJsonDocument data(128);
        data["rebooting"] = true;
        sendOk(req, data, "Factory reset complete — rebooting");
        delay(500);
        ESP.restart();
      });

  // ── Promos ────────────────────────────────────────────────────────────────
  _server->on("/api/promos", HTTP_GET, [this](AsyncWebServerRequest *req) {
    if (!requireAuth(req)) return;
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_MEDIUM);
    if (_promos->list(data)) sendOk(req, data);
    else sendError(req, 500, "Unable to load promos", "STORAGE_ERROR");
  });

  _server->on(
      "/api/promos", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
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

  // ── Vouchers ──────────────────────────────────────────────────────────────
  _server->on("/api/vouchers", HTTP_GET, [this](AsyncWebServerRequest *req) {
    if (!requireAuth(req)) return;
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_LARGE);
    if (_vouchers->list(data)) sendOk(req, data);
    else sendError(req, 500, "Unable to load vouchers", "STORAGE_ERROR");
  });

  _server->on(
      "/api/vouchers", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        if (!requireAuth(req)) return;
        DynamicJsonDocument body(RenzFiConfig::JSON_DOC_MEDIUM);
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        DynamicJsonDocument data(RenzFiConfig::JSON_DOC_MEDIUM);
        if (_vouchers->generate(body["count"] | 1, body["amount"] | 1,
                                body["minutes"] | 5, body["expires"] | "",
                                data))
          sendOk(req, data);
        else
          sendError(req, 500, "Unable to generate vouchers",
                    "VOUCHER_CREATE_FAILED");
      },
      nullptr, bodyCollect);

  // ── Users ─────────────────────────────────────────────────────────────────
  _server->on("/api/users", HTTP_GET, [this](AsyncWebServerRequest *req) {
    if (!requireAuth(req)) return;
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_LARGE);
    fillActiveUsers(_sessions, _portalSessions, data);
    sendOk(req, data);
  });

  _server->on(
      "/api/users/disconnect", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        if (!requireAuth(req)) return;
        DynamicJsonDocument body(256);
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        String mac = body["mac"] | "";
        _mikrotik->disconnectHotspotUser(mac);
        bool removed = false;
        if (_portalSessions && _portalSessions->reset(mac)) removed = true;
        if (_sessions->disconnect(mac)) removed = true;
        if (removed) sendOk(req);
        else sendError(req, 404, "Active user not found", "USER_NOT_FOUND");
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/users/pause", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        if (!requireAuth(req)) return;
        DynamicJsonDocument body(256);
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        String mac = body["mac"] | "";
        if (mac.isEmpty()) {
          sendError(req, 400, "mac field required", "MISSING_MAC");
          return;
        }
        if (_portalSessions && _portalSessions->hasSession(mac)) {
          if (_portalSessions->pause(mac)) sendOk(req, "Session paused");
          else
            sendError(req, 400, "Session cannot be paused", "INVALID_STATE");
          return;
        }
        if (_sessions && _sessions->pause(mac)) sendOk(req, "Session paused");
        else sendError(req, 404, "Active user not found", "USER_NOT_FOUND");
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/users/resume", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        if (!requireAuth(req)) return;
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
  _server->on("/api/sales/today", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
                if (!requireAuth(req)) return;
                DynamicJsonDocument data(256);
                _sessions->salesToday(data);
                sendOk(req, data);
              });

  _server->on("/api/sales/weekly", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
                if (!requireAuth(req)) return;
                DynamicJsonDocument data(256);
                _sessions->salesWeek(data);
                sendOk(req, data);
              });

  _server->on("/api/sales/monthly", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
                if (!requireAuth(req)) return;
                DynamicJsonDocument data(256);
                _sessions->salesMonth(data);
                sendOk(req, data);
              });

  _server->on("/api/sales/history", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
                if (!requireAuth(req)) return;
                DynamicJsonDocument data(RenzFiConfig::JSON_DOC_MEDIUM);
                _sessions->salesHistory(data);
                sendOk(req, data);
              });

  _server->on("/api/sales/export", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
                if (!requireAuth(req)) return;
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

  // ── Captive portal branding (admin) — register BEFORE /api/settings ───────
  _server->on("/api/settings/portal", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
                if (!requireAuth(req)) return;
                if (!_portalConfig) {
                  sendError(req, 500, "Portal config unavailable", "NOT_READY");
                  return;
                }
                JsonDocument data;
                JsonObject root = data.to<JsonObject>();
                const String base = "http://" + W5500Config::IP.toString();
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
        Serial.printf(
            "[portal-upload] %s kind=%s index=%u len=%u total=%u final=%s "
            "filename=%s contentLength=%u contentType=%s\n",
            via, isBanner ? "banner" : "music", (unsigned)index, (unsigned)len,
            (unsigned)total, final ? "yes" : "no", filename.c_str(),
            (unsigned)req->contentLength(), req->contentType().c_str());

        gPortalUpload.bodySeen = true;
        const size_t maxBytes =
            isBanner ? (200U * 1024U) : RenzFiConfig::PORTAL_MUSIC_MAX_BYTES;

        if (total > maxBytes) {
          gPortalUpload.rejected = true;
          gPortalUpload.rejectReason =
              isBanner ? "Banner exceeds 200 KiB limit"
                       : "Music exceeds 1000 KiB limit";
          return;
        }

        if (index == 0) {
          gPortalUpload.kind =
              isBanner ? PortalAssetUpload::Kind::Banner
                       : PortalAssetUpload::Kind::Music;
          gPortalUpload.filename =
              filename.length() > 0
                  ? filename
                  : (isBanner ? "raw-body.webp" : "raw-body.mp3");
          gPortalUpload.rejected = false;
          gPortalUpload.rejectReason = "";
          gPortalUpload.buffer.clear();
          gPortalUpload.streamActive = false;
          gPortalUpload.streamFinished = false;
          gPortalUpload.expectedTotal = total;
          gPortalUpload.bytesReceived = 0;

          if (!isBanner && filename.length() > 0 &&
              !filename.endsWith(".mp3") && !filename.endsWith(".MP3")) {
            gPortalUpload.rejected = true;
            gPortalUpload.rejectReason = "Only MP3 files are allowed";
            return;
          }

          if (_portalConfig && total > 0) {
            const bool began =
                isBanner
                    ? _portalConfig->beginBannerUpload(total,
                                                       gPortalUpload.filename)
                    : _portalConfig->beginMusicUpload(total,
                                                      gPortalUpload.filename);
            gPortalUpload.streamActive = began;
          }
        }

        if (gPortalUpload.rejected) return;

        const bool isFinal = final || (total > 0 && index + len >= total);

        if (gPortalUpload.streamActive && _portalConfig && len > 0) {
          if (!_portalConfig->appendUploadChunk(data, len, index, isFinal)) {
            gPortalUpload.rejected = true;
            gPortalUpload.rejectReason = "Unable to write upload chunk";
            _portalConfig->abortUpload();
            return;
          }
          gPortalUpload.bytesReceived += len;
        } else if (len > 0) {
          if (gPortalUpload.buffer.size() + len > maxBytes) {
            gPortalUpload.rejected = true;
            gPortalUpload.rejectReason =
                isBanner ? "Banner exceeds 200 KiB limit"
                         : "Music exceeds 1000 KiB limit";
            return;
          }
          gPortalUpload.buffer.insert(gPortalUpload.buffer.end(), data,
                                      data + len);
          gPortalUpload.bytesReceived += len;
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
        if (index == 0) {
          gPortalUpload = PortalAssetUpload{};
          gPortalUpload.source = PortalAssetUpload::Source::RawBody;
        }
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
        if (index == 0) {
          gPortalUpload = PortalAssetUpload{};
          gPortalUpload.source = PortalAssetUpload::Source::RawBody;
        }
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
        if (index == 0) {
          gPortalUpload = PortalAssetUpload{};
          gPortalUpload.source = PortalAssetUpload::Source::Upload;
        }
        size_t total = req->contentLength();
        if (total == 0 && final && len > 0) total = index + len;
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
        if (index == 0) {
          gPortalUpload = PortalAssetUpload{};
          gPortalUpload.source = PortalAssetUpload::Source::Upload;
        }
        size_t total = req->contentLength();
        if (total == 0 && final && len > 0) total = index + len;
        portalHandleChunk(req, false, data, len, index, total, final, "UPLOAD",
                          filename);
      };

  auto portalBannerComplete = [this](AsyncWebServerRequest *req) {
    Serial.printf(
        "[portal-upload] COMPLETE banner contentLength=%u contentType=%s "
        "bodySeen=%s bytes=%u streamActive=%s streamFinished=%s\n",
        (unsigned)req->contentLength(), req->contentType().c_str(),
        gPortalUpload.bodySeen ? "yes" : "no", (unsigned)gPortalUpload.bytesReceived,
        gPortalUpload.streamActive ? "yes" : "no",
        gPortalUpload.streamFinished ? "yes" : "no");

    if (!requireAuth(req)) return;
    if (!_portalConfig) {
      sendError(req, 500, "Portal config unavailable", "NOT_READY");
      return;
    }

    if (gPortalUpload.rejected) {
      sendError(req, 400, gPortalUpload.rejectReason, "INVALID_UPLOAD");
      _portalConfig->abortUpload();
      gPortalUpload = PortalAssetUpload{};
      return;
    }

    if (!gPortalUpload.bodySeen || gPortalUpload.bytesReceived == 0) {
      sendError(req, 400,
                "No upload body received (body/upload callback did not run)",
                "INVALID_UPLOAD");
      _portalConfig->abortUpload();
      gPortalUpload = PortalAssetUpload{};
      return;
    }

    bool ok = false;
    if (gPortalUpload.streamActive) {
      ok = _portalConfig->finishBannerUpload();
      if (!ok) _portalConfig->abortUpload();
    } else if (!gPortalUpload.buffer.empty()) {
      ok = _portalConfig->uploadBanner(gPortalUpload.buffer.data(),
                                       gPortalUpload.buffer.size());
    } else {
      sendError(req, 400, "Empty banner upload", "INVALID_UPLOAD");
      gPortalUpload = PortalAssetUpload{};
      return;
    }

    if (ok) {
      JsonDocument payload;
      JsonObject root = payload.to<JsonObject>();
      const String base = "http://" + W5500Config::IP.toString();
      _portalConfig->fillSettingsJson(root, base);
      sendOk(req, payload, "Banner uploaded");
    } else {
      sendError(req, 500, "Unable to save banner", "STORAGE_ERROR");
    }
    gPortalUpload = PortalAssetUpload{};
  };

  auto portalMusicComplete = [this](AsyncWebServerRequest *req) {
    Serial.printf(
        "[portal-upload] COMPLETE music contentLength=%u contentType=%s "
        "bodySeen=%s bytes=%u streamActive=%s streamFinished=%s\n",
        (unsigned)req->contentLength(), req->contentType().c_str(),
        gPortalUpload.bodySeen ? "yes" : "no", (unsigned)gPortalUpload.bytesReceived,
        gPortalUpload.streamActive ? "yes" : "no",
        gPortalUpload.streamFinished ? "yes" : "no");

    if (!requireAuth(req)) return;
    if (!_portalConfig) {
      sendError(req, 500, "Portal config unavailable", "NOT_READY");
      return;
    }

    if (gPortalUpload.rejected) {
      sendError(req, 400, gPortalUpload.rejectReason, "INVALID_UPLOAD");
      _portalConfig->abortUpload();
      gPortalUpload = PortalAssetUpload{};
      return;
    }

    if (!gPortalUpload.bodySeen || gPortalUpload.bytesReceived == 0) {
      sendError(req, 400,
                "No upload body received (body/upload callback did not run)",
                "INVALID_UPLOAD");
      _portalConfig->abortUpload();
      gPortalUpload = PortalAssetUpload{};
      return;
    }

    bool ok = false;
    if (gPortalUpload.streamActive) {
      ok = _portalConfig->finishMusicUpload();
      if (!ok) _portalConfig->abortUpload();
    } else if (!gPortalUpload.buffer.empty()) {
      ok = _portalConfig->uploadMusic(gPortalUpload.buffer.data(),
                                      gPortalUpload.buffer.size());
    } else {
      sendError(req, 400, "Empty music upload", "INVALID_UPLOAD");
      gPortalUpload = PortalAssetUpload{};
      return;
    }

    if (ok) {
      JsonDocument payload;
      JsonObject root = payload.to<JsonObject>();
      const String base = "http://" + W5500Config::IP.toString();
      _portalConfig->fillSettingsJson(root, base);
      sendOk(req, payload, "Music uploaded");
    } else {
      sendError(req, 500, "Unable to save music", "STORAGE_ERROR");
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
                if (!requireAuth(req)) return;
                if (!_portalConfig) {
                  sendError(req, 500, "Portal config unavailable", "NOT_READY");
                  return;
                }
                if (_portalConfig->deleteBanner()) sendOk(req);
                else sendError(req, 500, "Unable to remove banner", "STORAGE_ERROR");
              });

  _server->on("/api/settings/portal/music", HTTP_DELETE,
              [this](AsyncWebServerRequest *req) {
                if (!requireAuth(req)) return;
                if (!_portalConfig) {
                  sendError(req, 500, "Portal config unavailable", "NOT_READY");
                  return;
                }
                if (_portalConfig->deleteMusic()) sendOk(req);
                else sendError(req, 500, "Unable to remove music", "STORAGE_ERROR");
              });

  // ── Settings ──────────────────────────────────────────────────────────────
  _server->on("/api/settings", HTTP_GET, [this](AsyncWebServerRequest *req) {
    if (!requireAuth(req)) return;
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_MEDIUM);
    _storage->readJson(RenzFiConfig::SETTINGS_FILE, data);
    sendOk(req, data);
  });

  auto settingsSave = [this](AsyncWebServerRequest *req) {
    if (!requireAuth(req)) return;
    DynamicJsonDocument body(RenzFiConfig::JSON_DOC_MEDIUM);
    String raw = getBody(req);
    if (raw.length() > 0) deserializeJson(body, raw);
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_MEDIUM);
    data.set(body);
    if (_storage->writeJson(RenzFiConfig::SETTINGS_FILE, data)) sendOk(req);
    else sendError(req, 500, "Unable to save settings", "STORAGE_ERROR");
  };
  _server->on("/api/settings", HTTP_POST, settingsSave, nullptr, bodyCollect);
  _server->on("/api/settings", HTTP_PUT,  settingsSave, nullptr, bodyCollect);

  _server->on("/api/settings/admin", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
                if (!requireAuth(req)) return;
                DynamicJsonDocument data(128);
                data["username"] = "admin";
                sendOk(req, data);
              });

  _server->on("/api/settings/admin", HTTP_PUT,
              [this](AsyncWebServerRequest *req) {
                if (!requireAuth(req)) return;
                sendOk(req);
              });

  _server->on("/api/settings/backup", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
                if (!requireAuth(req)) return;
                if (!_backup.isSdAvailable()) {
                  sendError(req, 503, "SD Card is not available",
                            "SD_UNAVAILABLE");
                  return;
                }
                if (_logger) _logger->info("backup", "export started");
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

                if (_logger) _logger->info("backup", "export completed");
                Serial.println("[backup] export completed");
              });

  auto restoreUploadHandler = [this](AsyncWebServerRequest *req, String filename,
                                     size_t index, uint8_t *data, size_t len,
                                     bool final) {
    (void)req;
    (void)filename;
    if (index == 0) {
      gRestoreUpload = RestoreUpload{};
      gRestoreUpload.active = true;
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
      if (_logger) _logger->info("restore", "restore started");
      Serial.println("[restore] restore started");
    }

    if (gRestoreUpload.rejected || !gRestoreUpload.file) return;
    if (len > 0) {
      gRestoreUpload.file.write(data, len);
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
        if (!requireAuth(req)) return;

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
        if (_logger) _logger->info("restore", "restore completed");
        Serial.println("[restore] restore completed");

        DynamicJsonDocument data(128);
        data["rebooting"] = true;
        sendOk(req, data, "Restore complete — rebooting");
        delay(500);
        ESP.restart();
      },
      restoreUploadHandler, bodyCollect);

  // ── Firmware OTA ────────────────────────────────────────────────────────────
  _server->on("/api/system/firmware", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
                if (!requireAuth(req)) return;
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
        if (_logger) _logger->error("firmware", gOtaUpload.rejectReason);
        return;
      }
      if (_logger) _logger->info("firmware", "OTA upload started: " + filename);
    }

    if (gOtaUpload.rejected) return;

    if (len > 0) {
      gOtaUpload.md5.add(data, len);
      if (Update.write(data, len) != len) {
        Update.abort();
        gOtaUpload.rejected = true;
        gOtaUpload.rejectReason = "Flash write failed during OTA";
        if (_logger) _logger->error("firmware", gOtaUpload.rejectReason);
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
        if (_logger) _logger->error("firmware", gOtaUpload.rejectReason);
        return;
      }
      gOtaUpload.finalized = true;

      if (_logger) {
        _logger->info("firmware",
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
        if (_logger) _logger->error("firmware", gOtaUpload.rejectReason);
        return;
      }
      if (_logger) _logger->info("firmware", "OTA raw upload started");
    }
    if (gOtaUpload.rejected || len == 0) return;

    gOtaUpload.md5.add(data, len);
    if (Update.write(data, len) != len) {
      Update.abort();
      gOtaUpload.rejected = true;
      gOtaUpload.rejectReason = "Flash write failed during OTA";
      if (_logger) _logger->error("firmware", gOtaUpload.rejectReason);
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
        if (!requireAuth(req)) return;

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
            if (_logger) _logger->error("firmware", gOtaUpload.rejectReason);
            sendError(req, 400, gOtaUpload.rejectReason, "OTA_REJECTED");
            gOtaUpload = FirmwareOtaUpload{};
            return;
          }
          gOtaUpload.finalized = true;
          if (_logger) {
            _logger->info("firmware",
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
  _server->on("/api/logs", HTTP_GET, [this](AsyncWebServerRequest *req) {
    if (!requireAuth(req)) return;
    String q = req->hasArg("q") ? req->arg("q") : "";
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_LARGE);
    if (_logger->list(data, q)) sendOk(req, data);
    else sendError(req, 500, "Unable to load logs", "STORAGE_ERROR");
  });

  _server->on("/api/logs", HTTP_DELETE, [this](AsyncWebServerRequest *req) {
    if (!requireAuth(req)) return;
    if (_logger->clear()) sendOk(req);
    else sendError(req, 500, "Unable to clear logs", "STORAGE_ERROR");
  });

  _server->on("/api/logs/export", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
                if (!requireAuth(req)) return;
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
                if (!requireAuth(req)) return;
                DynamicJsonDocument data(512);
                _coin->settings(data);
                sendOk(req, data);
              });

  auto coinSave = [this](AsyncWebServerRequest *req) {
    if (!requireAuth(req)) return;
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
                if (!requireAuth(req)) return;
                DynamicJsonDocument data(RenzFiConfig::JSON_DOC_SMALL);
                _coin->diagnostics(data);
                sendOk(req, data);
              });

  _server->on("/api/coin/test", HTTP_POST, [this](AsyncWebServerRequest *req) {
    if (!requireAuth(req)) return;
    _sessions->grantCoinSession(1, _promos->minutesForAmount(1));
    sendOk(req);
  });

  _server->on("/api/coin/reset", HTTP_POST, [this](AsyncWebServerRequest *req) {
    if (!requireAuth(req)) return;
    _coin->resetCounters();
    sendOk(req);
  });

  // ── Router / MikroTik ─────────────────────────────────────────────────────
  _server->on("/api/router/settings", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
                if (!requireAuth(req)) return;
                DynamicJsonDocument data(RenzFiConfig::JSON_DOC_SMALL);
                if (_mikrotik->fillPublicSettings(data)) sendOk(req, data);
                else sendError(req, 500, "Unable to load router settings", "STORAGE_ERROR");
              });

  auto routerSave = [this](AsyncWebServerRequest *req) {
    if (!requireAuth(req)) return;
    DynamicJsonDocument body(RenzFiConfig::JSON_DOC_SMALL);
    String raw = getBody(req);
    if (raw.length() > 0) deserializeJson(body, raw);
    if (_mikrotik->save(body.as<JsonObjectConst>())) sendOk(req);
    else sendError(req, 500, "Unable to save router settings", "STORAGE_ERROR");
  };
  _server->on("/api/router/settings", HTTP_POST, routerSave, nullptr,
              bodyCollect);
  _server->on("/api/router/settings", HTTP_PUT, routerSave, nullptr,
              bodyCollect);

  _server->on("/api/router/profiles", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
                if (!requireAuth(req)) return;
                DynamicJsonDocument result(RenzFiConfig::JSON_DOC_MEDIUM);
                if (_mikrotik && _mikrotik->listProfiles(result)) {
                  sendOk(req, result, "Profiles loaded");
                } else {
                  const char *message = result["error"] | "Failed to load profiles";
                  sendOk(req, result, message);
                }
              });

  _server->on(
      "/api/router/test", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        if (!requireAuth(req)) return;
        DynamicJsonDocument body(RenzFiConfig::JSON_DOC_SMALL);
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        DynamicJsonDocument result(RenzFiConfig::JSON_DOC_SMALL);
        const bool ok = _mikrotik->test(body.as<JsonObjectConst>(), result);
        const char *message =
            ok ? "Router test passed"
               : (result["error"] | "Router test failed");
        sendOk(req, result, message);
      },
      nullptr, bodyCollect);

  // ── Ethernet / network status ─────────────────────────────────────────────
  auto ethStatus = [this](AsyncWebServerRequest *req) {
    if (!requireAuth(req)) return;
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_SMALL);
    bool link = _eth && _eth->linkUp();
    data["mode"]                = "ethernet";
    data["modeLabel"]           = link ? "W5500 wired (link up)"
                                       : "W5500 wired (no link)";
    data["ethernet"]["linkUp"]  = link;
    data["ethernet"]["ip"]      = _eth ? _eth->ip()
                                       : W5500Config::IP.toString();
    data["ethernet"]["gateway"] = _eth ? _eth->gateway()
                                       : W5500Config::GATEWAY.toString();
    data["ethernet"]["subnet"]  = _eth ? _eth->subnet()
                                       : W5500Config::SUBNET.toString();
    data["ethernet"]["mac"]     = _eth ? _eth->macAddress() : "";
    String mdns = _eth ? _eth->mdnsHostname()
                       : String(RenzFiConfig::MDNS_NAME) + ".local";
    data["mdns"]["hostname"] = mdns;
    data["mdns"]["adminUrl"] = "http://" + mdns + "/admin";
    sendOk(req, data);
  };
  _server->on("/api/system/network", HTTP_GET, ethStatus);
  _server->on("/api/system/wifi",    HTTP_GET, ethStatus);  // backward-compat

  _server->on("/api/system/wifi/config", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
                if (!requireAuth(req)) return;
                DynamicJsonDocument data(RenzFiConfig::JSON_DOC_SMALL);
                data["mode"]    = "ethernet-static";
                data["ip"]      = W5500Config::IP.toString();
                data["gateway"] = W5500Config::GATEWAY.toString();
                data["subnet"]  = W5500Config::SUBNET.toString();
                data["dns"]     = W5500Config::DNS.toString();
                data["note"]    =
                    "Network config is compile-time static (W5500Config.h). "
                    "Reflash firmware to change IP settings.";
                sendOk(req, data);
              });

  auto wifiConfigRO = [this](AsyncWebServerRequest *req) {
    if (!requireAuth(req)) return;
    sendError(req, 405,
              "Network config is static (W5500 wired). "
              "Edit W5500Config.h and reflash to change IP settings.",
              "STATIC_CONFIG");
  };
  _server->on("/api/system/wifi/config", HTTP_POST, wifiConfigRO);
  _server->on("/api/system/wifi/config", HTTP_PUT,  wifiConfigRO);

  // ── Portal session API (/api/portal/*) ────────────────────────────────────
  // These endpoints are intentionally open (no requireAuth) — called by the
  // MikroTik-hosted captive portal JS from the customer's device.

  _server->on("/api/portal/branding", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
                if (!_portalConfig) {
                  sendError(req, 500, "Portal config unavailable", "NOT_READY");
                  return;
                }
                DynamicJsonDocument data(512);
                String base = "http://" + W5500Config::IP.toString();
                _portalConfig->fillBrandingJson(data.to<JsonObject>(), base);
                sendOk(req, data);
              });

  _server->on("/api/portal/assets/banner", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
                if (_portalConfig && _portalConfig->serveBanner(req)) return;
                Serial.printf(
                    "[portal] GET assets/banner NOT_FOUND hasCustom=%s\n",
                    (_portalConfig && _portalConfig->hasCustomBanner()) ? "yes"
                                                                        : "no");
                sendError(req, 404, "Banner not found", "NOT_FOUND");
              });

  _server->on("/api/portal/assets/music", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
                if (_portalConfig && _portalConfig->serveMusic(req)) return;
                Serial.printf(
                    "[portal] GET assets/music NOT_FOUND hasCustom=%s\n",
                    (_portalConfig && _portalConfig->hasCustomMusic()) ? "yes"
                                                                       : "no");
                sendError(req, 404, "Music not found", "NOT_FOUND");
              });

  _server->on("/api/portal/session", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
                logRequest(req, "portal.session");
                String mac = req->hasArg("mac") ? req->arg("mac") : "";
                String ip  = req->hasArg("ip")  ? req->arg("ip")  : "";
                if (mac.isEmpty()) {
                  sendError(req, 400, "mac parameter required", "MISSING_MAC");
                  return;
                }
                DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
                if (_portalSessions && _portalSessions->getSession(mac, ip, doc))
                  sendOk(req, doc);
                else
                  sendError(req, 500, "Failed to load session", "SESSION_ERROR");
              });

  _server->on(
      "/api/portal/start-coin-session", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        logRequest(req, "portal.start-coin-session");
        DynamicJsonDocument body(RenzFiConfig::JSON_DOC_MEDIUM);
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        String mac = body["mac"] | "";
        String ip  = body["ip"]  | "";
        if (mac.isEmpty()) {
          sendError(req, 400, "mac field required", "MISSING_MAC");
          return;
        }
        if (_portalSessions && _portalSessions->startCoinWindow(mac, ip)) {
          DynamicJsonDocument out(RenzFiConfig::JSON_DOC_MEDIUM);
          _portalSessions->getSession(mac, ip, out);
          sendOk(req, out, "Coin window opened");
        } else {
          sendError(req, 500, "Failed to open coin window", "SESSION_ERROR");
        }
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/portal/done-paying", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        logRequest(req, "portal.done-paying");
        DynamicJsonDocument body(RenzFiConfig::JSON_DOC_MEDIUM);
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        String mac = body["mac"] | "";
        if (mac.isEmpty()) {
          sendError(req, 400, "mac field required", "MISSING_MAC");
          return;
        }
        if (!_portalSessions) {
          sendError(req, 500, "Portal session manager not ready", "NOT_READY");
          return;
        }
        if (_portalSessions->donePaying(mac)) {
          DynamicJsonDocument out(RenzFiConfig::JSON_DOC_MEDIUM);
          _portalSessions->getSession(mac, "", out);
          sendOk(req, out, "Session activated");
        } else {
          sendError(req, 400,
                    "No credits to convert — insert coins first", "NO_CREDITS");
        }
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/portal/pause", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        logRequest(req, "portal.pause");
        DynamicJsonDocument body(256);
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        String mac = body["mac"] | "";
        if (mac.isEmpty()) {
          sendError(req, 400, "mac field required", "MISSING_MAC");
          return;
        }
        if (_portalSessions && _portalSessions->pause(mac))
          sendOk(req, "Session paused");
        else
          sendError(req, 404, "Session not found", "NOT_FOUND");
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/portal/resume", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        logRequest(req, "portal.resume");
        DynamicJsonDocument body(256);
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        String mac = body["mac"] | "";
        if (mac.isEmpty()) {
          sendError(req, 400, "mac field required", "MISSING_MAC");
          return;
        }
        if (_portalSessions && _portalSessions->resume(mac))
          sendOk(req, "Session resumed");
        else
          sendError(req, 404, "Session not found", "NOT_FOUND");
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/portal/cancel-modal", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        logRequest(req, "portal.cancel-modal");
        DynamicJsonDocument body(256);
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        String mac = body["mac"] | "";
        if (mac.isEmpty()) {
          sendError(req, 400, "mac field required", "MISSING_MAC");
          return;
        }
        DynamicJsonDocument out(RenzFiConfig::JSON_DOC_MEDIUM);
        if (_portalSessions && _portalSessions->cancelModal(mac, out))
          sendOk(req, out, "Modal closed; credits preserved");
        else
          sendError(req, 500, "Cancel modal failed", "SESSION_ERROR");
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/portal/reset", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        logRequest(req, "portal.reset");
        DynamicJsonDocument body(256);
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

  _server->on(
      "/api/portal/heartbeat", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        DynamicJsonDocument body(256);
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
                Serial.println("[rates] entered handler");
                logRequest(req, "portal.rates");

                Serial.printf("[rates] _portalSessions ptr: %s\n",
                              _portalSessions ? "ok" : "NULL");
                if (!_portalSessions) {
                  Serial.println("[rates] FAILED: _portalSessions is NULL");
                  sendError(req, 500, "Failed to load rates", "PROMO_ERROR");
                  return;
                }

                Serial.println("[rates] allocating JSON document");
                DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
                Serial.printf("[rates] JSON_DOC_MEDIUM = %u bytes\n",
                              (unsigned)RenzFiConfig::JSON_DOC_MEDIUM);

                Serial.println("[rates] calling getRates()");
                bool ok = _portalSessions->getRates(doc);
                Serial.printf("[rates] getRates() returned: %s\n",
                              ok ? "true" : "false");

                if (ok) {
                  Serial.println("[rates] building response");
                  sendOk(req, doc);
                  Serial.println("[rates] response sent OK");
                } else {
                  Serial.println("[rates] FAILED: getRates() returned false");
                  sendError(req, 500, "Failed to load rates", "PROMO_ERROR");
                }
              });

  Serial.println("[boot] Portal session API routes registered:");
  Serial.println("[boot]   GET  /api/portal/branding");
  Serial.println("[boot]   GET  /api/portal/assets/banner");
  Serial.println("[boot]   GET  /api/portal/assets/music");
  Serial.println("[boot]   GET  /api/portal/session");
  Serial.println("[boot]   POST /api/portal/start-coin-session");
  Serial.println("[boot]   POST /api/portal/done-paying");
  Serial.println("[boot]   POST /api/portal/pause");
  Serial.println("[boot]   POST /api/portal/resume");
  Serial.println("[boot]   POST /api/portal/cancel-modal");
  Serial.println("[boot]   POST /api/portal/reset");
  Serial.println("[boot]   POST /api/portal/heartbeat");
  Serial.println("[boot]   GET  /api/portal/rates");

  // ── Customer portal (/ and /portal) ──────────────────────────────────────
  _server->on("/", HTTP_GET,
              [this](AsyncWebServerRequest *req) { sendStaticOrIndex(req); });
  _server->on("/portal", HTTP_GET,
              [this](AsyncWebServerRequest *req) { sendStaticOrIndex(req); });

  // ── Admin React SPA shell routes ──────────────────────────────────────────
  _server->on("/admin",                HTTP_GET,
              [this](AsyncWebServerRequest *req) { sendStaticOrIndex(req); });
  _server->on("/login",                HTTP_GET,
              [this](AsyncWebServerRequest *req) { sendStaticOrIndex(req); });
  _server->on("/dashboard",            HTTP_GET,
              [this](AsyncWebServerRequest *req) { sendStaticOrIndex(req); });
  _server->on("/manifest.webmanifest", HTTP_GET,
              [this](AsyncWebServerRequest *req) { sendStaticOrIndex(req); });
  _server->on("/sw.js",                HTTP_GET,
              [this](AsyncWebServerRequest *req) { sendStaticOrIndex(req); });
  _server->on("/favicon.svg",          HTTP_GET,
              [this](AsyncWebServerRequest *req) { sendStaticOrIndex(req); });
  _server->on("/favicon.ico",          HTTP_GET,
              [this](AsyncWebServerRequest *req) { sendStaticOrIndex(req); });

  Serial.println("[boot] Frontend routes registered:");
  Serial.println("[boot]   GET /assets/*  -> SPIFFS /assets/ (immutable cache)");
  Serial.println("[boot]   GET /, /portal -> SPIFFS /portal/index.html");
  Serial.println("[boot]   GET /portal/*  -> SPIFFS /portal/* (portal assets)");
  Serial.println("[boot]   GET /admin, /admin/* -> SPIFFS /index.html (admin React SPA)");
  Serial.println("[boot]   GET /manifest.webmanifest, /sw.js, /favicon.*");
  Serial.println("[boot]   onNotFound: parameterized API routes + SPA fallback");

  registerRenzFiPortalRoutes(*_server, SPIFFS);

  // ── Not-found handler ─────────────────────────────────────────────────────
  // Handles:
  //   • CORS preflight (OPTIONS) on any path
  //   • /admin/* SPA fallback
  //   • Parameterised API routes: /api/promos/{id}, /api/vouchers/{code},
  //     /api/sales/chart/{period}
  //   • /portal/*, /login/*, /dashboard/* SPA/static sub-paths
  //   • /api/* catch-all 404
  //   • Everything else → sendStaticOrIndex
  _server->onNotFound([this](AsyncWebServerRequest *req) {
    const String path   = req->url();
    const int    method = req->method();

    // CORS preflight for any path
    if (method == HTTP_OPTIONS) {
      AsyncWebServerResponse *res = req->beginResponse(204, "text/plain", "");
      addCorsHeaders(res);
      req->send(res);
      return;
    }

    // /admin/* — serve React SPA index
    if (serveRenzFiAdminSpaFallback(req, SPIFFS)) return;

    // /api/promos/{id}  PUT | DELETE
    if (path.startsWith("/api/promos/")) {
      if (!requireAuth(req)) return;
      int id = path.substring(12).toInt();
      if (method == HTTP_PUT) {
        DynamicJsonDocument body(RenzFiConfig::JSON_DOC_MEDIUM);
        String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        if (_promos->update(id, body.as<JsonObjectConst>())) sendOk(req);
        else sendError(req, 404, "Promo not found", "PROMO_NOT_FOUND");
        return;
      }
      if (method == HTTP_DELETE) {
        if (_promos->remove(id)) sendOk(req);
        else sendError(req, 404, "Promo not found", "PROMO_NOT_FOUND");
        return;
      }
      sendError(req, 405, "Method not allowed", "METHOD_NOT_ALLOWED");
      return;
    }

    // /api/vouchers/{code}  GET | DELETE
    if (path.startsWith("/api/vouchers/")) {
      if (!requireAuth(req)) return;
      String code = urlDecode(path.substring(14));
      if (method == HTTP_GET) {
        DynamicJsonDocument data(RenzFiConfig::JSON_DOC_SMALL);
        if (_vouchers->find(code, data)) sendOk(req, data);
        else sendError(req, 404, "Voucher not found", "VOUCHER_NOT_FOUND");
        return;
      }
      if (method == HTTP_DELETE) {
        if (_vouchers->remove(code)) sendOk(req);
        else sendError(req, 404, "Voucher not found", "VOUCHER_NOT_FOUND");
        return;
      }
      sendError(req, 405, "Method not allowed", "METHOD_NOT_ALLOWED");
      return;
    }

    // /api/sales/chart/{daily|weekly|monthly}
    if (path.startsWith("/api/sales/chart/")) {
      if (!requireAuth(req)) return;
      if (method == HTTP_GET) {
        int days = 7;
        if (path.endsWith("/weekly")) days = 28;
        else if (path.endsWith("/monthly")) days = 180;
        DynamicJsonDocument data(2048);
        if (_sessions->salesChart(data, days)) sendOk(req, data);
        else sendError(req, 500, "Unable to build sales chart", "SALES_CHART_ERROR");
        return;
      }
      sendError(req, 405, "Method not allowed", "METHOD_NOT_ALLOWED");
      return;
    }

    // Sub-paths for static-served SPA roots
    if (path.startsWith("/portal/") || path.startsWith("/login/") ||
        path.startsWith("/dashboard/")) {
      sendStaticOrIndex(req);
      return;
    }

    // /api/* — 404 JSON
    if (path.startsWith("/api/")) {
      IPAddress remoteIp = req->client()->remoteIP();
      Serial.printf("[tcp] 404 %s %s from=%s local=%s\n",
                    methodStr(req->method()), path.c_str(),
                    remoteIp.toString().c_str(),
                    W5500Config::IP.toString().c_str());
      sendError(req, 404, "API endpoint not found", "NOT_FOUND");
      return;
    }

    // /assets/* missing from SPIFFS (serveStatic already matched found files)
    if (path.startsWith("/assets/")) {
      IPAddress remoteIp = req->client()->remoteIP();
      Serial.printf("[tcp] 404 %s %s from=%s local=%s (missing asset)\n",
                    methodStr(req->method()), path.c_str(),
                    remoteIp.toString().c_str(),
                    W5500Config::IP.toString().c_str());
      AsyncWebServerResponse *res =
          req->beginResponse(404, "text/plain", "Not Found");
      addCorsHeaders(res);
      res->addHeader("Cache-Control", "no-store");
      req->send(res);
      return;
    }

    // Catch-all → SPA or static SPIFFS file
    IPAddress remoteIp = req->client()->remoteIP();
    Serial.printf("[tcp] %s %s from=%s local=%s -> SPA\n",
                  methodStr(req->method()), path.c_str(),
                  remoteIp.toString().c_str(),
                  W5500Config::IP.toString().c_str());
    sendStaticOrIndex(req);
  });
}
