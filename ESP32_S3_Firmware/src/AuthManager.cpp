#include "AuthManager.h"

#include "AuthCredentials.h"
#include "Config.h"
#include "StoragePaths.h"

void AuthManager::begin(StorageManager *storage, Logger *logger) {
  _storage = storage;
  _logger  = logger;
  _prefs.begin(RenzFiConfig::NVS_AUTH_NS, false);
  loadCredentials();
  loadOperatorFromStorage();
  invalidateAllSessions();
}

bool AuthManager::login(const String &username, const String &password, bool rememberIp,
                        JsonDocument &response, String &setCookie) {
  (void)rememberIp;

  const String trimmedUser = username;
  String user = trimmedUser;
  user.trim();
  const String ownerHash = hashPassword(password);
  AuthRole role          = AuthRole::None;

  if (user.isEmpty() || user == _ownerUsername || _ownerUsername.isEmpty()) {
    if (ownerHash == _passwordHash) {
      role = AuthRole::Owner;
    }
  }

  if (role == AuthRole::None && !_operatorPasswordHash.isEmpty()) {
    if ((user.isEmpty() || user == _operatorUsername) &&
        ownerHash == _operatorPasswordHash) {
      role = AuthRole::Operator;
    }
  }

  if (role == AuthRole::None) {
    // Local: same TWDT class as login-success tip (durable appendHistory on
    // async_tcp). Failed attempts must not flush SD history.
    if (_logger) _logger->warnLocal("auth", "Failed login");
    return false;
  }

  String token = makeToken();
  if (!saveSession(token, role)) {
    if (_logger) _logger->errorLocal("auth", "Failed to create admin session");
    return false;
  }

  response["authenticated"]      = true;
  response["rememberIp"]         = false;
  response["mustChangePassword"] = role == AuthRole::Owner ? _mustChangePassword : false;
  response["firstBootCompleted"] = _firstBootCompleted;
  response["role"]               = authRoleLabel(role);
  if (role == AuthRole::Owner && !_ownerUsername.isEmpty()) {
    response["username"] = _ownerUsername;
  } else if (role == AuthRole::Operator) {
    response["username"] = _operatorUsername;
    JsonArray perms = response["permissions"].to<JsonArray>();
    fillOperatorPermissions(perms);
  }
  setCookie = cookieHeader(token);

  // infoLocal: Serial/RAM/SSE only. Durable Logger::info → appendHistory +
  // flush on async_tcp is the same TWDT tip class as owner provision
  // (TWDT_OWNER_ENDPOINT_ROOT_CAUSE.md). Login must return quickly.
  if (_logger) _logger->infoLocal("auth", "Login successful");
  return true;
}

bool AuthManager::login(const String &password, bool rememberIp, JsonDocument &response,
                        String &setCookie) {
  return login(String(""), password, rememberIp, response, setCookie);
}

void AuthManager::logout(const String &cookieHeader) {
  deleteSession(extractToken(cookieHeader));
}

bool AuthManager::changePassword(const String &oldPassword, const String &newPassword) {
  if (newPassword.length() < 8) return false;
  if (AuthCredentials::hashPassword(oldPassword) != _passwordHash) return false;
  _passwordHash        = AuthCredentials::hashPassword(newPassword);
  _mustChangePassword  = false;
  _firstBootCompleted  = true;
  saveCredentials();
  // NVS-only credentials; do not tip TWDT with durable log history flush.
  if (_logger) _logger->infoLocal("auth", "Admin password changed");
  return true;
}

bool AuthManager::provisionOwnerCredentials(const String &password) {
  if (password.length() < 8) return false;
  _passwordHash       = AuthCredentials::hashPassword(password);
  _mustChangePassword = false;
  _firstBootCompleted = true;
  saveCredentials();
  invalidateAllSessions();
  // infoLocal: Serial/RAM/SSE only. Durable appendHistory flush on async_tcp
  // was the proven TWDT tip after this exact log line (see
  // TWDT_OWNER_ENDPOINT_ROOT_CAUSE.md).
  if (_logger) _logger->infoLocal("auth", "Owner credentials provisioned");
  return true;
}

bool AuthManager::provisionOperatorCredentials(const String &username,
                                               const String &password,
                                               String &errorCodeOut,
                                               bool invalidateSessions) {
  errorCodeOut = "";
  if (password.length() < 8) {
    errorCodeOut = "PASSWORD_TOO_SHORT";
    return false;
  }

  const String hash = AuthCredentials::hashPassword(password);
  if (!writeOperatorNvs(username, hash, errorCodeOut)) {
    return false;
  }

  _operatorUsername     = username;
  _operatorPasswordHash = hash;
  if (_operatorPermissionsCsv.isEmpty()) {
    _operatorPermissionsCsv =
        "dashboard,promo-rates,captive-portal,coin-settings";
    _prefs.putString(RenzFiConfig::NVS_KEY_OP_PERMS, _operatorPermissionsCsv);
  }
  if (invalidateSessions) {
    invalidateAllSessions();
  }
  // Called from Admin/Setup HTTP; NVS already durable — no SD history flush.
  if (_logger) {
    _logger->infoLocal("auth",
                       "operator persistence ok usernameLen=" +
                           String(username.length()) + " hashLen=64");
  }
  return true;
}

