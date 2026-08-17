#include "StorageManager.h"

#include <SPIFFS.h>
#include <cstring>
#include <math.h>

#include "BackupManager.h"
#include "Config.h"
#include "DmaMemoryMonitor.h"
#include "EthernetManager.h"
#include "EventBus.h"
#include "FinishTrace.h"
#include "JsonHeap.h"
#include "SdSpi.h"
#include "StoragePaths.h"

namespace {

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
    "\"profile\":\"default\",\"ssid\":\"RenzFi_PesoWifi\",\"wifiPassword\":\"\"}";

constexpr const char *kDefaultVouchers = "[]";
constexpr const char *kDefaultPortalSessions = "{\"sessions\":[]}";
constexpr const char *kDefaultSales = "[]";

uint32_t countSpoolRecords(const char *path) {
  if (!path || !SPIFFS.exists(path)) return 0;
  File file = SPIFFS.open(path, FILE_READ);
  if (!file) return 0;
  uint32_t records = 0;
  bool hasContent = false;
  while (file.available()) {
    const int value = file.read();
    if (value == '\n') {
      if (hasContent) ++records;
      hasContent = false;
    } else if (value != '\r' && value >= 0) {
      hasContent = true;
    }
  }
  if (hasContent) ++records;
  file.close();
  return records;
}

const char *sdStatusLabel(bool present, bool mounted, bool mountFailed) {
  if (mounted) return "Ready";
  if (!present) return "Missing";
  if (mountFailed) return "Mount Failed";
  return "Error";
}

}  // namespace

// ── Lifecycle ─────────────────────────────────────────────────────────────────

StorageManager::ScopedStorageLock::ScopedStorageLock(
    const StorageManager &owner)
    : _owner(owner), _locked(owner.lockStorage()) {}

StorageManager::ScopedStorageLock::~ScopedStorageLock() {
  if (_locked) _owner.unlockStorage();
}

void StorageManager::setError(const String &message) {
  _lastError = message;
  Serial.println(message);
}

const char *StorageManager::sdLifecycleName() const {
  switch (_sdLifecycle) {
    case SdLifecycle::Disabled:
      return "SD_DISABLED";
    case SdLifecycle::Mounting:
      return "SD_MOUNTING";
    case SdLifecycle::Ready:
      return "SD_READY";
    case SdLifecycle::Degraded:
      return "SD_DEGRADED";
    case SdLifecycle::Remounting:
      return "SD_REMOUNTING";
    case SdLifecycle::Syncing:
      return "SD_SYNCING";
    case SdLifecycle::Failed:
      return "SD_FAILED";
  }
  return "SD_UNKNOWN";
}

void StorageManager::setSdLifecycle(SdLifecycle next, const char *reason) {
  if (_sdLifecycle == next) return;
  const char *from = sdLifecycleName();
  _sdLifecycle = next;
  Serial.printf("[storage-lifecycle] state=%s -> %s reason=%s\n", from,
                sdLifecycleName(), reason ? reason : "");
  DmaMemoryMonitor::logSnapshot(sdLifecycleName());
  portENTER_CRITICAL(&_dashSnapMux);
  strlcpy(_dashSnap.sdLifecycle, sdLifecycleName(), sizeof(_dashSnap.sdLifecycle));
  _dashSnap.recoveryInProgress = _sdRecoveryInProgress;
  portEXIT_CRITICAL(&_dashSnapMux);
}

void StorageManager::tripSdMediaMissing(const char *reason) {
  // Idempotent once already degraded with fallback active.
  if (!_sdMounted && !_sdReadable && _usingFallback) return;
  handleSdRemoved(reason ? reason : "media_missing");
}

bool StorageManager::createStorageMutex() {
  if (_storageMutex) return true;
  _storageMutex = xSemaphoreCreateRecursiveMutex();
  if (!_storageMutex) {
    setError("[storage] Unable to create recursive storage mutex");
    return false;
  }
  return true;
}

bool StorageManager::lockStorage() const {
  if (!_storageMutex) return false;
  TickType_t wait = pdMS_TO_TICKS(RenzFiConfig::STORAGE_LOCK_TIMEOUT_MS);
  // Never park AsyncTCP on STORAGE_LOCK for the duration of remount/sync.
  if (_sdRecoveryInProgress) wait = 0;
  const BaseType_t taken = xSemaphoreTakeRecursive(_storageMutex, wait);
  if (taken != pdTRUE) {
    if (!_sdRecoveryInProgress) {
      Serial.println("[storage] Timed out waiting for storage transaction lock");
    }
    return false;
  }
  return true;
}

bool StorageManager::tryLockStorage() const {
  if (!_storageMutex) return false;
  return xSemaphoreTakeRecursive(_storageMutex, 0) == pdTRUE;
}

void StorageManager::unlockStorage() const {
  if (_storageMutex) xSemaphoreGiveRecursive(_storageMutex);
}

bool StorageManager::mountSdCard(const char *context, bool reinitBus) {
  setSdLifecycle(reinitBus ? SdLifecycle::Remounting : SdLifecycle::Mounting,
                 context ? context : "mount");
  DmaMemoryMonitor::ScopedProbe dmaProbe(reinitBus ? "sd-remount" : "sd-mount");
  Serial.println("[SD] SPI bus:");
  Serial.printf("MOSI=%d\n", RenzFiConfig::PIN_SD_MOSI);
  Serial.printf("MISO=%d\n", RenzFiConfig::PIN_SD_MISO);
  Serial.printf("SCK=%d\n", RenzFiConfig::PIN_SD_SCK);
  Serial.printf("CS=%d\n", RenzFiConfig::PIN_SD_CS);

  if (reinitBus) {
    Serial.printf("[SD] %s: releasing prior SD mount before reinit\n", context);
    SD.end();
  }

  renzFiSdSpiBegin(reinitBus);
  delay(100);

  Serial.printf("[SD] %s: mounting SD (cs=%d freq=%lu Hz csLevel=%d)\n", context,
                RenzFiConfig::PIN_SD_CS,
                (unsigned long)RenzFiConfig::SD_SPI_FREQ_HZ,
                digitalRead(RenzFiConfig::PIN_SD_CS));

  const bool ok = SD.begin(RenzFiConfig::PIN_SD_CS, renzFiSdSpi(),
                           RenzFiConfig::SD_SPI_FREQ_HZ);
  Serial.printf("[SD] %s: SD.begin %s\n", context, ok ? "OK" : "FAILED");
  if (ok) {
    Serial.printf("[SD] %s: cardType=%u cardSize=%llu bytes\n", context,
                  static_cast<unsigned>(SD.cardType()),
                  static_cast<unsigned long long>(SD.cardSize()));
    _sdIoFailStreak = 0;
  } else {
    setSdLifecycle(SdLifecycle::Failed, "SD.begin failed");
  }
  EthernetManager::logDiagnosticStage("after_sd_begin");
  return ok;
}

bool StorageManager::mountSpiffs() {
  if (_spiffsMounted) return true;
  // FirmwareApp Phase 2 may already have mounted SPIFFS; reuse that mount.
  if (SPIFFS.totalBytes() > 0) {
    _spiffsMounted = true;
    return true;
  }
  if (SPIFFS.begin(false)) {
    _spiffsMounted = true;
    return true;
  }
  return false;
}

