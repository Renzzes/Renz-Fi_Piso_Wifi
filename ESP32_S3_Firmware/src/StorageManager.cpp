#include "StorageManager.h"

#include <SPIFFS.h>
#include <math.h>

#include "Config.h"
#include "SdSpi.h"

namespace {

constexpr const char *kDefaultSettings =
    "{\"admin\":{\"username\":\"admin\",\"passwordHash\":\"\","
    "\"mustChangePassword\":true},\"coin\":{\"pesoPerPulse\":1,"
    "\"defaultMinutesPerPeso\":5,\"debounceMs\":35,\"settleMs\":450,"
    "\"enabled\":true},\"device\":{\"name\":\"Renz-Fi\","
    "\"timezone\":\"Asia/Manila\"}}";

constexpr const char *kDefaultPromos =
    "[{\"id\":1,\"name\":\"Peso WiFi 5 minutes\",\"coin\":1,\"minutes\":5,"
    "\"speed\":0,\"devices\":1,\"data_cap_mb\":0}]";

constexpr const char *kDefaultRouter =
    "{\"host\":\"10.40.0.1\",\"username\":\"\",\"password\":\"\","
    "\"profile\":\"default\",\"ssid\":\"RenzFi_PesoWifi\",\"wifiPassword\":\"\"}";

constexpr const char *kDefaultVouchers = "[]";
constexpr const char *kDefaultPortalSessions = "{\"sessions\":[]}";
constexpr const char *kDefaultSales = "[]";

}  // namespace

// ── Lifecycle ─────────────────────────────────────────────────────────────────

bool StorageManager::mountSdCard(const char *context) {
  Serial.println("[SD] SPI bus:");
  Serial.printf("MOSI=%d\n", RenzFiConfig::PIN_SD_MOSI);
  Serial.printf("MISO=%d\n", RenzFiConfig::PIN_SD_MISO);
  Serial.printf("SCK=%d\n", RenzFiConfig::PIN_SD_SCK);
  Serial.printf("CS=%d\n", RenzFiConfig::PIN_SD_CS);

  renzFiSdSpiBegin();
  delay(100);

  Serial.printf("[SD] %s: mounting SD (cs=%d freq=%lu Hz)\n", context,
                RenzFiConfig::PIN_SD_CS,
                (unsigned long)RenzFiConfig::SD_SPI_FREQ_HZ);

  const bool ok = SD.begin(RenzFiConfig::PIN_SD_CS, renzFiSdSpi(),
                           RenzFiConfig::SD_SPI_FREQ_HZ);
  Serial.printf("[SD] %s: SD.begin %s\n", context, ok ? "OK" : "FAILED");
  return ok;
}

bool StorageManager::mountSpiffs() {
  if (SPIFFS.begin(false)) {
    _spiffsMounted = true;
    return true;
  }
  return false;
}

bool StorageManager::begin() {
  Serial.println("[boot] SD card initialization starting");
  _spiffsMounted = mountSpiffs();

  _healthy = mountSdCard("SD mount");
  _sdRetryCount = 0;
  _disableSdPolling = false;

  if (_healthy) {
    _sdPresent = SD.cardType() != CARD_NONE;
    _sdMountFailed = false;
    Serial.printf("[boot] SD card mounted: total=%llu bytes used=%llu bytes\n",
                  SD.totalBytes(), SD.usedBytes());
    bool ok = ensureLayout();
    if (!ok) {
      _healthy = false;
      _sdMountFailed = _sdPresent;
    }
    Serial.println(ok ? "[boot] SD card layout ready"
                      : "[ERROR] SD card layout initialization failed");
    if (ok && _spiffsMounted && SPIFFS.exists(RenzFiConfig::FB_MANIFEST)) {
      Serial.println("[storage] SD restored, syncing fallback data");
      syncFallbackToSd();
    }
    _usingFallback = !ok;
    Serial.println(_usingFallback ? "[storage] Sales storage = SPIFFS fallback"
                                  : "[storage] Sales storage = SD");
    if (_usingFallback && _spiffsMounted) {
      if (!SPIFFS.exists(RenzFiConfig::FB_SALES)) {
        if (spiffsWriteFile(RenzFiConfig::FB_SALES, kDefaultSales)) {
          Serial.println("[storage] SPIFFS fallback seeded sales");
        } else {
          Serial.println("[ERROR] SPIFFS fallback sales seed failed");
        }
      }
    }
    return ok;
  }

  _sdPresent = SD.cardType() != CARD_NONE;
  _sdMountFailed = _sdPresent;
  setError("[ERROR] SD card mount failed");
  if (_spiffsMounted) {
    _usingFallback = true;
    Serial.println("[storage] SD unavailable, using SPIFFS fallback");
    Serial.println("[storage] Sales storage = SPIFFS fallback");
    if (!SPIFFS.exists(RenzFiConfig::FB_PORTAL_SESSIONS)) {
      if (spiffsWriteFile(RenzFiConfig::FB_PORTAL_SESSIONS,
                          kDefaultPortalSessions)) {
        Serial.println("[storage] SPIFFS fallback seeded portal sessions");
      } else {
        Serial.println("[ERROR] SPIFFS fallback portal sessions seed failed");
      }
    }
    if (!SPIFFS.exists(RenzFiConfig::FB_SALES)) {
      if (spiffsWriteFile(RenzFiConfig::FB_SALES, kDefaultSales)) {
        Serial.println("[storage] SPIFFS fallback seeded sales");
      } else {
        Serial.println("[ERROR] SPIFFS fallback sales seed failed");
      }
    }
  } else {
    Serial.println("[ERROR] SPIFFS not available — no fallback storage");
  }
  return false;
}

