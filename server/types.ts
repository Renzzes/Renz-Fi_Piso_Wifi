export type PromoRate = {
  id: number;
  name: string;
  coin: number;
  minutes: number;
  speed?: number;
  devices?: number;
  data_cap_mb?: number;
};

export type Voucher = {
  code: string;
  amount: number;
  minutes: number;
  status: "unused" | "active" | "expired";
  expires: string;
};

export type ActiveUser = {
  mac: string;
  ip: string;
  remaining: string;
  device: string;
};

export type LogEntry = {
  id?: number;
  t: string;
  lvl: string;
  msg: string;
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
  mikrotik: { ok: boolean; host: string; latencyMs: number };
  internet: { ok: boolean; latencyMs: number; known?: boolean };
  coinSlot: {
    enabled?: boolean;
    hardwareState?: string;
    lastPulseTimestamp?: string | null;
    lastCoinTimestamp?: string | null;
    totalPulseCount?: number;
    totalCoinCount?: number;
    ok: boolean;
    state: string;
    pulsesToday: number;
  };
  hotspot: { ok: boolean; ssid: string };
  esp32: { uptime: string; lastSeen: string | null };
  storage: {
    flashUsedMb: number;
    flashTotalMb: number;
    ramUsedKb: number;
    ramTotalKb: number;
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
  storageStatus?: {
    storageMode: "SD" | "SPIFFS";
    sdPresent: boolean;
    sdMounted: boolean;
    capacity: number;
    used: number;
    fallbackActive: boolean;
  };
};
