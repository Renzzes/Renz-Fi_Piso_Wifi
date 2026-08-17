#include "InstallationSession.h"

#include <esp_random.h>

String generateInstallationSessionId(const String &deviceId) {
  const uint32_t stamp = millis();
  const uint32_t rand  = esp_random();
  char buf[48];
  if (deviceId.length() > 0) {
    snprintf(buf, sizeof(buf), "ins-%s-%08lx-%04x", deviceId.c_str(),
             static_cast<unsigned long>(stamp), static_cast<unsigned>(rand & 0xFFFFU));
  } else {
    snprintf(buf, sizeof(buf), "ins-%08lx-%04x",
             static_cast<unsigned long>(stamp), static_cast<unsigned>(rand & 0xFFFFU));
  }
  return String(buf);
}

String generateInstallationResumeToken(const String &sessionId) {
  if (sessionId.isEmpty()) return "";
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < sessionId.length(); ++i) {
    hash ^= static_cast<uint8_t>(sessionId[i]);
    hash *= 16777619u;
  }
  char buf[16];
  snprintf(buf, sizeof(buf), "%08lx", static_cast<unsigned long>(hash));
  return String(buf);
}
