#include "AuthManager.h"

#include <mbedtls/sha256.h>

#include "Config.h"

void AuthManager::begin(StorageManager *storage, Logger *logger) {
  _storage = storage;
  _logger = logger;
  _prefs.begin("renz-auth", false);
  loadCredentials();
}

bool AuthManager::login(const String &password, bool rememberIp, JsonDocument &response, String &setCookie) {
  if (hashPassword(password) != _passwordHash) {
    if (_logger) _logger->warn("auth", "Failed admin login");
    return false;
  }

  String token = makeToken();
  saveSession(token, rememberIp);
  response["authenticated"] = true;
  response["username"] = "admin";
  response["rememberIp"] = rememberIp;
  response["mustChangePassword"] = _mustChangePassword;
  setCookie = cookieHeader(token, rememberIp ? RenzFiConfig::SESSION_TTL_SECONDS : 0);

  if (_logger) _logger->info("auth", "Admin login successful");
  return true;
}

void AuthManager::logout(AsyncWebServerRequest *request) {
  deleteSession(cookieToken(request));
}

bool AuthManager::changePassword(const String &oldPassword, const String &newPassword) {
  if (newPassword.length() < 8) return false;
  if (hashPassword(oldPassword) != _passwordHash) return false;
  _passwordHash = hashPassword(newPassword);
  _mustChangePassword = false;
  saveCredentials();
  if (_logger) _logger->info("auth", "Admin password changed");
  return true;
}

void AuthManager::resetToDefault() {
  _passwordHash = hashPassword(RenzFiConfig::DEFAULT_ADMIN_PASSWORD);
  _mustChangePassword = true;
  saveCredentials();
  if (_storage) _storage->clearJsonArray(RenzFiConfig::ADMIN_SESSIONS_FILE);
  if (_logger) _logger->warn("auth", "Admin password reset to default");
}

bool AuthManager::isAuthenticated(AsyncWebServerRequest *request) {
  String token = cookieToken(request);
  if (token.isEmpty()) return false;

  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  if (!_storage || !_storage->readJson(RenzFiConfig::ADMIN_SESSIONS_FILE, doc)) return false;
  uint32_t now = millis() / 1000;
  for (JsonObject session : doc.as<JsonArray>()) {
    if (token.equals(session["token"] | "") && (session["expiresAt"] | 0UL) > now) return true;
  }
  return false;
}

bool AuthManager::mustChangePassword() const {
  return _mustChangePassword;
}

String AuthManager::cookieHeader(const String &token, uint32_t maxAgeSeconds) const {
  String header = String(RenzFiConfig::SESSION_COOKIE) + "=" + token + "; Path=/; HttpOnly; SameSite=Lax";
  if (maxAgeSeconds > 0) header += "; Max-Age=" + String(maxAgeSeconds);
  return header;
}

void AuthManager::cleanupExpired() {
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  if (!_storage || !_storage->readJson(RenzFiConfig::ADMIN_SESSIONS_FILE, doc)) return;

  DynamicJsonDocument next(RenzFiConfig::JSON_DOC_MEDIUM);
  JsonArray out = next.to<JsonArray>();
  uint32_t now = millis() / 1000;
  for (JsonObject session : doc.as<JsonArray>()) {
    if ((session["expiresAt"] | 0UL) > now) {
      JsonObject copy = out.createNestedObject();
      copy.set(session);
    }
  }
  _storage->writeJson(RenzFiConfig::ADMIN_SESSIONS_FILE, next);
}

String AuthManager::hashPassword(const String &password) const {
  uint8_t hash[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, reinterpret_cast<const unsigned char *>(password.c_str()), password.length());
  mbedtls_sha256_finish(&ctx, hash);
  mbedtls_sha256_free(&ctx);

  char out[65];
  for (int i = 0; i < 32; i++) sprintf(out + (i * 2), "%02x", hash[i]);
  out[64] = 0;
  return String(out);
}

String AuthManager::makeToken() const {
  String seed = String(esp_random(), HEX) + String(millis(), HEX) + String(ESP.getEfuseMac(), HEX);
  return hashPassword(seed);
}

String AuthManager::cookieToken(AsyncWebServerRequest *request) const {
  if (!request->hasHeader("Cookie")) return "";
  String cookie = request->header("Cookie");
  String name = String(RenzFiConfig::SESSION_COOKIE) + "=";
  int start = cookie.indexOf(name);
  if (start < 0) return "";
  start += name.length();
  int end = cookie.indexOf(';', start);
  if (end < 0) end = cookie.length();
  return cookie.substring(start, end);
}

void AuthManager::loadCredentials() {
  _passwordHash = _prefs.getString("passwordHash", "");
  _mustChangePassword = _prefs.getBool("mustChange", true);
  if (_passwordHash.isEmpty()) {
    _passwordHash = hashPassword(RenzFiConfig::DEFAULT_ADMIN_PASSWORD);
    _mustChangePassword = true;
    saveCredentials();
  }
}

void AuthManager::saveCredentials() {
  _prefs.putString("passwordHash", _passwordHash);
  _prefs.putBool("mustChange", _mustChangePassword);
}

void AuthManager::saveSession(const String &token, bool rememberIp) {
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  if (!_storage->readJson(RenzFiConfig::ADMIN_SESSIONS_FILE, doc) || !doc.is<JsonArray>()) {
    doc.clear();
    doc.to<JsonArray>();
  }
  JsonObject item = doc.as<JsonArray>().createNestedObject();
  item["token"] = token;
  item["createdAt"] = millis() / 1000;
  item["expiresAt"] = (millis() / 1000) + RenzFiConfig::SESSION_TTL_SECONDS;
  item["rememberIp"] = rememberIp;
  _storage->writeJson(RenzFiConfig::ADMIN_SESSIONS_FILE, doc);
}

void AuthManager::deleteSession(const String &token) {
  if (token.isEmpty() || !_storage) return;
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_MEDIUM);
  if (!_storage->readJson(RenzFiConfig::ADMIN_SESSIONS_FILE, doc)) return;
  DynamicJsonDocument next(RenzFiConfig::JSON_DOC_MEDIUM);
  JsonArray out = next.to<JsonArray>();
  for (JsonObject session : doc.as<JsonArray>()) {
    if (!token.equals(session["token"] | "")) {
      JsonObject copy = out.createNestedObject();
      copy.set(session);
    }
  }
  _storage->writeJson(RenzFiConfig::ADMIN_SESSIONS_FILE, next);
}
