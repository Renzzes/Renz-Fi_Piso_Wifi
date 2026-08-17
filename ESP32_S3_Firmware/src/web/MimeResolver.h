#pragma once

#include <Arduino.h>

// Central MIME type detection for HTTP responses.
class MimeResolver {
 public:
  static String fromPath(const String &path);
  static String fromExtension(const String &ext);
};