bool StorageManager::healthy() const {
  return _healthy;
}

bool StorageManager::usingFallback() const {
  return _usingFallback;
}

String StorageManager::lastError() const {
  return _lastError;
}

bool StorageManager::isSdPollingDisabled() const {
  return _disableSdPolling;
}

uint8_t StorageManager::sdRetryCount() const {
  return _sdRetryCount;
}

bool StorageManager::attemptSdRecovery() {
  if (!_spiffsMounted && !mountSpiffs()) return false;

  if (!mountSdCard("SD remount")) {
    onSdRecoveryFailed();
    return false;
  }

  _healthy = true;
  if (!ensureLayout()) {
    Serial.println("[storage] SD remount layout failed");
    _healthy = false;
    onSdRecoveryFailed();
    return false;
  }

  onSdRecoverySucceeded();
  return true;
}

void StorageManager::onSdRecoveryFailed() {
  _healthy = false;
  _usingFallback = _spiffsMounted;
  _sdPresent = SD.cardType() != CARD_NONE;
  _sdMountFailed = _sdPresent;

  _sdRetryCount++;
  Serial.printf("[storage] SD recovery attempt %u/%u\n", _sdRetryCount,
                RenzFiConfig::SD_RECOVERY_MAX_ATTEMPTS);

  if (_sdRetryCount >= RenzFiConfig::SD_RECOVERY_MAX_ATTEMPTS &&
      !_disableSdPolling) {
    _disableSdPolling = true;
    Serial.println(
        "[storage] SD polling disabled after 3 failed recovery attempts");
    Serial.println(
        "[storage] SD polling disabled after repeated failures");
  }
}

void StorageManager::onSdRecoverySucceeded() {
  _healthy = true;
  _usingFallback = false;
  _sdRetryCount = 0;
  _disableSdPolling = false;
  _sdPresent = SD.cardType() != CARD_NONE;
  _sdMountFailed = false;
  Serial.println("[storage] SD recovered successfully");

  if (SPIFFS.exists(RenzFiConfig::FB_MANIFEST)) {
    Serial.println("[storage] SD restored, syncing fallback data");
    syncFallbackToSd();
  }
}

bool StorageManager::retrySd() {
  _disableSdPolling = false;
  _sdRetryCount = 0;
  Serial.println("[storage] Manual SD recovery requested");
  return attemptSdRecovery();
}

void StorageManager::pollStorageHealth() {
  if (_disableSdPolling) return;
  if (_syncInProgress) return;

  const uint32_t now = millis();
  if (_healthy) return;

  if (now - _lastHealthPollMs < RenzFiConfig::STORAGE_HEALTH_POLL_MS) return;
  _lastHealthPollMs = now;

  attemptSdRecovery();
}

// ── SD layout (unchanged) ─────────────────────────────────────────────────────

bool StorageManager::ensureLayout() {
  if (!_healthy) return false;

  bool ok = true;
  ok = ensureDir("/config") && ok;
  ok = ensureDir("/sales") && ok;
  ok = ensureDir("/logs") && ok;
  ok = ensureDir("/vouchers") && ok;
  ok = ensureDir("/sessions") && ok;
  ok = ensureDir(RenzFiConfig::WWW_ROOT) && ok;

  ok = ensureJsonFile(RenzFiConfig::SETTINGS_FILE, kDefaultSettings) && ok;
  ok = ensureJsonFile(RenzFiConfig::PROMOS_FILE, kDefaultPromos) && ok;
  ok = ensureJsonFile(RenzFiConfig::ROUTER_FILE, kDefaultRouter) && ok;
  ok = ensureJsonFile(RenzFiConfig::WIFI_CONFIG_FILE,
                      "{\"staSsid\":\"RenzFi_Admin\",\"useStaticIp\":true,"
                      "\"staIp\":\"10.10.10.2\",\"staGateway\":\"10.10.10.1\","
                      "\"staSubnet\":\"255.255.255.0\"}") &&
       ok;
  ok = ensureJsonFile(RenzFiConfig::VOUCHERS_FILE, kDefaultVouchers) && ok;
  ok = ensureJsonFile(RenzFiConfig::SALES_FILE, "[]") && ok;
  ok = ensureJsonFile(RenzFiConfig::LOGS_FILE, "[]") && ok;
  ok = ensureJsonFile(RenzFiConfig::USERS_FILE, "[]") && ok;
  ok = ensureJsonFile(RenzFiConfig::ADMIN_SESSIONS_FILE, "[]") && ok;
  ok = ensureJsonFile(RenzFiConfig::PORTAL_SESSIONS_FILE,
                      kDefaultPortalSessions) &&
       ok;
  ok = ensureJsonFile(RenzFiConfig::PORTAL_CONFIG_FILE,
                      "{\"revision\":0,\"hasBanner\":false,\"hasMusic\":false}") &&
       ok;

  return ok;
}