void AuthManager::setOwnerUsername(const String &username) {
  _ownerUsername = username;
  _prefs.putString("ownerUsername", _ownerUsername);
}

void AuthManager::resetToDefault(bool invalidateSessions) {
  _passwordHash       = AuthCredentials::hashPassword(RenzFiConfig::DEFAULT_ADMIN_PASSWORD);
  _mustChangePassword = true;
  _firstBootCompleted = false;
  _operatorPasswordHash = "";
  _operatorUsername     = "";
  _operatorPermissionsCsv = "";
  clearOperatorNvs();
  saveCredentials();
  if (invalidateSessions) invalidateAllSessions();
  if (_logger) _logger->warn("auth", "Admin password reset to default");
}

bool AuthManager::isAuthenticated(const String &cookieHeader) {
  return isAuthenticatedWithRole(cookieHeader, AuthRole::Operator);
}

AuthRole AuthManager::sessionRole(const String &cookieHeader) const {
  String token = extractToken(cookieHeader);
  if (token.isEmpty()) return AuthRole::None;
  AuthRole role = AuthRole::None;
  const uint32_t now = millis() / 1000;
  findInMemory(token, now, &role);
  return role;
}

bool AuthManager::isAuthenticatedWithRole(const String &cookieHeader,
                                          AuthRole required) const {
  String token = extractToken(cookieHeader);
  if (token.isEmpty()) return false;
  AuthRole role = AuthRole::None;
  const uint32_t now = millis() / 1000;
  if (!findInMemory(token, now, &role)) return false;
  return authRoleAtLeast(role, required);
}

bool AuthManager::hasActiveSessions() const {
  const uint32_t now = millis() / 1000;
  for (size_t i = 0; i < kMaxActiveSessions; ++i) {
    if (!_activeSessions[i].token.isEmpty() &&
        _activeSessions[i].expiresAt > now) {
      return true;
    }
  }
  return false;
}

bool AuthManager::mustChangePassword() const { return _mustChangePassword; }

bool AuthManager::firstBootCompleted() const { return _firstBootCompleted; }

String AuthManager::cookieHeader(const String &token) const {
  return String(RenzFiConfig::SESSION_COOKIE) + "=" + token +
         "; Path=/; HttpOnly; SameSite=Lax";
}

void AuthManager::cleanupExpired() { purgeExpiredMemory(millis() / 1000); }

void AuthManager::invalidateAllSessions() {
  // Capture before clearing RAM — after the loop hasActiveSessions() is always
  // false and cannot tell us whether a durable rewrite is needed.
  const bool hadMemorySessions = hasActiveSessions();
  for (size_t i = 0; i < kMaxActiveSessions; i++) _activeSessions[i] = {};
  // Admin sessions are memory-authoritative (saveSession never reloads the
  // JSON array). On first-boot owner create the seeded file is already "[]"
  // and there are no live sessions — a transactional rewrite here burned the
  // async_tcp TWDT budget before the provisioned log (forensic D).
  if (!hadMemorySessions) return;
  if (_storage) _storage->clearJsonArray(RenzFiConfig::ADMIN_SESSIONS_FILE);
}

String AuthManager::hashPassword(const String &password) const {
  return AuthCredentials::hashPassword(password);
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
  _passwordHash = _prefs.getString("passwordHash", "");
  _mustChangePassword = _prefs.getBool("mustChange", true);
  _firstBootCompleted = _prefs.getBool("firstBootDone", !_mustChangePassword);
  _ownerUsername = _prefs.getString("ownerUsername", "");

  if (_passwordHash.isEmpty()) {
    _passwordHash       = AuthCredentials::hashPassword(RenzFiConfig::DEFAULT_ADMIN_PASSWORD);
    _mustChangePassword = true;
    _firstBootCompleted = false;
    saveCredentials();
  }
}

void AuthManager::saveCredentials() {
  _prefs.putString("passwordHash", _passwordHash);
  _prefs.putBool("mustChange", _mustChangePassword);
  _prefs.putBool("firstBootDone", _firstBootCompleted);
}

void AuthManager::loadOperatorFromStorage() {
  _operatorUsername =
      _prefs.getString(RenzFiConfig::NVS_KEY_OP_USER, "");
  _operatorPasswordHash =
      _prefs.getString(RenzFiConfig::NVS_KEY_OP_HASH, "");
  _operatorPermissionsCsv =
      _prefs.getString(RenzFiConfig::NVS_KEY_OP_PERMS, "");
  if (!_operatorUsername.isEmpty() && _operatorPermissionsCsv.isEmpty()) {
    _operatorPermissionsCsv =
        "dashboard,promo-rates,captive-portal,coin-settings";
  }
}

