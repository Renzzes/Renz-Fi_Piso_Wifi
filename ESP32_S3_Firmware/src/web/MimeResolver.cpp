#include "MimeResolver.h"

namespace {

String lowerExt(const String &path) {
  const int dot = path.lastIndexOf('.');
  if (dot < 0) return String("");
  String ext = path.substring(dot);
  ext.toLowerCase();
  return ext;
}

}  // namespace

String MimeResolver::fromExtension(const String &ext) {
  if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
  if (ext == ".css") return "text/css; charset=utf-8";
  if (ext == ".js") return "application/javascript; charset=utf-8";
  if (ext == ".json") return "application/json; charset=utf-8";
  if (ext == ".webmanifest") return "application/manifest+json; charset=utf-8";
  if (ext == ".png") return "image/png";
  if (ext == ".webp") return "image/webp";
  if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
  if (ext == ".svg") return "image/svg+xml";
  if (ext == ".mp3") return "audio/mpeg";
  if (ext == ".mp4") return "video/mp4";
  if (ext == ".ico") return "image/x-icon";
  if (ext == ".woff") return "font/woff";
  if (ext == ".woff2") return "font/woff2";
  if (ext == ".ttf") return "font/ttf";
  if (ext == ".txt") return "text/plain; charset=utf-8";
  if (ext == ".bin") return "application/octet-stream";
  if (ext == ".zip") return "application/zip";
  if (ext == ".pdf") return "application/pdf";
  if (ext == ".gz") return "application/gzip";
  return "application/octet-stream";
}

String MimeResolver::fromPath(const String &path) {
  String normalized = path;
  if (normalized.endsWith(".gz")) {
    normalized = normalized.substring(0, normalized.length() - 3);
  }
  return fromExtension(lowerExt(normalized));
}
