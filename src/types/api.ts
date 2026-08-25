export type PromoRate = {
  id: number;
  name: string;
  coin: number;
  minutes: number;
  /** Legacy display Mbps (optional). */
  speed?: number;
  devices?: number;
  data_cap_mb?: number;
  /** profile = existing MikroTik user profile; custom = managed renzfi-speed-* */
  speedMode?: "profile" | "custom";
  profileName?: string;
  customDownloadMbps?: number;
  customUploadMbps?: number;
  managedProfileName?: string;
};

export type Voucher = {
  schemaVersion?: number;
  code: string;
  amount: number;
  minutes: number;
  status: "unused" | "redeeming" | "active" | "expired" | "disabled" | "archived";
  expires: string;
  validUntil?: string;
  boundMac?: string;
  sessionId?: string;
  redeemedAt?: string;
  activatedAt?: string;
  serviceExpiresAt?: string;
  profileName?: string;
  speed?: string;
  terminalReason?: string;
  archivedAt?: string;
};

export type SessionState =
  | "idle"
  | "waiting_coin"
  | "activating"
  | "active"
  | "paused"
  | "expiring"
  | "activation_error"
  | "expired";

export type ActiveUser = {
  mac: string;
  ip: string;
  sessionType: "coin" | "voucher";
  remainingMinutes: number;
  credits: number;
  paused: boolean;
  active: boolean;
  state: SessionState;
  source: "portal" | "voucher";
  secondsLeft?: number;
  /** Browser heartbeat freshness — not entitlement. */
  portalHeartbeatFresh?: boolean;
};

export type LogEntry = {
  id?: number;
  t: string;
  lvl: string;
  type?: string;
  msg: string;
};

export type CoinState =
  | "DISABLED"
  | "WAITING_FOR_ACTIVITY"
  | "RESPONDING"
  | "NO_RECENT_ACTIVITY"
  | "FAULT";

/** @deprecated Use CoinState */
export type CoinHardwareState = CoinState;

export type CoinSystemStatus = {
  enabled: boolean;
  state: CoinState;
  totalPulseCount: number;
  totalCoinCount: number;
  uptimePulseCount?: number;
  uptimeCoinCount?: number;
  lastPulseTimestamp?: string | null;
  lastCoinTimestamp?: string | null;
  faultReason?: string;
};

export type StorageHealth =
  | "HEALTHY"
  | "DEGRADED"
  | "WARNING"
  | "CRITICAL"
  | "READ_ONLY"
  | "UNKNOWN";

export type StorageDiagnosticCause =
  | "OK"
  | "MEDIA_MISSING"
  | "WRITE_PROBE_FAILED"
  | "WRITE_VERIFICATION_FAILED"
  | "TRANSACTION_FAILED"
  | "RESTORE_BLOCKED"
  | "FILESYSTEM_ERROR"
  | "READ_ONLY"
  | "UNKNOWN";

export type StorageRetryState = "idle" | "retrying" | "watch" | "disabled";

export type StorageConflict = {
  path: string;
  generation?: number;
  baseCrc?: number;
  sdCrc?: number;
  fallbackCrc?: number;
  detectedAt?: number;
};

export type StorageReplaySummary = {
  files?: string[];
  historyRecords?: number;
  skipped?: number;
  conflicts?: number;
  completedAt?: number | null;
};

export type StorageStatus = {
  storageMode: "SD" | "SPIFFS";
  sdPresent: boolean;
  sdMounted: boolean;
  capacity: number;
  used: number;
  fallbackActive: boolean;
  mounted?: boolean;
  mode?: "Normal SD Storage" | "Emergency Internal Storage" | "Read Only" | "Unknown";
  health?: StorageHealth;
  totalSpace?: number;
  freeSpace?: number;
  usedSpace?: number;
  journalHealthy?: boolean | null;
  lastWrite?: number | null;
  lastWriteAgeSeconds?: number | null;
  lastSuccessfulBackup?: string | null;
  lastSuccessfulBackupAgeSeconds?: number | null;
  pendingReplay?: number;
  emergencyUsage?: {
    percent: number;
    bytes: number;
    quotaBytes: number;
  };
  crcHealthy?: boolean | null;
  recoveryQueue?: number;
  filesystemMount?: "SD" | "SPIFFS" | "NONE";
  warnings?: string[];
  /** Additive serviceability fields */
  readable?: boolean;
  writable?: boolean;
  pendingConflicts?: number;
  pendingHistory?: number;
  retryState?: StorageRetryState;
  retryRemaining?: number;
  recoveryMode?: boolean;
  watchMode?: boolean;
  diagnosticCause?: StorageDiagnosticCause | string;
  internalDiagnosticState?: StorageDiagnosticCause | string;
  lastSuccessfulWrite?: number | null;
  lastSuccessfulReplay?: number | null;
  lastSuccessfulReplayAgeSeconds?: number | null;
  lastSdVerification?: number | null;
  lastSdVerificationAgeSeconds?: number | null;
  replaySummary?: StorageReplaySummary;
  conflicts?: StorageConflict[];
  recoveryInProgress?: boolean;
  reconciliationStatus?: "ok" | "conflict" | string;
};

export type RgbMode = "OFF" | "SOLID" | "BREATHING" | "RAINBOW" | "SYSTEM_STATUS";

export type RgbColor = {
  red: number;
  green: number;
  blue: number;
};

export type RgbSystemStatus = {
  enabled: boolean;
  mode: string;
  state: string;
  brightness: number;
  colorName: string;
  color: RgbColor;
  systemStatus?: string;
};

