#pragma once

#include <ESPAsyncWebServer.h>

enum class CachePolicy {
  NoCache,
  ShortCache,
  LongCache,
  Immutable,
};

class CacheManager {
 public:
  static const char *header(CachePolicy policy);
  static void apply(AsyncWebServerResponse *res, CachePolicy policy);
};
