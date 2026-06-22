#include "BackupManager.h"

#include <SD.h>
#include <SPIFFS.h>
#include <vector>
#include <time.h>
#include <cstring>

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
    "\"profile\":\"default\",\"ssid\":\"RenzFi_PisoWifi\",\"wifiPassword\":\"\"}";

constexpr const char *kDefaultPortalConfig =
    "{\"revision\":0,\"hasBanner\":false,\"hasMusic\":false}";

constexpr const char *kDefaultPortalSessions = "{\"sessions\":[]}";

struct BackupEntry {
  const char *archivePath;
  const char *sdPath;
  const char *defaultJson;
  bool isJson;
};

constexpr BackupEntry kJsonEntries[] = {
    {"/config/settings.json", RenzFiConfig::SETTINGS_FILE, kDefaultSettings, true},
    {"/config/router.json", RenzFiConfig::ROUTER_FILE, kDefaultRouter, true},
    {"/config/portal-config.json", RenzFiConfig::PORTAL_CONFIG_FILE,
     kDefaultPortalConfig, true},
    {"/config/promos.json", RenzFiConfig::PROMOS_FILE, kDefaultPromos, true},
    {"/config/vouchers.json", RenzFiConfig::VOUCHERS_FILE, "[]", true},
    {"/config/sales.json", RenzFiConfig::SALES_FILE, "[]", true},
    {"/config/users.json", RenzFiConfig::USERS_FILE, "[]", true},
};

constexpr BackupEntry kAssetEntries[] = {
    {"/assets/banner.webp", RenzFiConfig::PORTAL_BANNER_SD, nullptr, false},
    {"/assets/bg-music.mp3", RenzFiConfig::PORTAL_MUSIC_SD, nullptr, false},
};

uint32_t crc32Bytes(uint32_t crc, const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (0xEDB88320U & static_cast<uint32_t>(-(crc & 1)));
    }
  }
  return crc;
}

void writeLE16(File &f, uint16_t v) {
  f.write(static_cast<uint8_t>(v & 0xFF));
  f.write(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void writeLE32(File &f, uint32_t v) {
  f.write(static_cast<uint8_t>(v & 0xFF));
  f.write(static_cast<uint8_t>((v >> 8) & 0xFF));
  f.write(static_cast<uint8_t>((v >> 16) & 0xFF));
  f.write(static_cast<uint8_t>((v >> 24) & 0xFF));
}

uint16_t readLE16(const uint8_t *p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t readLE32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) |
                               (p[3] << 24));
}

String isoTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return String(millis());
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buf);
}

String manifestJson() {
  DynamicJsonDocument doc(256);
  doc["backupVersion"]  = BackupManager::BACKUP_VERSION;
  doc["firmwareVersion"] = RenzFiConfig::FIRMWARE_VERSION;
  doc["exportedAt"]     = isoTimestamp();
  String out;
  serializeJson(doc, out);
  return out;
}

struct ZipWriteEntry {
  String name;
  uint32_t offset = 0;
  uint32_t crc = 0;
  uint32_t size = 0;
};

bool writeStoredZipEntry(File &zip, std::vector<ZipWriteEntry> &catalog,
                         const char *archivePath, const uint8_t *data,
                         size_t len) {
  if (!data && len > 0) return false;

  ZipWriteEntry entry;
  entry.name   = archivePath;
  entry.offset = zip.position();
  entry.size   = static_cast<uint32_t>(len);
  entry.crc    = crc32Bytes(0xFFFFFFFFU, data, len) ^ 0xFFFFFFFFU;

  zip.write((const uint8_t *)"\x50\x4b\x03\x04", 4);
  writeLE16(zip, 20);
  writeLE16(zip, 0);
  writeLE16(zip, 0);
  writeLE16(zip, 0);
  writeLE16(zip, 0);
  writeLE32(zip, entry.crc);
  writeLE32(zip, entry.size);
  writeLE32(zip, entry.size);
  writeLE16(zip, static_cast<uint16_t>(entry.name.length()));
  writeLE16(zip, 0);
  zip.write(reinterpret_cast<const uint8_t *>(entry.name.c_str()),
            entry.name.length());
  if (len > 0) zip.write(data, len);

  catalog.push_back(entry);
  return true;
}

