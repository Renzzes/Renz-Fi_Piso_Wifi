#include "ErrorHandler.h"

#include "CacheManager.h"
#include "WebResponse.h"

namespace {

const char *messageForStatus(int status) {
  switch (status) {
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 500: return "Internal Server Error";
    default:  return "Error";
  }
}

String buildPage(int status) {
  const char *title = ErrorHandler::titleForStatus(status);
  const char *message = messageForStatus(status);
  String html = "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
  html += "<title>";
  html += title;
  html += "</title></head><body><h1>";
  html += String(status);
  html += " ";
  html += message;
  html += "</h1></body></html>";
  return html;
}

}  // namespace

const char *ErrorHandler::titleForStatus(int status) {
  return messageForStatus(status);
}

void ErrorHandler::serve(AsyncWebServerRequest *req, int status) {
  if (!req) return;
  if (!WebResponse::ensureEthTransmitHeadroom(req, "error-page")) return;
  AsyncWebServerResponse *res =
      req->beginResponse(status, "text/html; charset=utf-8", buildPage(status));
  WebResponse::addCorsHeaders(res);
  CacheManager::apply(res, CachePolicy::NoCache);
  req->send(res);
}
