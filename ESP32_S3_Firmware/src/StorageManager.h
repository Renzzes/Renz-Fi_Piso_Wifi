#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "NdjsonLedger.h"

class EventBus;

class StorageManager {
 public:
  bool begin();
  bool healthy() const;
  bool usingFallback() const;
  String lastError() const;

  void setEventBus(EventBus *events);
  void pollStorageHealth();
  /** HTTP-safe: request remount on the storage owner. Never runs SD.begin. */
  bool retrySd();
  bool sdRecoveryInProgress() const { return _sdRecoveryInProgress; }
  // Stops storage consumers after an integrity-critical boot recovery failure.
  void markDegraded(const String &reason);

  bool isSdPollingDisabled() const;
  bool isWatchMode() const;
  uint8_t sdRetryCount() const;
  const char *diagnosticCause() const;

  /** Explicit SD lifecycle for diagnostics and fail-fast gates. */
  enum class SdLifecycle : uint8_t {
    Disabled = 0,
    Mounting,
    Ready,
    Degraded,
    Remounting,
    Syncing,
    Failed,
  };
  SdLifecycle sdLifecycle() const { return _sdLifecycle; }
  const char *sdLifecycleName() const;

  /** True only when SD I/O is allowed (mounted + readable). */
  bool sdIoAllowed() const { return _sdMounted && _sdReadable; }

  bool ensureLayout();
  bool readJson(const char *path, JsonDocument &doc);
  bool writeJson(const char *path, const JsonDocument &doc,
                 bool forcePortalWrite = false);
  bool appendJsonArrayItem(const char *path, JsonObject item, size_t capacity);
  bool clearJsonArray(const char *path);
  bool exists(const char *path) const;
  File openStatic(const String &path);
  String contentType(const String &path) const;
  uint64_t totalBytes() const;
  uint64_t usedBytes() const;
  size_t fileSizeBytes(const char *path) const;
  bool appendHistory(NdjsonLedger::Kind kind, const String &eventId,
                     const String &eventAt, JsonObjectConst event,
                     bool allowSpool = true);
  /** One STORAGE_LOCK + one SD append for N prepared NDJSON lines. */
  bool appendHistoryPreparedLines(NdjsonLedger::Kind kind,
                                  const String &eventAt,
                                  const String *eventIds, const String *lines,
                                  size_t count, bool allowSpool = true);
  bool historyPath(NdjsonLedger::Kind kind, const String &month,
                   String &path) const;

  bool writeBinary(const char *sdPath, const uint8_t *data, size_t len);
  bool writeBinarySpiffs(const char *spiffsPath, const uint8_t *data, size_t len);
  bool removeBinary(const char *sdPath, const char *spiffsPath);

  uint64_t getSpiffsUsedBytes() const;
  uint64_t getSpiffsTotalBytes() const;
  bool isSdPresent() const;
  bool isSdMounted() const;
  bool isSdReadable() const;
  uint64_t getSdUsedBytes() const;
  uint64_t getSdTotalBytes() const;
  uint64_t getSdFreeBytes() const;
  void fillSdStatus(JsonObject sd) const;
  void fillStorageStatus(JsonObject storage) const;
  void noteRestoreJournalHealth(bool healthy);

  /** Recompute storage stats for health snapshots (loop / lifecycle only). */
  void refreshRuntimeSnapshot();

  /**
   * HTTP-safe copy of the last refreshRuntimeSnapshot() — no STORAGE_LOCK,
   * no SD/SPIFFS I/O. Stale values are kept if refresh failed.
   */
  uint32_t dashboardSnapshotAgeMs() const;
  void fillDashboardStatus(JsonObject storage, JsonObject sd,
                           JsonObject storageStatus) const;

  // SD-card-only helpers (no SPIFFS fallback).
  bool readSdText(const char *path, String &out) const;
  bool writeSdText(const char *path, const String &content);
  bool deleteSdOnly(const char *path);
  void clearAllFallbackData();

  // Wipe appliance data files and re-seed SD layout defaults.
  bool factoryResetData();
  // Bounded /history deletion for FactoryResetWorker (loopTask). done=true
  // when the tree is gone. Does not run on async_tcp.
  bool factoryResetHistoryTick(bool &done);

  // ── Phase 2.3: storage helpers — see docs/STORAGE_ARCHITECTURE.md ─────────

  bool isSpiffsMounted() const;
  bool sdDirectoryExists(const char *path) const;
  bool ensureSdDirectory(const char *path);
  bool isValidPath(const char *path) const;
  String joinSdPath(const char *dir, const char *leaf) const;

  // Probe SD with a transient write under /temp (does not overwrite user data).
  bool probeSdWritable();
  bool isSdWritable() const;

  // Verify required folders; optionally create missing (never deletes data).
  bool validateLayout(bool repairMissing = false);

  // Extended diagnostics for admin dashboard (future UI).
  void fillStorageHealth(JsonObject health) const;

 private:
  class ScopedStorageLock {
   public:
    explicit ScopedStorageLock(const StorageManager &owner);
    ~ScopedStorageLock();
    explicit operator bool() const { return _locked; }

