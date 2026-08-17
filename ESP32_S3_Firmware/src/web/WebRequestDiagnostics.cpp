#include "WebRequestDiagnostics.h"

#include "EthernetManager.h"
#include "HttpPlaneGate.h"
#include "ManagementApConfig.h"
#include "NetworkDiagnostics.h"
#include "RenzFiDebug.h"

namespace WebRequestDiagnostics {

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

IPAddress requestRemoteIp(AsyncWebServerRequest *req) {
  if (!req) return IPAddress();
  AsyncClient *client = req->client();
  return client ? client->remoteIP() : IPAddress();
}

IPAddress requestLocalIp(AsyncWebServerRequest *req) {
  if (!req) return IPAddress();
  AsyncClient *client = req->client();
  return client ? client->localIP() : IPAddress();
}

bool isManagementApRequest(AsyncWebServerRequest *req) {
  if (!req) return false;

  const IPAddress local = requestLocalIp(req);
  if (local == ManagementApConfig::IP) return true;

  if (req->hasHeader("Host")) {
    const String host = req->getHeader("Host")->value();
    if (host.startsWith("192.168.4.1")) return true;
  }
  return false;
}

const char *viaLabel(AsyncWebServerRequest *req) {
  if (isManagementApRequest(req)) return "AP";
  const IPAddress local = requestLocalIp(req);
  if (local[0] != 0) return "ETH";
  return "unknown";
}

String requestBaseUrl(AsyncWebServerRequest *req, EthernetManager *eth) {
  if (isManagementApRequest(req)) {
    return String(ManagementApConfig::PORTAL_URL);
  }
  if (eth && eth->hasIp()) {
    return String("http://") + eth->ip();
  }
  const IPAddress local = requestLocalIp(req);
  if (local[0] != 0) {
    return String("http://") + local.toString();
  }
  return String(ManagementApConfig::PORTAL_URL);
}

void logRequest(AsyncWebServerRequest *req, const char *handler) {
  NetworkDiagnostics::logHttpIncoming(req);
#if RENZFI_DEBUG_HTTP
  if (!req) return;
  const IPAddress remote = requestRemoteIp(req);
  const IPAddress local = requestLocalIp(req);
  const char *host =
      req->hasHeader("Host") ? req->getHeader("Host")->value().c_str() : "";
  Serial.printf("[http] %s %s remote=%s host=%s local=%s via=%s handler=%s\n",
                methodStr(req->method()), req->url().c_str(),
                remote.toString().c_str(), host, local.toString().c_str(),
                viaLabel(req), handler ? handler : "");
#else
  (void)handler;
#endif
}

RequestTimer::RequestTimer(AsyncWebServerRequest *req, const char *handler)
    : _req(req), _handler(handler ? handler : ""), _startMs(millis()) {
  if (!_req) return;
  NetworkDiagnostics::logHttpIncoming(_req);
  Serial.printf(
      "[http] begin method=%s path=%s remote=%s local=%s via=%s plane=%s handler=%s\n",
      methodStr(_req->method()), _req->url().c_str(),
      requestRemoteIp(_req).toString().c_str(),
      requestLocalIp(_req).toString().c_str(), viaLabel(_req),
      HttpPlaneGate::planeLabel(_req), _handler);
}

RequestTimer::~RequestTimer() {
  finish();
}

void RequestTimer::finish() {
  if (!_req) return;
  const uint32_t elapsedMs = millis() - _startMs;
  Serial.printf("[http] end   method=%s path=%s via=%s handler=%s elapsedMs=%u\n",
                methodStr(_req->method()), _req->url().c_str(), viaLabel(_req),
                _handler, (unsigned)elapsedMs);
  if (elapsedMs > RENZFI_SLOW_HANDLER_WARN_MS) {
    Serial.printf(
        "[http] SLOW HANDLER method=%s path=%s handler=%s elapsedMs=%u "
        "(threshold=%ums)\n",
        methodStr(_req->method()), _req->url().c_str(), _handler,
        (unsigned)elapsedMs, (unsigned)RENZFI_SLOW_HANDLER_WARN_MS);
  }
  _req = nullptr;
}

}  // namespace WebRequestDiagnostics
