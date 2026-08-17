#include "ProvisioningServer.h"

#include "AuthManager.h"
#include "Config.h"
#include "provisioning/ProvisioningEngine.h"
#include "HttpPlaneGate.h"
#include "WebRequestDiagnostics.h"
#include "WebResponse.h"
#include "WebServerManager.h"

void ProvisioningServer::begin(AuthManager *auth, ProvisioningEngine *engine) {
  _auth   = auth;
  _engine = engine;
}

bool ProvisioningServer::requireAuth(AsyncWebServerRequest *req) const {
  if (!_auth) return false;
  const String cookie =
      req->hasHeader("Cookie") ? req->getHeader("Cookie")->value() : String("");
  if (!_auth->isAuthenticated(cookie)) {
    sendError(req, 401, "Authentication required", "UNAUTHENTICATED");
    return false;
  }
  return true;
}

String ProvisioningServer::getBody(AsyncWebServerRequest *req) {
  if (req->_tempObject) return String(static_cast<char *>(req->_tempObject));
  return "";
}

void ProvisioningServer::bodyCollect(AsyncWebServerRequest *req, uint8_t *data,
                                     size_t len, size_t index, size_t total) {
  if (total > 8192) return;
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

void ProvisioningServer::sendOk(AsyncWebServerRequest *req, JsonDocument &data,
                                const char *message) const {
  DynamicJsonDocument envelope(RenzFiConfig::JSON_DOC_MEDIUM);
  envelope["success"] = true;
  envelope["message"] = message;
  JsonObject dataObj    = envelope["data"].to<JsonObject>();
  if (data.is<JsonObject>()) {
    dataObj.set(data.as<JsonObject>());
  }
  WebResponse::serveJsonEnvelope(req, 200, envelope);
}

void ProvisioningServer::sendError(AsyncWebServerRequest *req, int status,
                                   const char *error, const char *code) const {
  WebResponse::serveErrorJson(req, status, error, code);
}

void ProvisioningServer::registerRoutes(WebServerManager &web) {
  _server = &web.routeServer();
  if (!_server || !_engine) return;

  Serial.println("[web] ProvisioningServer registering workflow /api/provisioning/*");

  _server->on(
      "/api/provisioning/installation/begin", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        WebRequestDiagnostics::RequestTimer timer(req, "Provisioning/begin");
        if (!HttpPlaneGate::ensureSetupPlane(req)) return;
        if (!requireAuth(req)) return;
        DynamicJsonDocument body(RenzFiConfig::JSON_DOC_SMALL);
        const String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        DynamicJsonDocument result(RenzFiConfig::JSON_DOC_MEDIUM);
        if (_engine->beginInstallation(body.as<JsonObjectConst>(), result)) {
          sendOk(req, result, "Installation started");
        } else {
          sendError(req, 400, result["error"] | "Unable to start",
                    "PROVISIONING_ERROR");
        }
      },
      nullptr, bodyCollect);

  _server->on("/api/provisioning/installation/resume", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
                if (!requireAuth(req)) return;
                DynamicJsonDocument result(RenzFiConfig::JSON_DOC_MEDIUM);
                if (_engine->resumeInstallation(result)) {
                  sendOk(req, result, "Installation resumed");
                } else {
                  sendError(req, 500, result["error"] | "Unable to resume",
                            "PROVISIONING_ERROR");
                }
              });

  _server->on("/api/provisioning/installation/abort", HTTP_POST,
              [this](AsyncWebServerRequest *req) {
                if (!requireAuth(req)) return;
                DynamicJsonDocument result(RenzFiConfig::JSON_DOC_MEDIUM);
                if (_engine->abortInstallation(result)) {
                  sendOk(req, result, "Installation aborted");
                } else {
                  sendError(req, 400, result["error"] | "Unable to abort",
                            "PROVISIONING_ERROR");
                }
              });

  _server->on("/api/provisioning/installation/factory-reset", HTTP_POST,
              [this](AsyncWebServerRequest *req) {
                if (!requireAuth(req)) return;
                DynamicJsonDocument result(RenzFiConfig::JSON_DOC_MEDIUM);
                if (_engine->factoryReset(result)) {
                  sendOk(req, result, "Factory reset complete");
                } else {
                  sendError(req, 500, result["error"] | "Unable to reset",
                            "PROVISIONING_ERROR");
                }
              });

  _server->on("/api/provisioning/routers/detect", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
                if (!requireAuth(req)) return;
                DynamicJsonDocument result(RenzFiConfig::JSON_DOC_MEDIUM);
                if (_engine->detectRouters(result)) {
                  sendOk(req, result, "Router detection complete");
                } else {
                  sendError(req, 500, result["error"] | "Detection failed",
                            "PROVISIONING_ERROR");
                }
              });

  _server->on(
      "/api/provisioning/routers/select", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        if (!requireAuth(req)) return;
        DynamicJsonDocument body(RenzFiConfig::JSON_DOC_SMALL);
        const String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        DynamicJsonDocument result(RenzFiConfig::JSON_DOC_MEDIUM);
        if (_engine->selectDriver(body.as<JsonObjectConst>(), result)) {
          sendOk(req, result, "Router driver selected");
        } else {
          sendError(req, 400, result["error"] | "Unable to select driver",
                    "PROVISIONING_ERROR");
        }
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/provisioning/routers/connect", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        if (!requireAuth(req)) return;
        DynamicJsonDocument body(RenzFiConfig::JSON_DOC_SMALL);
        const String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        DynamicJsonDocument result(RenzFiConfig::JSON_DOC_MEDIUM);
        if (_engine->connectRouter(body.as<JsonObjectConst>(), result)) {
          sendOk(req, result, "Router connected");
        } else {
          sendError(req, 400, result["error"] | "Router connection failed",
                    "PROVISIONING_ERROR");
        }
      },
      nullptr, bodyCollect);

  _server->on("/api/provisioning/routers/profiles", HTTP_GET,
              [this](AsyncWebServerRequest *req) {
                if (!requireAuth(req)) return;
                DynamicJsonDocument result(RenzFiConfig::JSON_DOC_MEDIUM);
                if (_engine->listRouterProfiles(result)) {
                  sendOk(req, result, "Router profiles loaded");
                } else {
                  sendError(req, 500, result["error"] | "Unable to load profiles",
                            "PROVISIONING_ERROR");
                }
              });

  _server->on(
      "/api/provisioning/portal/configure", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        if (!requireAuth(req)) return;
        DynamicJsonDocument body(RenzFiConfig::JSON_DOC_SMALL);
        const String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        DynamicJsonDocument result(RenzFiConfig::JSON_DOC_MEDIUM);
        if (_engine->configurePortal(body.as<JsonObjectConst>(), result)) {
          sendOk(req, result, "Portal configured");
        } else {
          sendError(req, 400, result["error"] | "Portal configuration failed",
                    "PROVISIONING_ERROR");
        }
      },
      nullptr, bodyCollect);

  _server->on(
      "/api/provisioning/coin/configure", HTTP_POST,
      [this](AsyncWebServerRequest *req) {
        if (!requireAuth(req)) return;
        DynamicJsonDocument body(RenzFiConfig::JSON_DOC_SMALL);
        const String raw = getBody(req);
        if (raw.length() > 0) deserializeJson(body, raw);
        DynamicJsonDocument result(RenzFiConfig::JSON_DOC_MEDIUM);
        if (_engine->configureCoin(body.as<JsonObjectConst>(), result)) {
          sendOk(req, result, "Coin configured");
        } else {
          sendError(req, 400, result["error"] | "Coin configuration failed",
                    "PROVISIONING_ERROR");
        }
      },
      nullptr, bodyCollect);

  _server->on("/api/provisioning/validate", HTTP_POST,
              [this](AsyncWebServerRequest *req) {
                if (!requireAuth(req)) return;
                DynamicJsonDocument result(RenzFiConfig::JSON_DOC_MEDIUM);
                if (_engine->validateInstallation(result)) {
                  sendOk(req, result, "Validation passed");
                } else {
                  sendError(req, 400, result["error"] | "Validation failed",
                            "PROVISIONING_ERROR");
                }
              });

  _server->on("/api/provisioning/finish", HTTP_POST,
              [this](AsyncWebServerRequest *req) {
                if (!requireAuth(req)) return;
                DynamicJsonDocument result(RenzFiConfig::JSON_DOC_MEDIUM);
                if (_engine->finalizeInstallation(result)) {
                  sendOk(req, result, "Installation complete");
                } else {
                  sendError(req, 400, result["error"] | "Unable to finish",
                            "PROVISIONING_ERROR");
                }
              });
}

const char *ProvisioningServer::providerName() const {
  return "ProvisioningServer";
}
