#include "PortalTemplate.h"

#include "W5500Config.h"

namespace {

String clientIp(AsyncWebServerRequest *req) {
  if (!req) return "";
  if (req->hasArg("ip")) return req->arg("ip");
  AsyncClient *client = req->client();
  return client ? client->remoteIP().toString() : "";
}

String clientMac(AsyncWebServerRequest *req) {
  if (!req) return "";
  if (req->hasArg("mac")) return req->arg("mac");
  return "";
}

String routerLoginUrl() {
  return String("http://") + W5500Config::GATEWAY.toString() + "/login";
}

String linkOrig(AsyncWebServerRequest *req) {
  if (req && req->hasArg("dst")) return req->arg("dst");
  return "http://detectportal.firefox.com/";
}

bool hasChap(AsyncWebServerRequest *req) {
  return req && req->hasArg("chap-id") && req->arg("chap-id").length() > 0;
}

String replaceAll(String haystack, const String &needle, const String &value) {
  if (needle.isEmpty()) return haystack;
  int idx = 0;
  while ((idx = haystack.indexOf(needle, idx)) >= 0) {
    haystack.replace(needle, value);
    idx += value.length();
  }
  return haystack;
}

String stripChapBlock(String html) {
  const char *open = "$(if chap-id)";
  const char *close = "$(endif)";
  int start = html.indexOf(open);
  while (start >= 0) {
    const int end = html.indexOf(close, start);
    if (end < 0) break;
    html.remove(start, end + strlen(close) - start);
    start = html.indexOf(open);
  }
  html.replace("$(if chap-id)", "");
  html.replace("$(endif)", "");
  return html;
}

}  // namespace

String PortalTemplate::process(const String &html, AsyncWebServerRequest *req) {
  String out = html;
  if (!hasChap(req)) {
    out = stripChapBlock(out);
  }

  out = replaceAll(out, "$(ip)", clientIp(req));
  out = replaceAll(out, "$(mac)", clientMac(req));
  out = replaceAll(out, "$(link-login-only)", routerLoginUrl());
  out = replaceAll(out, "$(link-orig)", linkOrig(req));

  if (req) {
    if (req->hasArg("chap-id")) {
      out = replaceAll(out, "$(chap-id)", req->arg("chap-id"));
    } else {
      out.replace("$(chap-id)", "");
    }
    if (req->hasArg("chap-challenge")) {
      out = replaceAll(out, "$(chap-challenge)", req->arg("chap-challenge"));
    } else {
      out.replace("$(chap-challenge)", "");
    }
  }

  out.replace("$(if chap-id)", "");
  out.replace("$(endif)", "");
  return out;
}
