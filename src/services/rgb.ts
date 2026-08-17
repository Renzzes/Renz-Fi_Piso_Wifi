import type { RgbMode, RgbSystemStatus, SystemHealth } from "@/types/api";
import { api } from "./api";
import { embeddedApi } from "./embeddedApi";
import { logDashboardApi } from "@/lib/dashboardApiLog";

export type RgbStatus = RgbSystemStatus;

export const rgbApi = {
  system: async () => {
    try {
      return await api.get<RgbSystemStatus>(`${embeddedApi.system}/rgb`);
    } catch (err) {
      logDashboardApi("GET /api/system/rgb", err);
      throw err;
    }
  },
  saveSystem: (settings: Pick<RgbSystemStatus, "enabled" | "brightness">) =>
    api.put<RgbSystemStatus>(`${embeddedApi.system}/rgb`, settings),
  status: () => api.get<RgbStatus>("/api/rgb/status"),
  setMode: (mode: RgbMode) => api.post("/api/rgb/mode", { mode }),
  setColor: (color: { red: number; green: number; blue: number }) =>
    api.post("/api/rgb/color", color),
  setBrightness: (brightness: number) => api.post("/api/rgb/brightness", { brightness }),
};

export const healthApi = {
  get: async () => {
    try {
      return await api.get<SystemHealth>(`${embeddedApi.system}/health`);
    } catch (err) {
      logDashboardApi("GET /api/system/health", err);
      throw err;
    }
  },
};
