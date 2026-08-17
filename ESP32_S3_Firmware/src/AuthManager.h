#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#include "AuthRole.h"
#include "Logger.h"
#include "StorageManager.h"

class AuthManager {
 public:
  void begin(StorageManager *storage, Logger *logger);

  // Returns true and populates response/setCookie on success.
  bool login(const String &username, const String &password, bool rememberIp,
             JsonDocument &response, String &setCookie);

  // Backward-compatible password-only login (owner).
  bool login(const String &password, bool rememberIp, JsonDocument &response,
             String &setCookie);

  // Invalidates the session identified by the raw Cookie header value.
  void logout(const String &cookieHeader);

  bool changePassword(const String &oldPassword, const String &newPassword);
  // First-run setup: set owner password without requiring the default admin password.
  bool provisionOwnerCredentials(const String &password);
  bool provisionOperatorCredentials(const String &username, const String &password,
                                    String &errorCodeOut,
                                    bool invalidateSessions = true);
  bool setOperatorPermissions(const String &csvPermissions,
                              String &errorCodeOut);
  void fillOperatorPermissions(JsonArray out) const;
  String operatorPermissionsCsv() const { return _operatorPermissionsCsv; }
  void setOwnerUsername(const String &username);
  void resetToDefault(bool invalidateSessions = true);

  bool hasOperatorNvsCredentials() const;

  // Checks whether the raw Cookie header value contains a valid session token.
  bool isAuthenticated(const String &cookieHeader);
  AuthRole sessionRole(const String &cookieHeader) const;
  bool isAuthenticatedWithRole(const String &cookieHeader, AuthRole required) const;

  // True when at least one non-expired admin session is active in memory.
  bool hasActiveSessions() const;

  bool   mustChangePassword() const;
  bool   firstBootCompleted() const;
  String ownerUsername() const { return _ownerUsername; }
  String operatorUsername() const { return _operatorUsername; }
  String cookieHeader(const String &token) const;
  void   cleanupExpired();

 private:
  struct ActiveSession {
    String   token;
    uint32_t expiresAt = 0;
    AuthRole role       = AuthRole::None;
  };

  static constexpr size_t kMaxActiveSessions = 4;

  StorageManager *_storage = nullptr;
  Logger         *_logger  = nullptr;
  Preferences     _prefs;

  String _passwordHash;
  String _operatorPasswordHash;
  String _ownerUsername;
  String _operatorUsername;
  String _operatorPermissionsCsv;
  bool   _mustChangePassword = true;
  bool   _firstBootCompleted = false;
  ActiveSession _activeSessions[kMaxActiveSessions]{};

  String hashPassword(const String &password) const;
  String makeToken() const;
  // Extracts the session token value from a raw Cookie header string.
  String extractToken(const String &cookieHeader) const;
  void   loadCredentials();
  void   saveCredentials();
  bool   rememberInMemory(const String &token, uint32_t expiresAt, AuthRole role);
  bool   findInMemory(const String &token, uint32_t now, AuthRole *roleOut = nullptr) const;
  void   forgetInMemory(const String &token);
  void   purgeExpiredMemory(uint32_t now);
  bool   saveSession(const String &token, AuthRole role);
  void   deleteSession(const String &token);
  void   invalidateAllSessions();
  void   loadOperatorFromStorage();
  bool   writeOperatorNvs(const String &username, const String &passwordHash,
                          String &errorCodeOut);
  void   clearOperatorNvs();
};
