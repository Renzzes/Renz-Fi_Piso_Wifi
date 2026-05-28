#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>

#include "Logger.h"
#include "StorageManager.h"

class AuthManager {
 public:
  void begin(StorageManager *storage, Logger *logger);
  bool login(const String &password, bool rememberIp, JsonDocument &response, String &setCookie);
  void logout(AsyncWebServerRequest *request);
  bool changePassword(const String &oldPassword, const String &newPassword);
  void resetToDefault();
  bool isAuthenticated(AsyncWebServerRequest *request);
  bool mustChangePassword() const;
  String cookieHeader(const String &token, uint32_t maxAgeSeconds) const;
  void cleanupExpired();

 private:
  StorageManager *_storage = nullptr;
  Logger *_logger = nullptr;
  Preferences _prefs;

  String _passwordHash;
  bool _mustChangePassword = true;

  String hashPassword(const String &password) const;
  String makeToken() const;
  String cookieToken(AsyncWebServerRequest *request) const;
  void loadCredentials();
  void saveCredentials();
  void saveSession(const String &token, bool rememberIp);
  void deleteSession(const String &token);
};
