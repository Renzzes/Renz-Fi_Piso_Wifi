import type { ChartData, SalesHistoryRow } from "@/types/api";
import { api } from "./api";
import { embeddedApi } from "./embeddedApi";

export const salesApi = {
  today: () => api.get<{ amount: number; sessions: number }>(`${embeddedApi.sales}/today`),
  weekly: () => api.get<{ amount: number; sessions: number }>(`${embeddedApi.sales}/weekly`),
  monthly: () => api.get<{ amount: number; sessions: number }>(`${embeddedApi.sales}/monthly`),
  history: () => api.get<SalesHistoryRow[]>(`${embeddedApi.sales}/history`),
  chartDaily: () => api.get<ChartData>(`${embeddedApi.sales}/chart/daily`),
  chartWeekly: () => api.get<ChartData>(`${embeddedApi.sales}/chart/weekly`),
  chartMonthly: () => api.get<ChartData>(`${embeddedApi.sales}/chart/monthly`),
  exportUrl: () => `${embeddedApi.sales}/export`,
};
