#include "WebResponse.h"

#include "MimeResolver.h"

void WebResponse::addCorsHeaders(AsyncWebServerResponse *res) {
  if (!res) return;
  addSecurityHeaders(res);
  // Access-Control-Allow-Origin is owned solely by DefaultHeaders (registered
  // once in WebServerManager::initialize). Adding it here again duplicated ACAO
  // when DefaultHeaders already contributed one (Chrome: "*, *").
  res->addHeader("Access-Control-Allow-Methods",
                 "GET, POST, PUT, DELETE, OPTIONS");
  res->addHeader("Access-Control-Allow-Headers",
                 "Content-Type, Authorization");
}

void WebResponse::addSecurityHeaders(AsyncWebServerResponse *res) {
  if (!res) return;
  res->addHeader("X-Content-Type-Options", "nosniff");
  res->addHeader("X-Frame-Options", "SAMEORIGIN");
  res->addHeader("Referrer-Policy", "same-origin");
}

void WebResponse::serveFile(AsyncWebServerRequest *req, fs::FS &fs,
                            const String &fsPath, const String &mimePath,
                            CachePolicy cache, bool gzip) {
  if (!req) return;
  AsyncWebServerResponse *res =
      req->beginResponse(fs, fsPath, MimeResolver::fromPath(mimePath));
  if (gzip) res->addHeader("Content-Encoding", "gzip");
  addCorsHeaders(res);
  CacheManager::apply(res, cache);
  req->send(res);
}

void WebResponse::serveJson(AsyncWebServerRequest *req, int status,
                            const String &body, CachePolicy cache) {
  if (!req) return;
  AsyncWebServerResponse *res =
      req->beginResponse(status, "application/json", body);
  addCorsHeaders(res);
  CacheManager::apply(res, cache);
  req->send(res);
}

void WebResponse::serveJsonEnvelope(AsyncWebServerRequest *req, int status,
                                    JsonDocument &doc, CachePolicy cache) {
  String body;
  serializeJson(doc, body);
  serveJson(req, status, body, cache);
}

void WebResponse::serveErrorJson(AsyncWebServerRequest *req, int status,
                                 const String &error, const String &code) {
  DynamicJsonDocument doc(256);
  doc["success"] = false;
  doc["error"] = error;
  doc["code"] = code;
  String body;
  serializeJson(doc, body);
  serveJson(req, status, body, CachePolicy::NoCache);
}

void WebResponse::serveNotFound(AsyncWebServerRequest *req, bool plainText) {
  if (!req) return;
  if (plainText) {
    AsyncWebServerResponse *res =
        req->beginResponse(404, "text/plain", "Not Found");
    addCorsHeaders(res);
    CacheManager::apply(res, CachePolicy::NoCache);
    req->send(res);
    return;
  }
  req->send(404, "text/plain", "Not Found");
}

void WebResponse::serveRedirect(AsyncWebServerRequest *req,
                                const String &location, int statusCode) {
  if (!req) return;
  AsyncWebServerResponse *res = req->beginResponse(statusCode);
  res->addHeader("Location", location);
  addCorsHeaders(res);
  CacheManager::apply(res, CachePolicy::NoCache);
  req->send(res);
}

void WebResponse::serveDownload(AsyncWebServerRequest *req, fs::FS &fs,
                                const String &fsPath, const String &filename,
                                const char *mime) {
  if (!req) return;
  AsyncWebServerResponse *res = req->beginResponse(fs, fsPath, mime);
  res->addHeader("Content-Disposition",
                 String("attachment; filename=\"") + filename + "\"");
  addCorsHeaders(res);
  CacheManager::apply(res, CachePolicy::NoCache);
  req->send(res);
}

void WebResponse::serveOptions(AsyncWebServerRequest *req) {
  if (!req) return;
  AsyncWebServerResponse *res = req->beginResponse(204, "text/plain", "");
  addCorsHeaders(res);
  req->send(res);
}
