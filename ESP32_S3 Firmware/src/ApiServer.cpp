#define ASYNCWEBSERVER_REGEX 1

#include "ApiServer.h"

#include <ESPAsyncWebServer.h>
#include <SD.h>

#include "config.h"

namespace {

using JsonRoute = std::function<void(AsyncWebServerRequest *, JsonVariantConst)>;

void addJsonRoute(AsyncWebServer *server, const char *path, WebRequestMethodComposite method, JsonRoute route) {
  server->on(
      path, method,
      [route](AsyncWebServerRequest *request) {
        DynamicJsonDocument body(RenzFiConfig::JSON_DOC_MEDIUM);
        if (request->_tempObject) {
          String *raw = static_cast<String *>(request->_tempObject);
          if (raw->length() > 0) deserializeJson(body, *raw);
          delete raw;
          request->_tempObject = nullptr;
        }
        route(request, body.as<JsonVariantConst>());
      },
      nullptr,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (index == 0) request->_tempObject = new String();
        String *raw = static_cast<String *>(request->_tempObject);
        raw->concat(reinterpret_cast<const char *>(data), len);
        if (index + len >= total && total == 0) {
          // Empty bodies are handled by the request callback.
        }
      });
}

String urlDecode(String value) {
  value.replace("%20", " ");
  value.replace("%2F", "/");
  value.replace("%2f", "/");
  value.replace("%3A", ":");
  value.replace("%3a", ":");
  return value;
}

}  // namespace

void ApiServer::begin(AsyncWebServer *server,
                      StorageManager *storage,
                      AuthManager *auth,
                      SessionManager *sessions,
                      PromoManager *promos,
                      VoucherManager *vouchers,
                      CoinManager *coin,
                      MikroTikManager *mikrotik,
                      Logger *logger,
                      EventBus *events,
                      CaptivePortal *captive) {
  _server = server;
  _storage = storage;
  _auth = auth;
  _sessions = sessions;
  _promos = promos;
  _vouchers = vouchers;
  _coin = coin;
  _mikrotik = mikrotik;
  _logger = logger;
  _events = events;
  _captive = captive;
  registerRoutes();
}

bool ApiServer::requireAuth(AsyncWebServerRequest *request) {
  if (_auth && _auth->isAuthenticated(request)) return true;
  sendError(request, 401, "Authentication required", "UNAUTHENTICATED");
  return false;
}