// ── Fallback routing helpers ──────────────────────────────────────────────────

bool StorageManager::isFallbackEligible(const char *path) const {
  return strcmp(path, RenzFiConfig::SETTINGS_FILE) == 0 ||
         strcmp(path, RenzFiConfig::PROMOS_FILE) == 0 ||
         strcmp(path, RenzFiConfig::ROUTER_FILE) == 0 ||
         strcmp(path, RenzFiConfig::VOUCHERS_FILE) == 0 ||
         strcmp(path, RenzFiConfig::PORTAL_SESSIONS_FILE) == 0 ||
         strcmp(path, RenzFiConfig::SALES_FILE) == 0 ||
         strcmp(path, RenzFiConfig::PORTAL_CONFIG_FILE) == 0;
}

String StorageManager::toFallbackPath(const char *sdPath) const {
  if (strcmp(sdPath, RenzFiConfig::SETTINGS_FILE) == 0)
    return RenzFiConfig::FB_SETTINGS;
  if (strcmp(sdPath, RenzFiConfig::PROMOS_FILE) == 0)
    return RenzFiConfig::FB_PROMOS;
  if (strcmp(sdPath, RenzFiConfig::ROUTER_FILE) == 0)
    return RenzFiConfig::FB_ROUTER;
  if (strcmp(sdPath, RenzFiConfig::VOUCHERS_FILE) == 0)
    return RenzFiConfig::FB_VOUCHERS;
  if (strcmp(sdPath, RenzFiConfig::PORTAL_SESSIONS_FILE) == 0)
    return RenzFiConfig::FB_PORTAL_SESSIONS;
  if (strcmp(sdPath, RenzFiConfig::SALES_FILE) == 0)
    return RenzFiConfig::FB_SALES;
  if (strcmp(sdPath, RenzFiConfig::PORTAL_CONFIG_FILE) == 0)
    return RenzFiConfig::FB_PORTAL_CONFIG;
  return "";
}

const char *StorageManager::toSdPath(const char *fbPath) const {
  if (strcmp(fbPath, RenzFiConfig::FB_SETTINGS) == 0)
    return RenzFiConfig::SETTINGS_FILE;
  if (strcmp(fbPath, RenzFiConfig::FB_PROMOS) == 0)
    return RenzFiConfig::PROMOS_FILE;
  if (strcmp(fbPath, RenzFiConfig::FB_ROUTER) == 0)
    return RenzFiConfig::ROUTER_FILE;
  if (strcmp(fbPath, RenzFiConfig::FB_VOUCHERS) == 0)
    return RenzFiConfig::VOUCHERS_FILE;
  if (strcmp(fbPath, RenzFiConfig::FB_PORTAL_SESSIONS) == 0)
    return RenzFiConfig::PORTAL_SESSIONS_FILE;
  if (strcmp(fbPath, RenzFiConfig::FB_SALES) == 0)
    return RenzFiConfig::SALES_FILE;
  if (strcmp(fbPath, RenzFiConfig::FB_PORTAL_CONFIG) == 0)
    return RenzFiConfig::PORTAL_CONFIG_FILE;
  return nullptr;
}

size_t StorageManager::perFileLimit(const char *sdPath) const {
  if (strcmp(sdPath, RenzFiConfig::SETTINGS_FILE) == 0)
    return RenzFiConfig::FB_LIMIT_SETTINGS;
  if (strcmp(sdPath, RenzFiConfig::PROMOS_FILE) == 0)
    return RenzFiConfig::FB_LIMIT_PROMOS;
  if (strcmp(sdPath, RenzFiConfig::ROUTER_FILE) == 0)
    return RenzFiConfig::FB_LIMIT_ROUTER;
  if (strcmp(sdPath, RenzFiConfig::VOUCHERS_FILE) == 0)
    return RenzFiConfig::FB_LIMIT_VOUCHERS;
  if (strcmp(sdPath, RenzFiConfig::PORTAL_SESSIONS_FILE) == 0)
    return RenzFiConfig::FB_LIMIT_PORTAL_SESSIONS;
  if (strcmp(sdPath, RenzFiConfig::SALES_FILE) == 0)
    return RenzFiConfig::FB_LIMIT_SALES;
  if (strcmp(sdPath, RenzFiConfig::PORTAL_CONFIG_FILE) == 0)
    return RenzFiConfig::FB_LIMIT_PORTAL_CONFIG;
  return 0;
}