bool finalizeStoredZip(File &zip, std::vector<ZipWriteEntry> &catalog) {
  const uint32_t centralStart = zip.position();
  for (const ZipWriteEntry &entry : catalog) {
    zip.write((const uint8_t *)"\x50\x4b\x01\x02", 4);
    writeLE16(zip, 20);
    writeLE16(zip, 20);
    writeLE16(zip, 0);
    writeLE16(zip, 0);
    writeLE16(zip, 0);
    writeLE16(zip, 0);
    writeLE32(zip, entry.crc);
    writeLE32(zip, entry.size);
    writeLE32(zip, entry.size);
    writeLE16(zip, static_cast<uint16_t>(entry.name.length()));
    writeLE16(zip, 0);
    writeLE16(zip, 0);
    writeLE16(zip, 0);
    writeLE16(zip, 0);
    writeLE32(zip, 0);
    writeLE32(zip, entry.offset);
    zip.write(reinterpret_cast<const uint8_t *>(entry.name.c_str()),
              entry.name.length());
  }

  const uint32_t centralSize = zip.position() - centralStart;
  zip.write((const uint8_t *)"\x50\x4b\x05\x06", 4);
  writeLE16(zip, 0);
  writeLE16(zip, 0);
  writeLE16(zip, static_cast<uint16_t>(catalog.size()));
  writeLE16(zip, static_cast<uint16_t>(catalog.size()));
  writeLE32(zip, centralSize);
  writeLE32(zip, centralStart);
  writeLE16(zip, 0);
  return true;
}

}  // namespace

void BackupManager::begin(StorageManager *storage, Logger *logger,
                            AuthManager *auth,
                            PortalConfigManager *portalConfig) {
  _storage      = storage;
  _logger       = logger;
  _auth         = auth;
  _portalConfig = portalConfig;
}

bool BackupManager::isSdAvailable() const {
  return _storage && _storage->isSdMounted() && _storage->healthy();
}

bool BackupManager::ensureBackupDir() {
  if (!isSdAvailable()) return false;
  if (SD.exists("/backup")) return true;
  return SD.mkdir("/backup");
}

bool BackupManager::mapArchivePathToSd(const char *archivePath, String &sdPath) {
  if (!archivePath) return false;
  String path = archivePath;
  if (path.startsWith("/")) path = path.substring(1);

  for (const BackupEntry &entry : kJsonEntries) {
    String archive = String(entry.archivePath);
    if (archive.startsWith("/")) archive = archive.substring(1);
    if (path.equalsIgnoreCase(archive)) {
      sdPath = entry.sdPath;
      return true;
    }
  }
  for (const BackupEntry &entry : kAssetEntries) {
    String archive = String(entry.archivePath);
    if (archive.startsWith("/")) archive = archive.substring(1);
    if (path.equalsIgnoreCase(archive)) {
      sdPath = entry.sdPath;
      return true;
    }
  }
  if (path.equalsIgnoreCase("renzfi-manifest.json")) return false;
  return false;
}