export type CoinSlotStatus = {
  enabled?: boolean;
  hardwareState?: CoinState;
  state?: CoinState | string;
  stateLabel?: string;
  lastPulseTimestamp?: string | null;
  lastCoinTimestamp?: string | null;
  totalPulseCount?: number;
  totalCoinCount?: number;
  uptimePulseCount?: number;
  uptimeCoinCount?: number;
  ok: boolean;
  pulsesToday: number;
};

export type SystemHealth = {
  level: "HEALTHY" | "ACTIVE_SESSION" | "WARNING" | "ERROR";
  ethernet: {
    driver: string;
    link: string;
    ip: string;
    gateway?: string;
    netmask?: string;
    dns?: string;
    mac?: string;
    mode?: string;
  };
  storage: StorageStatus;
  coin: CoinSlotStatus;
  rgb: {
    mode?: RgbMode;
    brightness: number;
    enabled?: boolean;
    state?: string;
    colorName: string;
    color: RgbColor;
    systemStatus?: string;
  };
  memory: {
    heap: number;
    minimumHeap: number;
    psram?: number;
    psramSize?: number;
  };
  esp32?: {
    cpuFreqMHz?: number;
    chipModel?: string;
    chipRevision?: number;
    chipTempC?: number;
    chipTempAvailable?: boolean;
  };
};

export type SystemStatus = {
  server: { ok: boolean; uptimeSeconds: number };
  database: { ok: boolean; path?: string };
  sales: {
    today: { amount: number; sessions: number };
    weekly: { amount: number; sessions: number };
    monthly: { amount: number; sessions: number };
  };
  activeUsers: { count: number; paused: number; idle: number };
  mikrotik: {
    ok: boolean;
    host: string;
    latencyMs: number;
    configured?: boolean;
    connectivity?: "online" | "offline" | "unknown" | string;
    lastSuccessfulContactAt?: string;
    lastContactError?: string;
  };
  routerCache?: {
    populated?: boolean;
    identity?: string;
    routerOsVersion?: string;
    ssid?: string;
    security?: string;
    wirelessInterface?: string;
    hotspotProfile?: string;
    lastSynchronizedAt?: string;
    provisionStatus?: string;
    cacheAgeSeconds?: number;
    staleThresholdHours?: number;
    stale?: boolean;
    syncWallClockValid?: boolean;
    lastSynchronizedMillis?: number;
    routerOs?: {
      version?: string;
      cpuLoad?: string;
      freeMemory?: string;
      totalMemory?: string;
      uptime?: string;
    };
    observation?: {
      connectivity?: string;
      hotspotStatus?: string;
      lastSuccessfulContactAt?: string;
      lastContactError?: string;
    };
    productionNetwork?: {
      verified?: boolean;
      interface?: string;
      ssid?: string;
      expectedSsid?: string;
      frequency?: number | string;
      channel?: number | string;
      mode?: string;
      verifiedAt?: string;
      reason?: string;
    };
  };
  internet: { ok: boolean; latencyMs: number; known?: boolean };
  wan?: {
    known?: boolean;
    interface?: string;
    link?: string;
    dhcp?: string;
    ip?: string;
    gateway?: string;
    defaultRoute?: string;
    internet?: string;
    dns?: string;
    note?: string;
  };
  coinSlot: CoinSlotStatus;
  storageStatus?: StorageStatus;
  hotspot: {
    ok: boolean;
    status?: "available" | "unavailable" | "unknown" | string;
    server?: string;
    interface?: string;
  };
  esp32: { uptime: string; lastSeen: string | null };
  storage: {
    flashUsedMb: number;
    flashTotalMb: number;
    /** Internal Arduino heap used (not total chip RAM). */
    ramUsedKb: number;
    ramTotalKb: number;
    ramLabel?: string;
    internalHeap?: {
      totalKb: number;
      freeKb: number;
      usedKb: number;
      minFreeKb?: number;
      largestKb?: number;
    };
    psram?: {
      present: boolean;
      totalKb: number;
      freeKb: number;
      usedKb: number;
      minFreeKb?: number;
    };
    dma?: {
      freeKb: number;
      largestKb: number;
      minimumKb?: number;
    };
    logsUsedKb: number;
    logsTotalKb: number;
    sd: {
      present: boolean;
      mounted: boolean;
      usedMb: number;
      totalMb: number;
      freeMb: number;
      status: "Ready" | "Missing" | "Mount Failed" | "Read Only" | "Error";
      fallback?: boolean;
      pollingDisabled?: boolean;
      recoveryAttempts?: number;
      mode?: "SD" | "SPIFFS Fallback";
    };
  };
  sync: { pending: number; lastSyncAt: string | null };
};

export type ChartData = { labels: string[]; data: number[] };

export type SalesHistoryRow = {
  date: string;
  sessions: number;
  revenue: number;
};

export type SaleSessionRecord = {
  id: string;
  sessionId: string;
  recorded_at?: string;
  recordedAt?: string;
  paymentType: "coin" | "voucher";
  amount: number;
  durationMinutes: number;
  macAddress?: string;
  ipAddress?: string;
  voucherCode?: string;
  credits?: number;
  profile?: string;
  speed?: string;
  expiresAt?: string;
  operatorName?: string;
  connectedAt?: string;
  endedAt?: string;
  actualConnectedSeconds?: number;
  status?: string;
  terminationReason?: string;
};