bool StorageManager::begin() {
  if (!createStorageMutex()) return false;
  ScopedStorageLock lock(*this);
  if (!lock) return false;
  Serial.println("[boot] SD card initialization starting");
  _spiffsMounted = mountSpiffs();

  _sdMounted = mountSdCard("SD mount", false);
  _sdReadable = _sdMounted;
  _healthy = _sdReadable;
  _sdRetryCount = 0;
  _disableSdPolling = false;

  if (_sdMounted) {
    setSdLifecycle(SdLifecycle::Ready, "boot mount");
    _sdPresent = SD.cardType() != CARD_NONE;
    _sdMountFailed = false;
    Serial.printf("[boot] SD card mounted: total=%llu bytes used=%llu bytes\n",
                  SD.totalBytes(), SD.usedBytes());
    _sdWritable = probeSdWritable();
    if (!_sdWritable &&
        (_diagnosticCause == nullptr ||
         strcmp(_diagnosticCause, "UNKNOWN") == 0 ||
         strcmp(_diagnosticCause, "OK") == 0)) {
      setDiagnosticCause("READ_ONLY");
    } else if (_sdWritable) {
      setDiagnosticCause("OK");
    }
    recoverBootTransactions();
    String restoreRecoveryError;
    const bool restoreRecovered =
        BackupManager::recoverPendingRestore(this, restoreRecoveryError);
    noteRestoreJournalHealth(restoreRecovered);
    if (!restoreRecovered) {
      _healthy = false;
      _sdWritable = false;
      _usingFallback = false;
      _disableSdPolling = true;
      _watchMode = false;
      setDiagnosticCause("RESTORE_BLOCKED");
      _lastError =
          String("Pending restore recovery failed: ") + restoreRecoveryError;
      Serial.printf("[storage] DEGRADED: %s\n", _lastError.c_str());
      return false;
    }
    const bool ok = _sdWritable ? ensureLayout() : validateLayout(false);
    if (_sdWritable) {
      _layoutValid = validateLayout(false);
    } else {
      Serial.println("[storage] SD mounted read-only; writes use bounded fallback");
    }
    Serial.println(ok ? "[boot] SD card layout ready"
                      : "[storage] SD layout incomplete or read-only");
    if (_sdWritable && _spiffsMounted &&
        SPIFFS.exists(RenzFiConfig::FB_MANIFEST)) {
      Serial.println("[storage] SD restored, syncing fallback data");
      syncFallbackToSd();
    }
    if (_sdWritable && _spiffsMounted) replayHistorySpools();
    _usingFallback = !_sdWritable && _spiffsMounted;
    Serial.println(_usingFallback ? "[storage] Sales storage = SPIFFS fallback"
                                  : "[storage] Sales storage = SD");
    refreshRuntimeSnapshot();
    return _healthy;
  } else {
    setSdLifecycle(SdLifecycle::Degraded, "boot mount failed");
    _sdPresent = SD.cardType() != CARD_NONE;
    _sdMounted = false;
    _sdReadable = false;
    _sdMountFailed = _sdPresent;
    setError("[ERROR] SD card mount failed");
    setDiagnosticCause(_sdPresent ? "FILESYSTEM_ERROR" : "MEDIA_MISSING");
    if (_spiffsMounted) {
      _usingFallback = true;
      Serial.println("[storage] Entering fallback mode");
      Serial.println("[storage] SD unavailable, using SPIFFS fallback");
      Serial.println("[storage] Sales storage = SPIFFS fallback");
      Serial.println("[storage] Waiting for SD reinsertion");
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
    refreshRuntimeSnapshot();
    return false;
  }
}

bool StorageManager::healthy() const {
  return _healthy;
}

bool StorageManager::usingFallback() const {
  return _usingFallback;
}

String StorageManager::lastError() const {
  if (!tryLockStorage()) return String();
  String out = _lastError;
  unlockStorage();
  return out;
}

void StorageManager::setEventBus(EventBus *events) {
  _events = events;
}

void StorageManager::markDegraded(const String &reason) {
  ScopedStorageLock lock(*this);
  if (!lock) return;
  _healthy = false;
  _sdWritable = false;
  _usingFallback = false;
  _disableSdPolling = true;
  _watchMode = false;
  _lastError = reason;
  if (reason.indexOf("restore") >= 0 || reason.indexOf("Restore") >= 0 ||
      reason.indexOf("RESTORE") >= 0) {
    setDiagnosticCause("RESTORE_BLOCKED");
  } else {
    setDiagnosticCause("FILESYSTEM_ERROR");
  }
  Serial.printf("[storage] DEGRADED: %s\n", reason.c_str());
}

void StorageManager::emitStorageChanged() {
  if (_events) {
    _events->emit("storage.changed");
    _events->emit("system.status");
  }
}

void StorageManager::handleSdRemoved(const char *reason) {
  if (!_healthy && _usingFallback && !_sdMounted && !_sdReadable) return;

  Serial.println("[storage] Detected SD removal");
  Serial.printf("[storage] SD removed/failed: %s\n", reason);
  Serial.println("[storage] Entering fallback mode");
  setSdLifecycle(SdLifecycle::Degraded, reason ? reason : "media_missing");
  SD.end();
  _healthy = false;
  _sdMounted = false;
  _sdReadable = false;
  _sdWritable = false;
  _usingFallback = _spiffsMounted;
  _sdPresent = false;
  _sdMountFailed = true;
  _sdIoFailStreak = 0;
  _watchMode = false;  // restart fast retry budget on fresh removal
  _sdRetryCount = 0;
  setDiagnosticCause("MEDIA_MISSING");
  setError(String("[storage] SD unavailable: ") + reason);
  emitStorageChanged();
  refreshRuntimeSnapshot();
}

bool StorageManager::verifySdHealthy() {
  if (!_sdMounted || !_sdReadable) return false;

  if (SD.cardType() == CARD_NONE) {
    handleSdRemoved("card not present");
    return false;
  }

  File probe = SD.open(StoragePaths::Config, FILE_READ);
  if (!probe) {
    setDiagnosticCause("FILESYSTEM_ERROR");
    handleSdRemoved("health check open failed");
    return false;
  }
  probe.close();
  if (!_sdWritable && probeSdWritable()) {
    Serial.println("[storage] Write capability restored");
    _usingFallback = false;
    setDiagnosticCause("OK");
    if (_spiffsMounted && SPIFFS.exists(RenzFiConfig::FB_MANIFEST)) {
      Serial.println("[storage] Recovering fallback files");
      syncFallbackToSd();
    }
    if (_spiffsMounted) {
      replayHistorySpools();
    }
    emitStorageChanged();
  }
  return true;
}

bool StorageManager::isSdPollingDisabled() const {
  return _disableSdPolling;
}

bool StorageManager::isWatchMode() const {
  return _watchMode;
}

uint8_t StorageManager::sdRetryCount() const {
  return _sdRetryCount;
}

const char *StorageManager::diagnosticCause() const {
  return _diagnosticCause ? _diagnosticCause : "UNKNOWN";
}

void StorageManager::setDiagnosticCause(const char *cause) {
  _diagnosticCause = cause && cause[0] ? cause : "UNKNOWN";
}

void StorageManager::clearConflicts() {
  _conflictCount = 0;
  for (uint8_t i = 0; i < RenzFiConfig::STORAGE_CONFLICT_CAP; ++i) {
    _conflicts[i] = ConflictRecord{};
  }
}

void StorageManager::recordConflict(const char *sdPath, uint32_t generation,
                                    uint32_t baseCrc, uint32_t sdCrc,
                                    uint32_t fallbackCrc) {
  if (!sdPath || !sdPath[0]) return;
  for (uint8_t i = 0; i < _conflictCount; ++i) {
    if (strncmp(_conflicts[i].path, sdPath, sizeof(_conflicts[i].path) - 1) ==
        0) {
      _conflicts[i].generation = generation;
      _conflicts[i].baseCrc = baseCrc;
      _conflicts[i].sdCrc = sdCrc;
      _conflicts[i].fallbackCrc = fallbackCrc;
      _conflicts[i].detectedAtMs = millis();
      return;
    }
  }
  if (_conflictCount >= RenzFiConfig::STORAGE_CONFLICT_CAP) return;
  ConflictRecord &slot = _conflicts[_conflictCount++];
  strncpy(slot.path, sdPath, sizeof(slot.path) - 1);
  slot.path[sizeof(slot.path) - 1] = '\0';
  slot.generation = generation;
  slot.baseCrc = baseCrc;
  slot.sdCrc = sdCrc;
  slot.fallbackCrc = fallbackCrc;
  slot.detectedAtMs = millis();
  Serial.printf(
      "[storage] Conflict detected path=%s generation=%lu baseCrc=%08lx "
      "sdCrc=%08lx fallbackCrc=%08lx (no auto-merge)\n",
      slot.path, static_cast<unsigned long>(generation),
      static_cast<unsigned long>(baseCrc), static_cast<unsigned long>(sdCrc),
      static_cast<unsigned long>(fallbackCrc));
}

const char *StorageManager::fallbackFileLabel(const char *fbPath) {
  if (!fbPath) return "Unknown";
  if (strstr(fbPath, "settings")) return "Settings";
  if (strstr(fbPath, "portal_config") || strstr(fbPath, "portal-config"))
    return "Portal";
  if (strstr(fbPath, "portal_sessions") || strstr(fbPath, "portal-sessions"))
    return "Completed Sessions";
  if (strstr(fbPath, "voucher")) return "Voucher";
  if (strstr(fbPath, "sales")) return "Sales";
  if (strstr(fbPath, "promo")) return "Promos";
  if (strstr(fbPath, "router")) return "Router";
  if (strstr(fbPath, "installation")) return "Installation";
  if (strstr(fbPath, "provisioning")) return "Provisioning";
  if (strstr(fbPath, "setup")) return "Setup";
  return "Config";
}

bool StorageManager::attemptSdRecovery() {
  if (!_spiffsMounted && !mountSpiffs()) return false;

  Serial.println("[storage-recovery] remount started");
  if (!mountSdCard("SD remount", true)) {
    onSdRecoveryFailed();
    return false;
  }

  // Keep HTTP off the SD bus until verification + sync finish.
  _sdMounted = true;
  _sdReadable = false;
  _healthy = true;
  _sdWritable = probeSdWritable();
  recoverBootTransactions();
  String restoreRecoveryError;
  const bool restoreRecovered =
      BackupManager::recoverPendingRestore(this, restoreRecoveryError);
  noteRestoreJournalHealth(restoreRecovered);
  if (!restoreRecovered) {
    _healthy = false;
    _sdWritable = false;
    _usingFallback = false;
    _disableSdPolling = true;
    _watchMode = false;
    setDiagnosticCause("RESTORE_BLOCKED");
    _lastError =
        String("Pending restore recovery failed: ") + restoreRecoveryError;
    Serial.printf("[storage] DEGRADED: %s\n", _lastError.c_str());
    return false;
  }
  if (_sdWritable && !ensureLayout()) {
    Serial.println("[storage] SD remount layout repair failed");
  }
  _layoutValid = validateLayout(false);
  onSdRecoverySucceeded();
  Serial.println("[storage-recovery] remount verified");
  return true;
}

void StorageManager::onSdRecoveryFailed() {
  _healthy = false;
  _sdMounted = false;
  _sdReadable = false;
  _sdWritable = false;
  _usingFallback = _spiffsMounted;
  _sdPresent = SD.cardType() != CARD_NONE;
  _sdMountFailed = _sdPresent;
  setDiagnosticCause(_sdPresent ? "FILESYSTEM_ERROR" : "MEDIA_MISSING");

  _sdRetryCount++;
  Serial.printf("[storage] SD recovery attempt %u/%u\n", _sdRetryCount,
                RenzFiConfig::SD_RECOVERY_MAX_ATTEMPTS);

  if (_sdRetryCount >= RenzFiConfig::SD_RECOVERY_MAX_ATTEMPTS && !_watchMode) {
    _watchMode = true;
    Serial.println("[storage] Entering low-power watch mode");
    Serial.println("[storage] Waiting for SD reinsertion");
  }
  emitStorageChanged();
}

void StorageManager::onSdRecoverySucceeded() {
  _healthy = true;
  _sdMounted = true;
  _sdReadable = true;
  _usingFallback = !_sdWritable && _spiffsMounted;
  _sdRetryCount = 0;
  _disableSdPolling = false;
  _watchMode = false;
  _sdPresent = SD.cardType() != CARD_NONE;
  _sdMountFailed = false;
  if (_sdWritable) {
    setDiagnosticCause("OK");
  } else if (_diagnosticCause == nullptr ||
             strcmp(_diagnosticCause, "OK") == 0 ||
             strcmp(_diagnosticCause, "UNKNOWN") == 0 ||
             strcmp(_diagnosticCause, "MEDIA_MISSING") == 0 ||
             strcmp(_diagnosticCause, "FILESYSTEM_ERROR") == 0) {
    setDiagnosticCause("READ_ONLY");
  }
  Serial.println("[storage] SD recovered successfully");
  setSdLifecycle(SdLifecycle::Ready, "remount verified");
  emitStorageChanged();

  if (_sdWritable && SPIFFS.exists(RenzFiConfig::FB_MANIFEST)) {
    setSdLifecycle(SdLifecycle::Syncing, "fallback reconcile");
    Serial.println("[storage] SD restored, syncing fallback data");
    Serial.println("[storage] Recovering fallback files");
    syncFallbackToSd();
  }
  if (_sdWritable && _spiffsMounted) replayHistorySpools();
  Serial.println("[storage] Replay complete");
  if (_sdWritable) setSdLifecycle(SdLifecycle::Ready, "sync complete");
}

void StorageManager::requestSdRecovery(const char *source) {
  const char *src = source ? source : "system";
  if (_sdRecoveryInProgress) {
    Serial.printf("[storage-recovery] requested source=%s (already in flight)\n",
                  src);
    return;
  }
  _sdRecoveryRequested = true;
  Serial.printf("[storage-recovery] requested source=%s\n", src);
  Serial.println("[storage-recovery] queued");
}

bool StorageManager::retrySd() {
  _disableSdPolling = false;
  _watchMode = false;
  _sdRetryCount = 0;
  Serial.println("[storage] Manual SD recovery requested");
  requestSdRecovery("api");
  return true;
}

void StorageManager::pollStorageHealth() {
  // Never stall loopTask or AsyncTCP behind a long SD remount on STORAGE_LOCK.
  if (_sdRecoveryInProgress) return;
  if (!tryLockStorage()) return;
  auto unlock = [this]() { unlockStorage(); };

  if (_disableSdPolling && !_sdRecoveryRequested) {
    unlock();
    return;
  }
  if (_syncInProgress) {
    unlock();
    return;
  }

  const uint32_t now = millis();
  const uint32_t interval = _watchMode ? RenzFiConfig::STORAGE_WATCH_POLL_MS
                                       : RenzFiConfig::STORAGE_HEALTH_POLL_MS;
  const bool requested = _sdRecoveryRequested;
  if (!requested && now - _lastHealthPollMs < interval) {
    unlock();
    return;
  }
  _lastHealthPollMs = now;

  if (_healthy && !requested) {
    verifySdHealthy();
    refreshRuntimeSnapshot();
    unlock();
    return;
  }

  if (_watchMode && !requested) {
    Serial.println("[storage] Waiting for SD reinsertion");
    const uint8_t cardType = SD.cardType();
    _sdPresent = cardType != CARD_NONE;
    if (!_sdPresent) {
      setDiagnosticCause("MEDIA_MISSING");
      refreshRuntimeSnapshot();
      unlock();
      return;
    }
    Serial.println("[storage] SD detected");
  }

  _sdRecoveryRequested = false;
  _sdRecoveryInProgress = true;
  _sdReadable = false;
  setSdLifecycle(SdLifecycle::Remounting, "recovery owner");
  unlock();

  Serial.println("[storage-recovery] worker started");
  attemptSdRecovery();
  if (tryLockStorage()) {
    refreshRuntimeSnapshot();
    unlockStorage();
  }
  _sdRecoveryInProgress = false;
  Serial.println("[storage-recovery] worker finished");
}

// ── SD layout ─────────────────────────────────────────────────────────────────

bool StorageManager::ensureRequiredDirectories() {
  if (!_healthy) return false;

  bool ok = true;
  for (size_t i = 0; i < StoragePaths::requiredSdDirectoryCount(); ++i) {
    const char *dir = StoragePaths::requiredSdDirectory(i);
    if (!dir) continue;
    ok = ensureDir(dir) && ok;
  }
  return ok;
}

bool StorageManager::seedDefaultJsonFiles() {
  if (!_healthy) return false;

  bool ok = true;
  ok = ensureJsonFile(StoragePaths::SettingsFile, kDefaultSettings) && ok;
  ok = ensureJsonFile(StoragePaths::PromosFile, kDefaultPromos) && ok;
  ok = ensureJsonFile(StoragePaths::RouterFile, kDefaultRouter) && ok;
  ok = ensureJsonFile(StoragePaths::WifiConfigFile,
                      "{\"staSsid\":\"RenzFi_Admin\",\"useStaticIp\":true,"
                      "\"staIp\":\"10.10.10.2\",\"staGateway\":\"10.10.10.1\","
                      "\"staSubnet\":\"255.255.255.0\"}") &&
       ok;
  ok = ensureJsonFile(StoragePaths::VouchersFile, kDefaultVouchers) && ok;
  ok = ensureJsonFile(StoragePaths::SalesFile, "[]") && ok;
  ok = ensureJsonFile(StoragePaths::LogsFile, "[]") && ok;
  ok = ensureJsonFile(StoragePaths::UsersFile, "[]") && ok;
  ok = ensureJsonFile(StoragePaths::AdminSessionsFile, "[]") && ok;
  ok = ensureJsonFile(StoragePaths::PortalSessionsFile, kDefaultPortalSessions) &&
       ok;
  ok = ensureJsonFile(StoragePaths::PortalConfigFile,
                      "{\"revision\":0,\"hasBanner\":false,\"hasMusic\":false}") &&
       ok;
  ok = ensureJsonFile(StoragePaths::InstallationFile,
                      "{\"state\":\"factory\",\"updatedAt\":0,"
                      "\"completedSteps\":[],\"firmwareVersion\":\"0.5.0-w5500\","
                      "\"installationVersion\":2,"
                      "\"session\":{\"sessionId\":\"\",\"startedAt\":0,"
                      "\"lastActivity\":0,\"installerName\":\"\",\"deviceId\":\"\","
                      "\"isRecovery\":false,\"attempt\":0}}") &&
       ok;

  for (size_t i = 0; i < StoragePaths::reservedConfigFileCount(); ++i) {
    const char *path = StoragePaths::reservedConfigFile(i);
    const char *def = StoragePaths::reservedConfigDefaultJson(i);
    if (!path || !def) continue;
    ok = ensureJsonFile(path, def) && ok;
  }
  return ok;
}

bool StorageManager::ensureLayout() {
  ScopedStorageLock lock(*this);
  if (!lock) return false;
  if (!_healthy) return false;
  return ensureRequiredDirectories() && seedDefaultJsonFiles();
}

// ── Fallback routing helpers ──────────────────────────────────────────────────

bool StorageManager::isFallbackEligible(const char *path) const {
  return strcmp(path, RenzFiConfig::SETTINGS_FILE) == 0 ||
         strcmp(path, RenzFiConfig::PROMOS_FILE) == 0 ||
         strcmp(path, RenzFiConfig::ROUTER_FILE) == 0 ||
         strcmp(path, RenzFiConfig::VOUCHERS_FILE) == 0 ||
         strcmp(path, RenzFiConfig::PORTAL_SESSIONS_FILE) == 0 ||
         strcmp(path, RenzFiConfig::SALES_FILE) == 0 ||
         strcmp(path, RenzFiConfig::PORTAL_CONFIG_FILE) == 0 ||
         strcmp(path, StoragePaths::InstallationFile) == 0 ||
         strcmp(path, StoragePaths::ProvisioningFile) == 0 ||
         strcmp(path, StoragePaths::RouterConnectionFile) == 0 ||
         strcmp(path, StoragePaths::RouterProvisioningFile) == 0 ||
         strcmp(path, StoragePaths::SetupWizardFile) == 0;
}

bool StorageManager::isContinuousCheckpointEligible(const char *path) const {
  if (!path) return false;
  // Vouchers are excluded: after a successful SD writeJson the continuous
  // SPIFFS mirror runs a full transactional LittleFS rewrite (stage / verify /
  // rename) while holding STORAGE_LOCK. On hardware that blocks async_tcp
  // waiters for > TWDT (~5s) even though voucher_worker is on CPU0.
  // SD remains authoritative; dirty fallback is cleared via removeFromManifest.
  // SD-failure path still uses writeJsonToSpiffs (true fallback).
  return strcmp(path, RenzFiConfig::SETTINGS_FILE) == 0 ||
         strcmp(path, RenzFiConfig::PROMOS_FILE) == 0 ||
         strcmp(path, RenzFiConfig::ROUTER_FILE) == 0 ||
         strcmp(path, RenzFiConfig::PORTAL_SESSIONS_FILE) == 0 ||
         strcmp(path, StoragePaths::InstallationFile) == 0 ||
         strcmp(path, StoragePaths::ProvisioningFile) == 0 ||
         strcmp(path, StoragePaths::RouterConnectionFile) == 0 ||
         strcmp(path, StoragePaths::RouterProvisioningFile) == 0 ||
         strcmp(path, StoragePaths::SetupWizardFile) == 0;
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
  if (strcmp(sdPath, StoragePaths::InstallationFile) == 0)
    return RenzFiConfig::FB_INSTALLATION;
  if (strcmp(sdPath, StoragePaths::ProvisioningFile) == 0)
    return RenzFiConfig::FB_PROVISIONING;
  if (strcmp(sdPath, StoragePaths::RouterConnectionFile) == 0)
    return RenzFiConfig::FB_ROUTER_CONNECTION;
  if (strcmp(sdPath, StoragePaths::RouterProvisioningFile) == 0)
    return RenzFiConfig::FB_ROUTER_PROVISIONING;
  if (strcmp(sdPath, StoragePaths::RouterCacheFile) == 0)
    return RenzFiConfig::FB_ROUTER_CACHE;
  if (strcmp(sdPath, StoragePaths::ExistingNetworkScanFile) == 0)
    return RenzFiConfig::FB_EXISTING_NETWORK_SCAN;
  if (strcmp(sdPath, StoragePaths::SetupWizardFile) == 0)
    return RenzFiConfig::FB_SETUP_WIZARD;
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
  if (strcmp(fbPath, RenzFiConfig::FB_INSTALLATION) == 0)
    return StoragePaths::InstallationFile;
  if (strcmp(fbPath, RenzFiConfig::FB_PROVISIONING) == 0)
    return StoragePaths::ProvisioningFile;
  if (strcmp(fbPath, RenzFiConfig::FB_ROUTER_CONNECTION) == 0)
    return StoragePaths::RouterConnectionFile;
  if (strcmp(fbPath, RenzFiConfig::FB_ROUTER_PROVISIONING) == 0)
    return StoragePaths::RouterProvisioningFile;
  if (strcmp(fbPath, RenzFiConfig::FB_ROUTER_CACHE) == 0)
    return StoragePaths::RouterCacheFile;
  if (strcmp(fbPath, RenzFiConfig::FB_EXISTING_NETWORK_SCAN) == 0)
    return StoragePaths::ExistingNetworkScanFile;
  if (strcmp(fbPath, RenzFiConfig::FB_SETUP_WIZARD) == 0)
    return StoragePaths::SetupWizardFile;
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
  if (strcmp(sdPath, StoragePaths::InstallationFile) == 0)
    return RenzFiConfig::FB_LIMIT_INSTALLATION;
  if (strcmp(sdPath, StoragePaths::ProvisioningFile) == 0)
    return RenzFiConfig::FB_LIMIT_PROVISIONING;
  if (strcmp(sdPath, StoragePaths::RouterConnectionFile) == 0)
    return RenzFiConfig::FB_LIMIT_ROUTER_CONNECTION;
  if (strcmp(sdPath, StoragePaths::RouterProvisioningFile) == 0)
    return RenzFiConfig::FB_LIMIT_ROUTER_PROVISIONING;
  if (strcmp(sdPath, StoragePaths::RouterCacheFile) == 0)
    return RenzFiConfig::FB_LIMIT_ROUTER_CACHE;
  if (strcmp(sdPath, StoragePaths::ExistingNetworkScanFile) == 0)
    return RenzFiConfig::FB_LIMIT_EXISTING_NETWORK_SCAN;
  if (strcmp(sdPath, StoragePaths::SetupWizardFile) == 0)
    return RenzFiConfig::FB_LIMIT_SETUP_WIZARD;
  return 0;
}

void StorageManager::recoverBootTransactions() {
  ScopedStorageLock lock(*this);
  if (!lock) return;
  const char *sdPaths[] = {
      RenzFiConfig::SETTINGS_FILE,
      RenzFiConfig::PROMOS_FILE,
      RenzFiConfig::ROUTER_FILE,
      RenzFiConfig::VOUCHERS_FILE,
      RenzFiConfig::PORTAL_SESSIONS_FILE,
      RenzFiConfig::SALES_FILE,
      RenzFiConfig::PORTAL_CONFIG_FILE,
      StoragePaths::InstallationFile,
      StoragePaths::ProvisioningFile,
      StoragePaths::RouterConnectionFile,
      StoragePaths::RouterProvisioningFile,
      StoragePaths::SetupWizardFile,
  };
  if (_sdMounted) {
    for (const char *path : sdPaths) recoverSdTransaction(path);
  }
  if (_spiffsMounted) {
    recoverSpiffsTransaction(RenzFiConfig::FB_MANIFEST);
    for (const char *path : sdPaths) {
      const String fbPath = toFallbackPath(path);
      if (!fbPath.isEmpty()) recoverSpiffsTransaction(fbPath);
    }
  }
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
  else if (strcmp(sdPath, StoragePaths::InstallationFile) == 0 ||
           strcmp(sdPath, StoragePaths::ProvisioningFile) == 0 ||
           strcmp(sdPath, StoragePaths::RouterConnectionFile) == 0 ||
           strcmp(sdPath, StoragePaths::RouterProvisioningFile) == 0 ||
           strcmp(sdPath, StoragePaths::RouterCacheFile) == 0 ||
           strcmp(sdPath, StoragePaths::ExistingNetworkScanFile) == 0 ||
           strcmp(sdPath, StoragePaths::SetupWizardFile) == 0)
    json = "{}";
  else
    return false;

  return !deserializeJson(doc, json);
}

// ── Transactional SD I/O ─────────────────────────────────────────────────────

uint32_t StorageManager::payloadCrc(const String &payload) const {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < payload.length(); ++i) {
    crc ^= static_cast<uint8_t>(payload[i]);
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
    }
  }
  return ~crc;
}