bool StorageManager::seedFallbackDefaults(const char *sdPath,
                                          JsonDocument &doc) const {
  doc.clear();
  const char *json = nullptr;
  if (strcmp(sdPath, RenzFiConfig::SETTINGS_FILE) == 0)
    json = kDefaultSettings;
  else if (strcmp(sdPath, RenzFiConfig::PROMOS_FILE) == 0)
    json = kDefaultPromos;
  else if (strcmp(sdPath, RenzFiConfig::ROUTER_FILE) == 0)
    json = kDefaultRouter;
  else if (strcmp(sdPath, RenzFiConfig::VOUCHERS_FILE) == 0)
    json = kDefaultVouchers;
  else if (strcmp(sdPath, RenzFiConfig::PORTAL_SESSIONS_FILE) == 0)
    json = kDefaultPortalSessions;
  else if (strcmp(sdPath, RenzFiConfig::SALES_FILE) == 0)
    json = kDefaultSales;
  else
    return false;

  return !deserializeJson(doc, json);
}

// ── SD I/O (preserved behavior) ───────────────────────────────────────────────

bool StorageManager::readJsonFromSd(const char *path, JsonDocument &doc) {
  File file = SD.open(path, FILE_READ);
  if (!file) {
    setError(String("Unable to open ") + path);
    return false;
  }

  DeserializationError err = deserializeJson(doc, file);
  file.close();

  if (err) {
    setError(String("JSON parse failed for ") + path + ": " + err.c_str());
    String badPath = String(path) + ".bad";
    SD.remove(badPath);
    SD.rename(path, badPath);
    const char *emptyJson =
        (String(path).indexOf("/config/") >= 0) ? "{}" : "[]";
    ensureJsonFile(path, emptyJson);
    doc.clear();
    deserializeJson(doc, emptyJson);
    return true;
  }
  return true;
}

bool StorageManager::writeJsonToSd(const char *path, JsonDocument &doc) {
  String serialized;
  serializeJson(doc, serialized);
  return writeJsonToSdSerialized(path, serialized);
}

bool StorageManager::writeJsonToSdSerialized(const char *path,
                                             const String &serialized) {
  String tmp = String(path) + ".tmp";
  File file = SD.open(tmp, FILE_WRITE);
  if (!file) {
    setError(String("Unable to write ") + tmp);
    return false;
  }
  if (file.print(serialized) == 0) {
    file.close();
    SD.remove(tmp);
    setError(String("JSON serialize failed for ") + path);
    return false;
  }
  file.flush();
  file.close();
  SD.remove(path);
  if (!SD.rename(tmp, path)) {
    setError(String("Unable to rename temp file for ") + path);
    return false;
  }
  return true;
}

// ── SPIFFS fallback I/O ─────────────────────────────────────────────────────

bool StorageManager::spiffsReadFile(const String &fbPath, JsonDocument &doc) {
  if (!SPIFFS.exists(fbPath)) return false;
  File file = SPIFFS.open(fbPath, "r");
  if (!file) return false;
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  return !err;
}

bool StorageManager::spiffsWriteFile(const String &fbPath, const String &data) {
  String tmp = fbPath + ".tmp";
  SPIFFS.remove(tmp.c_str());
  File file = SPIFFS.open(tmp, "w");
  if (!file) return false;
  if (file.print(data) == 0) {
    file.close();
    SPIFFS.remove(tmp.c_str());
    return false;
  }
  file.flush();
  file.close();
  SPIFFS.remove(fbPath.c_str());
  return SPIFFS.rename(tmp.c_str(), fbPath.c_str());
}

size_t StorageManager::spiffsFreeBytes() const {
  if (!_spiffsMounted) return 0;
  const size_t total = SPIFFS.totalBytes();
  const size_t used = SPIFFS.usedBytes();
  return total > used ? total - used : 0;
}

size_t StorageManager::spiffsFileSize(const String &fbPath) const {
  if (!SPIFFS.exists(fbPath)) return 0;
  File f = SPIFFS.open(fbPath, "r");
  if (!f) return 0;
  size_t sz = f.size();
  f.close();
  return sz;
}