bool BackupManager::createZipBackup(String &error) {
  if (!ensureBackupDir()) {
    error = "SD Card is not available";
    return false;
  }

  if (SD.exists(TEMP_ZIP_PATH)) SD.remove(TEMP_ZIP_PATH);

  File zip = SD.open(TEMP_ZIP_PATH, FILE_WRITE);
  if (!zip) {
    error = "Unable to create backup file on SD";
    return false;
  }

  std::vector<ZipWriteEntry> catalog;
  const String manifest = manifestJson();
  if (!writeStoredZipEntry(zip, catalog, "renzfi-manifest.json",
                           reinterpret_cast<const uint8_t *>(manifest.c_str()),
                           manifest.length())) {
    zip.close();
    SD.remove(TEMP_ZIP_PATH);
    error = "Unable to write backup manifest";
    return false;
  }

  for (const BackupEntry &entry : kJsonEntries) {
    String content;
    if (!_storage->readSdText(entry.sdPath, content) || content.isEmpty()) {
      content = entry.defaultJson;
    }
    if (!writeStoredZipEntry(
            zip, catalog, entry.archivePath,
            reinterpret_cast<const uint8_t *>(content.c_str()),
            content.length())) {
      zip.close();
      SD.remove(TEMP_ZIP_PATH);
      error = String("Unable to archive ") + entry.archivePath;
      return false;
    }
  }

  for (const BackupEntry &entry : kAssetEntries) {
    if (!SD.exists(entry.sdPath)) continue;
    File asset = SD.open(entry.sdPath, FILE_READ);
    if (!asset) continue;
    const size_t size = asset.size();
    std::vector<uint8_t> buf(size);
    size_t read = asset.read(buf.data(), size);
    asset.close();
    if (read != size) continue;
    if (!writeStoredZipEntry(zip, catalog, entry.archivePath, buf.data(),
                             read)) {
      zip.close();
      SD.remove(TEMP_ZIP_PATH);
      error = String("Unable to archive ") + entry.archivePath;
      return false;
    }
  }

  if (!finalizeStoredZip(zip, catalog)) {
    zip.close();
    SD.remove(TEMP_ZIP_PATH);
    error = "Unable to finalize backup archive";
    return false;
  }
  zip.close();
  return true;
}

bool BackupManager::createJsonBackup(String &error) {
  if (!ensureBackupDir()) {
    error = "SD Card is not available";
    return false;
  }

  DynamicJsonDocument doc(128 * 1024);
  doc["backupVersion"]   = BACKUP_VERSION;
  doc["firmwareVersion"] = RenzFiConfig::FIRMWARE_VERSION;
  doc["exportedAt"]        = isoTimestamp();

  JsonObject files = doc["files"].to<JsonObject>();
  for (const BackupEntry &entry : kJsonEntries) {
    String content;
    if (!_storage->readSdText(entry.sdPath, content) || content.isEmpty()) {
      content = entry.defaultJson;
    }
    DynamicJsonDocument parsed(8192);
    if (deserializeJson(parsed, content) == DeserializationError::Ok) {
      files[entry.archivePath].set(parsed.as<JsonVariantConst>());
    } else {
      files[entry.archivePath] = content;
    }
  }

  JsonObject assets = doc["assets"].to<JsonObject>();
  for (const BackupEntry &entry : kAssetEntries) {
    if (!SD.exists(entry.sdPath)) continue;
    File asset = SD.open(entry.sdPath, FILE_READ);
    if (!asset) continue;
    const size_t size = asset.size();
    if (size == 0 || size > RenzFiConfig::PORTAL_MUSIC_MAX_BYTES) {
      asset.close();
      continue;
    }
    std::vector<uint8_t> buf(size);
    if (asset.read(buf.data(), size) != size) {
      asset.close();
      continue;
    }
    asset.close();
    assets[entry.archivePath] = String("present");
    assets[String(entry.archivePath) + "_size"] = size;
  }

  if (SD.exists(TEMP_JSON_PATH)) SD.remove(TEMP_JSON_PATH);
  File out = SD.open(TEMP_JSON_PATH, FILE_WRITE);
  if (!out) {
    error = "Unable to create JSON backup on SD";
    return false;
  }
  if (serializeJson(doc, out) == 0) {
    out.close();
    SD.remove(TEMP_JSON_PATH);
    error = "Unable to serialize JSON backup";
    return false;
  }
  out.close();
  return true;
}