bool StorageManager::validateJsonPayload(const String &payload) const {
  if (payload.isEmpty()) return false;
  PsramJsonDocument verify;
  return !deserializeJson(verify.doc(), payload);
}

bool StorageManager::readSdPayload(const char *path, String &payload) const {
  payload = "";
  if (!_sdMounted || !_sdReadable || !path) return false;
  // Prefer open-only (avoid a second sdSelectCard via SD.exists when media is
  // already gone — each select can block 500 ms on async_tcp).
  File file = SD.open(path, FILE_READ);
  if (!file) {
    auto *self = const_cast<StorageManager *>(this);
    if (SD.cardType() == CARD_NONE) {
      self->tripSdMediaMissing("readSdPayload card absent");
    } else {
      self->_sdIoFailStreak++;
      if (self->_sdIoFailStreak >= 2) {
        self->tripSdMediaMissing("readSdPayload open fail streak");
      }
    }
    return false;
  }
  const size_t size = file.size();
  payload.reserve(size + 8);
  // Buffered read — byte-at-a-time SD reads dominate voucher persist latency.
  constexpr size_t kChunk = 512;
  char buf[kChunk];
  while (file.available()) {
    const size_t n = file.read(reinterpret_cast<uint8_t *>(buf), kChunk);
    if (n == 0) break;
    for (size_t i = 0; i < n; ++i) payload += buf[i];
  }
  file.close();
  const_cast<StorageManager *>(this)->_sdIoFailStreak = 0;
  return true;
}

bool StorageManager::readSpiffsPayload(const String &path,
                                       String &payload) const {
  payload = "";
  if (!_spiffsMounted || !SPIFFS.exists(path)) return false;
  File file = SPIFFS.open(path, "r");
  if (!file) return false;
  const size_t size = file.size();
  payload.reserve(size + 8);
  constexpr size_t kChunk = 512;
  char buf[kChunk];
  while (file.available()) {
    const size_t n = file.read(reinterpret_cast<uint8_t *>(buf), kChunk);
    if (n == 0) break;
    for (size_t i = 0; i < n; ++i) payload += buf[i];
  }
  file.close();
  return true;
}

bool StorageManager::recoverSdTransaction(const char *path) {
  if (!_sdMounted || !_sdReadable || !path) return false;
  const String stage = String(path) + StoragePaths::TransactionStageSuffix;
  const String backup = String(path) + StoragePaths::TransactionBackupSuffix;
  String targetPayload;
  const bool openedOk = readSdPayload(path, targetPayload);
  if (openedOk && validateJsonPayload(targetPayload)) {
    if (SD.exists(stage)) SD.remove(stage);
    return true;
  }

  // readSdPayload already trips media-missing on open failure.
  if (!_sdMounted || !_sdReadable) return false;
  // A failed open incremented the fail streak. Do not probe .stage/.backup —
  // each SD.exists/open can sdSelectCard for ~500 ms on a missing card.
  if (_sdIoFailStreak > 0) return false;

  const bool targetExists = SD.exists(path);
  if (!_sdMounted || !_sdReadable) return false;

  String candidate;
  const String first = targetExists ? backup : stage;
  const String second = targetExists ? stage : backup;
  String selected;
  if (readSdPayload(first.c_str(), candidate) && validateJsonPayload(candidate)) {
    selected = first;
  } else if (readSdPayload(second.c_str(), candidate) &&
             validateJsonPayload(candidate)) {
    selected = second;
  } else {
    return false;
  }

  if (!_sdWritable) return false;
  if (targetExists) {
    const String bad = String(path) + ".bad";
    SD.remove(bad);
    SD.rename(path, bad);
  }
  if (!SD.rename(selected, path)) return false;
  if (SD.exists(stage)) SD.remove(stage);
  Serial.printf("[storage] Recovered SD transaction for %s\n", path);
  return true;
}

bool StorageManager::writeJsonToSdOnce(const char *path,
                                       const String &serialized,
                                       const char **failReasonOut) {
  DmaMemoryMonitor::ScopedProbe dmaProbe(
      (path && strstr(path, "sales") != nullptr) ? "sales-sd-write"
      : (path && strstr(path, "voucher") != nullptr)
          ? "voucher-sd-write"
          : "sd-writeJson");
  auto setReason = [&](const char *reason) {
    if (failReasonOut) *failReasonOut = reason;
  };
  const bool voucherDiag =
      path != nullptr && strstr(path, "vouchers") != nullptr;

  // Resolve any prior interrupted replace first so the retained backup is
  // always the immediately preceding valid committed value.
  recoverSdTransaction(path);
  const String stage = String(path) + StoragePaths::TransactionStageSuffix;
  const String backup = String(path) + StoragePaths::TransactionBackupSuffix;
  SD.remove(stage);
  File file = SD.open(stage, FILE_WRITE);
  if (!file) {
    setReason("OPEN_STAGE_FAILED");
    tripSdMediaMissing("writeJson open stage failed");
    return false;
  }
  const uint32_t tStage = millis();
  const size_t written = file.print(serialized);
  file.flush();
  file.close();
  if (written != serialized.length()) {
    SD.remove(stage);
    setReason("WRITE_FAILED");
    return false;
  }
  if (voucherDiag) {
    Serial.printf("[voucher-job] stage write elapsed=%lu\n",
                  static_cast<unsigned long>(millis() - tStage));
  }
  String staged;
  if (voucherDiag) Serial.println("[voucher-job] verify start");
  const uint32_t tVerify = millis();
  if (!readSdPayload(stage.c_str(), staged) || staged != serialized) {
    SD.remove(stage);
    setReason("READBACK_FAILED");
    return false;
  }
  if (!validateJsonPayload(staged)) {
    SD.remove(stage);
    setReason("JSON_VALIDATE_FAILED");
    return false;
  }
  if (voucherDiag) {
    Serial.printf("[voucher-job] verify complete elapsed=%lu\n",
                  static_cast<unsigned long>(millis() - tVerify));
  }

  if (SD.exists(path)) {
    // Prior successful writeJson already verified this file. Re-reading the
    // entire payload here doubled SD I/O on every voucher persist (~hundreds
    // of ms). Rename to backup without a redundant full read.
    SD.remove(backup);
    if (!SD.rename(path, backup)) {
      SD.remove(stage);
      setReason("BACKUP_RENAME_FAILED");
      return false;
    }
    if (voucherDiag) Serial.println("[voucher-job] backup rename ok");
  }
  if (!SD.rename(stage, path)) {
    SD.remove(path);
    if (SD.exists(backup)) SD.rename(backup, path);
    SD.remove(stage);
    setReason("FINAL_RENAME_FAILED");
    return false;
  }
  if (voucherDiag) Serial.println("[voucher-job] final rename ok");
  if (voucherDiag) Serial.println("[voucher-job] final verify start");
  const uint32_t tFinal = millis();
  if (!verifySdMatches(path, serialized)) {
    SD.remove(path);
    if (SD.exists(backup)) SD.rename(backup, path);
    SD.remove(stage);
    setReason("VERIFY_FAILED");
    return false;
  }
  if (voucherDiag) {
    Serial.printf("[voucher-job] final verify complete elapsed=%lu\n",
                  static_cast<unsigned long>(millis() - tFinal));
  }
  return true;
}

bool StorageManager::readJsonFromSd(const char *path, JsonDocument &doc) {
  if (!_sdMounted || !_sdReadable) return false;

  const bool salesPath = path && strstr(path, "sales") != nullptr;
  DmaMemoryMonitor::ScopedProbe dmaProbe(salesPath ? "sales-sd-read"
                                                   : "sd-readJson");
  // Recover interrupted transactions only when deserialize fails. The previous
  // eager recoverSdTransaction() read the whole file into an Arduino String and
  // re-parsed it (validateJsonPayload) on every sales chart/history/records
  // GET, allocating 6–8 KB of INTERNAL/DMA SRAM before the real parse.
  if (!_sdMounted || !_sdReadable) return false;

  File file = SD.open(path, FILE_READ);
  if (!file) {
    tripSdMediaMissing("readJson open failed");
    setError(String("Unable to open ") + path);
    return false;
  }

  DeserializationError err = deserializeJson(doc, file);
  file.close();

  if (err) {
    setError(String("JSON parse failed for ") + path + ": " + err.c_str());
    if (recoverSdTransaction(path)) {
      if (!_sdMounted || !_sdReadable) return false;
      File recovered = SD.open(path, FILE_READ);
      if (recovered) {
        const DeserializationError recoveredErr = deserializeJson(doc, recovered);
        recovered.close();
        if (!recoveredErr) {
          _sdIoFailStreak = 0;
          return true;
        }
      } else {
        tripSdMediaMissing("readJson recover open failed");
      }
    }
    return false;
  }
  _sdIoFailStreak = 0;
  return true;
}

bool StorageManager::writeJsonToSd(const char *path, JsonDocument &doc) {
  String serialized;
  serializeJson(doc, serialized);
  return writeJsonToSdSerialized(path, serialized);
}