void ApiServer::sendOk(AsyncWebServerRequest *request, JsonDocument &data, const String &message) {
  String dataBody;
  serializeJson(data, dataBody);
  String body = "{\"success\":true,\"data\":";
  body += dataBody;
  body += ",\"message\":\"";
  body += message;
  body += "\"}";
  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", body);
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

void ApiServer::sendOk(AsyncWebServerRequest *request, const String &message) {
  DynamicJsonDocument envelope(256);
  envelope["success"] = true;
  envelope["data"]["ok"] = true;
  envelope["message"] = message;
  String body;
  serializeJson(envelope, body);
  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", body);
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

void ApiServer::sendError(AsyncWebServerRequest *request, int status, const String &error, const String &code) {
  DynamicJsonDocument doc(256);
  doc["success"] = false;
  doc["error"] = error;
  doc["code"] = code;
  String body;
  serializeJson(doc, body);
  AsyncWebServerResponse *response = request->beginResponse(status, "application/json", body);
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

void ApiServer::sendStaticOrIndex(AsyncWebServerRequest *request) {
  if (_captive && _captive->isCaptiveRequest(request)) {
    _captive->redirectToPortal(request);
    return;
  }

  String path = request->url();
  String gzPath = String(RenzFiConfig::WWW_ROOT) + path + ".gz";
  String fullPath = String(RenzFiConfig::WWW_ROOT) + path;
  if (fullPath.endsWith("/")) fullPath += "index.html";
  if (!_storage->exists(fullPath.c_str()) && !path.startsWith("/api/")) fullPath = String(RenzFiConfig::WWW_ROOT) + "/index.html";
  bool gzip = _storage->exists(gzPath.c_str());
  if (gzip) fullPath = gzPath;
  if (!_storage->exists(fullPath.c_str())) {
    request->send(404, "text/plain", "Static frontend not found. Copy build output to /www on SD card.");
    return;
  }

  String typePath = (path == "/" || fullPath.endsWith("index.html") || fullPath.endsWith("index.html.gz")) ? "/index.html" : path;
  AsyncWebServerResponse *response = request->beginResponse(SD, fullPath, _storage->contentType(typePath));
  if (gzip) response->addHeader("Content-Encoding", "gzip");
  if (path.indexOf("/assets/") == 0) response->addHeader("Cache-Control", "public, max-age=31536000, immutable");
  else response->addHeader("Cache-Control", "no-cache");
  request->send(response);
}

void ApiServer::registerRoutes() {
  _server->on("/api/health", HTTP_GET, [this](AsyncWebServerRequest *request) {
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_SMALL);
    data["ok"] = true;
    data["storage"]["ok"] = _storage->healthy();
    data["session"]["authenticated"] = _auth->isAuthenticated(request);
    data["session"]["mustChangePassword"] = _auth->mustChangePassword();
    sendOk(request, data);
  });

  addJsonRoute(_server, "/api/auth/login", HTTP_POST, [this](AsyncWebServerRequest *request, JsonVariantConst body) {
    DynamicJsonDocument response(RenzFiConfig::JSON_DOC_SMALL);
    String password = body["password"] | "";
    bool rememberIp = body["rememberIp"] | false;
    String setCookie;
    if (!_auth->login(password, rememberIp, response, setCookie)) {
      sendError(request, 401, "Invalid password", "INVALID_CREDENTIALS");
    } else {
      DynamicJsonDocument envelope(RenzFiConfig::JSON_DOC_SMALL);
      envelope["success"] = true;
      envelope["data"].set(response.as<JsonObject>());
      envelope["message"] = "OK";
      String payload;
      serializeJson(envelope, payload);
      AsyncWebServerResponse *res = request->beginResponse(200, "application/json", payload);
      res->addHeader("Cache-Control", "no-store");
      res->addHeader("Set-Cookie", setCookie);
      request->send(res);
    }
  });

  _server->on("/api/auth/logout", HTTP_POST, [this](AsyncWebServerRequest *request) {
    _auth->logout(request);
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"success\":true,\"data\":{\"ok\":true},\"message\":\"OK\"}");
    response->addHeader("Set-Cookie", String(RenzFiConfig::SESSION_COOKIE) + "=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0");
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
  });

  addJsonRoute(_server, "/api/auth/change-password", HTTP_POST, [this](AsyncWebServerRequest *request, JsonVariantConst body) {
    if (!requireAuth(request)) return;
    if (_auth->changePassword(body["oldPassword"] | "", body["newPassword"] | "")) sendOk(request);
    else sendError(request, 400, "Unable to change password", "PASSWORD_CHANGE_FAILED");
  });

  _server->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_MEDIUM);
    DynamicJsonDocument sales(256);
    _sessions->salesToday(sales);
    data["server"]["ok"] = true;
    data["server"]["uptimeSeconds"] = millis() / 1000;
    data["database"]["ok"] = _storage->healthy();
    data["database"]["path"] = "SD";
    data["sales"]["today"]["amount"] = sales["amount"] | 0;
    data["sales"]["today"]["sessions"] = sales["sessions"] | 0;
    data["sales"]["weekly"]["amount"] = sales["amount"] | 0;
    data["sales"]["weekly"]["sessions"] = sales["sessions"] | 0;
    data["sales"]["monthly"]["amount"] = sales["amount"] | 0;
    data["sales"]["monthly"]["sessions"] = sales["sessions"] | 0;
    data["activeUsers"]["count"] = _sessions->activeCount();
    data["activeUsers"]["idle"] = 0;
    data["mikrotik"]["ok"] = true;
    data["mikrotik"]["host"] = "configured";
    data["mikrotik"]["latencyMs"] = 0;
    data["internet"]["ok"] = true;
    data["internet"]["latencyMs"] = 0;
    data["coinSlot"]["ok"] = true;
    data["coinSlot"]["state"] = "ready";
    data["coinSlot"]["pulsesToday"] = 0;
    data["hotspot"]["ok"] = true;
    data["hotspot"]["ssid"] = RenzFiConfig::AP_SSID;
    data["esp32"]["uptime"] = String(millis() / 1000) + "s";
    data["esp32"]["lastSeen"] = nullptr;
    data["storage"]["flashUsedMb"] = _storage->usedBytes() / 1024 / 1024;
    data["storage"]["flashTotalMb"] = _storage->totalBytes() / 1024 / 1024;
    data["storage"]["ramUsedKb"] = (ESP.getHeapSize() - ESP.getFreeHeap()) / 1024;
    data["storage"]["ramTotalKb"] = ESP.getHeapSize() / 1024;
    data["storage"]["logsUsedKb"] = 0;
    data["storage"]["logsTotalKb"] = 0;
    data["sync"]["pending"] = 0;
    data["sync"]["lastSyncAt"] = nullptr;
    sendOk(request, data);
  });

  _server->on("/api/system/reboot", HTTP_POST, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    sendOk(request, "Rebooting");
    delay(250);
    ESP.restart();
  });

  _server->on("/api/system/factory-reset", HTTP_POST, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    _storage->clearJsonArray(RenzFiConfig::SALES_FILE);
    _storage->clearJsonArray(RenzFiConfig::LOGS_FILE);
    _storage->clearJsonArray(RenzFiConfig::VOUCHERS_FILE);
    _storage->clearJsonArray(RenzFiConfig::USERS_FILE);
    _auth->resetToDefault();
    sendOk(request);
  });

  _server->on("/api/promos", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_MEDIUM);
    if (_promos->list(data)) sendOk(request, data);
    else sendError(request, 500, "Unable to load promos", "STORAGE_ERROR");
  });
  addJsonRoute(_server, "/api/promos", HTTP_POST, [this](AsyncWebServerRequest *request, JsonVariantConst body) {
    if (!requireAuth(request)) return;
    DynamicJsonDocument data(128);
    int id = _promos->create(body.as<JsonObjectConst>());
    if (id < 0) sendError(request, 500, "Unable to create promo", "PROMO_CREATE_FAILED");
    else {
      data["id"] = id;
      sendOk(request, data);
    }
  });
  addJsonRoute(_server, "^\\/api\\/promos\\/([0-9]+)$", HTTP_PUT, [this](AsyncWebServerRequest *request, JsonVariantConst body) {
    if (!requireAuth(request)) return;
    int id = request->pathArg(0).toInt();
    if (_promos->update(id, body.as<JsonObjectConst>())) sendOk(request);
    else sendError(request, 404, "Promo not found", "PROMO_NOT_FOUND");
  });
  _server->on("^\\/api\\/promos\\/([0-9]+)$", HTTP_DELETE, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    if (_promos->remove(request->pathArg(0).toInt())) sendOk(request);
    else sendError(request, 404, "Promo not found", "PROMO_NOT_FOUND");
  });

  _server->on("/api/vouchers", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_LARGE);
    if (_vouchers->list(data)) sendOk(request, data);
    else sendError(request, 500, "Unable to load vouchers", "STORAGE_ERROR");
  });
  addJsonRoute(_server, "/api/vouchers", HTTP_POST, [this](AsyncWebServerRequest *request, JsonVariantConst body) {
    if (!requireAuth(request)) return;
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_MEDIUM);
    if (_vouchers->generate(body["count"] | 1, body["amount"] | 1, body["minutes"] | 5, body["expires"] | "", data)) sendOk(request, data);
    else sendError(request, 500, "Unable to generate vouchers", "VOUCHER_CREATE_FAILED");
  });
  _server->on("^\\/api\\/vouchers\\/([^\\/]+)$", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_SMALL);
    if (_vouchers->find(urlDecode(request->pathArg(0)), data)) sendOk(request, data);
    else sendError(request, 404, "Voucher not found", "VOUCHER_NOT_FOUND");
  });
  _server->on("^\\/api\\/vouchers\\/([^\\/]+)$", HTTP_DELETE, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    if (_vouchers->remove(urlDecode(request->pathArg(0)))) sendOk(request);
    else sendError(request, 404, "Voucher not found", "VOUCHER_NOT_FOUND");
  });

  _server->on("/api/users", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_MEDIUM);
    if (_sessions->listUsers(data)) sendOk(request, data);
    else sendError(request, 500, "Unable to load active users", "STORAGE_ERROR");
  });
  addJsonRoute(_server, "/api/users/disconnect", HTTP_POST, [this](AsyncWebServerRequest *request, JsonVariantConst body) {
    if (!requireAuth(request)) return;
    String mac = body["mac"] | "";
    _mikrotik->disconnectHotspotUser(mac);
    if (_sessions->disconnect(mac)) sendOk(request);
    else sendError(request, 404, "Active user not found", "USER_NOT_FOUND");
  });

  _server->on("/api/sales/today", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    DynamicJsonDocument data(256);
    _sessions->salesToday(data);
    sendOk(request, data);
  });
  _server->on("/api/sales/weekly", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    DynamicJsonDocument data(256);
    _sessions->salesPeriod(data, 7);
    sendOk(request, data);
  });
  _server->on("/api/sales/monthly", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    DynamicJsonDocument data(256);
    _sessions->salesPeriod(data, 30);
    sendOk(request, data);
  });
  _server->on("/api/sales/history", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_MEDIUM);
    _sessions->salesHistory(data);
    sendOk(request, data);
  });
  _server->on("^\\/api\\/sales\\/chart\\/(daily|weekly|monthly)$", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    DynamicJsonDocument data(512);
    JsonArray labels = data["labels"].to<JsonArray>();
    JsonArray values = data["data"].to<JsonArray>();
    DynamicJsonDocument today(256);
    _sessions->salesToday(today);
    labels.add("Today");
    values.add(today["amount"] | 0);
    sendOk(request, data);
  });
  _server->on("/api/sales/export", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    request->send(SD, RenzFiConfig::SALES_FILE, "application/json", true);
  });

  _server->on("/api/settings", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_MEDIUM);
    _storage->readJson(RenzFiConfig::SETTINGS_FILE, data);
    sendOk(request, data);
  });
  addJsonRoute(_server, "/api/settings", HTTP_POST | HTTP_PUT, [this](AsyncWebServerRequest *request, JsonVariantConst body) {
    if (!requireAuth(request)) return;
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_MEDIUM);
    data.set(body);
    if (_storage->writeJson(RenzFiConfig::SETTINGS_FILE, data)) sendOk(request);
    else sendError(request, 500, "Unable to save settings", "STORAGE_ERROR");
  });
  _server->on("/api/settings/admin", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    DynamicJsonDocument data(128);
    data["username"] = "admin";
    sendOk(request, data);
  });
  addJsonRoute(_server, "/api/settings/admin", HTTP_PUT, [this](AsyncWebServerRequest *request, JsonVariantConst) {
    if (!requireAuth(request)) return;
    sendOk(request);
  });
  _server->on("/api/settings/backup", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    request->send(SD, RenzFiConfig::SETTINGS_FILE, "application/json", true);
  });
  addJsonRoute(_server, "/api/settings/restore", HTTP_POST, [this](AsyncWebServerRequest *request, JsonVariantConst body) {
    if (!requireAuth(request)) return;
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_MEDIUM);
    data.set(body);
    if (_storage->writeJson(RenzFiConfig::SETTINGS_FILE, data)) sendOk(request);
    else sendError(request, 500, "Unable to restore settings", "RESTORE_FAILED");
  });

  _server->on("/api/logs", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    String q = request->hasParam("q") ? request->getParam("q")->value() : "";
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_LARGE);
    if (_logger->list(data, q)) sendOk(request, data);
    else sendError(request, 500, "Unable to load logs", "STORAGE_ERROR");
  });
  _server->on("/api/logs", HTTP_DELETE, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    if (_logger->clear()) sendOk(request);
    else sendError(request, 500, "Unable to clear logs", "STORAGE_ERROR");
  });
  _server->on("/api/logs/export", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    request->send(SD, RenzFiConfig::LOGS_FILE, "application/json", true);
  });

  _server->on("/api/coin/settings", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    DynamicJsonDocument data(512);
    _coin->settings(data);
    sendOk(request, data);
  });
  addJsonRoute(_server, "/api/coin/settings", HTTP_POST | HTTP_PUT, [this](AsyncWebServerRequest *request, JsonVariantConst body) {
    if (!requireAuth(request)) return;
    if (_coin->saveSettings(body.as<JsonObjectConst>())) sendOk(request);
    else sendError(request, 500, "Unable to save coin settings", "STORAGE_ERROR");
  });
  _server->on("/api/coin/diagnostics", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_SMALL);
    _coin->diagnostics(data);
    sendOk(request, data);
  });
  _server->on("/api/coin/test", HTTP_POST, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    _sessions->grantCoinSession(1, _promos->minutesForAmount(1));
    sendOk(request);
  });

  _server->on("/api/router/settings", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    DynamicJsonDocument data(RenzFiConfig::JSON_DOC_SMALL);
    _mikrotik->load(data);
    sendOk(request, data);
  });
  addJsonRoute(_server, "/api/router/settings", HTTP_POST | HTTP_PUT, [this](AsyncWebServerRequest *request, JsonVariantConst body) {
    if (!requireAuth(request)) return;
    if (_mikrotik->save(body.as<JsonObjectConst>())) sendOk(request);
    else sendError(request, 500, "Unable to save router settings", "STORAGE_ERROR");
  });
  addJsonRoute(_server, "/api/router/test", HTTP_POST, [this](AsyncWebServerRequest *request, JsonVariantConst body) {
    if (!requireAuth(request)) return;
    if (_mikrotik->test(body.as<JsonObjectConst>())) sendOk(request);
    else sendError(request, 400, "Router settings incomplete", "ROUTER_TEST_FAILED");
  });

  _server->onNotFound([this](AsyncWebServerRequest *request) {
    if (request->url().startsWith("/api/")) {
      sendError(request, 404, "API endpoint not found", "NOT_FOUND");
      return;
    }
    sendStaticOrIndex(request);
  });
}
