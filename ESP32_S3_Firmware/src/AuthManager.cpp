#include "AuthManager.h"

#include <mbedtls/sha256.h>

#include "Config.h"

void AuthManager::begin(StorageManager *storage, Logger *logger) {
  _storage = storage;
  _logger  = logger;
  _prefs.begin("renz-auth", false);
  loadCredentials();
  invalidateAllSessions();
}

bool AuthManager::login(const String &password, bool rememberIp,
                        JsonDocument &response, String &setCookie) {
  (void)rememberIp;

  if (hashPassword(password) != _passwordHash) {
    if (_logger) _logger->warn("auth", "Failed admin login");
    return false;
  }

  String token = makeToken();
  if (!saveSession(token)) {
    if (_logger) _logger->error("auth", "Failed to create admin session");
    return false;
  }
  response["authenticated"]      = true;
  response["username"]           = "admin";
  response["rememberIp"]         = false;
  response["mustChangePassword"] = _mustChangePassword;
  setCookie                      = cookieHeader(token);

  if (_logger) _logger->info("auth", "Admin login successful");
  return true;
}

void AuthManager::logout(const String &cookieHeader) {
  deleteSession(extractToken(cookieHeader));
}

bool AuthManager::changePassword(const String &oldPassword, const String &newPassword) {
  if (newPassword.length() < 8) return false;
  if (hashPassword(oldPassword) != _passwordHash) return false;
  _passwordHash       = hashPassword(newPassword);
  _mustChangePassword = false;
  saveCredentials();
  if (_logger) _logger->info("auth", "Admin password changed");
  return true;
}

void AuthManager::resetToDefault() {
  _passwordHash       = hashPassword(RenzFiConfig::DEFAULT_ADMIN_PASSWORD);
  _mustChangePassword = true;
  saveCredentials();
  invalidateAllSessions();
  if (_logger) _logger->warn("auth", "Admin password reset to default");
}

bool AuthManager::isAuthenticated(const String &cookieHeader) {
  String token = extractToken(cookieHeader);
  if (token.isEmpty()) return false;

  const uint32_t now = millis() / 1000;
  return findInMemory(token, now);
}

bool AuthManager::mustChangePassword() const {
  return _mustChangePassword;
}

String AuthManager::cookieHeader(const String &token) const {
  return String(RenzFiConfig::SESSION_COOKIE) + "=" + token +
         "; Path=/; HttpOnly; SameSite=Lax";
}

void AuthManager::cleanupExpired() {
  purgeExpiredMemory(millis() / 1000);
}

void AuthManager::invalidateAllSessions() {
  for (size_t i = 0; i < kMaxActiveSessions; i++) _activeSessions[i] = {};
  if (_storage) _storage->clearJsonArray(RenzFiConfig::ADMIN_SESSIONS_FILE);
}

// ── Private helpers ──────────────────────────────────────────────────────────

String AuthManager::hashPassword(const String &password) const {
  uint8_t                hash[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx,
                        reinterpret_cast<const unsigned char *>(password.c_str()),
                        password.length());
  mbedtls_sha256_finish(&ctx, hash);
  mbedtls_sha256_free(&ctx);

  char out[65];
  for (int i = 0; i < 32; i++) sprintf(out + (i * 2), "%02x", hash[i]);
  out[64] = 0;
  return String(out);
}

String AuthManager::makeToken() const {
  String seed = String(esp_random(), HEX) + String(millis(), HEX) +
                String(ESP.getEfuseMac(), HEX);
  return hashPassword(seed);
}

String AuthManager::extractToken(const String &cookieHeader) const {
  String name  = String(RenzFiConfig::SESSION_COOKIE) + "=";
  int    start = cookieHeader.indexOf(name);
  if (start < 0) return "";
  start += name.length();
  int end = cookieHeader.indexOf(';', start);
  if (end < 0) end = cookieHeader.length();
  return cookieHeader.substring(start, end);
}

void AuthManager::loadCredentials() {
  _passwordHash       = _prefs.getString("passwordHash", "");
  _mustChangePassword = _prefs.getBool("mustChange", true);
  if (_passwordHash.isEmpty()) {
    _passwordHash       = hashPassword(RenzFiConfig::DEFAULT_ADMIN_PASSWORD);
    _mustChangePassword = true;
    saveCredentials();
  }
}

void AuthManager::saveCredentials() {
  _prefs.putString("passwordHash", _passwordHash);
  _prefs.putBool("mustChange", _mustChangePassword);
}

bool AuthManager::rememberInMemory(const String &token, uint32_t expiresAt) {
  purgeExpiredMemory(millis() / 1000);

  for (size_t i = 0; i < kMaxActiveSessions; i++) {
    if (_activeSessions[i].token == token) {
      _activeSessions[i].expiresAt = expiresAt;
      return true;
    }
  }

  for (size_t i = 0; i < kMaxActiveSessions; i++) {
    if (_activeSessions[i].token.isEmpty()) {
      _activeSessions[i].token     = token;
      _activeSessions[i].expiresAt = expiresAt;
      return true;
    }
  }

  _activeSessions[0] = {token, expiresAt};
  return true;
}

bool AuthManager::findInMemory(const String &token, uint32_t now) const {
  for (size_t i = 0; i < kMaxActiveSessions; i++) {
    if (_activeSessions[i].token == token &&
        _activeSessions[i].expiresAt > now) {
      return true;
    }
  }
  return false;
}

void AuthManager::forgetInMemory(const String &token) {
  for (size_t i = 0; i < kMaxActiveSessions; i++) {
    if (_activeSessions[i].token == token) _activeSessions[i] = {};
  }
}

void AuthManager::purgeExpiredMemory(uint32_t now) {
  for (size_t i = 0; i < kMaxActiveSessions; i++) {
    if (!_activeSessions[i].token.isEmpty() &&
        _activeSessions[i].expiresAt <= now) {
      _activeSessions[i] = {};
    }
  }
}

bool AuthManager::saveSession(const String &token) {
  const uint32_t expiresAt = (millis() / 1000) + RenzFiConfig::SESSION_TTL_SECONDS;
  return rememberInMemory(token, expiresAt);
}

void AuthManager::deleteSession(const String &token) {
  if (token.isEmpty()) return;
  forgetInMemory(token);
}