bool StorageManager::writeJsonToSdSerialized(const char *path,
                                             const String &serialized) {
  FinishTrace::BlockingOpScope ioScope(
      FinishTrace::pipelineActive()
          ? FinishTrace::storageWriteOp(path, "SD card write")
          : FinishTrace::BlockingOpConfig{""});
  if (!_sdMounted || !_sdWritable) return false;
  for (uint8_t attempt = 0;
       attempt < RenzFiConfig::STORAGE_WRITE_ATTEMPTS; ++attempt) {
    const char *failReason = "UNKNOWN";
    if (writeJsonToSdOnce(path, serialized, &failReason)) return true;
    Serial.printf("[storage] SD write attempt %u/%u failed for %s reason=%s\n",
                  static_cast<unsigned>(attempt + 1),
                  static_cast<unsigned>(RenzFiConfig::STORAGE_WRITE_ATTEMPTS),
                  path, failReason);
    if (path && strstr(path, "portal_sessions") != nullptr) {
      Serial.printf(
          "[storage] portal_session_write attempt=%u result=fail reason=%s\n",
          static_cast<unsigned>(attempt + 1), failReason);
    }
  }
  if (FinishTrace::pipelineActive()) ioScope.fail("SD transaction failed");
  setError(String("Transactional SD write failed for ") + path);
  _sdWritable = false;
  _usingFallback = _spiffsMounted;
  setDiagnosticCause("TRANSACTION_FAILED");
  emitStorageChanged();
  refreshRuntimeSnapshot();
  return false;
}

// ── SPIFFS fallback I/O ─────────────────────────────────────────────────────

bool StorageManager::spiffsReadFile(const String &fbPath, JsonDocument &doc) {
  recoverSpiffsTransaction(fbPath);
  if (!SPIFFS.exists(fbPath)) return false;
  File file = SPIFFS.open(fbPath, "r");
  if (!file) return false;
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  return !err;
}

bool StorageManager::spiffsWriteFile(const String &fbPath, const String &data) {
  FinishTrace::BlockingOpScope ioScope(
      FinishTrace::pipelineActive()
          ? FinishTrace::storageWriteOp(fbPath.c_str(), "LittleFS write")
          : FinishTrace::BlockingOpConfig{""});
  recoverSpiffsTransaction(fbPath);
  const String stage = fbPath + StoragePaths::TransactionStageSuffix;
  const String backup = fbPath + StoragePaths::TransactionBackupSuffix;
  SPIFFS.remove(stage.c_str());
  File file = SPIFFS.open(stage, "w");
  if (!file) {
    if (FinishTrace::pipelineActive()) ioScope.fail("SPIFFS open failed");
    return false;
  }
  if (file.print(data) == 0) {
    if (FinishTrace::pipelineActive()) ioScope.fail("SPIFFS write failed");
    file.close();
    SPIFFS.remove(stage.c_str());
    return false;
  }
  file.flush();
  file.close();
  String staged;
  if (!readSpiffsPayload(stage, staged) || staged != data ||
      !validateJsonPayload(staged)) {
    SPIFFS.remove(stage.c_str());
    if (FinishTrace::pipelineActive()) ioScope.fail("SPIFFS verify failed");
    return false;
  }
  if (SPIFFS.exists(fbPath)) {
    String current;
    if (readSpiffsPayload(fbPath, current) && validateJsonPayload(current)) {
      SPIFFS.remove(backup.c_str());
      if (!SPIFFS.rename(fbPath, backup)) {
        SPIFFS.remove(stage.c_str());
        return false;
      }
    } else {
      SPIFFS.remove(fbPath);
    }
  }
  if (!SPIFFS.rename(stage, fbPath)) {
    if (FinishTrace::pipelineActive()) ioScope.fail("SPIFFS rename failed");
    if (SPIFFS.exists(backup)) SPIFFS.rename(backup, fbPath);
    return false;
  }
  String committed;
  if (!readSpiffsPayload(fbPath, committed) || committed != data) {
    SPIFFS.remove(fbPath);
    if (SPIFFS.exists(backup)) SPIFFS.rename(backup, fbPath);
    return false;
  }
  return true;
}

bool StorageManager::recoverSpiffsTransaction(const String &path) {
  if (!_spiffsMounted) return false;
  const String stage = path + StoragePaths::TransactionStageSuffix;
  const String backup = path + StoragePaths::TransactionBackupSuffix;
  String target;
  if (readSpiffsPayload(path, target) && validateJsonPayload(target)) {
    if (SPIFFS.exists(stage)) SPIFFS.remove(stage);
    return true;
  }
  const bool targetExists = SPIFFS.exists(path);
  const String first = targetExists ? backup : stage;
  const String second = targetExists ? stage : backup;
  String candidate;
  String selected;
  if (readSpiffsPayload(first, candidate) && validateJsonPayload(candidate)) {
    selected = first;
  } else if (readSpiffsPayload(second, candidate) &&
             validateJsonPayload(candidate)) {
    selected = second;
  } else {
    return false;
  }
  if (targetExists) SPIFFS.remove(path);
  if (!SPIFFS.rename(selected, path)) return false;
  if (SPIFFS.exists(stage)) SPIFFS.remove(stage);
  Serial.printf("[storage] Recovered SPIFFS transaction for %s\n", path.c_str());
  return true;
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
  const char *paths[] = {
      RenzFiConfig::FB_SETTINGS,          RenzFiConfig::FB_PROMOS,
      RenzFiConfig::FB_ROUTER,            RenzFiConfig::FB_VOUCHERS,
      RenzFiConfig::FB_PORTAL_SESSIONS,   RenzFiConfig::FB_SALES,
      RenzFiConfig::FB_PORTAL_CONFIG,     RenzFiConfig::FB_INSTALLATION,
      RenzFiConfig::FB_PROVISIONING,      RenzFiConfig::FB_ROUTER_CONNECTION,
      RenzFiConfig::FB_ROUTER_PROVISIONING,
      RenzFiConfig::FB_SETUP_WIZARD,      RenzFiConfig::FB_ROUTER_CACHE,
      RenzFiConfig::FB_EXISTING_NETWORK_SCAN, RenzFiConfig::FB_MANIFEST,
      StoragePaths::Spiffs::SalesHistorySpool,
      StoragePaths::Spiffs::SessionsHistorySpool,
      StoragePaths::Spiffs::VouchersHistorySpool,
  };
  for (const char *path : paths) {
    const String base(path);
    total += spiffsFileSize(base);
    total += spiffsFileSize(base + StoragePaths::TransactionStageSuffix);
    total += spiffsFileSize(base + StoragePaths::TransactionBackupSuffix);
  }
  total += spiffsFileSize(
      String(StoragePaths::Spiffs::SalesHistorySpool) + ".q");
  total += spiffsFileSize(
      String(StoragePaths::Spiffs::SessionsHistorySpool) + ".q");
  total += spiffsFileSize(
      String(StoragePaths::Spiffs::VouchersHistorySpool) + ".q");
  return total;
}

bool StorageManager::checkQuota(const char *sdPath, size_t newSize,
                                size_t oldSize) const {
  const size_t limit = perFileLimit(sdPath);
  if (limit > 0 && newSize > limit) {
    Serial.println("[storage] SPIFFS fallback quota exceeded (per-file)");
    return false;
  }

  const String fbPath = toFallbackPath(sdPath);
  const size_t backupSize =
      fbPath.isEmpty()
          ? 0
          : spiffsFileSize(fbPath + StoragePaths::TransactionBackupSuffix);
  const size_t totalNow = fallbackTotalBytes();
  const size_t totalAfter = totalNow - backupSize + newSize;
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
  (void)oldSize;
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
                                       bool forcePortalWrite,
                                       uint32_t baseCrc) {
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

  if (!addToManifest(fbPath, serialized.length(), baseCrc,
                     payloadCrc(serialized))) {
    return false;
  }
  Serial.printf("[storage] SPIFFS fallback write %s (%u bytes)\n", fbPath.c_str(),
                (unsigned)serialized.length());
  return true;
}

bool StorageManager::checkpointToSpiffs(const char *sdPath,
                                        const String &serialized) {
  if (!_spiffsMounted || !isContinuousCheckpointEligible(sdPath)) return false;
  const String fbPath = toFallbackPath(sdPath);
  uint32_t ignoredBase = 0;
  uint32_t ignoredPayload = 0;
  if (manifestEntry(fbPath, ignoredBase, ignoredPayload)) {
    return false;  // Never replace an unsynced fallback checkpoint.
  }
  if (isPortalWriteThrottled(sdPath, false)) return false;
  const size_t oldSize = spiffsFileSize(fbPath);
  if (!checkQuota(sdPath, serialized.length(), oldSize)) return false;
  if (!spiffsWriteFile(fbPath, serialized)) return false;
  if (strcmp(sdPath, RenzFiConfig::PORTAL_SESSIONS_FILE) == 0) {
    _lastFbPortalWriteMs = millis();
  }
  return true;
}

// ── Public JSON API ───────────────────────────────────────────────────────────

bool StorageManager::readJson(const char *path, JsonDocument &doc) {
  ScopedStorageLock lock(*this);
  if (!lock) return false;
  if (_sdReadable) {
    if (_usingFallback && !_sdWritable && _spiffsMounted &&
        isFallbackEligible(path) && SPIFFS.exists(toFallbackPath(path))) {
      return readJsonFromSpiffs(path, doc);
    }
    if (_spiffsMounted && isFallbackEligible(path)) {
      uint32_t baseCrc = 0;
      uint32_t currentCrc = 0;
      if (manifestEntry(toFallbackPath(path), baseCrc, currentCrc)) {
        // Dirty SPIFFS must not permanently mask healthy SD. If SD is writable
        // and holds valid JSON for this path, clear the stale dirty entry and
        // prefer SD (RESET must not resurrect pre-SD-write fallback).
        if (_sdWritable) {
          String sdPayload;
          if (readSdPayload(path, sdPayload) && validateJsonPayload(sdPayload)) {
            removeFromManifest(toFallbackPath(path));
            if (readJsonFromSd(path, doc)) return true;
          }
        }
        return readJsonFromSpiffs(path, doc);
      }
    }
    if (readJsonFromSd(path, doc)) return true;
    if (_spiffsMounted && isFallbackEligible(path)) {
      return readJsonFromSpiffs(path, doc);
    }
    return false;
  }

  if (!_usingFallback || !_spiffsMounted || !isFallbackEligible(path))
    return false;

  return readJsonFromSpiffs(path, doc);
}

bool StorageManager::writeJson(const char *path, const JsonDocument &doc,
                               bool forcePortalWrite) {
  const bool voucherDiag =
      path != nullptr && strstr(path, "vouchers") != nullptr;
  const uint32_t t0 = millis();

  ScopedStorageLock lock(*this);
  if (!lock) {
    if (voucherDiag) {
      Serial.printf(
          "[voucher-job] storage fail stage=lock elapsed=%lu reason=timeout\n",
          static_cast<unsigned long>(millis() - t0));
    }
    return false;
  }
  if (voucherDiag) {
    Serial.printf("[voucher-job] storage lock acquired elapsed=%lu\n",
                  static_cast<unsigned long>(millis() - t0));
    Serial.println("[voucher-job] serialize start");
  }
  const uint32_t tSer = millis();
  String serialized;
  serializeJson(doc, serialized);
  if (serialized.length() == 0) {
    setError(String("JSON serialize failed for ") + path);
    if (voucherDiag) {
      Serial.printf(
          "[voucher-job] storage fail stage=serialize elapsed=%lu reason=empty\n",
          static_cast<unsigned long>(millis() - tSer));
    }
    return false;
  }
  if (voucherDiag) {
    Serial.printf(
        "[voucher-job] serialize complete elapsed=%lu bytes=%u\n",
        static_cast<unsigned long>(millis() - tSer),
        static_cast<unsigned>(serialized.length()));
  }
  const uint32_t originalPayloadCrc = payloadCrc(serialized);

  // Defer baseCrc SD read until SD write fails and SPIFFS fallback is needed.
  // On the success path this avoided a full vouchers.json read (~hundreds ms).
  uint32_t baseCrc = 0;
  bool baseCrcReady = false;

  if (_sdWritable) {
    if (voucherDiag) Serial.println("[voucher-job] write start");
    const uint32_t tWrite = millis();
    if (writeJsonToSdSerialized(path, serialized)) {
      if (voucherDiag) {
        Serial.printf("[voucher-job] write complete elapsed=%lu\n",
                      static_cast<unsigned long>(millis() - tWrite));
      }
      // Successful SD persistence is authoritative. A prior dirty SPIFFS
      // fallback for this path must not continue to override SD on readJson.
      // checkpointToSpiffs refuses to replace dirty entries, so clear first.
      if (voucherDiag) {
        Serial.println("[voucher-job] checkpoint phase=manifest_clear start");
      }
      const uint32_t tManifest = millis();
      removeFromManifest(toFallbackPath(path));
      if (voucherDiag) {
        Serial.printf(
            "[voucher-job] checkpoint phase=manifest_clear complete elapsed=%lu\n",
            static_cast<unsigned long>(millis() - tManifest));
      }
      if (voucherDiag) Serial.println("[voucher-job] checkpoint start");
      const uint32_t tCp = millis();
      if (voucherDiag) {
        Serial.println("[voucher-job] checkpoint phase=spiffs_mirror start");
      }
      const uint32_t tMirror = millis();
      const bool mirrored = checkpointToSpiffs(path, serialized);
      if (voucherDiag) {
        Serial.printf(
            "[voucher-job] checkpoint phase=spiffs_mirror complete "
            "elapsed=%lu mirrored=%s\n",
            static_cast<unsigned long>(millis() - tMirror),
            mirrored ? "yes" : "skipped");
        Serial.printf("[voucher-job] checkpoint complete elapsed=%lu\n",
                      static_cast<unsigned long>(millis() - tCp));
      }
      noteSuccessfulWrite();
      return true;
    }
    if (voucherDiag) {
      Serial.printf(
          "[voucher-job] storage fail stage=write elapsed=%lu reason=sd\n",
          static_cast<unsigned long>(millis() - tWrite));
    }
  }

  if (!_spiffsMounted || !isFallbackEligible(path))
    return false;

  if (!baseCrcReady && _sdReadable) {
    String current;
    if (readSdPayload(path, current) && validateJsonPayload(current)) {
      baseCrc = payloadCrc(current);
      baseCrcReady = true;
    }
  }
  (void)baseCrcReady;

  // The exact immutable serialization attempted on SD is the fallback payload.
  // Detect any accidental future mutation before committing degraded storage.
  if (payloadCrc(serialized) != originalPayloadCrc) {
    setError(String("Fallback payload changed after SD failure for ") + path);
    return false;
  }
  _usingFallback = true;
  if (voucherDiag) Serial.println("[voucher-job] fallback write start");
  const uint32_t tFb = millis();
  const bool written =
      writeJsonToSpiffs(path, serialized, forcePortalWrite, baseCrc);
  if (voucherDiag) {
    Serial.printf(
        "[voucher-job] fallback write %s elapsed=%lu\n",
        written ? "complete" : "fail",
        static_cast<unsigned long>(millis() - tFb));
  }
  if (written) noteSuccessfulWrite();
  return written;
}