bool AuthManager::setOperatorPermissions(const String &csvPermissions,
                                         String &errorCodeOut) {
  errorCodeOut = "";
  if (!hasOperatorNvsCredentials()) {
    errorCodeOut = "OPERATOR_NOT_CONFIGURED";
    return false;
  }
  String cleaned;
  cleaned.reserve(csvPermissions.length());
  // Allow only known keys; keep CSV compact for NVS.
  const char *allowed[] = {
      "dashboard",       "promo-rates",     "vouchers",
      "active-users",    "sales-reports",   "captive-portal",
      "coin-settings",   "system-configuration", "logs",
      "firmware",
  };
  int start = 0;
  while (start <= static_cast<int>(csvPermissions.length())) {
    int comma = csvPermissions.indexOf(',', start);
    if (comma < 0) comma = csvPermissions.length();
    String token = csvPermissions.substring(start, comma);
    token.trim();
    token.toLowerCase();
    bool ok = false;
    for (const char *key : allowed) {
      if (token == key) {
        ok = true;
        break;
      }
    }
    if (ok) {
      if (!cleaned.isEmpty()) cleaned += ',';
      cleaned += token;
    }
    start = comma + 1;
  }
  if (cleaned.isEmpty()) {
    cleaned = "dashboard,promo-rates,captive-portal,coin-settings";
  }
  if (!_prefs.putString(RenzFiConfig::NVS_KEY_OP_PERMS, cleaned)) {
    errorCodeOut = "OPERATOR_PERSISTENCE_FAILED";
    return false;
  }
  _operatorPermissionsCsv = cleaned;
  return true;
}

void AuthManager::fillOperatorPermissions(JsonArray out) const {
  if (!_operatorPermissionsCsv.isEmpty()) {
    int start = 0;
    const String &csv = _operatorPermissionsCsv;
    while (start <= static_cast<int>(csv.length())) {
      int comma = csv.indexOf(',', start);
      if (comma < 0) comma = csv.length();
      String token = csv.substring(start, comma);
      token.trim();
      if (!token.isEmpty()) out.add(token);
      start = comma + 1;
    }
    return;
  }
  out.add("dashboard");
  out.add("promo-rates");
  out.add("captive-portal");
  out.add("coin-settings");
}

bool AuthManager::writeOperatorNvs(const String &username,
                                   const String &passwordHash,
                                   String &errorCodeOut) {
  errorCodeOut = "";
  if (username.isEmpty() || passwordHash.isEmpty()) {
    errorCodeOut = "OPERATOR_PERSISTENCE_FAILED";
    return false;
  }

  if (!_prefs.putString(RenzFiConfig::NVS_KEY_OP_USER, username)) {
    errorCodeOut = "OPERATOR_PERSISTENCE_FAILED";
    return false;
  }
  if (!_prefs.putString(RenzFiConfig::NVS_KEY_OP_HASH, passwordHash)) {
    clearOperatorNvs();
    errorCodeOut = "OPERATOR_PERSISTENCE_FAILED";
    return false;
  }

  const String savedUser =
      _prefs.getString(RenzFiConfig::NVS_KEY_OP_USER, "");
  const String savedHash =
      _prefs.getString(RenzFiConfig::NVS_KEY_OP_HASH, "");
  if (savedUser != username || savedHash != passwordHash) {
    clearOperatorNvs();
    errorCodeOut = "OPERATOR_PERSISTENCE_MISMATCH";
    return false;
  }
  return true;
}

void AuthManager::clearOperatorNvs() {
  _prefs.remove(RenzFiConfig::NVS_KEY_OP_USER);
  _prefs.remove(RenzFiConfig::NVS_KEY_OP_HASH);
  _prefs.remove(RenzFiConfig::NVS_KEY_OP_PERMS);
}

bool AuthManager::hasOperatorNvsCredentials() const {
  return !_operatorUsername.isEmpty() && !_operatorPasswordHash.isEmpty();
}

bool AuthManager::rememberInMemory(const String &token, uint32_t expiresAt,
                                   AuthRole role) {
  purgeExpiredMemory(millis() / 1000);

  for (size_t i = 0; i < kMaxActiveSessions; i++) {
    if (_activeSessions[i].token == token) {
      _activeSessions[i].expiresAt = expiresAt;
      _activeSessions[i].role      = role;
      return true;
    }
  }

  for (size_t i = 0; i < kMaxActiveSessions; i++) {
    if (_activeSessions[i].token.isEmpty()) {
      _activeSessions[i].token     = token;
      _activeSessions[i].expiresAt = expiresAt;
      _activeSessions[i].role      = role;
      return true;
    }
  }

  _activeSessions[0] = {token, expiresAt, role};
  return true;
}

bool AuthManager::findInMemory(const String &token, uint32_t now,
                               AuthRole *roleOut) const {
  for (size_t i = 0; i < kMaxActiveSessions; i++) {
    if (_activeSessions[i].token == token &&
        _activeSessions[i].expiresAt > now) {
      if (roleOut) *roleOut = _activeSessions[i].role;
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

bool AuthManager::saveSession(const String &token, AuthRole role) {
  const uint32_t expiresAt = (millis() / 1000) + RenzFiConfig::SESSION_TTL_SECONDS;
  return rememberInMemory(token, expiresAt, role);
}

void AuthManager::deleteSession(const String &token) {
  if (token.isEmpty()) return;
  forgetInMemory(token);
}
