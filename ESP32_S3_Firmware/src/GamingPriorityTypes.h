#pragma once

#include <Arduino.h>
#include <ctype.h>
#include <string.h>

namespace GamingPriority {

static constexpr uint8_t kSchemaVersion = 1;
static constexpr uint8_t kMaxProfiles = 8;
static constexpr uint16_t kMaxMbps = 1000;
static constexpr const char *kOwnerPrefix = "renzfi-gp-";
static constexpr const char *kConnMark = "renzfi-gp-conn-gaming";
static constexpr const char *kPktMark = "renzfi-gp-pkt-gaming";
static constexpr const char *kPcqDownload = "renzfi-gp-pcq-download";
static constexpr const char *kQtDownload = "renzfi-gp-qt-download";
static constexpr const char *kClassMethod = "dst-port";

struct GameProfile {
  char id[20];
  char name[40];
  char slug[40];
  char classData[72];
  char priority[8];
  bool enabled = false;
};

inline bool isValidPriorityLabel(const char *label) {
  return label && (strcmp(label, "highest") == 0 || strcmp(label, "high") == 0 ||
                   strcmp(label, "normal") == 0);
}

inline bool isValidSlug(const char *slug) {
  if (!slug) return false;
  const size_t n = strlen(slug);
  if (n < 2 || n > 48) return false;
  for (size_t i = 0; i < n; ++i) {
    const char c = slug[i];
    if (c != '-' && !islower(static_cast<unsigned char>(c)) &&
        !isdigit(static_cast<unsigned char>(c))) {
      return false;
    }
  }
  return true;
}

inline uint8_t routerOsQueuePriority(const char *label) {
  if (label && strcmp(label, "highest") == 0) return 1;
  if (label && strcmp(label, "high") == 0) return 2;
  return 4;
}

inline const char *applyStatusLabel(uint8_t code) {
  if (code == 1) return "pending_changes";
  if (code == 2) return "applied";
  if (code == 3) return "error";
  return "disabled";
}

inline void copyField(char *dst, size_t cap, const char *src) {
  if (!dst || cap == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
}

}  // namespace GamingPriority