bool StorageManager::appendJsonArrayItem(const char *path, JsonObject item,
                                         size_t capacity) {
  ScopedStorageLock lock(*this);
  if (!lock) return false;
  // Large JSON docs must live on the heap — async_tcp stack is ~8 KB.
  if (capacity > RenzFiConfig::JSON_DOC_SMALL) {
    HeapJsonDocument heapDoc(capacity);
    DynamicJsonDocument &doc = heapDoc.doc();
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
  ScopedStorageLock lock(*this);
  if (!lock) return false;
  DynamicJsonDocument doc(128);
  doc.to<JsonArray>();
  return writeJson(path, doc);
}

bool StorageManager::appendHistory(NdjsonLedger::Kind kind,
                                   const String &eventId,
                                   const String &eventAt,
                                   JsonObjectConst event,
                                   bool allowSpool) {
  ScopedStorageLock lock(*this);
  if (!lock || eventId.isEmpty()) return false;
  if (_sdWritable && NdjsonLedger::appendSd(kind, eventId, eventAt, event)) {
    noteSuccessfulWrite();
    return true;
  }
  if (_sdMounted && _sdWritable) {
    // Append failed while we still believed SD was writable — fail fast.
    tripSdMediaMissing("history appendSd failed");
  }
  if (!allowSpool || !_spiffsMounted) return false;
  const bool appended = NdjsonLedger::appendSpool(
      kind, eventId, eventAt, event, fallbackTotalBytes());
  if (appended) noteSuccessfulWrite();
  return appended;
}

bool StorageManager::appendHistoryPreparedLines(NdjsonLedger::Kind kind,
                                                const String &eventAt,
                                                const String *eventIds,
                                                const String *lines,
                                                size_t count,
                                                bool allowSpool) {
  ScopedStorageLock lock(*this);
  if (!lock || !eventIds || !lines || count == 0) return false;
  if (_sdWritable &&
      NdjsonLedger::appendSdPreparedLines(kind, eventAt, eventIds, lines,
                                          count)) {
    noteSuccessfulWrite();
    return true;
  }
  if (!allowSpool || !_spiffsMounted) return false;
  bool any = false;
  for (size_t i = 0; i < count; ++i) {
    if (eventIds[i].isEmpty() || lines[i].isEmpty()) continue;
    DynamicJsonDocument doc(lines[i].length() + 64);
    if (deserializeJson(doc, lines[i])) continue;
    doc.remove("eventId");
    doc.remove("eventAt");
    if (NdjsonLedger::appendSpool(kind, eventIds[i], eventAt,
                                  doc.as<JsonObjectConst>(),
                                  fallbackTotalBytes())) {
      any = true;
    }
  }
  if (any) noteSuccessfulWrite();
  return any;
}

bool StorageManager::historyPath(NdjsonLedger::Kind kind,
                                 const String &month, String &path) const {
  ScopedStorageLock lock(*this);
  if (!lock) return false;
  return NdjsonLedger::pathFor(kind, month, path);
}

bool StorageManager::replayHistorySpools() {
  ScopedStorageLock lock(*this);
  if (!lock || !_sdWritable || !_spiffsMounted) return false;

  uint32_t before = 0;
  const char *spools[] = {
      StoragePaths::Spiffs::SalesHistorySpool,
      StoragePaths::Spiffs::SessionsHistorySpool,
      StoragePaths::Spiffs::VouchersHistorySpool,
  };
  for (const char *spool : spools) before += countSpoolRecords(spool);

  const bool ok = NdjsonLedger::replaySpools();

  uint32_t after = 0;
  for (const char *spool : spools) after += countSpoolRecords(spool);
  const uint32_t recovered =
      before > after ? static_cast<uint32_t>(before - after) : 0U;
  const uint32_t remaining = after;
  _lastReplaySummary.historyRecords =
      static_cast<uint16_t>(recovered > 0xFFFFU ? 0xFFFFU : recovered);
  if (!ok && remaining > 0) {
    _lastReplaySummary.skipped = static_cast<uint16_t>(
        remaining > 0xFFFFU ? 0xFFFFU : remaining);
  }
  _hasSuccessfulReplay = ok || recovered > 0;
  if (_hasSuccessfulReplay) _lastSuccessfulReplayMs = millis();
  _lastReplaySummary.completedAtMs = millis();
  _lastReplaySummary.valid = true;

  Serial.printf(
      "[storage] History replay: recovered=%u remaining=%u ok=%s\n",
      static_cast<unsigned>(recovered), static_cast<unsigned>(remaining),
      ok ? "yes" : "no");
  Serial.println(ok ? "[storage] History spools replayed"
                    : "[storage] History spool replay incomplete");
  return ok;
}

bool StorageManager::exists(const char *path) const {
  ScopedStorageLock lock(*this);
  if (!lock) return false;
  if (_sdReadable && _sdMounted) {
    // Open-probe avoids a dedicated exists()+select when media is missing.
    File probe = SD.open(path, FILE_READ);
    if (probe) {
      probe.close();
      const_cast<StorageManager *>(this)->_sdIoFailStreak = 0;
      return true;
    }
    if (SD.cardType() == CARD_NONE) {
      const_cast<StorageManager *>(this)->tripSdMediaMissing(
          "exists card absent");
    }
  }
  if (!_usingFallback || !_spiffsMounted || !isFallbackEligible(path))
    return false;
  return SPIFFS.exists(toFallbackPath(path));
}

size_t StorageManager::fileSizeBytes(const char *path) const {
  ScopedStorageLock lock(*this);
  if (!lock) return 0;
  if (path == nullptr) return 0;
  if (_sdReadable && _sdMounted) {
    File file = SD.open(path, FILE_READ);
    if (!file) {
      if (SD.cardType() == CARD_NONE) {
        const_cast<StorageManager *>(this)->tripSdMediaMissing(
            "fileSize card absent");
      }
    } else {
      size_t bytes = file.size();
      file.close();
      const_cast<StorageManager *>(this)->_sdIoFailStreak = 0;
      return bytes;
    }
  }
  if (!_usingFallback || !_spiffsMounted || !isFallbackEligible(path)) return 0;
  return spiffsFileSize(toFallbackPath(path));
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
  if (serialized.length() > RenzFiConfig::FB_LIMIT_MANIFEST) return false;
  return spiffsWriteFile(RenzFiConfig::FB_MANIFEST, serialized);
}

bool StorageManager::manifestEntry(const String &fbPath, uint32_t &baseCrc,
                                   uint32_t &storedPayloadCrc) {
  baseCrc = 0;
  storedPayloadCrc = 0;
  DynamicJsonDocument manifest(RenzFiConfig::FB_LIMIT_MANIFEST);
  if (!readManifest(manifest)) return false;
  JsonArray files = manifest["files"].as<JsonArray>();
  bool listed = false;
  for (JsonVariant v : files) {
    if (strcmp(v.as<const char *>(), fbPath.c_str()) == 0) {
      listed = true;
      break;
    }
  }
  if (!listed) return false;
  JsonObject meta = manifest["meta"][fbPath].as<JsonObject>();
  if (!meta.isNull()) {
    baseCrc = meta["baseCrc"] | 0U;
    storedPayloadCrc = meta["payloadCrc"] | 0U;
  }
  return true;
}

bool StorageManager::addToManifest(const String &fbPath, size_t fileBytes,
                                   uint32_t baseCrc,
                                   uint32_t storedPayloadCrc) {
  DynamicJsonDocument manifest(RenzFiConfig::FB_LIMIT_MANIFEST);
  if (!readManifest(manifest)) {
    manifest["v"] = 2;
    manifest["dirty"] = true;
    manifest["bytes"] = 0;
    manifest["generation"] = 0;
    manifest["files"].to<JsonArray>();
    manifest["meta"].to<JsonObject>();
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
  manifest["v"] = 2;
  const uint32_t generation = (manifest["generation"] | 0U) + 1U;
  manifest["generation"] = generation;
  JsonObject meta = manifest["meta"][fbPath].to<JsonObject>();
  meta["generation"] = generation;
  meta["baseCrc"] = baseCrc;
  meta["payloadCrc"] = storedPayloadCrc;
  meta["bytes"] = fileBytes;

  size_t total = 0;
  for (JsonVariant v : files) {
    total += spiffsFileSize(String(v.as<const char *>()));
  }
  manifest["bytes"] = total;

  return writeManifest(manifest);
}

void StorageManager::removeFromManifest(const String &fbPath) {
  DynamicJsonDocument manifest(RenzFiConfig::FB_LIMIT_MANIFEST);
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
  manifest["meta"].as<JsonObject>().remove(fbPath);
  writeManifest(manifest);
}

bool StorageManager::verifySdMatches(const char *sdPath,
                                     const String &expected) {
  File file = SD.open(sdPath, FILE_READ);
  if (!file) return false;
  if (file.size() != expected.length()) {
    file.close();
    return false;
  }
  // Buffered compare — same byte-equality guarantee as before.
  constexpr size_t kChunk = 512;
  uint8_t buf[kChunk];
  size_t offset = 0;
  while (offset < expected.length()) {
    const size_t want = expected.length() - offset;
    const size_t n = file.read(buf, want > kChunk ? kChunk : want);
    if (n == 0) {
      file.close();
      return false;
    }
    for (size_t i = 0; i < n; ++i) {
      if (buf[i] != static_cast<uint8_t>(expected[offset + i])) {
        file.close();
        return false;
      }
    }
    offset += n;
  }
  file.close();
  return true;
}

bool StorageManager::syncFallbackToSd() {
  ScopedStorageLock lock(*this);
  if (!lock) return false;
  if (!_healthy || !_spiffsMounted) return false;
  if (_syncInProgress) return false;

  _syncInProgress = true;
  const uint32_t startMs = millis();
  ReplaySummary summary;
  summary.valid = true;
  summary.completedAtMs = startMs;
  uint16_t skipped = 0;

  DynamicJsonDocument manifest(2048);
  if (!readManifest(manifest)) {
    _syncInProgress = false;
    return true;
  }

  JsonArray files = manifest["files"].as<JsonArray>();
  if (files.isNull() || files.size() == 0) {
    SPIFFS.remove(RenzFiConfig::FB_MANIFEST);
    SPIFFS.remove((String(RenzFiConfig::FB_MANIFEST) +
                   StoragePaths::TransactionStageSuffix).c_str());
    SPIFFS.remove((String(RenzFiConfig::FB_MANIFEST) +
                   StoragePaths::TransactionBackupSuffix).c_str());
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
    if (!sdPath || !SPIFFS.exists(fbPath)) {
      ++skipped;
      continue;
    }

    File in = SPIFFS.open(fbPath, "r");
    if (!in) {
      allOk = false;
      ++skipped;
      continue;
    }
    String payload;
    payload.reserve(in.size() + 8);
    while (in.available()) payload += (char)in.read();
    in.close();

    uint32_t baseCrc = 0;
    uint32_t expectedCrc = 0;
    manifestEntry(fbPath, baseCrc, expectedCrc);
    uint32_t generation = 0;
    if (manifest["meta"][fbPath].is<JsonObject>()) {
      generation = manifest["meta"][fbPath]["generation"] | 0U;
    }
    const uint32_t fallbackCrc = payloadCrc(payload);
    if (expectedCrc != 0 && expectedCrc != fallbackCrc) {
      allOk = false;
      _snapshotCrcHealthKnown = true;
      _snapshotCrcHealthy = false;
      recordConflict(sdPath, generation, baseCrc, 0, fallbackCrc);
      ++summary.conflicts;
      Serial.printf("[storage] Sync conflict: fallback CRC changed for %s\n",
                    sdPath);
      continue;
    }

    String current;
    const bool sdExists = readSdPayload(sdPath, current);
    if (sdExists) {
      const uint32_t currentCrc = payloadCrc(current);
      if (currentCrc == fallbackCrc) {
        SPIFFS.remove((String(fbPath) +
                       StoragePaths::TransactionStageSuffix).c_str());
        SPIFFS.remove((String(fbPath) +
                       StoragePaths::TransactionBackupSuffix).c_str());
        removeFromManifest(fbPath);
        filesSynced++;
        bytesSynced += payload.length();
        if (summary.fileCount < RenzFiConfig::STORAGE_REPLAY_FILE_CAP) {
          summary.files[summary.fileCount++] = fallbackFileLabel(fbPath);
        }
        continue;
      }
      if (baseCrc == 0 || currentCrc != baseCrc) {
        allOk = false;
        _snapshotCrcHealthKnown = true;
        _snapshotCrcHealthy = false;
        recordConflict(sdPath, generation, baseCrc, currentCrc, fallbackCrc);
        ++summary.conflicts;
        Serial.printf("[storage] Sync conflict: divergent SD retained for %s\n",
                      sdPath);
        continue;
      }
    }

    if (!writeJsonToSdSerialized(sdPath, payload)) {
      allOk = false;
      ++skipped;
      Serial.printf("[storage] Sync failed for %s\n", sdPath);
      continue;
    }
    if (!verifySdMatches(sdPath, payload)) {
      allOk = false;
      _snapshotCrcHealthKnown = true;
      _snapshotCrcHealthy = false;
      ++skipped;
      Serial.printf("[storage] Sync verify failed for %s\n", sdPath);
      continue;
    }

    SPIFFS.remove((String(fbPath) +
                   StoragePaths::TransactionStageSuffix).c_str());
    SPIFFS.remove((String(fbPath) +
                   StoragePaths::TransactionBackupSuffix).c_str());
    removeFromManifest(fbPath);
    filesSynced++;
    bytesSynced += payload.length();
    if (summary.fileCount < RenzFiConfig::STORAGE_REPLAY_FILE_CAP) {
      summary.files[summary.fileCount++] = fallbackFileLabel(fbPath);
    }
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
      SPIFFS.remove((String(RenzFiConfig::FB_MANIFEST) +
                     StoragePaths::TransactionStageSuffix).c_str());
      SPIFFS.remove((String(RenzFiConfig::FB_MANIFEST) +
                     StoragePaths::TransactionBackupSuffix).c_str());
      manifestCleared = true;
      _snapshotCrcHealthKnown = true;
      _snapshotCrcHealthy = true;
      if (summary.conflicts == 0) clearConflicts();
      Serial.println("[storage] Sync successful");
      Serial.println("[storage] Dirty fallback cleared; checkpoints retained");
    }
  } else {
    Serial.println("[storage] Sync incomplete — fallback retained for retry");
  }

  summary.skipped = skipped;
  summary.completedAtMs = millis();
  // Preserve history counters if history already replayed in this recovery.
  summary.historyRecords = _lastReplaySummary.historyRecords;
  _lastReplaySummary = summary;
  if (filesSynced > 0 || summary.conflicts > 0) {
    _hasSuccessfulReplay = true;
    _lastSuccessfulReplayMs = millis();
  }

  Serial.printf(
      "[storage] Replay summary: files=%u history=%u skipped=%u conflicts=%u\n",
      static_cast<unsigned>(summary.fileCount),
      static_cast<unsigned>(summary.historyRecords),
      static_cast<unsigned>(summary.skipped),
      static_cast<unsigned>(summary.conflicts));
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
  ScopedStorageLock lock(*this);
  if (!lock) return File();
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
  ScopedStorageLock lock(*this);
  if (!lock) return false;
  if (!_sdMounted || !_sdWritable || !sdPath || !data || len == 0) return false;

  String path = String(sdPath);
  int slash = path.lastIndexOf('/');
  if (slash > 0) {
    String dir = path.substring(0, slash);
    if (!ensureDir(dir.c_str())) return false;
  }

  const String stage = String(sdPath) + StoragePaths::TransactionStageSuffix;
  const String backup = String(sdPath) + StoragePaths::TransactionBackupSuffix;
  SD.remove(stage);
  File out = SD.open(stage, FILE_WRITE);
  if (!out) {
    setError("Unable to open SD file for write");
    return false;
  }
  const size_t written = out.write(data, len);
  out.flush();
  out.close();
  File verify = SD.open(stage, FILE_READ);
  bool verified = written == len && verify && verify.size() == len;
  for (size_t i = 0; verified && i < len; ++i) {
    verified = verify.read() == data[i];
  }
  if (verify) verify.close();
  if (!verified) {
    SD.remove(stage);
    return false;
  }
  SD.remove(backup);
  if (SD.exists(sdPath) && !SD.rename(sdPath, backup)) {
    SD.remove(stage);
    return false;
  }
  if (!SD.rename(stage, sdPath)) {
    if (SD.exists(backup)) SD.rename(backup, sdPath);
    return false;
  }
  noteSuccessfulWrite();
  return true;
}

bool StorageManager::writeBinarySpiffs(const char *spiffsPath,
                                       const uint8_t *data, size_t len) {
  ScopedStorageLock lock(*this);
  if (!lock) return false;
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

  const String stage =
      String(spiffsPath) + StoragePaths::TransactionStageSuffix;
  const String backup =
      String(spiffsPath) + StoragePaths::TransactionBackupSuffix;
  SPIFFS.remove(stage);
  File out = SPIFFS.open(stage, "w");
  if (!out) {
    setError("Unable to open SPIFFS file for write");
    return false;
  }
  const size_t written = out.write(data, len);
  out.flush();
  out.close();
  File verify = SPIFFS.open(stage, "r");
  bool verified = written == len && verify && verify.size() == len;
  for (size_t i = 0; verified && i < len; ++i) {
    verified = verify.read() == data[i];
  }
  if (verify) verify.close();
  if (!verified) {
    SPIFFS.remove(stage);
    return false;
  }
  SPIFFS.remove(backup);
  if (SPIFFS.exists(spiffsPath) && !SPIFFS.rename(spiffsPath, backup)) {
    SPIFFS.remove(stage);
    return false;
  }
  if (!SPIFFS.rename(stage, spiffsPath)) {
    if (SPIFFS.exists(backup)) SPIFFS.rename(backup, spiffsPath);
    return false;
  }
  noteSuccessfulWrite();
  return true;
}

bool StorageManager::removeBinary(const char *sdPath, const char *spiffsPath) {
  ScopedStorageLock lock(*this);
  if (!lock) return false;
  bool ok = true;
  if (_healthy && sdPath && SD.exists(sdPath)) ok = SD.remove(sdPath) && ok;
  if (spiffsPath && SPIFFS.exists(spiffsPath)) ok = SPIFFS.remove(spiffsPath) && ok;
  if (_sdMounted && sdPath) {
    SD.remove((String(sdPath) + StoragePaths::TransactionStageSuffix).c_str());
    SD.remove((String(sdPath) + StoragePaths::TransactionBackupSuffix).c_str());
  }
  if (_spiffsMounted && spiffsPath) {
    SPIFFS.remove(
        (String(spiffsPath) + StoragePaths::TransactionStageSuffix).c_str());
    SPIFFS.remove(
        (String(spiffsPath) + StoragePaths::TransactionBackupSuffix).c_str());
  }
  return ok;
}

uint64_t StorageManager::getSpiffsUsedBytes() const {
  ScopedStorageLock lock(*this);
  if (!lock) return 0;
  return _spiffsMounted ? SPIFFS.usedBytes() : 0;
}

uint64_t StorageManager::getSpiffsTotalBytes() const {
  ScopedStorageLock lock(*this);
  if (!lock) return 0;
  return _spiffsMounted ? SPIFFS.totalBytes() : 0;
}

bool StorageManager::isSdPresent() const {
  return _sdPresent;
}

bool StorageManager::isSdMounted() const {
  return _sdMounted;
}

bool StorageManager::isSdReadable() const {
  return _sdReadable;
}

uint64_t StorageManager::getSdUsedBytes() const {
  DashboardStorageSnap snap;
  portENTER_CRITICAL(&_dashSnapMux);
  snap = _dashSnap;
  portEXIT_CRITICAL(&_dashSnapMux);
  return snap.sdUsedBytes;
}

uint64_t StorageManager::getSdTotalBytes() const {
  DashboardStorageSnap snap;
  portENTER_CRITICAL(&_dashSnapMux);
  snap = _dashSnap;
  portEXIT_CRITICAL(&_dashSnapMux);
  return snap.sdTotalBytes;
}

uint64_t StorageManager::getSdFreeBytes() const {
  DashboardStorageSnap snap;
  portENTER_CRITICAL(&_dashSnapMux);
  snap = _dashSnap;
  portEXIT_CRITICAL(&_dashSnapMux);
  return snap.sdFreeBytes;
}

namespace {

double bytesToMb(uint64_t bytes) {
  return round((bytes / 1024.0 / 1024.0) * 10.0) / 10.0;
}

// ArduinoJson 7 treats `const char[N]` (string literals / const snapshot
// buffers) as length N-1, which embeds trailing NULs. JSON.parse then fails
// with "Bad control character in string literal". Decay to const char* so
// serializeJson uses strlen() like the pre-snapshot String/literal contract.
const char *jsonCString(const char *value) { return value ? value : ""; }

}  // namespace

void StorageManager::fillSdStatus(JsonObject sd) const {
  DashboardStorageSnap snap;
  portENTER_CRITICAL(&_dashSnapMux);
  snap = _dashSnap;
  portEXIT_CRITICAL(&_dashSnapMux);

  sd["present"] = snap.sdPresent;
  sd["mounted"] = snap.sdMounted;
  sd["readable"] = snap.sdReadable;
  sd["writable"] = snap.sdWritable;
  sd["readOnly"] = snap.sdMounted && snap.sdReadable && !snap.sdWritable;
  sd["fallback"] = snap.usingFallback;
  sd["pollingDisabled"] = snap.pollingDisabled;
  sd["recoveryAttempts"] = snap.recoveryAttempts;
  sd["mode"] = jsonCString(snap.sdMode);
  sd["sdLifecycle"] = jsonCString(snap.sdLifecycle);
  sd["recoveryInProgress"] = snap.recoveryInProgress;
  sd["usedMb"] = snap.sdMounted ? (snap.sdTotalBytes == 0 ? 0 : snap.usedMb) : 0;
  sd["totalMb"] = snap.sdMounted ? snap.totalMb : 0;
  sd["freeMb"] = snap.sdMounted ? snap.freeMb : 0;
  sd["status"] = jsonCString(snap.sdStatus);
}

void StorageManager::fillStorageStatus(JsonObject storage) const {
  DashboardStorageSnap snap;
  portENTER_CRITICAL(&_dashSnapMux);
  snap = _dashSnap;
  portEXIT_CRITICAL(&_dashSnapMux);
  fillStorageStatusFromSnap(storage, snap);
}

void StorageManager::publishDashboardSnapUnlocked() {
  DashboardStorageSnap next;
  next.valid = true;
  next.refreshedAtMs = millis();
  next.healthy = _healthy;
  next.usingFallback = _usingFallback;
  next.fallbackActive = _snapshotFallbackActive;
  next.spiffsMounted = _spiffsMounted;
  next.sdPresent = _snapshotSdPresent;
  next.sdMounted = _snapshotSdMounted;
  next.sdReadable = _sdReadable;
  next.sdWritable = _sdWritable;
  next.sdMountFailed = _sdMountFailed;
  next.pollingDisabled = _disableSdPolling;
  next.watchMode = _watchMode;
  next.recoveryInProgress = _sdRecoveryInProgress;
  strlcpy(next.sdLifecycle, sdLifecycleName(), sizeof(next.sdLifecycle));
  next.recoveryAttempts = _sdRetryCount;
  next.retryRemaining =
      _sdRetryCount >= RenzFiConfig::SD_RECOVERY_MAX_ATTEMPTS
          ? 0
          : static_cast<uint8_t>(RenzFiConfig::SD_RECOVERY_MAX_ATTEMPTS -
                                 _sdRetryCount);
  if (_snapshotSdMounted) {
    next.sdUsedBytes = _snapshotUsedBytes;
    next.sdTotalBytes = _snapshotTotalBytes;
    next.sdFreeBytes = _snapshotFreeBytes;
  }
  next.spiffsUsedBytes = _snapshotSpiffsUsedBytes;
  next.spiffsTotalBytes = _snapshotSpiffsTotalBytes;
  next.fsUsedBytes = _snapshotUsedBytes;
  next.fsTotalBytes = _snapshotTotalBytes;
  next.fsFreeBytes = _snapshotFreeBytes;
  next.logsBytes = _snapshotLogsBytes;
  next.usedMb = _snapshotUsedMb;
  next.totalMb = bytesToMb(next.sdTotalBytes);
  next.freeMb = bytesToMb(next.sdFreeBytes);
  next.capacityMb = _snapshotCapacityMb;
  next.emergencyBytes = _snapshotEmergencyBytes;
  next.emergencyPercent = _snapshotEmergencyPercent;
  next.pendingReplay = _snapshotPendingReplay;
  next.recoveryQueue = _snapshotRecoveryQueue;
  next.pendingConflicts = _snapshotPendingConflicts;
  next.journalKnown = _snapshotJournalHealthKnown;
  next.journalHealthy = _snapshotJournalHealthy;
  next.crcKnown = _snapshotCrcHealthKnown;
  next.crcHealthy = _snapshotCrcHealthy;
  next.hasSuccessfulWrite = _hasSuccessfulWrite;
  next.lastSuccessfulWriteMs = _lastSuccessfulWriteMs;
  next.hasSuccessfulReplay = _hasSuccessfulReplay;
  next.lastSuccessfulReplayMs = _lastSuccessfulReplayMs;
  next.hasSdVerification = _hasSdVerification;
  next.lastSdVerificationMs = _lastSdVerificationMs;
  strlcpy(next.storageMode, _snapshotStorageMode.c_str(), sizeof(next.storageMode));
  strlcpy(next.health, _snapshotHealth.c_str(), sizeof(next.health));
  strlcpy(next.filesystemMount, _snapshotFilesystemMount.c_str(),
          sizeof(next.filesystemMount));
  strlcpy(next.sdStatus,
          sdStatusLabel(_snapshotSdPresent, _snapshotSdMounted, _sdMountFailed),
          sizeof(next.sdStatus));
  if (_snapshotSdMounted) {
    strlcpy(next.sdStatus, "Ready", sizeof(next.sdStatus));
    if (next.sdTotalBytes == 0) strlcpy(next.sdStatus, "Error", sizeof(next.sdStatus));
  }
  strlcpy(next.sdMode, _snapshotSdMounted ? "SD" : "SPIFFS Fallback",
          sizeof(next.sdMode));
  if (_snapshotSdMounted && _sdReadable && !_sdWritable) {
    strlcpy(next.storageModeLabel, "Read Only", sizeof(next.storageModeLabel));
  } else if (_snapshotSdMounted && !_snapshotFallbackActive) {
    strlcpy(next.storageModeLabel, "Normal SD Storage",
            sizeof(next.storageModeLabel));
  } else if (_spiffsMounted) {
    strlcpy(next.storageModeLabel, "Emergency Internal Storage",
            sizeof(next.storageModeLabel));
  } else {
    strlcpy(next.storageModeLabel, "Unknown", sizeof(next.storageModeLabel));
  }
  strlcpy(next.retryState,
          _disableSdPolling ? "disabled"
                            : (_watchMode ? "watch"
                                          : (_healthy ? "idle" : "retrying")),
          sizeof(next.retryState));
  strlcpy(next.diagnosticCause, diagnosticCause() ? diagnosticCause() : "UNKNOWN",
          sizeof(next.diagnosticCause));
  next.warningCount = _snapshotWarningCount > 5 ? 5 : _snapshotWarningCount;
  for (uint8_t i = 0; i < next.warningCount; ++i) {
    next.warnings[i] = _snapshotWarnings[i];
  }
  next.replayValid = _lastReplaySummary.valid;
  next.replayFileCount = _lastReplaySummary.fileCount;
  for (uint8_t i = 0; i < next.replayFileCount && i < 8; ++i) {
    next.replayFiles[i] = _lastReplaySummary.files[i];
  }
  next.replayHistoryRecords = _lastReplaySummary.historyRecords;
  next.replaySkipped = _lastReplaySummary.skipped;
  next.replayConflicts = _lastReplaySummary.conflicts;
  next.replayCompletedAtMs = _lastReplaySummary.completedAtMs;
  next.conflictCount = _conflictCount > 4 ? 4 : _conflictCount;
  for (uint8_t i = 0; i < next.conflictCount; ++i) {
    next.conflicts[i] = _conflicts[i];
  }

  portENTER_CRITICAL(&_dashSnapMux);
  _dashSnap = next;
  portEXIT_CRITICAL(&_dashSnapMux);
}

uint32_t StorageManager::dashboardSnapshotAgeMs() const {
  DashboardStorageSnap snap;
  portENTER_CRITICAL(&_dashSnapMux);
  snap = _dashSnap;
  portEXIT_CRITICAL(&_dashSnapMux);
  if (!snap.valid) return 0xFFFFFFFFu;
  return millis() - snap.refreshedAtMs;
}

void StorageManager::fillDashboardStatus(JsonObject storage, JsonObject sd,
                                         JsonObject storageStatus) const {
  DashboardStorageSnap snap;
  portENTER_CRITICAL(&_dashSnapMux);
  snap = _dashSnap;
  portEXIT_CRITICAL(&_dashSnapMux);

  storage["flashUsedMb"] =
      round((snap.spiffsUsedBytes / 1024.0 / 1024.0) * 10.0) / 10.0;
  storage["flashTotalMb"] =
      round((snap.spiffsTotalBytes / 1024.0 / 1024.0) * 10.0) / 10.0;
  storage["logsUsedKb"] = (snap.logsBytes + 1023) / 1024;
  storage["logsTotalKb"] = RenzFiConfig::LOGS_QUOTA_KB;

  sd["present"] = snap.sdPresent;
  sd["mounted"] = snap.sdMounted;
  sd["readable"] = snap.sdReadable;
  sd["writable"] = snap.sdWritable;
  sd["readOnly"] = snap.sdMounted && snap.sdReadable && !snap.sdWritable;
  sd["fallback"] = snap.usingFallback;
  sd["pollingDisabled"] = snap.pollingDisabled;
  sd["recoveryAttempts"] = snap.recoveryAttempts;
  sd["mode"] = jsonCString(snap.sdMode);
  if (snap.sdMounted) {
    sd["usedMb"] = snap.sdTotalBytes == 0 ? 0 : snap.usedMb;
    sd["totalMb"] = snap.totalMb;
    sd["freeMb"] = snap.freeMb;
    sd["status"] = jsonCString(snap.sdStatus);
  } else {
    sd["usedMb"] = 0;
    sd["totalMb"] = 0;
    sd["freeMb"] = 0;
    sd["status"] = jsonCString(snap.sdStatus);
  }

  fillStorageStatusFromSnap(storageStatus, snap);
}

void StorageManager::fillStorageStatusFromSnap(
    JsonObject storage, const DashboardStorageSnap &snap) const {
  storage["storageMode"] = jsonCString(snap.storageMode);
  storage["sdPresent"] = snap.sdPresent;
  storage["sdMounted"] = snap.sdMounted;
  storage["sdLifecycle"] = jsonCString(snap.sdLifecycle);
  storage["recoveryInProgress"] = snap.recoveryInProgress;
  storage["fallbackActive"] = snap.fallbackActive;
  storage["capacity"] = snap.capacityMb;
  storage["used"] = snap.usedMb;
  storage["mounted"] = snap.sdMounted;
  storage["mode"] = jsonCString(snap.storageModeLabel);
  storage["health"] = jsonCString(snap.health);
  storage["totalSpace"] = snap.fsTotalBytes;
  storage["freeSpace"] = snap.fsFreeBytes;
  storage["usedSpace"] = snap.fsUsedBytes;
  if (snap.journalKnown)
    storage["journalHealthy"] = snap.journalHealthy;
  else
    storage["journalHealthy"] = nullptr;
  if (snap.hasSuccessfulWrite) {
    storage["lastWrite"] = snap.lastSuccessfulWriteMs;
    storage["lastWriteAgeSeconds"] =
        static_cast<uint32_t>((millis() - snap.lastSuccessfulWriteMs) / 1000U);
  } else {
    storage["lastWrite"] = nullptr;
    storage["lastWriteAgeSeconds"] = nullptr;
  }
  storage["pendingReplay"] = snap.pendingReplay;
  storage["reconciliationStatus"] =
      snap.pendingConflicts > 0 ? "conflict" : "ok";
  JsonObject emergency = storage["emergencyUsage"].to<JsonObject>();
  emergency["percent"] = snap.emergencyPercent;
  emergency["bytes"] = snap.emergencyBytes;
  emergency["quotaBytes"] = RenzFiConfig::FB_HARD_LIMIT_BYTES;
  if (snap.crcKnown)
    storage["crcHealthy"] = snap.crcHealthy;
  else
    storage["crcHealthy"] = nullptr;
  storage["recoveryQueue"] = snap.recoveryQueue;
  storage["filesystemMount"] = jsonCString(snap.filesystemMount);
  JsonArray warnings = storage["warnings"].to<JsonArray>();
  for (uint8_t i = 0; i < snap.warningCount; ++i) {
    if (snap.warnings[i]) warnings.add(snap.warnings[i]);
  }
  storage["readable"] = snap.sdReadable;
  storage["writable"] = snap.sdWritable;
  storage["pendingConflicts"] = snap.pendingConflicts;
  storage["pendingHistory"] = snap.pendingReplay;
  storage["retryState"] = jsonCString(snap.retryState);
  storage["retryRemaining"] = snap.retryRemaining;
  storage["recoveryMode"] = snap.usingFallback || !snap.sdMounted;
  storage["watchMode"] = snap.watchMode;
  storage["diagnosticCause"] = jsonCString(snap.diagnosticCause);
  storage["internalDiagnosticState"] = jsonCString(snap.diagnosticCause);
  if (snap.hasSuccessfulWrite) {
    storage["lastSuccessfulWrite"] = snap.lastSuccessfulWriteMs;
  } else {
    storage["lastSuccessfulWrite"] = nullptr;
  }
  if (snap.hasSuccessfulReplay) {
    storage["lastSuccessfulReplay"] = snap.lastSuccessfulReplayMs;
    storage["lastSuccessfulReplayAgeSeconds"] = static_cast<uint32_t>(
        (millis() - snap.lastSuccessfulReplayMs) / 1000U);
  } else {
    storage["lastSuccessfulReplay"] = nullptr;
    storage["lastSuccessfulReplayAgeSeconds"] = nullptr;
  }
  if (snap.hasSdVerification) {
    storage["lastSdVerification"] = snap.lastSdVerificationMs;
    storage["lastSdVerificationAgeSeconds"] = static_cast<uint32_t>(
        (millis() - snap.lastSdVerificationMs) / 1000U);
  } else {
    storage["lastSdVerification"] = nullptr;
    storage["lastSdVerificationAgeSeconds"] = nullptr;
  }

  JsonObject replay = storage["replaySummary"].to<JsonObject>();
  if (snap.replayValid) {
    JsonArray recoveredFiles = replay["files"].to<JsonArray>();
    for (uint8_t i = 0; i < snap.replayFileCount; ++i) {
      if (snap.replayFiles[i]) recoveredFiles.add(snap.replayFiles[i]);
    }
    replay["historyRecords"] = snap.replayHistoryRecords;
    replay["skipped"] = snap.replaySkipped;
    replay["conflicts"] = snap.replayConflicts;
    replay["completedAt"] = snap.replayCompletedAtMs;
  } else {
    replay["files"].to<JsonArray>();
    replay["historyRecords"] = 0;
    replay["skipped"] = 0;
    replay["conflicts"] = 0;
    replay["completedAt"] = nullptr;
  }

  JsonArray conflicts = storage["conflicts"].to<JsonArray>();
  for (uint8_t i = 0; i < snap.conflictCount; ++i) {
    JsonObject row = conflicts.createNestedObject();
    row["path"] = jsonCString(snap.conflicts[i].path);
    row["generation"] = snap.conflicts[i].generation;
    row["baseCrc"] = snap.conflicts[i].baseCrc;
    row["sdCrc"] = snap.conflicts[i].sdCrc;
    row["fallbackCrc"] = snap.conflicts[i].fallbackCrc;
    row["detectedAt"] = snap.conflicts[i].detectedAtMs;
  }
}

void StorageManager::refreshRuntimeSnapshot() {
  ScopedStorageLock lock(*this);
  if (!lock) return;
  const bool mounted = isSdMounted();
  const bool present = isSdPresent();
  _snapshotSdPresent = present;
  _snapshotSdMounted = mounted;
  _snapshotFallbackActive = _usingFallback || !mounted;
  _snapshotStorageMode =
      mounted && !_usingFallback ? String("SD") : String("SPIFFS");

  if (mounted && _sdReadable && !_usingFallback) {
    _snapshotTotalBytes = SD.totalBytes();
    _snapshotUsedBytes = SD.usedBytes();
    _snapshotFilesystemMount = "SD";
  } else {
    _snapshotTotalBytes = getSpiffsTotalBytes();
    _snapshotUsedBytes = getSpiffsUsedBytes();
    _snapshotFilesystemMount = _spiffsMounted ? "SPIFFS" : "NONE";
  }
  _snapshotFreeBytes =
      _snapshotTotalBytes > _snapshotUsedBytes
          ? _snapshotTotalBytes - _snapshotUsedBytes
          : 0;
  _snapshotCapacityMb = bytesToMb(_snapshotTotalBytes);
  _snapshotUsedMb = bytesToMb(_snapshotUsedBytes);

  _snapshotSpiffsUsedBytes = _spiffsMounted ? SPIFFS.usedBytes() : 0;
  _snapshotSpiffsTotalBytes = _spiffsMounted ? SPIFFS.totalBytes() : 0;
  const uint32_t nowMs = millis();
  // Heavy FS walks (fallbackTotalBytes, spool counts, manifest) starve
  // async_tcp on shared CPU1 if run every 2s health refresh. Keep last
  // values between intervals (ADMIN_LOGIN_TWDT_ROOT_CAUSE.md).
  if (_lastSnapshotHeavyMs == 0 ||
      nowMs - _lastSnapshotHeavyMs >=
          RenzFiConfig::STORAGE_SNAPSHOT_HEAVY_INTERVAL_MS) {
    _lastSnapshotHeavyMs = nowMs;
    size_t logsBytes = 0;
    if (_sdReadable) {
      if (SD.exists(RenzFiConfig::LOGS_FILE)) {
        File logsFile = SD.open(RenzFiConfig::LOGS_FILE, FILE_READ);
        if (logsFile) {
          logsBytes = logsFile.size();
          logsFile.close();
        }
      }
    } else if (_usingFallback && _spiffsMounted &&
               isFallbackEligible(RenzFiConfig::LOGS_FILE)) {
      logsBytes = spiffsFileSize(toFallbackPath(RenzFiConfig::LOGS_FILE));
    }
    _snapshotLogsBytes = logsBytes;

    _snapshotEmergencyBytes = _spiffsMounted ? fallbackTotalBytes() : 0;

    _snapshotPendingReplay = 0;
    if (_spiffsMounted) {
      const char *spools[] = {
          StoragePaths::Spiffs::SalesHistorySpool,
          StoragePaths::Spiffs::SessionsHistorySpool,
          StoragePaths::Spiffs::VouchersHistorySpool,
      };
      for (const char *spool : spools)
        _snapshotPendingReplay += countSpoolRecords(spool);
    }

    _snapshotRecoveryQueue = 0;
    if (_sdMounted && _sdReadable) {
      const char *restoreArtifacts[] = {
          BackupManager::RESTORE_JOURNAL_PATH,
          BackupManager::RESTORE_JOURNAL_STAGE_PATH,
          BackupManager::RESTORE_JOURNAL_BACKUP_PATH,
      };
      for (const char *path : restoreArtifacts) {
        if (SD.exists(path)) ++_snapshotRecoveryQueue;
      }
    }
    if (_spiffsMounted) {
      DynamicJsonDocument manifest(2048);
      File manifestFile = SPIFFS.open(RenzFiConfig::FB_MANIFEST, FILE_READ);
      if (manifestFile &&
          deserializeJson(manifest, manifestFile) == DeserializationError::Ok) {
        JsonArray files = manifest["files"].as<JsonArray>();
        if (!files.isNull()) _snapshotRecoveryQueue += files.size();
      }
      if (manifestFile) manifestFile.close();
      const char *spools[] = {
          StoragePaths::Spiffs::SalesHistorySpool,
          StoragePaths::Spiffs::SessionsHistorySpool,
          StoragePaths::Spiffs::VouchersHistorySpool,
      };
      for (const char *spool : spools) {
        if (SPIFFS.exists((String(spool) + ".q").c_str())) {
          ++_snapshotRecoveryQueue;
        }
      }
    }
  }
  const size_t emergencyPercent =
      (_snapshotEmergencyBytes * 100U) / RenzFiConfig::FB_HARD_LIMIT_BYTES;
  _snapshotEmergencyPercent =
      static_cast<uint8_t>(emergencyPercent > 100U ? 100U : emergencyPercent);

  _snapshotWarningCount = 0;
  auto addWarning = [this](const char *message) {
    if (_snapshotWarningCount < 5) {
      _snapshotWarnings[_snapshotWarningCount++] = message;
    }
  };
  if (!_sdMounted) addWarning("SD card is unavailable; emergency storage is active.");
  if (_sdMounted && _sdReadable && !_sdWritable)
    addWarning("SD card is mounted read-only.");
  if (!_layoutValid && _sdMounted)
    addWarning("Required SD card folders are incomplete.");
  if (_snapshotPendingReplay > 0)
    addWarning("Stored history is waiting to replay to the SD card.");
  _snapshotPendingConflicts = _conflictCount;
  if (_snapshotPendingConflicts > 0)
    addWarning("SPIFFS/SD conflict detected; owner review required (no auto-merge).");
  if (_watchMode)
    addWarning("SD watch mode active; checking for card reinsertion periodically.");
  const bool emergencyActive =
      _snapshotFallbackActive || _snapshotPendingReplay > 0;
  if (emergencyActive && _snapshotEmergencyPercent >= 90)
    addWarning("Emergency storage is critically full.");
  else if (emergencyActive && _snapshotEmergencyPercent >= 70)
    addWarning("Emergency storage is nearing its safe limit.");

  const String previousHealth = _snapshotHealth;
  if (emergencyActive && _snapshotEmergencyPercent >= 90) {
    _snapshotHealth = "CRITICAL";
  } else if (_sdMounted && _sdReadable && !_sdWritable) {
    _snapshotHealth = "READ_ONLY";
  } else if (emergencyActive && _snapshotEmergencyPercent >= 70) {
    _snapshotHealth = "WARNING";
  } else if (!_sdMounted || _usingFallback || !_layoutValid ||
             _snapshotPendingReplay > 0 || _snapshotRecoveryQueue > 0) {
    _snapshotHealth = "DEGRADED";
  } else if (_healthy && _sdWritable) {
    _snapshotHealth = "HEALTHY";
  } else {
    _snapshotHealth = "UNKNOWN";
  }

  if (_healthy && _sdWritable && !_usingFallback) {
    setDiagnosticCause("OK");
  } else if (!_sdPresent &&
             strcmp(diagnosticCause(), "RESTORE_BLOCKED") != 0) {
    setDiagnosticCause("MEDIA_MISSING");
  } else if (!_sdMounted && _sdPresent &&
             strcmp(diagnosticCause(), "RESTORE_BLOCKED") != 0 &&
             strcmp(diagnosticCause(), "WRITE_PROBE_FAILED") != 0 &&
             strcmp(diagnosticCause(), "WRITE_VERIFICATION_FAILED") != 0 &&
             strcmp(diagnosticCause(), "TRANSACTION_FAILED") != 0) {
    setDiagnosticCause("FILESYSTEM_ERROR");
  } else if (_sdMounted && _sdReadable && !_sdWritable) {
    const char *cause = diagnosticCause();
    if (strcmp(cause, "OK") == 0 || strcmp(cause, "UNKNOWN") == 0 ||
        strcmp(cause, "MEDIA_MISSING") == 0) {
      setDiagnosticCause("READ_ONLY");
    }
  }

  if (previousHealth != _snapshotHealth) {
    if (_snapshotSdMounted) {
      Serial.println("[storage] SD Mounted");
      Serial.printf("[storage] Mode=%s Capacity=%.1fMB Free=%.1fMB\n",
                    _sdWritable ? "Normal" : "ReadOnly",
                    bytesToMb(_snapshotTotalBytes),
                    bytesToMb(_snapshotFreeBytes));
    } else {
      Serial.println("[storage] SD Missing");
      Serial.println("[storage] Entering Degraded Mode");
      Serial.printf("[storage] Emergency Storage Enabled Usage=%u%%\n",
                    static_cast<unsigned>(_snapshotEmergencyPercent));
    }
    Serial.printf(
        "[storage] Health=%s Cause=%s PendingReplay=%u Conflicts=%u "
        "Watch=%s\n",
        _snapshotHealth.c_str(), diagnosticCause(),
        static_cast<unsigned>(_snapshotPendingReplay),
        static_cast<unsigned>(_snapshotPendingConflicts),
        _watchMode ? "yes" : "no");
    if (_snapshotHealth == "WARNING" || _snapshotHealth == "CRITICAL") {
      Serial.println("[storage] Owner notification required");
    }
    if (_events) _events->emit("storage.changed");
  }

  publishDashboardSnapUnlocked();
}

void StorageManager::noteRestoreJournalHealth(bool healthy) {
  _snapshotJournalHealthKnown = true;
  _snapshotJournalHealthy = healthy;
}

void StorageManager::noteSuccessfulWrite() {
  _hasSuccessfulWrite = true;
  _lastSuccessfulWriteMs = millis();
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
  ScopedStorageLock lock(*this);
  if (!lock) return false;
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
  ScopedStorageLock lock(*this);
  if (!lock) return false;
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
  const bool ok = written == content.length();
  if (ok) noteSuccessfulWrite();
  return ok;
}

bool StorageManager::deleteSdOnly(const char *path) {
  ScopedStorageLock lock(*this);
  if (!lock) return false;
  if (!_healthy || path == nullptr) return true;
  if (SD.exists(path)) return SD.remove(path);
  return true;
}

void StorageManager::clearAllFallbackData() {
  ScopedStorageLock lock(*this);
  if (!lock) return;
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
      RenzFiConfig::FB_INSTALLATION,
      RenzFiConfig::FB_PROVISIONING,
      RenzFiConfig::FB_ROUTER_CONNECTION,
      RenzFiConfig::FB_ROUTER_PROVISIONING,
      RenzFiConfig::FB_ROUTER_CACHE,
      RenzFiConfig::FB_EXISTING_NETWORK_SCAN,
      RenzFiConfig::FB_SETUP_WIZARD,
      RenzFiConfig::PORTAL_BANNER_SPIFFS,
      RenzFiConfig::PORTAL_MUSIC_SPIFFS,
  };
  for (const char *path : fallbackPaths) {
    if (SPIFFS.exists(path)) SPIFFS.remove(path);
    const String stage = String(path) + StoragePaths::TransactionStageSuffix;
    const String backup = String(path) + StoragePaths::TransactionBackupSuffix;
    if (SPIFFS.exists(stage)) SPIFFS.remove(stage);
    if (SPIFFS.exists(backup)) SPIFFS.remove(backup);
  }
  NdjsonLedger::clearSpools();
}

