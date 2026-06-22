#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#include "Logger.h"
#include "StorageManager.h"

class AuthManager {
 public:
  void begin(StorageManager *storage, Logger *logger);

  // Returns true and populates response/setCookie on success.
  bool login(const String &password, bool rememberIp,
             JsonDocument &response, String &setCookie);

  // Invalidates the session identified by the raw Cookie header value.
  void logout(const String &cookieHeader);

  bool changePassword(const String &oldPassword, const String &newPassword);
  void resetToDefault();

  // Checks whether the raw Cookie header value contains a valid session token.
  bool isAuthenticated(const String &cookieHeader);

  bool   mustChangePassword() const;
  String cookieHeader(const String &token) const;
  void   cleanupExpired();

 private:
  struct ActiveSession {
    String   token;
    uint32_t expiresAt = 0;
  };

  static constexpr size_t kMaxActiveSessions = 4;

  StorageManager *_storage = nullptr;
  Logger         *_logger  = nullptr;
  Preferences     _prefs;

  String _passwordHash;
  bool   _mustChangePassword = true;
  ActiveSession _activeSessions[kMaxActiveSessions]{};

  String hashPassword(const String &password) const;
  String makeToken() const;
  // Extracts the session token value from a raw Cookie header string.
  String extractToken(const String &cookieHeader) const;
  void   loadCredentials();
  void   saveCredentials();
  bool   rememberInMemory(const String &token, uint32_t expiresAt);
  bool   findInMemory(const String &token, uint32_t now) const;
  void   forgetInMemory(const String &token);
  void   purgeExpiredMemory(uint32_t now);
  bool   saveSession(const String &token);
  void   deleteSession(const String &token);
  void   invalidateAllSessions();
};