bool BackupManager::createBackup(String &outPath, bool &useZip, String &error) {
  useZip = true;
  if (createZipBackup(error)) {
    outPath = TEMP_ZIP_PATH;
    return true;
  }
  Serial.printf("[backup] ZIP export failed (%s), falling back to JSON\n",
                error.c_str());
  useZip = false;
  if (createJsonBackup(error)) {
    outPath = TEMP_JSON_PATH;
    return true;
  }
  return false;
}

bool BackupManager::validateManifest(JsonObjectConst manifest, String &error) {
  if (manifest.isNull()) {
    error = "Missing backup manifest";
    return false;
  }
  const int version = manifest["backupVersion"] | 0;
  if (version != BACKUP_VERSION) {
    error = "Unsupported backup version";
    return false;
  }
  const char *exportedAt = manifest["exportedAt"] | "";
  if (strlen(exportedAt) == 0) {
    error = "Missing exportedAt";
    return false;
  }
  return true;
}

bool BackupManager::writeArchiveEntryToSd(const char *archivePath,
                                          const uint8_t *data, size_t len,
                                          String &error) {
  String sdPath;
  if (!mapArchivePathToSd(archivePath, sdPath)) return true;

  if (len == 0) {
    if (sdPath.endsWith(".json")) {
      for (const BackupEntry &entry : kJsonEntries) {
        if (strcmp(entry.sdPath, sdPath.c_str()) == 0) {
          return _storage->writeSdText(sdPath.c_str(), entry.defaultJson);
        }
      }
    }
    return true;
  }

  if (sdPath.endsWith(".webp") || sdPath.endsWith(".mp3")) {
    return _storage->writeBinary(sdPath.c_str(), data, len);
  }

  String content;
  content.reserve(len + 1);
  for (size_t i = 0; i < len; i++) content += static_cast<char>(data[i]);
  return _storage->writeSdText(sdPath.c_str(), content);
}

bool BackupManager::restoreFromZip(const char *zipPath, String &error) {
  if (!isSdAvailable()) {
    error = "SD Card is not available";
    return false;
  }

  File zip = SD.open(zipPath, FILE_READ);
  if (!zip) {
    error = "Unable to open backup archive";
    return false;
  }

  bool manifestOk = false;
  DynamicJsonDocument manifestDoc(512);

  while (zip.available()) {
    uint8_t header[30];
    if (zip.read(header, 30) != 30) break;
    if (readLE32(header) != 0x04034b50) break;

    const uint16_t method   = readLE16(header + 8);
    const uint32_t compSize = readLE32(header + 18);
    const uint16_t nameLen  = readLE16(header + 26);
    const uint16_t extraLen = readLE16(header + 28);

    String name;
    name.reserve(nameLen + 1);
    for (uint16_t i = 0; i < nameLen; i++) {
      if (!zip.available()) break;
      name += static_cast<char>(zip.read());
    }
    if (extraLen > 0) zip.seek(zip.position() + extraLen);

    if (method != 0) {
      zip.close();
      error = "Compressed backup entries are not supported";
      return false;
    }

    std::vector<uint8_t> payload(compSize);
    if (compSize > 0 && zip.read(payload.data(), compSize) != compSize) {
      zip.close();
      error = "Truncated backup archive";
      return false;
    }

    if (name.equalsIgnoreCase("renzfi-manifest.json")) {
      if (deserializeJson(manifestDoc, payload.data(), compSize) !=
          DeserializationError::Ok) {
        zip.close();
        error = "Invalid backup manifest";
        return false;
      }
      manifestOk = validateManifest(manifestDoc.as<JsonObjectConst>(), error);
      if (!manifestOk) {
        zip.close();
        return false;
      }
      continue;
    }

    if (!writeArchiveEntryToSd(name.c_str(), payload.data(), compSize, error)) {
      zip.close();
      error = String("Unable to restore ") + name;
      return false;
    }
  }

  zip.close();
  if (!manifestOk) {
    error = "Missing backup manifest";
    return false;
  }
  return true;
}