namespace {

bool deleteOneLeafInTree(const char *dirPath) {
  if (!dirPath || !SD.exists(dirPath)) return false;
  File root = SD.open(dirPath, FILE_READ);
  if (!root) return false;
  if (!root.isDirectory()) {
    root.close();
    return SD.remove(dirPath);
  }
  File child = root.openNextFile();
  if (!child) {
    root.close();
    return SD.rmdir(dirPath);
  }
  String childPath = child.path();
  const bool isDir = child.isDirectory();
  child.close();
  root.close();
  if (isDir) return deleteOneLeafInTree(childPath.c_str());
  return SD.remove(childPath.c_str());
}

}  // namespace


bool StorageManager::removeSdTree(const char *path) {
  if (!path || !SD.exists(path)) return true;
  File root = SD.open(path, FILE_READ);
  if (!root) return false;
  if (!root.isDirectory()) {
    root.close();
    return SD.remove(path);
  }
  File child = root.openNextFile();
  while (child) {
    String childPath = child.path();
    child.close();
    if (!removeSdTree(childPath.c_str())) {
      root.close();
      return false;
    }
    child = root.openNextFile();
  }
  root.close();
  return SD.rmdir(path);
}

bool StorageManager::factoryResetData() {
  ScopedStorageLock lock(*this);
  if (!lock) return false;
  if (!_sdMounted || !_sdWritable) return false;
  const char *paths[] = {
      StoragePaths::SettingsFile,
      StoragePaths::PromosFile,
      StoragePaths::RouterFile,
      StoragePaths::WifiConfigFile,
      StoragePaths::VouchersFile,
      StoragePaths::SalesFile,
      StoragePaths::LogsFile,
      StoragePaths::UsersFile,
      StoragePaths::AdminSessionsFile,
      StoragePaths::PortalSessionsFile,
      StoragePaths::PortalConfigFile,
      StoragePaths::InstallationFile,
      StoragePaths::ProvisioningFile,
      StoragePaths::RouterConnectionFile,
      StoragePaths::RouterProvisioningFile,
      StoragePaths::RouterCacheFile,
      StoragePaths::ExistingNetworkScanFile,
      StoragePaths::SetupWizardFile,
      StoragePaths::NetworkAdoptionWorkflowFile,
  };
  for (const char *path : paths) {
    SD.remove(path);
    SD.remove((String(path) + StoragePaths::TransactionStageSuffix).c_str());
    SD.remove((String(path) + StoragePaths::TransactionBackupSuffix).c_str());
  }
  if (!removeSdTree(StoragePaths::History)) return false;
  NdjsonLedger::clearSpools();
  return ensureLayout();
}

