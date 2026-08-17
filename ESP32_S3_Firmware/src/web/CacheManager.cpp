#include "CacheManager.h"

const char *CacheManager::header(CachePolicy policy) {
  switch (policy) {
    case CachePolicy::NoCache:
      return "no-store";
    case CachePolicy::ShortCache:
      return "max-age=86400";
    case CachePolicy::LongCache:
      return "public, max-age=31536000";
    case CachePolicy::Immutable:
      return "public, max-age=31536000, immutable";
  }
  return "no-store";
}

void CacheManager::apply(AsyncWebServerResponse *res, CachePolicy policy) {
  if (res) res->addHeader("Cache-Control", header(policy));
}