size_t StorageManager::fallbackTotalBytes() const {
  size_t total = 0;
  total += spiffsFileSize(RenzFiConfig::FB_SETTINGS);
  total += spiffsFileSize(RenzFiConfig::FB_PROMOS);
  total += spiffsFileSize(RenzFiConfig::FB_ROUTER);
  total += spiffsFileSize(RenzFiConfig::FB_VOUCHERS);
  total += spiffsFileSize(RenzFiConfig::FB_PORTAL_SESSIONS);
  total += spiffsFileSize(RenzFiConfig::FB_SALES);
  if (SPIFFS.exists(RenzFiConfig::FB_MANIFEST))
    total += spiffsFileSize(RenzFiConfig::FB_MANIFEST);
  return total;
}

bool StorageManager::checkQuota(const char *sdPath, size_t newSize,
                                size_t oldSize) const {
  const size_t limit = perFileLimit(sdPath);
  if (limit > 0 && newSize > limit) {
    Serial.println("[storage] SPIFFS fallback quota exceeded (per-file)");
    return false;
  }

  const size_t totalAfter =
      fallbackTotalBytes() - oldSize + newSize;
  if (totalAfter > RenzFiConfig::FB_HARD_LIMIT_BYTES) {
    Serial.println("[storage] SPIFFS fallback quota exceeded (hard limit)");
    return false;
  }
  if (totalAfter > RenzFiConfig::FB_SOFT_LIMIT_BYTES) {
    Serial.println("[storage] SPIFFS fallback soft quota warning");
  }

  if (spiffsFreeBytes() < RenzFiConfig::SPIFFS_MIN_FREE_BYTES + newSize) {
    Serial.println("[storage] SPIFFS fallback quota exceeded (free space)");
    return false;
  }
  return true;
}

bool StorageManager::isPortalWriteThrottled(const char *path, bool force) const {
  if (force) return false;
  if (strcmp(path, RenzFiConfig::PORTAL_SESSIONS_FILE) != 0) return false;
  if (_lastFbPortalWriteMs == 0) return false;
  return (millis() - _lastFbPortalWriteMs) < RenzFiConfig::FB_WRITE_MIN_INTERVAL_MS;
}

bool StorageManager::readJsonFromSpiffs(const char *sdPath, JsonDocument &doc) {
  const String fbPath = toFallbackPath(sdPath);
  if (fbPath.isEmpty()) return false;

  if (spiffsReadFile(fbPath, doc)) return true;
  return seedFallbackDefaults(sdPath, doc);
}

bool StorageManager::writeJsonToSpiffs(const char *sdPath, const String &serialized,
                                     bool forcePortalWrite) {
  if (_syncInProgress) return false;

  if (isPortalWriteThrottled(sdPath, forcePortalWrite)) {
    Serial.println(
        "[storage] SPIFFS fallback write throttled (portal_sessions, deferred)");
    return false;
  }

  const String fbPath = toFallbackPath(sdPath);
  if (fbPath.isEmpty()) return false;

  const size_t oldSize = spiffsFileSize(fbPath);
  if (!checkQuota(sdPath, serialized.length(), oldSize)) return false;

  if (!spiffsWriteFile(fbPath, serialized)) return false;

  if (strcmp(sdPath, RenzFiConfig::PORTAL_SESSIONS_FILE) == 0)
    _lastFbPortalWriteMs = millis();

  addToManifest(fbPath, serialized.length());
  Serial.printf("[storage] SPIFFS fallback write %s (%u bytes)\n", fbPath.c_str(),
                (unsigned)serialized.length());
  return true;
}

// ── Public JSON API ───────────────────────────────────────────────────────────

bool StorageManager::readJson(const char *path, JsonDocument &doc) {
  if (_healthy) return readJsonFromSd(path, doc);

  if (!_usingFallback || !_spiffsMounted || !isFallbackEligible(path))
    return false;

  return readJsonFromSpiffs(path, doc);
}

bool StorageManager::writeJson(const char *path, JsonDocument &doc,
                               bool forcePortalWrite) {
  String serialized;
  serializeJson(doc, serialized);
  if (serialized.length() == 0) {
    setError(String("JSON serialize failed for ") + path);
    return false;
  }

  if (_healthy) {
    return writeJsonToSdSerialized(path, serialized);
  }

  if (!_usingFallback || !_spiffsMounted || !isFallbackEligible(path))
    return false;

  return writeJsonToSpiffs(path, serialized, forcePortalWrite);
}

bool StorageManager::appendJsonArrayItem(const char *path, JsonObject item,
                                         size_t capacity) {
  DynamicJsonDocument doc(capacity);
  if (!readJson(path, doc) || !doc.is<JsonArray>()) {
    doc.clear();
    doc.to<JsonArray>();
  }
  JsonArray arr = doc.as<JsonArray>();
  JsonObject target = arr.createNestedObject();
  for (JsonPair kv : item) {
    target[kv.key()] = kv.value();
  }
  return writeJson(path, doc);
}

bool StorageManager::clearJsonArray(const char *path) {
  DynamicJsonDocument doc(128);
  doc.to<JsonArray>();
  return writeJson(path, doc);
}