bool BackupManager::restoreFromJsonFile(const char *jsonPath, String &error) {
  String raw;
  if (!_storage->readSdText(jsonPath, raw)) {
    error = "Unable to read backup file";
    return false;
  }

  DynamicJsonDocument doc(128 * 1024);
  if (deserializeJson(doc, raw) != DeserializationError::Ok) {
    error = "Malformed backup JSON";
    return false;
  }

  if (!validateManifest(doc.as<JsonObjectConst>(), error)) return false;

  JsonObjectConst files = doc["files"].as<JsonObjectConst>();
  if (files.isNull()) {
    error = "Malformed backup JSON";
    return false;
  }

  for (JsonPairConst kv : files) {
    String sdPath;
    if (!mapArchivePathToSd(kv.key().c_str(), sdPath)) continue;
    String content;
    if (kv.value().is<const char *>()) {
      content = kv.value().as<const char *>();
    } else {
      serializeJson(kv.value(), content);
    }
    if (!_storage->writeSdText(sdPath.c_str(), content)) {
      error = String("Unable to restore ") + kv.key().c_str();
      return false;
    }
  }

  return true;
}

bool BackupManager::restoreFromFile(const char *uploadPath, String &error) {
  if (!uploadPath || !SD.exists(uploadPath)) {
    error = "Backup file not found";
    return false;
  }

  File probe = SD.open(uploadPath, FILE_READ);
  if (!probe) {
    error = "Unable to open uploaded backup";
    return false;
  }
  uint8_t sig[2] = {0, 0};
  if (probe.available() >= 2) probe.read(sig, 2);
  probe.close();

  if (sig[0] == 'P' && sig[1] == 'K') {
    return restoreFromZip(uploadPath, error);
  }
  return restoreFromJsonFile(uploadPath, error);
}

bool BackupManager::wipeUserData(String &error) {
  (void)error;
  if (_portalConfig) {
    _portalConfig->deleteBanner();
    _portalConfig->deleteMusic();
  }

  if (_storage) {
    _storage->clearAllFallbackData();
    if (_storage->isSdMounted()) {
      _storage->deleteSdOnly(RenzFiConfig::SETTINGS_FILE);
      _storage->deleteSdOnly(RenzFiConfig::ROUTER_FILE);
      _storage->deleteSdOnly(RenzFiConfig::PORTAL_CONFIG_FILE);
      _storage->deleteSdOnly(RenzFiConfig::PROMOS_FILE);
      _storage->deleteSdOnly(RenzFiConfig::VOUCHERS_FILE);
      _storage->deleteSdOnly(RenzFiConfig::SALES_FILE);
      _storage->deleteSdOnly(RenzFiConfig::USERS_FILE);
      _storage->deleteSdOnly(RenzFiConfig::LOGS_FILE);
      _storage->deleteSdOnly(RenzFiConfig::PORTAL_SESSIONS_FILE);
      _storage->deleteSdOnly(RenzFiConfig::ADMIN_SESSIONS_FILE);
      _storage->deleteSdOnly(RenzFiConfig::PORTAL_BANNER_SD);
      _storage->deleteSdOnly(RenzFiConfig::PORTAL_MUSIC_SD);
      _storage->factoryResetData();
    }
  }

  if (_auth) _auth->resetToDefault();
  if (_portalConfig) _portalConfig->loadMeta();
  return true;
}

bool BackupManager::performFactoryReset(String &error) {
  (void)error;
  if (_logger) _logger->info("system", "factory reset started");
  Serial.println("[system] factory reset started");

  if (!wipeUserData(error)) return false;

  if (_logger) _logger->info("system", "factory reset completed");
  Serial.println("[system] factory reset completed");
  return true;
}
