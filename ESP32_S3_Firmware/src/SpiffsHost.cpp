#include "SpiffsHost.h"

#include <SPIFFS.h>

namespace {

String normalizeSpiffsPath(String path) {
  if (path.isEmpty()) return "/";
  if (!path.startsWith("/")) path = "/" + path;
  return path;
}

String stripQueryString(String path) {
  const int query = path.indexOf('?');
  if (query >= 0) path = path.substring(0, query);
  return path;
}

bool existsOnSpiffs(const String &path) {
  const String normalized = normalizeSpiffsPath(path);
  return SPIFFS.exists(normalized.c_str());
}

bool hasFileExtension(const String &path) {
  const int slash = path.lastIndexOf('/');
  const int dot = path.lastIndexOf('.');
  return dot > slash && dot > 0;
}

void listSpiffsDirectory(const char *dirPath) {
  const String normalizedDir = normalizeSpiffsPath(String(dirPath));
  File dir = SPIFFS.open(normalizedDir);
  if (!dir) {
    Serial.printf("[boot]   %s: (missing)\n", normalizedDir.c_str());
    return;
  }
  if (!dir.isDirectory()) {
    Serial.printf("[boot]   %s (%u bytes)\n", normalizedDir.c_str(), dir.size());
    dir.close();
    return;
  }

  File entry = dir.openNextFile();
  while (entry) {
    String name = normalizeSpiffsPath(String(entry.name()));
    Serial.printf("[boot]   %s (%u bytes)\n", name.c_str(), entry.size());
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();
}

bool hasFilesUnderAssetsPrefix() {
  // Open "/assets" as a SPIFFS directory prefix and check for at least one
  // file entry.  This avoids relying on entry.name() returning a full path
  // (which varies between ESP32 SDK versions) when iterating from root.
  File dir = SPIFFS.open("/assets");
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }
  File entry = dir.openNextFile();
  bool found = (entry && !entry.isDirectory());
  if (entry) entry.close();
  dir.close();
  return found;
}

}  // namespace

void logSpiffsInventory() {
  Serial.println("[boot] SPIFFS inventory:");

  File root = SPIFFS.open("/");
  if (root && root.isDirectory()) {
    File entry = root.openNextFile();
    while (entry) {
      String name = normalizeSpiffsPath(String(entry.name()));
      Serial.printf("[boot]   %s%s (%u bytes)\n",
                    entry.isDirectory() ? "[dir] " : "",
                    name.c_str(),
                    entry.size());
      entry.close();
      entry = root.openNextFile();
    }
    root.close();
  } else {
    if (root) root.close();
    Serial.println("[boot]   (unable to list SPIFFS root)");
  }

  // Detect assets using a direct directory open — more reliable than checking
  // entry.name() prefixes (SPIFFS name() behaviour varies by SDK version).
  bool hasAssetsPrefix = hasFilesUnderAssetsPrefix();

  Serial.println("[boot] SPIFFS /assets listing:");
  listSpiffsDirectory("/assets");

  Serial.printf("[boot] /index.html exists       : %s\n", SPIFFS.exists("/index.html") ? "yes" : "no");
  Serial.printf("[boot] /assets/* present        : %s\n", hasAssetsPrefix ? "yes" : "no");
  Serial.printf("[boot] /manifest.webmanifest    : %s\n", SPIFFS.exists("/manifest.webmanifest") ? "yes" : "no");
  Serial.printf("[boot] /sw.js                   : %s\n", SPIFFS.exists("/sw.js") ? "yes" : "no");
}

String resolveSpiffsServePath(const String &requestPath, bool *gzipOut) {
  if (gzipOut) *gzipOut = false;

  String path = stripQueryString(requestPath);
  if (path.startsWith("/api/")) return "";

  if (path.isEmpty()) path = "/";

  // ── Customer portal root paths → serve captive portal ──────────────
  // "/" and "/portal" always serve the customer portal, not the admin SPA.
  if (path == "/" || path == "/portal" || path == "/portal/") {
    if (existsOnSpiffs("/portal/index.html")) return "/portal/index.html";
    if (existsOnSpiffs("/portal/index.html.gz")) {
      if (gzipOut) *gzipOut = true;
      return "/portal/index.html.gz";
    }
    // Portal not uploaded yet — fall through to admin SPA as fallback
  }

  if (path.endsWith("/")) path += "index.html";

  if (existsOnSpiffs(path)) return normalizeSpiffsPath(path);

  String gzPath = path + ".gz";
  if (existsOnSpiffs(gzPath)) {
    if (gzipOut) *gzipOut = true;
    return normalizeSpiffsPath(gzPath);
  }

  if (path.startsWith("/assets/")) {
    return "";  // serveStatic() handles /assets/*; nothing to resolve here
  }

  // /portal/* assets that are missing from SPIFFS get a 404, not the admin SPA.
  if (path.startsWith("/portal/")) {
    return "";
  }

  if (path == "/manifest.webmanifest" || path == "/sw.js" || path == "/favicon.svg" ||
      path == "/favicon.ico") {
    return "";
  }

  // Admin SPA and any other extensionless path → serve React index.html
  if (!hasFileExtension(path) || path == "/admin" || path.startsWith("/admin/")) {
    if (existsOnSpiffs("/index.html")) return "/index.html";
    if (existsOnSpiffs("/index.html.gz")) {
      if (gzipOut) *gzipOut = true;
      return "/index.html.gz";
    }
    return "";
  }

  return "";
}