bool StorageManager::factoryResetHistoryTick(bool &done) {
  ScopedStorageLock lock(*this);
  if (!lock) {
    done = false;
    return false;
  }
  done = false;
  if (!_sdMounted || !_sdWritable) {
    done = true;
    return true;
  }
  if (!SD.exists(StoragePaths::History)) {
    done = true;
    return true;
  }
  const uint32_t start = millis();
  while (static_cast<uint32_t>(millis() - start) < 40U) {
    if (!SD.exists(StoragePaths::History)) {
      done = true;
      return true;
    }
    if (!deleteOneLeafInTree(StoragePaths::History)) {
      done = !SD.exists(StoragePaths::History);
      return true;
    }
  }
  done = !SD.exists(StoragePaths::History);
  return true;
}

// ── Phase 2: path helpers and storage health ───────────────────────────────────

bool StorageManager::isSpiffsMounted() const {
  return _spiffsMounted;
}

bool StorageManager::sdDirectoryExists(const char *path) const {
  ScopedStorageLock lock(*this);
  if (!lock) return false;
  if (!_healthy || path == nullptr || !StoragePaths::isValidSdPath(path))
    return false;
  return SD.exists(path);
}

bool StorageManager::ensureSdDirectory(const char *path) {
  ScopedStorageLock lock(*this);
  if (!lock) return false;
  if (!_healthy || path == nullptr || !StoragePaths::isValidSdPath(path))
    return false;
  return ensureDir(path);
}

