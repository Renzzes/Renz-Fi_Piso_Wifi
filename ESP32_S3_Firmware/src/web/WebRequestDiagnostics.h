#pragma once

#include <ESPAsyncWebServer.h>

class EthernetManager;

namespace WebRequestDiagnostics {

const char *methodStr(WebRequestMethodComposite method);
IPAddress requestRemoteIp(AsyncWebServerRequest *req);
IPAddress requestLocalIp(AsyncWebServerRequest *req);
bool isManagementApRequest(AsyncWebServerRequest *req);
const char *viaLabel(AsyncWebServerRequest *req);
String requestBaseUrl(AsyncWebServerRequest *req, EthernetManager *eth);
void logRequest(AsyncWebServerRequest *req, const char *handler);

// Low-volume per-request begin/end diagnostics for the async_tcp watchdog
// investigation (see docs/PHASE_9_ETH_AP_COEXISTENCE.md). Exactly one "begin"
// line is logged on construction and exactly one "end" line is logged on
// destruction (plus one "SLOW HANDLER" warning line if the handler's
// synchronous work exceeded RENZFI_SLOW_HANDLER_WARN_MS). Never logs from a
// file-chunk or other high-frequency callback.
class RequestTimer {
 public:
  RequestTimer(AsyncWebServerRequest *req, const char *handler);
  ~RequestTimer();

  RequestTimer(const RequestTimer &) = delete;
  RequestTimer &operator=(const RequestTimer &) = delete;

  // Log the "end" line while the request is still valid, then detach so the
  // destructor does not touch req after req->send().
  void finish();

 private:
  AsyncWebServerRequest *_req;
  const char            *_handler;
  uint32_t               _startMs;
};

}  // namespace WebRequestDiagnostics