bool StorageManager::exists(const char *path) const {
  if (_healthy) return SD.exists(path);
  if (!_usingFallback || !_spiffsMounted || !isFallbackEligible(path))
    return false;
  return SPIFFS.exists(toFallbackPath(path));
}

size_t StorageManager::fileSizeBytes(const char *path) const {
  if (!_healthy || path == nullptr || !SD.exists(path)) return 0;
  File file = SD.open(path, FILE_READ);
  if (!file) return 0;
  size_t bytes = file.size();
  file.close();
  return bytes;
}

// ── Manifest ──────────────────────────────────────────────────────────────────

bool StorageManager::readManifest(JsonDocument &doc) {
  doc.clear();
  if (!SPIFFS.exists(RenzFiConfig::FB_MANIFEST)) return false;
  return spiffsReadFile(RenzFiConfig::FB_MANIFEST, doc);
}

bool StorageManager::writeManifest(const JsonDocument &doc) {
  String serialized;
  serializeJson(doc, serialized);
  return spiffsWriteFile(RenzFiConfig::FB_MANIFEST, serialized);
}

void StorageManager::addToManifest(const String &fbPath, size_t fileBytes) {
  DynamicJsonDocument manifest(1024);
  if (!readManifest(manifest)) {
    manifest["v"] = 1;
    manifest["dirty"] = true;
    manifest["bytes"] = 0;
    manifest["files"].to<JsonArray>();
  }

  JsonArray files = manifest["files"].as<JsonArray>();
  bool found = false;
  for (JsonVariant v : files) {
    if (strcmp(v.as<const char *>(), fbPath.c_str()) == 0) {
      found = true;
      break;
    }
  }
  if (!found) files.add(fbPath);

  manifest["dirty"] = true;
  manifest["v"] = 1;

  size_t total = 0;
  for (JsonVariant v : files) {
    total += spiffsFileSize(String(v.as<const char *>()));
  }
  manifest["bytes"] = total;

  (void)fileBytes;
  writeManifest(manifest);
}

void StorageManager::removeFromManifest(const String &fbPath) {
  DynamicJsonDocument manifest(1024);
  if (!readManifest(manifest)) return;

  JsonArray oldFiles = manifest["files"].as<JsonArray>();
  DynamicJsonDocument next(1024);
  JsonArray newFiles = next["files"].to<JsonArray>();
  for (JsonVariant v : oldFiles) {
    if (strcmp(v.as<const char *>(), fbPath.c_str()) != 0)
      newFiles.add(v.as<const char *>());
  }

  manifest["files"].set(newFiles);
  size_t total = 0;
  for (JsonVariant v : newFiles) {
    total += spiffsFileSize(String(v.as<const char *>()));
  }
  manifest["bytes"] = total;
  manifest["dirty"] = newFiles.size() > 0;
  writeManifest(manifest);
}

bool StorageManager::verifySdMatches(const char *sdPath,
                                     const String &expected) {
  File file = SD.open(sdPath, FILE_READ);
  if (!file) return false;
  String onDisk;
  onDisk.reserve(expected.length() + 16);
  while (file.available()) onDisk += (char)file.read();
  file.close();
  return onDisk == expected;
}

bool StorageManager::syncFallbackToSd() {
  if (!_healthy || !_spiffsMounted) return false;
  if (_syncInProgress) return false;

  _syncInProgress = true;
  const uint32_t startMs = millis();

  DynamicJsonDocument manifest(2048);
  if (!readManifest(manifest)) {
    _syncInProgress = false;
    return true;
  }

  JsonArray files = manifest["files"].as<JsonArray>();
  if (files.isNull() || files.size() == 0) {
    SPIFFS.remove(RenzFiConfig::FB_MANIFEST);
    _syncInProgress = false;
    Serial.println("[storage] Sync complete: no fallback files");
    return true;
  }

  unsigned filesSynced = 0;
  size_t bytesSynced = 0;
  bool allOk = true;

  for (JsonVariant v : files) {
    const char *fbPath = v.as<const char *>();
    if (!fbPath || strcmp(fbPath, RenzFiConfig::FB_MANIFEST) == 0) continue;

    const char *sdPath = toSdPath(fbPath);
    if (!sdPath || !SPIFFS.exists(fbPath)) continue;

    File in = SPIFFS.open(fbPath, "r");
    if (!in) {
      allOk = false;
      continue;
    }
    String payload;
    payload.reserve(in.size() + 8);
    while (in.available()) payload += (char)in.read();
    in.close();

    if (!writeJsonToSdSerialized(sdPath, payload)) {
      allOk = false;
      Serial.printf("[storage] Sync failed for %s\n", sdPath);
      continue;
    }
    if (!verifySdMatches(sdPath, payload)) {
      allOk = false;
      Serial.printf("[storage] Sync verify failed for %s\n", sdPath);
      continue;
    }

    SPIFFS.remove(fbPath);
    removeFromManifest(fbPath);
    filesSynced++;
    bytesSynced += payload.length();
    Serial.printf("[storage] Synced %s -> %s (%u bytes)\n", fbPath, sdPath,
                  (unsigned)payload.length());
  }

  const uint32_t durationMs = millis() - startMs;
  bool manifestCleared = false;

  if (allOk) {
    DynamicJsonDocument check(512);
    if (!readManifest(check) ||
        check["files"].as<JsonArray>().size() == 0) {
      SPIFFS.remove(RenzFiConfig::FB_MANIFEST);
      manifestCleared = true;
      Serial.println("[storage] Sync successful");
      Serial.println("[storage] SPIFFS fallback cleared");
    }
  } else {
    Serial.println("[storage] Sync incomplete — fallback retained for retry");
  }

  Serial.printf(
      "[storage] Sync stats: files=%u bytes=%u duration_ms=%lu "
      "manifest_cleared=%s\n",
      filesSynced, (unsigned)bytesSynced, (unsigned long)durationMs,
      manifestCleared ? "yes" : "no");

  _syncInProgress = false;
  return allOk;
}