bool StorageManager::isValidPath(const char *path) const {
  return StoragePaths::isValidSdPath(path);
}

String StorageManager::joinSdPath(const char *dir, const char *leaf) const {
  char buf[128];
  if (!StoragePaths::joinPath(dir, leaf, buf, sizeof(buf))) return String();
  return String(buf);
}

bool StorageManager::probeSdWritable() {
  ScopedStorageLock lock(*this);
  if (!lock) return false;
  _sdWritable = false;
  if (!_sdMounted) return false;

  Serial.println("[storage] Verifying write capability");

  if (!ensureSdDirectory(StoragePaths::Temp)) {
    setDiagnosticCause("WRITE_PROBE_FAILED");
    return false;
  }

  // Option A hotfix: use an absolute probe path. joinPath(Temp, ".write_probe")
  // rejects the relative leaf via isValidSdPath (requires leading '/'), so the
  // probe never reached SD.open and falsely reported WRITE_PROBE_FAILED.
  static constexpr const char kWriteProbePath[] = "/temp/.write_probe";
  if (!StoragePaths::isValidSdPath(kWriteProbePath)) {
    setDiagnosticCause("WRITE_PROBE_FAILED");
    return false;
  }
  const char *probePath = kWriteProbePath;

  static const char kPayload[] = "ok";
  {
    File file = SD.open(probePath, FILE_WRITE);
    if (!file) {
      setDiagnosticCause("WRITE_PROBE_FAILED");
      return false;
    }
    const size_t written = file.print(kPayload);
    file.flush();
    file.close();
    if (written != strlen(kPayload)) {
      if (SD.exists(probePath)) SD.remove(probePath);
      setDiagnosticCause("WRITE_PROBE_FAILED");
      return false;
    }
  }

  {
    File verify = SD.open(probePath, FILE_READ);
    if (!verify) {
      if (SD.exists(probePath)) SD.remove(probePath);
      setDiagnosticCause("WRITE_VERIFICATION_FAILED");
      return false;
    }
    char buf[8] = {};
    const size_t n = verify.read(reinterpret_cast<uint8_t *>(buf), sizeof(buf) - 1);
    verify.close();
    if (n != strlen(kPayload) || strcmp(buf, kPayload) != 0) {
      if (SD.exists(probePath)) SD.remove(probePath);
      setDiagnosticCause("WRITE_VERIFICATION_FAILED");
      return false;
    }
  }

  if (!SD.remove(probePath) || SD.exists(probePath)) {
    setDiagnosticCause("WRITE_VERIFICATION_FAILED");
    return false;
  }

  _sdWritable = true;
  _hasSdVerification = true;
  _lastSdVerificationMs = millis();
  Serial.println("[storage] Verification passed");
  return true;
}

bool StorageManager::isSdWritable() const {
  return _sdWritable;
}

bool StorageManager::validateLayout(bool repairMissing) {
  ScopedStorageLock lock(*this);
  if (!lock) return false;
  if (!_healthy) {
    _layoutValid = false;
    return false;
  }

  bool allPresent = true;
  for (size_t i = 0; i < StoragePaths::requiredSdDirectoryCount(); ++i) {
    const char *dir = StoragePaths::requiredSdDirectory(i);
    if (!dir) continue;
    const bool exists = sdDirectoryExists(dir);
    if (!exists) {
      allPresent = false;
      if (repairMissing && !ensureDir(dir)) return false;
    }
  }

  if (repairMissing && !allPresent) {
    allPresent = true;
    for (size_t i = 0; i < StoragePaths::requiredSdDirectoryCount(); ++i) {
      const char *dir = StoragePaths::requiredSdDirectory(i);
      if (dir && !sdDirectoryExists(dir)) allPresent = false;
    }
  }

  _layoutValid = allPresent;
  return allPresent;
}

void StorageManager::fillFolderHealth(JsonArray folders) const {
  for (size_t i = 0; i < StoragePaths::requiredSdDirectoryCount(); ++i) {
    const char *dir = StoragePaths::requiredSdDirectory(i);
    if (!dir) continue;
    JsonObject row = folders.createNestedObject();
    row["path"] = dir;
    row["owner"] = StoragePaths::ownerLabel(
        StoragePaths::ownerForDirectory(dir));
    row["required"] = true;
    if (_healthy) {
      row["exists"] = SD.exists(dir);
    } else {
      row["exists"] = false;
    }
  }
}

void StorageManager::fillStorageHealth(JsonObject health) const {
  DashboardStorageSnap snap;
  portENTER_CRITICAL(&_dashSnapMux);
  snap = _dashSnap;
  portEXIT_CRITICAL(&_dashSnapMux);

  health["sdMounted"] = snap.sdMounted;
  health["sdPresent"] = snap.sdPresent;
  health["sdReadable"] = snap.sdReadable;
  health["sdWritable"] = snap.sdWritable;
  health["sdReadOnly"] = snap.sdMounted && snap.sdReadable && !snap.sdWritable;
  health["spiffsMounted"] = snap.spiffsMounted;
  health["usingFallback"] = snap.usingFallback;
  health["layoutValid"] = _layoutValid;
  health["healthy"] = snap.healthy;
  health["sdLifecycle"] = jsonCString(snap.sdLifecycle);
  health["pollingDisabled"] = snap.pollingDisabled;
  health["watchMode"] = snap.watchMode;
  health["recoveryAttempts"] = snap.recoveryAttempts;
  health["recoveryInProgress"] = snap.recoveryInProgress;
  health["diagnosticCause"] = jsonCString(snap.diagnosticCause);

  JsonObject sd = health["sd"].to<JsonObject>();
  fillSdStatus(sd);

  JsonObject spiffs = health["spiffs"].to<JsonObject>();
  spiffs["mounted"] = snap.spiffsMounted;
  spiffs["totalBytes"] = snap.spiffsTotalBytes;
  spiffs["usedBytes"] = snap.spiffsUsedBytes;
  spiffs["freeBytes"] = snap.spiffsTotalBytes > snap.spiffsUsedBytes
                            ? snap.spiffsTotalBytes - snap.spiffsUsedBytes
                            : 0;
  spiffs["fallbackBytes"] = snap.emergencyBytes;

  JsonObject fs = health["filesystem"].to<JsonObject>();
  fs["primary"] = (snap.healthy && !snap.usingFallback) ? "SD" : "SPIFFS";
  fs["readWriteOk"] = snap.sdWritable || (!snap.sdReadable && snap.spiffsMounted);
}