   private:
    const StorageManager &_owner;
    bool _locked = false;
  };

  EventBus *_events = nullptr;
  mutable SemaphoreHandle_t _storageMutex = nullptr;
  bool _healthy = false;
  bool _sdPresent = false;
  bool _sdMounted = false;
  bool _sdReadable = false;
  bool _sdMountFailed = false;
  bool _spiffsMounted = false;
  bool _usingFallback = false;
  bool _syncInProgress = false;
  bool _disableSdPolling = false;  // restore-blocked / integrity halt only
  bool _watchMode = false;         // low-power remount watch after retry budget
  mutable volatile bool _sdRecoveryInProgress = false;
  volatile bool _sdRecoveryRequested = false;
  bool _sdWritable = false;
  bool _layoutValid = false;
  SdLifecycle _sdLifecycle = SdLifecycle::Disabled;

  uint8_t _sdRetryCount = 0;
  uint8_t _sdIoFailStreak = 0;
  uint32_t _lastHealthPollMs = 0;
  uint32_t _lastFbPortalWriteMs = 0;
  uint32_t _lastSdVerificationMs = 0;
  uint32_t _lastSnapshotHeavyMs = 0;
  uint32_t _lastSuccessfulReplayMs = 0;
  bool _hasSuccessfulReplay = false;
  bool _hasSdVerification = false;

  // Internal diagnostic cause (serviceability). Owner UI may stay simplified.
  const char *_diagnosticCause = "UNKNOWN";

  struct ConflictRecord {
    char path[48] = {};
    uint32_t generation = 0;
    uint32_t baseCrc = 0;
    uint32_t sdCrc = 0;
    uint32_t fallbackCrc = 0;
    uint32_t detectedAtMs = 0;
  };
  ConflictRecord _conflicts[4] = {};
  uint8_t _conflictCount = 0;
  uint8_t _snapshotPendingConflicts = 0;

  struct ReplaySummary {
    const char *files[8] = {};
    uint8_t fileCount = 0;
    uint16_t historyRecords = 0;
    uint16_t skipped = 0;
    uint16_t conflicts = 0;
    bool valid = false;
    uint32_t completedAtMs = 0;
  };
  ReplaySummary _lastReplaySummary;

  double _snapshotCapacityMb = 0.0;
  double _snapshotUsedMb = 0.0;
  String _snapshotStorageMode = "SPIFFS";
  bool _snapshotSdPresent = false;
  bool _snapshotSdMounted = false;
  bool _snapshotFallbackActive = false;
  uint64_t _snapshotTotalBytes = 0;
  uint64_t _snapshotUsedBytes = 0;
  uint64_t _snapshotFreeBytes = 0;
  size_t _snapshotEmergencyBytes = 0;
  uint8_t _snapshotEmergencyPercent = 0;
  uint32_t _snapshotPendingReplay = 0;
  uint8_t _snapshotRecoveryQueue = 0;
  bool _snapshotJournalHealthy = false;
  bool _snapshotJournalHealthKnown = false;
  bool _snapshotCrcHealthy = false;
  bool _snapshotCrcHealthKnown = false;
  bool _hasSuccessfulWrite = false;
  uint32_t _lastSuccessfulWriteMs = 0;
  String _snapshotHealth = "UNKNOWN";
  String _snapshotFilesystemMount = "NONE";
  const char *_snapshotWarnings[5] = {};
  uint8_t _snapshotWarningCount = 0;
  uint64_t _snapshotSpiffsUsedBytes = 0;
  uint64_t _snapshotSpiffsTotalBytes = 0;
  size_t _snapshotLogsBytes = 0;