// ── Static / misc (unchanged SD paths) ────────────────────────────────────────

File StorageManager::openStatic(const String &path) {
  String full = String(RenzFiConfig::WWW_ROOT) + path;
  if (full.endsWith("/")) full += "index.html";
  if (!SD.exists(full) && !path.startsWith("/api/")) {
    full = String(RenzFiConfig::WWW_ROOT) + "/index.html";
  }
  return SD.open(full, FILE_READ);
}

String StorageManager::contentType(const String &path) const {
  if (path.endsWith(".html")) return "text/html; charset=utf-8";
  if (path.endsWith(".css")) return "text/css; charset=utf-8";
  if (path.endsWith(".js")) return "application/javascript; charset=utf-8";
  if (path.endsWith(".webmanifest"))
    return "application/manifest+json; charset=utf-8";
  if (path.endsWith(".json")) return "application/json; charset=utf-8";
  if (path.endsWith(".svg")) return "image/svg+xml";
  if (path.endsWith(".png")) return "image/png";
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
  if (path.endsWith(".ico")) return "image/x-icon";
  if (path.endsWith(".gz")) return "application/gzip";
  return "application/octet-stream";
}

uint64_t StorageManager::totalBytes() const {
  return getSdTotalBytes();
}

uint64_t StorageManager::usedBytes() const {
  return getSdUsedBytes();
}

bool StorageManager::writeBinary(const char *sdPath, const uint8_t *data,
                                 size_t len) {
  if (!_healthy || !sdPath || !data || len == 0) return false;

  String path = String(sdPath);
  int slash = path.lastIndexOf('/');
  if (slash > 0) {
    String dir = path.substring(0, slash);
    if (!ensureDir(dir.c_str())) return false;
  }

  if (SD.exists(sdPath)) SD.remove(sdPath);
  File out = SD.open(sdPath, FILE_WRITE);
  if (!out) {
    setError("Unable to open SD file for write");
    return false;
  }
  const size_t written = out.write(data, len);
  out.close();
  return written == len;
}

bool StorageManager::writeBinarySpiffs(const char *spiffsPath,
                                       const uint8_t *data, size_t len) {
  if (!_spiffsMounted || !spiffsPath || !data || len == 0) return false;

  String path = String(spiffsPath);
  int slash = path.lastIndexOf('/');
  if (slash > 0) {
    String dir = path.substring(0, slash);
    if (!SPIFFS.exists(dir)) {
      // SPIFFS has no real directories; create parent path marker if needed.
      if (dir.startsWith("/portal") && !SPIFFS.exists("/portal")) {
        // no-op — SPIFFS treats paths as flat keys with slashes
      }
    }
  }

  if (SPIFFS.exists(spiffsPath)) SPIFFS.remove(spiffsPath);
  File out = SPIFFS.open(spiffsPath, "w");
  if (!out) {
    setError("Unable to open SPIFFS file for write");
    return false;
  }
  const size_t written = out.write(data, len);
  out.close();
  return written == len;
}

bool StorageManager::removeBinary(const char *sdPath, const char *spiffsPath) {
  bool ok = true;
  if (_healthy && sdPath && SD.exists(sdPath)) ok = SD.remove(sdPath) && ok;
  if (spiffsPath && SPIFFS.exists(spiffsPath)) ok = SPIFFS.remove(spiffsPath) && ok;
  return ok;
}

uint64_t StorageManager::getSpiffsUsedBytes() const {
  return _spiffsMounted ? SPIFFS.usedBytes() : 0;
}

uint64_t StorageManager::getSpiffsTotalBytes() const {
  return _spiffsMounted ? SPIFFS.totalBytes() : 0;
}

bool StorageManager::isSdPresent() const {
  return _sdPresent;
}

