#include "BackupManager.h"

#include "InstallationStateManager.h"
#include "StoragePaths.h"

#include <SD.h>
#include <SPIFFS.h>
#include <cstring>
#include <time.h>
#include <vector>

namespace {

constexpr size_t kIoBytes = 4096;
constexpr size_t kMaxArchiveBytes = 3U * 1024U * 1024U;
constexpr size_t kMaxJsonBackupBytes = 128U * 1024U;
constexpr size_t kMaxJsonEntryBytes = 1024U * 1024U;
constexpr size_t kMaxEntries = 1 + 7 + 2;

constexpr const char *kDefaultSettings =
    "{\"admin\":{\"passwordHash\":\"\",\"mustChangePassword\":true,"
    "\"firstBootCompleted\":false},\"network\":{\"ip\":\"10.40.0.2\","
    "\"gateway\":\"10.40.0.1\",\"subnet\":\"255.255.255.0\","
    "\"dns\":\"10.40.0.1\"},\"coin\":{\"pulsesPerPeso\":1,\"pesoPerPulse\":1,"
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

struct BackupEntry {
  const char *archivePath;
  const char *sdPath;
  const char *defaultJson;
  bool arrayRoot;
  size_t maxBytes;
};

constexpr BackupEntry kJsonEntries[] = {
    {"/config/settings.json", RenzFiConfig::SETTINGS_FILE, kDefaultSettings,
     false, kMaxJsonEntryBytes},
    {"/config/router.json", RenzFiConfig::ROUTER_FILE, kDefaultRouter, false,
     kMaxJsonEntryBytes},
    {"/config/portal-config.json", RenzFiConfig::PORTAL_CONFIG_FILE,
     kDefaultPortalConfig, false, kMaxJsonEntryBytes},
    {"/config/promos.json", RenzFiConfig::PROMOS_FILE, kDefaultPromos, true,
     kMaxJsonEntryBytes},
    {"/config/vouchers.json", RenzFiConfig::VOUCHERS_FILE, "[]", true,
     kMaxJsonEntryBytes},
    {"/config/sales.json", RenzFiConfig::SALES_FILE, "[]", true,
     kMaxJsonEntryBytes},
    {"/config/users.json", RenzFiConfig::USERS_FILE, "[]", true,
     kMaxJsonEntryBytes},
};

constexpr BackupEntry kAssetEntries[] = {
    {"/assets/banner.webp", RenzFiConfig::PORTAL_BANNER_SD, nullptr, false,
     200U * 1024U},
    {"/assets/bg-music.mp3", RenzFiConfig::PORTAL_MUSIC_SD, nullptr, false,
     RenzFiConfig::PORTAL_MUSIC_MAX_BYTES},
};

uint16_t readLE16(const uint8_t *p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t readLE32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) |
                               (p[3] << 24));
}
void writeLE16(File &f, uint16_t v) {
  f.write(static_cast<uint8_t>(v));
  f.write(static_cast<uint8_t>(v >> 8));
}
void writeLE32(File &f, uint32_t v) {
  f.write(static_cast<uint8_t>(v));
  f.write(static_cast<uint8_t>(v >> 8));
  f.write(static_cast<uint8_t>(v >> 16));
  f.write(static_cast<uint8_t>(v >> 24));
}
uint32_t crc32Update(uint32_t crc, const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^
            (0xEDB88320U & static_cast<uint32_t>(-(crc & 1U)));
    }
  }
  return crc;
}

String isoTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return String(millis());
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buf);
}

String verifiedWallClockTimestamp() {
  const time_t now = time(nullptr);
  if (now < 1700000000) return String();
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buf);
}

String manifestJson() {
  DynamicJsonDocument doc(256);
  doc["backupVersion"] = BackupManager::BACKUP_VERSION;
  doc["firmwareVersion"] = RenzFiConfig::FIRMWARE_VERSION;
  doc["exportedAt"] = isoTimestamp();
  String out;
  serializeJson(doc, out);
  return out;
}

const BackupEntry *findEntry(const String &archivePath, bool *asset = nullptr) {
  String normalized = archivePath;
  if (!normalized.startsWith("/")) normalized = "/" + normalized;
  for (const BackupEntry &entry : kJsonEntries) {
    if (normalized.equalsIgnoreCase(entry.archivePath)) {
      if (asset) *asset = false;
      return &entry;
    }
  }
  for (const BackupEntry &entry : kAssetEntries) {
    if (normalized.equalsIgnoreCase(entry.archivePath)) {
      if (asset) *asset = true;
      return &entry;
    }
  }
  return nullptr;
}