  struct DashboardStorageSnap {
    bool valid = false;
    uint32_t refreshedAtMs = 0;
    bool healthy = false;
    bool usingFallback = false;
    bool sdPresent = false;
    bool sdMounted = false;
    bool sdReadable = false;
    bool sdWritable = false;
    bool sdMountFailed = false;
    bool pollingDisabled = false;
    bool watchMode = false;
    bool recoveryInProgress = false;
    char sdLifecycle[16] = {};
    bool fallbackActive = false;
    bool spiffsMounted = false;
    uint8_t recoveryAttempts = 0;
    uint8_t retryRemaining = 0;
    uint64_t sdUsedBytes = 0;
    uint64_t sdTotalBytes = 0;
    uint64_t sdFreeBytes = 0;
    uint64_t spiffsUsedBytes = 0;
    uint64_t spiffsTotalBytes = 0;
    uint64_t fsUsedBytes = 0;
    uint64_t fsTotalBytes = 0;
    uint64_t fsFreeBytes = 0;
    size_t logsBytes = 0;
    double usedMb = 0;
    double totalMb = 0;
    double freeMb = 0;
    double capacityMb = 0;
    size_t emergencyBytes = 0;
    uint8_t emergencyPercent = 0;
    uint32_t pendingReplay = 0;
    uint8_t recoveryQueue = 0;
    uint8_t pendingConflicts = 0;
    bool journalKnown = false;
    bool journalHealthy = false;
    bool crcKnown = false;
    bool crcHealthy = false;
    bool hasSuccessfulWrite = false;
    uint32_t lastSuccessfulWriteMs = 0;
    bool hasSuccessfulReplay = false;
    uint32_t lastSuccessfulReplayMs = 0;
    bool hasSdVerification = false;
    uint32_t lastSdVerificationMs = 0;
    char storageMode[16] = {};
    char health[16] = {};
    char filesystemMount[16] = {};
    char sdStatus[16] = {};
    char sdMode[24] = {};
    char storageModeLabel[40] = {};
    char retryState[16] = {};
    char diagnosticCause[48] = {};
    const char *warnings[5] = {};
    uint8_t warningCount = 0;
    bool replayValid = false;
    uint8_t replayFileCount = 0;
    const char *replayFiles[8] = {};
    uint16_t replayHistoryRecords = 0;
    uint16_t replaySkipped = 0;
    uint16_t replayConflicts = 0;
    uint32_t replayCompletedAtMs = 0;
    uint8_t conflictCount = 0;
    ConflictRecord conflicts[4] = {};
  };
  mutable portMUX_TYPE _dashSnapMux = portMUX_INITIALIZER_UNLOCKED;
  DashboardStorageSnap _dashSnap;
  void publishDashboardSnapUnlocked();
  void fillStorageStatusFromSnap(JsonObject storage,
                                 const DashboardStorageSnap &snap) const;

  String _lastError;

  void setError(const String &message);
  void setSdLifecycle(SdLifecycle next, const char *reason);
  void tripSdMediaMissing(const char *reason);
  bool mountSdCard(const char *context, bool reinitBus = false);
  bool attemptSdRecovery();
  void requestSdRecovery(const char *source);
  void onSdRecoveryFailed();
  void onSdRecoverySucceeded();
  void handleSdRemoved(const char *reason);
  bool verifySdHealthy();
  void emitStorageChanged();
  void setDiagnosticCause(const char *cause);
  void recordConflict(const char *sdPath, uint32_t generation, uint32_t baseCrc,
                      uint32_t sdCrc, uint32_t fallbackCrc);
  void clearConflicts();
  static const char *fallbackFileLabel(const char *fbPath);
  bool ensureDir(const char *path);
  bool ensureJsonFile(const char *path, const char *contents);
  bool ensureRequiredDirectories();
  bool seedDefaultJsonFiles();
  void fillFolderHealth(JsonArray folders) const;

  bool mountSpiffs();
  bool isFallbackEligible(const char *path) const;
  bool isContinuousCheckpointEligible(const char *path) const;
  String toFallbackPath(const char *sdPath) const;
  const char *toSdPath(const char *fbPath) const;
  size_t perFileLimit(const char *sdPath) const;

  bool readJsonFromSd(const char *path, JsonDocument &doc);
  bool writeJsonToSd(const char *path, JsonDocument &doc);
  bool writeJsonToSdSerialized(const char *path, const String &serialized);
  bool writeJsonToSdOnce(const char *path, const String &serialized,
                         const char **failReasonOut = nullptr);
  bool recoverSdTransaction(const char *path);
  bool recoverSpiffsTransaction(const String &path);
  bool validateJsonPayload(const String &payload) const;
  bool readSdPayload(const char *path, String &payload) const;
  bool readSpiffsPayload(const String &path, String &payload) const;
  uint32_t payloadCrc(const String &payload) const;

  bool readJsonFromSpiffs(const char *sdPath, JsonDocument &doc);
  bool writeJsonToSpiffs(const char *sdPath, const String &serialized,
                         bool forcePortalWrite = false,
                         uint32_t baseCrc = 0);
  bool seedFallbackDefaults(const char *sdPath, JsonDocument &doc) const;

  bool spiffsReadFile(const String &fbPath, JsonDocument &doc);
  bool spiffsWriteFile(const String &fbPath, const String &data);
  size_t spiffsFreeBytes() const;
  size_t spiffsFileSize(const String &fbPath) const;
  size_t fallbackTotalBytes() const;

  bool checkQuota(const char *sdPath, size_t newSize, size_t oldSize) const;
  bool isPortalWriteThrottled(const char *path, bool force) const;

  bool readManifest(JsonDocument &doc);
  bool writeManifest(const JsonDocument &doc);
  bool manifestEntry(const String &fbPath, uint32_t &baseCrc,
                     uint32_t &payloadCrc);
  bool addToManifest(const String &fbPath, size_t fileBytes, uint32_t baseCrc,
                     uint32_t payloadCrc);
  void removeFromManifest(const String &fbPath);
  bool checkpointToSpiffs(const char *sdPath, const String &serialized);
  void recoverBootTransactions();
  bool syncFallbackToSd();
  bool replayHistorySpools();
  bool removeSdTree(const char *path);
  bool verifySdMatches(const char *sdPath, const String &expected);
  bool createStorageMutex();
  bool lockStorage() const;
  bool tryLockStorage() const;
  void unlockStorage() const;
  void noteSuccessfulWrite();
};