bool StorageManager::isSdMounted() const {
  return _healthy;
}

uint64_t StorageManager::getSdUsedBytes() const {
  if (!_healthy) return 0;
  return SD.usedBytes();
}

uint64_t StorageManager::getSdTotalBytes() const {
  if (!_healthy) return 0;
  return SD.totalBytes();
}

uint64_t StorageManager::getSdFreeBytes() const {
  const uint64_t total = getSdTotalBytes();
  const uint64_t used = getSdUsedBytes();
  return total > used ? total - used : 0;
}

namespace {

double bytesToMb(uint64_t bytes) {
  return round((bytes / 1024.0 / 1024.0) * 10.0) / 10.0;
}

const char *sdStatusLabel(bool present, bool mounted, bool mountFailed) {
  if (mounted) return "Ready";
  if (!present) return "Missing";
  if (mountFailed) return "Mount Failed";
  return "Error";
}

}  // namespace

void StorageManager::fillSdStatus(JsonObject sd) const {
  const bool present = isSdPresent();
  const bool mounted = isSdMounted();

  sd["present"] = present;
  sd["mounted"] = mounted;
  sd["fallback"] = _usingFallback;
  sd["pollingDisabled"] = _disableSdPolling;
  sd["recoveryAttempts"] = _sdRetryCount;
  sd["mode"] = mounted ? "SD" : "SPIFFS Fallback";

  if (mounted) {
    const uint64_t total = getSdTotalBytes();
    const uint64_t used = getSdUsedBytes();
    const uint64_t freeBytes = getSdFreeBytes();
    if (total == 0) {
      sd["status"] = "Error";
      sd["usedMb"] = 0;
      sd["totalMb"] = 0;
      sd["freeMb"] = 0;
      return;
    }
    sd["usedMb"] = bytesToMb(used);
    sd["totalMb"] = bytesToMb(total);
    sd["freeMb"] = bytesToMb(freeBytes);
    sd["status"] = "Ready";
    return;
  }

  sd["usedMb"] = 0;
  sd["totalMb"] = 0;
  sd["freeMb"] = 0;
  sd["status"] = sdStatusLabel(present, mounted, _sdMountFailed);
}

void StorageManager::setError(const String &message) {
  _lastError = message;
  Serial.println(message);
}

bool StorageManager::ensureDir(const char *path) {
  if (SD.exists(path)) return true;
  if (!SD.mkdir(path)) {
    setError(String("Unable to create directory ") + path);
    return false;
  }
  return true;
}

bool StorageManager::ensureJsonFile(const char *path, const char *contents) {
  if (SD.exists(path)) return true;
  File file = SD.open(path, FILE_WRITE);
  if (!file) {
    setError(String("Unable to create ") + path);
    return false;
  }
  file.print(contents);
  file.close();
  return true;
}

bool StorageManager::readSdText(const char *path, String &out) const {
  out = "";
  if (!_healthy || path == nullptr || !SD.exists(path)) return false;
  File file = SD.open(path, FILE_READ);
  if (!file) return false;
  out.reserve(file.size() + 8);
  while (file.available()) out += static_cast<char>(file.read());
  file.close();
  return true;
}

bool StorageManager::writeSdText(const char *path, const String &content) {
  if (!_healthy || path == nullptr) return false;
  const int slash = String(path).lastIndexOf('/');
  if (slash > 0) {
    String dir = String(path).substring(0, slash);
    if (!SD.exists(dir.c_str())) SD.mkdir(dir.c_str());
  }
  File file = SD.open(path, FILE_WRITE);
  if (!file) {
    setError(String("Unable to write SD file ") + path);
    return false;
  }
  const size_t written = file.print(content);
  file.close();
  return written == content.length();
}

bool StorageManager::deleteSdOnly(const char *path) {
  if (!_healthy || path == nullptr) return true;
  if (SD.exists(path)) return SD.remove(path);
  return true;
}

void StorageManager::clearAllFallbackData() {
  if (!_spiffsMounted) return;
  const char *fallbackPaths[] = {
      RenzFiConfig::FB_SETTINGS,
      RenzFiConfig::FB_PROMOS,
      RenzFiConfig::FB_ROUTER,
      RenzFiConfig::FB_VOUCHERS,
      RenzFiConfig::FB_PORTAL_SESSIONS,
      RenzFiConfig::FB_SALES,
      RenzFiConfig::FB_PORTAL_CONFIG,
      RenzFiConfig::FB_MANIFEST,
      RenzFiConfig::PORTAL_BANNER_SPIFFS,
      RenzFiConfig::PORTAL_MUSIC_SPIFFS,
  };
  for (const char *path : fallbackPaths) {
    if (SPIFFS.exists(path)) SPIFFS.remove(path);
  }
}

bool StorageManager::factoryResetData() {
  if (!_healthy) return false;
  return ensureLayout();
}