int jsonEntryIndex(const String &archivePath) {
  String normalized = archivePath;
  if (!normalized.startsWith("/")) normalized = "/" + normalized;
  for (size_t i = 0; i < sizeof(kJsonEntries) / sizeof(kJsonEntries[0]); ++i) {
    if (normalized.equalsIgnoreCase(kJsonEntries[i].archivePath)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

struct ZipWriteEntry {
  String name;
  uint32_t offset = 0;
  uint32_t crc = 0;
  uint32_t size = 0;
};

bool writeLocalHeader(File &zip, ZipWriteEntry &entry) {
  entry.offset = zip.position();
  zip.write(reinterpret_cast<const uint8_t *>("\x50\x4b\x03\x04"), 4);
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
  return zip.write(reinterpret_cast<const uint8_t *>(entry.name.c_str()),
                   entry.name.length()) == entry.name.length();
}

bool writeBytesEntry(File &zip, std::vector<ZipWriteEntry> &catalog,
                     const char *name, const uint8_t *data, size_t len) {
  ZipWriteEntry entry;
  entry.name = name;
  entry.size = len;
  entry.crc = crc32Update(0xFFFFFFFFU, data, len) ^ 0xFFFFFFFFU;
  if (!writeLocalHeader(zip, entry) ||
      (len && zip.write(data, len) != len)) {
    return false;
  }
  catalog.push_back(entry);
  return true;
}

bool writeFileEntry(File &zip, std::vector<ZipWriteEntry> &catalog,
                    const char *name, File &source) {
  ZipWriteEntry entry;
  entry.name = name;
  entry.size = source.size();
  uint8_t buffer[kIoBytes];
  uint32_t crc = 0xFFFFFFFFU;
  while (source.available()) {
    const size_t got = source.read(buffer, sizeof(buffer));
    if (!got) return false;
    crc = crc32Update(crc, buffer, got);
  }
  entry.crc = crc ^ 0xFFFFFFFFU;
  if (!source.seek(0) || !writeLocalHeader(zip, entry)) return false;
  uint32_t copied = 0;
  while (source.available()) {
    const size_t got = source.read(buffer, sizeof(buffer));
    if (!got || zip.write(buffer, got) != got) return false;
    copied += got;
  }
  if (copied != entry.size) return false;
  catalog.push_back(entry);
  return true;
}

bool finalizeZip(File &zip, const std::vector<ZipWriteEntry> &catalog) {
  const uint32_t centralStart = zip.position();
  for (const ZipWriteEntry &entry : catalog) {
    zip.write(reinterpret_cast<const uint8_t *>("\x50\x4b\x01\x02"), 4);
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
    if (zip.write(reinterpret_cast<const uint8_t *>(entry.name.c_str()),
                  entry.name.length()) != entry.name.length()) {
      return false;
    }
  }
  const uint32_t centralSize = zip.position() - centralStart;
  zip.write(reinterpret_cast<const uint8_t *>("\x50\x4b\x05\x06"), 4);
  writeLE16(zip, 0);
  writeLE16(zip, 0);
  writeLE16(zip, catalog.size());
  writeLE16(zip, catalog.size());
  writeLE32(zip, centralSize);
  writeLE32(zip, centralStart);
  writeLE16(zip, 0);
  return true;
}

struct RestoreEntry {
  String name;
  String livePath;
  String stagePath;
  String backupPath;
  uint32_t localOffset = 0;
  uint32_t dataOffset = 0;
  uint32_t size = 0;
  uint32_t crc = 0;
  bool asset = false;
  bool arrayRoot = false;
  bool hadOriginal = false;
};

void cleanupRestoreFiles(const std::vector<RestoreEntry> &entries) {
  for (const RestoreEntry &entry : entries) {
    SD.remove(entry.stagePath);
    SD.remove(entry.backupPath);
  }
}

bool hasRestoreArtifacts() {
  for (size_t i = 0; i < kMaxEntries; ++i) {
    char suffix[8];
    snprintf(suffix, sizeof(suffix), "%02u", unsigned(i));
    if (SD.exists((String("/backup/restore-stage-") + suffix).c_str()) ||
        SD.exists((String("/backup/restore-live-") + suffix + ".bak").c_str())) {
      return true;
    }
  }
  return false;
}

bool serializeJournal(const char *path,
                      const std::vector<RestoreEntry> &entries,
                      const char *state) {
  SD.remove(path);
  File file = SD.open(path, FILE_WRITE);
  if (!file) return false;
  DynamicJsonDocument doc(4096);
  doc["journalVersion"] = 1;
  doc["state"] = state;
  JsonArray list = doc["entries"].to<JsonArray>();
  for (const RestoreEntry &entry : entries) {
    JsonObject item = list.add<JsonObject>();
    item["name"] = entry.name;
    item["live"] = entry.livePath;
    item["stage"] = entry.stagePath;
    item["backup"] = entry.backupPath;
    item["hadOriginal"] = entry.hadOriginal;
    item["rollbackAction"] =
        entry.hadOriginal ? "restore_backup" : "remove_created";
  }
  const bool ok = serializeJson(doc, file) > 0;
  file.flush();
  file.close();
  return ok;
}

bool loadJournalCandidate(const char *path,
                          std::vector<RestoreEntry> &entries,
                          bool *commitComplete = nullptr) {
  if (!SD.exists(path)) return false;
  File file = SD.open(path, FILE_READ);
  if (!file || file.size() == 0 || file.size() > 8192) {
    if (file) file.close();
    return false;
  }
  DynamicJsonDocument doc(4096);
  const bool parsed =
      deserializeJson(doc, file) == DeserializationError::Ok;
  file.close();
  const char *state = doc["state"] | "";
  const bool complete = strcmp(state, "commit_complete") == 0;
  const bool rollbackComplete = strcmp(state, "rollback_complete") == 0;
  if (!parsed || (doc["journalVersion"] | 0) != 1 ||
      (!complete && !rollbackComplete &&
       strcmp(state, "commit_pending") != 0) ||
      !doc["entries"].is<JsonArray>()) {
    return false;
  }
  JsonArrayConst list = doc["entries"].as<JsonArrayConst>();
  if (list.size() < sizeof(kJsonEntries) / sizeof(kJsonEntries[0]) ||
      list.size() > kMaxEntries) {
    return false;
  }
  std::vector<RestoreEntry> loaded;
  loaded.reserve(list.size());
  bool required[sizeof(kJsonEntries) / sizeof(kJsonEntries[0])] = {};
  size_t index = 0;
  for (JsonObjectConst item : list) {
    RestoreEntry entry;
    entry.name = item["name"] | "";
    entry.livePath = item["live"] | "";
    entry.stagePath = item["stage"] | "";
    entry.backupPath = item["backup"] | "";
    if (!item["hadOriginal"].is<bool>() ||
        !item["rollbackAction"].is<const char *>()) {
      return false;
    }
    entry.hadOriginal = item["hadOriginal"].as<bool>();
    bool asset = false;
    const BackupEntry *known = findEntry(entry.name, &asset);
    char suffix[8];
    snprintf(suffix, sizeof(suffix), "%02u", unsigned(index));
    const String expectedStage = String("/backup/restore-stage-") + suffix;
    const String expectedBackup =
        String("/backup/restore-live-") + suffix + ".bak";
    const char *expectedAction =
        entry.hadOriginal ? "restore_backup" : "remove_created";
    if (!known || entry.livePath != known->sdPath ||
        entry.stagePath != expectedStage ||
        entry.backupPath != expectedBackup ||
        strcmp(item["rollbackAction"] | "", expectedAction) != 0) {
      return false;
    }
    const int requiredIndex = jsonEntryIndex(entry.name);
    if (requiredIndex >= 0) {
      if (required[requiredIndex]) return false;
      required[requiredIndex] = true;
    }
    for (const RestoreEntry &prior : loaded) {
      if (entry.name.equalsIgnoreCase(prior.name)) return false;
    }
    loaded.push_back(entry);
    ++index;
  }
  for (bool present : required) {
    if (!present) return false;
  }
  entries = loaded;
  if (commitComplete) *commitComplete = complete || rollbackComplete;
  return true;
}

bool writeJournal(const std::vector<RestoreEntry> &entries) {
  if (SD.exists(BackupManager::RESTORE_JOURNAL_PATH) ||
      SD.exists(BackupManager::RESTORE_JOURNAL_STAGE_PATH) ||
      SD.exists(BackupManager::RESTORE_JOURNAL_BACKUP_PATH)) {
    return false;
  }
  if (!serializeJournal(BackupManager::RESTORE_JOURNAL_STAGE_PATH, entries,
                        "commit_pending")) {
    return false;
  }
  std::vector<RestoreEntry> verified;
  if (!loadJournalCandidate(BackupManager::RESTORE_JOURNAL_STAGE_PATH,
                            verified) ||
      verified.size() != entries.size()) {
    return false;
  }
  if (SD.exists(BackupManager::RESTORE_JOURNAL_PATH) &&
      !SD.rename(BackupManager::RESTORE_JOURNAL_PATH,
                 BackupManager::RESTORE_JOURNAL_BACKUP_PATH)) {
    return false;
  }
  if (!SD.rename(BackupManager::RESTORE_JOURNAL_STAGE_PATH,
                 BackupManager::RESTORE_JOURNAL_PATH)) {
    return false;
  }
  verified.clear();
  return loadJournalCandidate(BackupManager::RESTORE_JOURNAL_PATH, verified) &&
         verified.size() == entries.size();
}

bool markJournalState(const std::vector<RestoreEntry> &entries,
                      const char *state) {
  SD.remove(BackupManager::RESTORE_JOURNAL_STAGE_PATH);
  if (!serializeJournal(BackupManager::RESTORE_JOURNAL_STAGE_PATH, entries,
                        state)) {
    return false;
  }
  std::vector<RestoreEntry> verified;
  bool complete = false;
  if (!loadJournalCandidate(BackupManager::RESTORE_JOURNAL_STAGE_PATH,
                            verified, &complete) ||
      !complete || verified.size() != entries.size()) {
    return false;
  }
  SD.remove(BackupManager::RESTORE_JOURNAL_BACKUP_PATH);
  if (!SD.rename(BackupManager::RESTORE_JOURNAL_PATH,
                 BackupManager::RESTORE_JOURNAL_BACKUP_PATH) ||
      !SD.rename(BackupManager::RESTORE_JOURNAL_STAGE_PATH,
                 BackupManager::RESTORE_JOURNAL_PATH)) {
    return false;
  }
  verified.clear();
  complete = false;
  return loadJournalCandidate(BackupManager::RESTORE_JOURNAL_PATH, verified,
                              &complete) &&
         complete && verified.size() == entries.size();
}

bool finalizeCommittedRestore(const std::vector<RestoreEntry> &entries) {
  for (const RestoreEntry &entry : entries) {
    if (SD.exists(entry.stagePath) && !SD.remove(entry.stagePath)) return false;
    if (SD.exists(entry.backupPath) && !SD.remove(entry.backupPath)) return false;
  }
  if (SD.exists(BackupManager::RESTORE_JOURNAL_STAGE_PATH) &&
      !SD.remove(BackupManager::RESTORE_JOURNAL_STAGE_PATH)) {
    return false;
  }
  if (SD.exists(BackupManager::RESTORE_JOURNAL_BACKUP_PATH) &&
      !SD.remove(BackupManager::RESTORE_JOURNAL_BACKUP_PATH)) {
    return false;
  }
  return !SD.exists(BackupManager::RESTORE_JOURNAL_PATH) ||
         SD.remove(BackupManager::RESTORE_JOURNAL_PATH);
}

bool copyFile(const String &sourcePath, const String &targetPath) {
  File source = SD.open(sourcePath, FILE_READ);
  const String stagePath = targetPath + ".restore";
  SD.remove(stagePath);
  File target = SD.open(stagePath, FILE_WRITE);
  if (!source || !target) {
    if (source) source.close();
    if (target) target.close();
    return false;
  }
  uint8_t buffer[kIoBytes];
  size_t copied = 0;
  bool ok = true;
  while (source.available()) {
    const size_t got = source.read(buffer, sizeof(buffer));
    if (!got || target.write(buffer, got) != got) {
      ok = false;
      break;
    }
    copied += got;
  }
  const size_t expected = source.size();
  source.close();
  target.flush();
  target.close();
  if (!ok || copied != expected) return false;
  File verify = SD.open(stagePath, FILE_READ);
  const bool sizeOk = verify && verify.size() == expected;
  if (verify) verify.close();
  if (!sizeOk || (SD.exists(targetPath) && !SD.remove(targetPath)) ||
      !SD.rename(stagePath, targetPath)) {
    return false;
  }
  return true;
}

bool rollbackRestore(const std::vector<RestoreEntry> &entries) {
  for (size_t i = entries.size(); i > 0; --i) {
    const RestoreEntry &entry = entries[i - 1];
    const bool stageExists = SD.exists(entry.stagePath);
    const bool backupExists = SD.exists(entry.backupPath);
    const bool liveExists = SD.exists(entry.livePath);
    if (entry.hadOriginal) {
      if (backupExists) {
        if (!copyFile(entry.backupPath, entry.livePath)) return false;
      } else if (!stageExists || !liveExists) {
        return false;
      }
    } else if (!stageExists) {
      if (liveExists && !SD.remove(entry.livePath)) return false;
    } else if (liveExists) {
      return false;
    }
  }
  return markJournalState(entries, "rollback_complete") &&
         finalizeCommittedRestore(entries);
}

bool commitRestore(const std::vector<RestoreEntry> &entries, String &error) {
  if (!writeJournal(entries)) {
    error = "Unable to create restore journal";
    return false;
  }
  for (const RestoreEntry &entry : entries) {
    SD.remove(entry.backupPath);
    if (SD.exists(entry.livePath) &&
        !SD.rename(entry.livePath, entry.backupPath)) {
      error = String("Unable to preserve ") + entry.name;
      if (!rollbackRestore(entries)) error += "; rollback incomplete";
      return false;
    }
    if (!SD.rename(entry.stagePath, entry.livePath)) {
      error = String("Unable to commit ") + entry.name;
      if (!rollbackRestore(entries)) error += "; rollback incomplete";
      return false;
    }
  }
  if (!markJournalState(entries, "commit_complete")) {
    error = "Unable to finalize restore journal";
    return false;
  }
  if (!finalizeCommittedRestore(entries)) {
    error = "Restore committed but cleanup is incomplete";
    return false;
  }
  return true;
}

bool validateAssetSignature(File &file, const RestoreEntry &entry) {
  uint8_t sig[16] = {0};
  const size_t got = file.read(sig, sizeof(sig));
  file.seek(0);
  if (entry.name.endsWith(".webp")) {
    return got >= 12 && memcmp(sig, "RIFF", 4) == 0 &&
           memcmp(sig + 8, "WEBP", 4) == 0;
  }
  if (entry.name.endsWith(".mp3")) {
    return got >= 3 &&
           (memcmp(sig, "ID3", 3) == 0 ||
            (sig[0] == 0xFF && (sig[1] & 0xE0) == 0xE0));
  }
  return false;
}

bool validateStagedEntry(const RestoreEntry &entry, String &error) {
  File file = SD.open(entry.stagePath, FILE_READ);
  if (!file) {
    error = String("Unable to validate ") + entry.name;
    return false;
  }
  if (entry.asset) {
    const bool ok = validateAssetSignature(file, entry);
    file.close();
    if (!ok) error = String("Invalid asset signature: ") + entry.name;
    return ok;
  }
  int first = -1;
  while (file.available()) {
    first = file.read();
    if (first != ' ' && first != '\t' && first != '\r' && first != '\n') break;
  }
  const bool rootOk =
      entry.arrayRoot ? first == '[' : first == '{';
  file.seek(0);
  DynamicJsonDocument doc(64);
  DynamicJsonDocument filter(32);
  filter.set(false);
  const DeserializationError parsed =
      deserializeJson(doc, file, DeserializationOption::Filter(filter));
  file.close();
  if (!rootOk || parsed != DeserializationError::Ok) {
    error = String("Invalid JSON schema: ") + entry.name;
    return false;
  }
  return true;
}

bool validateJsonBackupKeys(File &file, String &error) {
  if (!file.seek(0)) return false;
  int objectDepth = 0;
  int filesDepth = -1;
  int filesKeyCount = 0;
  bool inString = false;
  bool escaped = false;
  bool pendingString = false;
  bool waitingFilesObject = false;
  int pendingDepth = 0;
  String token;
  bool required[sizeof(kJsonEntries) / sizeof(kJsonEntries[0])] = {};
  size_t requiredCount = 0;
  while (file.available()) {
    const char c = static_cast<char>(file.read());
    if (inString) {
      if (escaped) {
        if (token.length() < 96) token += c;
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        inString = false;
        pendingString = true;
        pendingDepth = objectDepth;
      } else if (token.length() < 96) {
        token += c;
      }
      continue;
    }
    if (c == '"') {
      inString = true;
      token = "";
      pendingString = false;
      continue;
    }
    if (c == ':' && pendingString) {
      if (pendingDepth == 1 && token == "files") {
        ++filesKeyCount;
        waitingFilesObject = true;
      } else if (filesDepth > 0 && pendingDepth == filesDepth) {
        const int index = jsonEntryIndex(token);
        if (index < 0 || required[index]) {
          error = "Duplicate or unknown JSON backup path";
          file.seek(0);
          return false;
        }
        required[index] = true;
        ++requiredCount;
      }
      pendingString = false;
      continue;
    }
    if (c == '{') {
      if (waitingFilesObject && filesKeyCount == 1 && filesDepth < 0) {
        filesDepth = objectDepth + 1;
        waitingFilesObject = false;
      }
      ++objectDepth;
    } else if (c == '}') {
      if (objectDepth == filesDepth) filesDepth = -2;
      --objectDepth;
    }
    if (waitingFilesObject && c != ' ' && c != '\t' && c != '\r' &&
        c != '\n' && c != '{') {
      error = "JSON backup files member must be an object";
      file.seek(0);
      return false;
    }
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
      pendingString = false;
    }
  }
  file.seek(0);
  if (inString || objectDepth != 0 || filesKeyCount != 1 ||
      requiredCount != sizeof(kJsonEntries) / sizeof(kJsonEntries[0])) {
    error = "Backup must contain each required configuration entry exactly once";
    return false;
  }
  return true;
}

}  // namespace

void BackupManager::begin(StorageManager *storage, Logger *logger,
                          AuthManager *auth,
                          PortalConfigManager *portalConfig,
                          AssetManager *assets,
                          InstallationStateManager *installation) {
  _storage = storage;
  _logger = logger;
  _auth = auth;
  _portalConfig = portalConfig;
  _assets = assets;
  _installation = installation;
  String recoveryError;
  if (!recoverPendingRestore(_storage, recoveryError) && _storage) {
    _storage->markDegraded(String("Restore recovery failed: ") + recoveryError);
  }
}

bool BackupManager::isSdAvailable() const {
  return _storage && _storage->isSdMounted() && _storage->healthy() &&
         _storage->sdIoAllowed() && !_storage->sdRecoveryInProgress();
}

const String &BackupManager::lastSuccessfulBackup() const {
  return _lastSuccessfulBackup;
}

bool BackupManager::hasSuccessfulBackup() const {
  return _hasSuccessfulBackup;
}

uint32_t BackupManager::lastSuccessfulBackupAgeSeconds() const {
  if (!_hasSuccessfulBackup) return 0;
  return (millis() - _lastSuccessfulBackupMs) / 1000U;
}

bool BackupManager::ensureBackupDir() {
  if (!isSdAvailable()) return false;
  return SD.exists("/backup") || SD.mkdir("/backup");
}

bool BackupManager::mapArchivePathToSd(const char *archivePath, String &sdPath) {
  if (!archivePath) return false;
  const BackupEntry *entry = findEntry(String(archivePath));
  if (!entry) return false;
  sdPath = entry->sdPath;
  return true;
}

bool BackupManager::createZipBackup(String &error) {
  if (!ensureBackupDir()) {
    error = "SD Card is not available";
    return false;
  }
  SD.remove(TEMP_ZIP_PATH);
  File zip = SD.open(TEMP_ZIP_PATH, FILE_WRITE);
  if (!zip) {
    error = "Unable to create backup file on SD";
    return false;
  }
  std::vector<ZipWriteEntry> catalog;
  catalog.reserve(kMaxEntries);
  const String manifest = manifestJson();
  bool ok = writeBytesEntry(zip, catalog, "renzfi-manifest.json",
                            reinterpret_cast<const uint8_t *>(manifest.c_str()),
                            manifest.length());
  for (const BackupEntry &entry : kJsonEntries) {
    if (!ok) break;
    File source = SD.open(entry.sdPath, FILE_READ);
    if (source && source.size() > 0) {
      ok = source.size() <= entry.maxBytes &&
           writeFileEntry(zip, catalog, entry.archivePath, source);
      source.close();
    } else {
      if (source) source.close();
      ok = writeBytesEntry(
          zip, catalog, entry.archivePath,
          reinterpret_cast<const uint8_t *>(entry.defaultJson),
          strlen(entry.defaultJson));
    }
  }
  for (const BackupEntry &entry : kAssetEntries) {
    if (!ok || !SD.exists(entry.sdPath)) continue;
    File source = SD.open(entry.sdPath, FILE_READ);
    if (!source) continue;
    if (source.size() == 0 || source.size() > entry.maxBytes) {
      source.close();
      continue;
    }
    ok = writeFileEntry(zip, catalog, entry.archivePath, source);
    source.close();
  }
  if (ok) ok = finalizeZip(zip, catalog);
  zip.flush();
  zip.close();
  if (!ok) {
    SD.remove(TEMP_ZIP_PATH);
    error = "Unable to stream backup archive";
  }
  return ok;
}

bool BackupManager::createJsonBackup(String &error) {
  if (!ensureBackupDir()) {
    error = "SD Card is not available";
    return false;
  }
  DynamicJsonDocument doc(128U * 1024U);
  doc["backupVersion"] = BACKUP_VERSION;
  doc["firmwareVersion"] = RenzFiConfig::FIRMWARE_VERSION;
  doc["exportedAt"] = isoTimestamp();
  JsonObject files = doc["files"].to<JsonObject>();
  for (const BackupEntry &entry : kJsonEntries) {
    File source = SD.open(entry.sdPath, FILE_READ);
    DynamicJsonDocument parsed(8192);
    DeserializationError result = source
                                      ? deserializeJson(parsed, source)
                                      : deserializeJson(parsed, entry.defaultJson);
    if (source) source.close();
    if (result != DeserializationError::Ok) {
      deserializeJson(parsed, entry.defaultJson);
    }
    files[entry.archivePath].set(parsed.as<JsonVariantConst>());
  }
  JsonObject assets = doc["assets"].to<JsonObject>();
  for (const BackupEntry &entry : kAssetEntries) {
    File source = SD.open(entry.sdPath, FILE_READ);
    if (!source) continue;
    const size_t size = source.size();
    source.close();
    if (size == 0 || size > entry.maxBytes) continue;
    assets[entry.archivePath] = "present";
    assets[String(entry.archivePath) + "_size"] = size;
  }
  SD.remove(TEMP_JSON_PATH);
  File out = SD.open(TEMP_JSON_PATH, FILE_WRITE);
  if (!out || serializeJson(doc, out) == 0) {
    if (out) out.close();
    SD.remove(TEMP_JSON_PATH);
    error = "Unable to create JSON backup on SD";
    return false;
  }
  out.close();
  return true;
}

bool BackupManager::createBackup(String &outPath, bool &useZip, String &error) {
  useZip = true;
  if (createZipBackup(error)) {
    outPath = TEMP_ZIP_PATH;
    _hasSuccessfulBackup = true;
    _lastSuccessfulBackupMs = millis();
    _lastSuccessfulBackup = verifiedWallClockTimestamp();
    return true;
  }
  Serial.printf("[backup] ZIP export failed (%s), falling back to JSON\n",
                error.c_str());
  useZip = false;
  if (createJsonBackup(error)) {
    outPath = TEMP_JSON_PATH;
    _hasSuccessfulBackup = true;
    _lastSuccessfulBackupMs = millis();
    _lastSuccessfulBackup = verifiedWallClockTimestamp();
    return true;
  }
  return false;
}

bool BackupManager::validateManifest(JsonObjectConst manifest, String &error) {
  if (manifest.isNull() ||
      (manifest["backupVersion"] | 0) != BACKUP_VERSION) {
    error = manifest.isNull() ? "Missing backup manifest"
                              : "Unsupported backup version";
    return false;
  }
  const char *exportedAt = manifest["exportedAt"] | "";
  if (!*exportedAt) {
    error = "Missing exportedAt";
    return false;
  }
  return true;
}

bool BackupManager::restoreFromZip(const char *zipPath, String &error) {
  if (!isSdAvailable()) {
    error = "SD Card is not available";
    return false;
  }
  File zip = SD.open(zipPath, FILE_READ);
  if (!zip || zip.size() < 22 || zip.size() > kMaxArchiveBytes) {
    if (zip) zip.close();
    error = "Invalid backup archive size";
    return false;
  }
  const uint32_t zipSize = zip.size();
  uint8_t eocd[22];
  if (!zip.seek(zipSize - sizeof(eocd)) ||
      zip.read(eocd, sizeof(eocd)) != sizeof(eocd) ||
      readLE32(eocd) != 0x06054b50 || readLE16(eocd + 4) != 0 ||
      readLE16(eocd + 6) != 0 || readLE16(eocd + 20) != 0) {
    zip.close();
    error = "Malformed or ambiguous ZIP footer";
    return false;
  }
  const uint16_t count = readLE16(eocd + 10);
  const uint32_t centralSize = readLE32(eocd + 12);
  const uint32_t centralOffset = readLE32(eocd + 16);
  if (count == 0 || count > kMaxEntries ||
      readLE16(eocd + 8) != count ||
      centralOffset + centralSize != zipSize - sizeof(eocd) ||
      !zip.seek(centralOffset)) {
    zip.close();
    error = "Invalid ZIP central directory";
    return false;
  }

  std::vector<RestoreEntry> entries;
  entries.reserve(count - 1);
  bool manifestSeen = false;
  bool requiredJson[sizeof(kJsonEntries) / sizeof(kJsonEntries[0])] = {};
  uint32_t manifestOffset = 0;
  uint32_t manifestSize = 0;
  uint32_t manifestCrc = 0;
  for (uint16_t n = 0; n < count; ++n) {
    uint8_t header[46];
    if (zip.read(header, sizeof(header)) != sizeof(header) ||
        readLE32(header) != 0x02014b50) {
      zip.close();
      error = "Truncated ZIP central directory";
      cleanupRestoreFiles(entries);
      return false;
    }
    const uint16_t flags = readLE16(header + 8);
    const uint16_t method = readLE16(header + 10);
    const uint32_t crc = readLE32(header + 16);
    const uint32_t compSize = readLE32(header + 20);
    const uint32_t size = readLE32(header + 24);
    const uint16_t nameLen = readLE16(header + 28);
    const uint16_t extraLen = readLE16(header + 30);
    const uint16_t commentLen = readLE16(header + 32);
    const uint16_t disk = readLE16(header + 34);
    const uint32_t localOffset = readLE32(header + 42);
    if (flags != 0 || method != 0 || compSize != size || nameLen == 0 ||
        nameLen > 96 || extraLen != 0 || commentLen != 0 || disk != 0) {
      zip.close();
      error = "Unsupported ZIP entry encoding";
      cleanupRestoreFiles(entries);
      return false;
    }
    String name;
    name.reserve(nameLen);
    for (uint16_t i = 0; i < nameLen; ++i) name += char(zip.read());
    for (const RestoreEntry &prior : entries) {
      if (name.equalsIgnoreCase(prior.name)) {
        zip.close();
        error = "Duplicate backup path";
        cleanupRestoreFiles(entries);
        return false;
      }
    }
    if (name.equalsIgnoreCase("renzfi-manifest.json")) {
      if (manifestSeen || size == 0 || size > 2048) {
        zip.close();
        error = "Invalid backup manifest entry";
        cleanupRestoreFiles(entries);
        return false;
      }
      manifestSeen = true;
      manifestOffset = localOffset;
      manifestSize = size;
      manifestCrc = crc;
      continue;
    }
    bool asset = false;
    const BackupEntry *known = findEntry(name, &asset);
    if (!known || size == 0 || size > known->maxBytes) {
      zip.close();
      error = String("Unknown or oversized backup path: ") + name;
      cleanupRestoreFiles(entries);
      return false;
    }
    for (const RestoreEntry &prior : entries) {
      if (prior.livePath == known->sdPath) {
        zip.close();
        error = "Duplicate backup path";
        cleanupRestoreFiles(entries);
        return false;
      }
    }
    const int requiredIndex = jsonEntryIndex(name);
    if (requiredIndex >= 0) requiredJson[requiredIndex] = true;
    RestoreEntry entry;
    entry.name = name;
    entry.livePath = known->sdPath;
    entry.localOffset = localOffset;
    entry.size = size;
    entry.crc = crc;
    entry.asset = asset;
    entry.arrayRoot = known->arrayRoot;
    char suffix[8];
    snprintf(suffix, sizeof(suffix), "%02u", unsigned(entries.size()));
    entry.stagePath = String("/backup/restore-stage-") + suffix;
    entry.backupPath = String("/backup/restore-live-") + suffix + ".bak";
    entries.push_back(entry);
  }
  if (!manifestSeen) {
    zip.close();
    error = "Missing backup manifest";
    cleanupRestoreFiles(entries);
    return false;
  }
  for (bool present : requiredJson) {
    if (!present) {
      zip.close();
      error = "Backup is missing required configuration entries";
      cleanupRestoreFiles(entries);
      return false;
    }
  }

  auto verifyLocal = [&](uint32_t offset, const String &name, uint32_t size,
                         uint32_t crc, uint32_t &dataOffset) -> bool {
    uint8_t local[30];
    if (offset >= centralOffset || !zip.seek(offset) ||
        zip.read(local, sizeof(local)) != sizeof(local) ||
        readLE32(local) != 0x04034b50 || readLE16(local + 6) != 0 ||
        readLE16(local + 8) != 0 || readLE32(local + 14) != crc ||
        readLE32(local + 18) != size || readLE32(local + 22) != size ||
        readLE16(local + 26) != name.length() ||
        readLE16(local + 28) != 0) {
      return false;
    }
    for (size_t i = 0; i < name.length(); ++i) {
      if (zip.read() != name[i]) return false;
    }
    dataOffset = zip.position();
    return dataOffset + size <= centralOffset;
  };

  uint32_t manifestData = 0;
  if (!verifyLocal(manifestOffset, "renzfi-manifest.json", manifestSize,
                   manifestCrc, manifestData) ||
      !zip.seek(manifestData)) {
    zip.close();
    error = "Manifest local header mismatch";
    cleanupRestoreFiles(entries);
    return false;
  }
  for (RestoreEntry &entry : entries) {
    if (!verifyLocal(entry.localOffset, entry.name, entry.size, entry.crc,
                     entry.dataOffset)) {
      zip.close();
      error = String("Local header mismatch: ") + entry.name;
      cleanupRestoreFiles(entries);
      return false;
    }
    const uint32_t entryEnd = entry.dataOffset + entry.size;
    const uint32_t manifestEnd = manifestData + manifestSize;
    if (!(entryEnd <= manifestOffset || entry.localOffset >= manifestEnd)) {
      zip.close();
      error = "Overlapping ZIP entries";
      cleanupRestoreFiles(entries);
      return false;
    }
    for (const RestoreEntry &prior : entries) {
      if (&prior == &entry) break;
      const uint32_t priorEnd = prior.dataOffset + prior.size;
      if (!(entryEnd <= prior.localOffset ||
            entry.localOffset >= priorEnd)) {
        zip.close();
        error = "Overlapping ZIP entries";
        cleanupRestoreFiles(entries);
        return false;
      }
    }
  }
  if (!zip.seek(manifestData)) {
    zip.close();
    error = "Unable to read backup manifest";
    cleanupRestoreFiles(entries);
    return false;
  }
  DynamicJsonDocument manifestDoc(2048);
  uint8_t manifestBuffer[2048];
  if (zip.read(manifestBuffer, manifestSize) != manifestSize ||
      (crc32Update(0xFFFFFFFFU, manifestBuffer, manifestSize) ^ 0xFFFFFFFFU) !=
          manifestCrc ||
      deserializeJson(manifestDoc, manifestBuffer, manifestSize) !=
          DeserializationError::Ok ||
      !manifestDoc.is<JsonObject>() ||
      !validateManifest(manifestDoc.as<JsonObjectConst>(), error)) {
    zip.close();
    if (error.isEmpty()) error = "Invalid backup manifest";
    cleanupRestoreFiles(entries);
    return false;
  }

  uint8_t buffer[kIoBytes];
  for (RestoreEntry &entry : entries) {
    if (!zip.seek(entry.dataOffset)) {
      zip.close();
      error = String("Local header mismatch: ") + entry.name;
      cleanupRestoreFiles(entries);
      return false;
    }
    SD.remove(entry.stagePath);
    File stage = SD.open(entry.stagePath, FILE_WRITE);
    uint32_t remaining = entry.size;
    uint32_t crc = 0xFFFFFFFFU;
    bool ok = bool(stage);
    while (ok && remaining > 0) {
      const size_t wanted = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
      const size_t got = zip.read(buffer, wanted);
      if (got != wanted || stage.write(buffer, got) != got) {
        ok = false;
        break;
      }
      crc = crc32Update(crc, buffer, got);
      remaining -= got;
    }
    if (stage) {
      stage.flush();
      stage.close();
    }
    if (!ok || remaining != 0 || (crc ^ 0xFFFFFFFFU) != entry.crc ||
        !validateStagedEntry(entry, error)) {
      zip.close();
      if (error.isEmpty()) error = String("CRC mismatch: ") + entry.name;
      cleanupRestoreFiles(entries);
      return false;
    }
  }
  zip.close();
  for (RestoreEntry &entry : entries) {
    entry.hadOriginal = SD.exists(entry.livePath);
  }
  return commitRestore(entries, error);
}

bool BackupManager::restoreFromJsonFile(const char *jsonPath, String &error) {
  File source = SD.open(jsonPath, FILE_READ);
  if (!source || source.size() == 0 ||
      source.size() > kMaxJsonBackupBytes) {
    if (source) source.close();
    error = "Invalid JSON backup size";
    return false;
  }
  if (!validateJsonBackupKeys(source, error)) {
    source.close();
    return false;
  }
  DynamicJsonDocument doc(128U * 1024U);
  const DeserializationError parsed = deserializeJson(doc, source);
  source.close();
  if (parsed != DeserializationError::Ok || !doc.is<JsonObject>() ||
      !validateManifest(doc.as<JsonObjectConst>(), error)) {
    if (error.isEmpty()) error = "Malformed backup JSON";
    return false;
  }
  JsonObjectConst files = doc["files"].as<JsonObjectConst>();
  if (files.isNull()) {
    error = "Malformed backup JSON";
    return false;
  }
  std::vector<RestoreEntry> entries;
  entries.reserve(7);
  bool requiredJson[sizeof(kJsonEntries) / sizeof(kJsonEntries[0])] = {};
  for (JsonPairConst kv : files) {
    bool asset = false;
    const BackupEntry *known = findEntry(kv.key().c_str(), &asset);
    if (!known || asset) {
      error = String("Unknown JSON backup path: ") + kv.key().c_str();
      cleanupRestoreFiles(entries);
      return false;
    }
    const int requiredIndex = jsonEntryIndex(kv.key().c_str());
    if (requiredIndex < 0 || requiredJson[requiredIndex]) {
      error = "Duplicate or ambiguous JSON backup path";
      cleanupRestoreFiles(entries);
      return false;
    }
    requiredJson[requiredIndex] = true;
    RestoreEntry entry;
    entry.name = kv.key().c_str();
    entry.livePath = known->sdPath;
    entry.arrayRoot = known->arrayRoot;
    char suffix[8];
    snprintf(suffix, sizeof(suffix), "%02u", unsigned(entries.size()));
    entry.stagePath = String("/backup/restore-stage-") + suffix;
    entry.backupPath = String("/backup/restore-live-") + suffix + ".bak";
    SD.remove(entry.stagePath);
    File stage = SD.open(entry.stagePath, FILE_WRITE);
    if (!stage || serializeJson(kv.value(), stage) == 0) {
      if (stage) stage.close();
      error = String("Unable to stage ") + entry.name;
      cleanupRestoreFiles(entries);
      return false;
    }
    stage.close();
    File verify = SD.open(entry.stagePath, FILE_READ);
    entry.size = verify ? verify.size() : 0;
    if (verify) verify.close();
    entries.push_back(entry);
    if (entry.size > known->maxBytes ||
        !validateStagedEntry(entries.back(), error)) {
      cleanupRestoreFiles(entries);
      return false;
    }
  }
  for (bool present : requiredJson) {
    if (!present) {
      error = "Backup is missing required configuration entries";
      cleanupRestoreFiles(entries);
      return false;
    }
  }
  for (RestoreEntry &entry : entries) {
    entry.hadOriginal = SD.exists(entry.livePath);
  }
  return commitRestore(entries, error);
}

bool BackupManager::restoreFromFile(const char *uploadPath, String &error) {
  if (!recoverPendingRestore(_storage, error)) return false;
  if (!uploadPath || !SD.exists(uploadPath)) {
    error = "Backup file not found";
    return false;
  }
  File probe = SD.open(uploadPath, FILE_READ);
  uint8_t sig[2] = {0, 0};
  if (!probe || probe.read(sig, sizeof(sig)) != sizeof(sig)) {
    if (probe) probe.close();
    error = "Unable to open uploaded backup";
    return false;
  }
  probe.close();
  const bool restored = sig[0] == 'P' && sig[1] == 'K'
                            ? restoreFromZip(uploadPath, error)
                            : restoreFromJsonFile(uploadPath, error);
  if (!restored &&
      (SD.exists(RESTORE_JOURNAL_PATH) ||
       SD.exists(RESTORE_JOURNAL_STAGE_PATH) ||
       SD.exists(RESTORE_JOURNAL_BACKUP_PATH)) &&
      _storage) {
    _storage->markDegraded(String("Restore transaction incomplete: ") + error);
  }
  return restored;
}

bool BackupManager::recoverPendingRestore(StorageManager *storage,
                                          String &error) {
  if (!storage || !storage->isSdMounted()) return true;
  const bool hasPrimary = SD.exists(RESTORE_JOURNAL_PATH);
  const bool hasStage = SD.exists(RESTORE_JOURNAL_STAGE_PATH);
  const bool hasBackup = SD.exists(RESTORE_JOURNAL_BACKUP_PATH);
  const bool hasArtifacts = hasRestoreArtifacts();
  if (!hasPrimary && !hasStage && !hasBackup && !hasArtifacts) return true;

  std::vector<RestoreEntry> entries;
  const char *selected = nullptr;
  bool commitComplete = false;
  if (hasPrimary &&
      loadJournalCandidate(RESTORE_JOURNAL_PATH, entries, &commitComplete)) {
    selected = RESTORE_JOURNAL_PATH;
  } else if (hasStage &&
             loadJournalCandidate(RESTORE_JOURNAL_STAGE_PATH, entries,
                                  &commitComplete)) {
    selected = RESTORE_JOURNAL_STAGE_PATH;
  } else if (hasBackup &&
             loadJournalCandidate(RESTORE_JOURNAL_BACKUP_PATH, entries,
                                  &commitComplete)) {
    selected = RESTORE_JOURNAL_BACKUP_PATH;
  }
  if (!selected) {
    error =
        "Restore journal candidates are invalid; artifacts preserved for "
        "forensic recovery";
    return false;
  }
  const bool recovered = commitComplete ? finalizeCommittedRestore(entries)
                                        : rollbackRestore(entries);
  if (!recovered) {
    error = String(commitComplete ? "Commit cleanup from " : "Rollback from ") +
            selected + " did not complete; artifacts preserved";
    return false;
  }
  Serial.printf("[restore] Recovered incomplete restore using %s\n", selected);
  return true;
}

bool BackupManager::wipeUserData(String &error) {
  (void)error;
  if (_assets) {
    _assets->deleteAsset(AssetType::Banner);
    _assets->deleteAsset(AssetType::Music);
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
      _storage->deleteSdOnly(StoragePaths::ExistingNetworkScanFile);
      _storage->deleteSdOnly(StoragePaths::RouterCacheFile);
      _storage->deleteSdOnly(StoragePaths::RouterProvisioningFile);
      _storage->deleteSdOnly(StoragePaths::InstallationFile);
      _storage->factoryResetData();
    }
  }
  if (_auth) _auth->resetToDefault();
  if (_portalConfig) _portalConfig->loadMeta();
  if (_assets) _assets->loadMetadata();
  return true;
}

bool BackupManager::performFactoryReset(String &error) {
  if (_logger) _logger->info("system", "factory reset started");
  Serial.println("[system] factory reset started");
  if (!wipeUserData(error)) return false;
  if (_installation && !_installation->resetToFactory()) {
    error = "Unable to reset installation state";
    return false;
  }
  if (_logger) _logger->info("system", "factory reset completed");
  Serial.println("[system] factory reset completed");
  return true;
}
