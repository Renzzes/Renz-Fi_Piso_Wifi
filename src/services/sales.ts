import type { ChartData, SalesHistoryRow } from "@/types/api";
import { ApiError, api } from "./api";
import { apiUrl, embeddedApi } from "./embeddedApi";

function parseFilename(contentDisposition: string | null, fallback: string) {
  if (!contentDisposition) return fallback;
  const match = /filename="([^"]+)"/i.exec(contentDisposition);
  return match?.[1] ?? fallback;
}

function downloadBlob(blob: Blob, filename: string) {
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = filename;
  anchor.click();
  URL.revokeObjectURL(url);
}

export const salesApi = {
  today: () => api.get<{ amount: number; sessions: number }>(`${embeddedApi.sales}/today`),
  weekly: () => api.get<{ amount: number; sessions: number }>(`${embeddedApi.sales}/weekly`),
  monthly: () => api.get<{ amount: number; sessions: number }>(`${embeddedApi.sales}/monthly`),
  history: () => api.get<SalesHistoryRow[]>(`${embeddedApi.sales}/history`),
  chartDaily: () => api.get<ChartData>(`${embeddedApi.sales}/chart/daily`),
  chartWeekly: () => api.get<ChartData>(`${embeddedApi.sales}/chart/weekly`),
  chartMonthly: () => api.get<ChartData>(`${embeddedApi.sales}/chart/monthly`),

  exportCsv: async () => {
    const res = await fetch(apiUrl(`${embeddedApi.sales}/export`), {
      credentials: "include",
    });
    if (!res.ok) {
      let message = res.statusText;
      let code: string | undefined;
      try {
        const json = (await res.json()) as { error?: string; code?: string };
        message = String(json.error ?? message);
        code = json.code;
      } catch {
        const text = await res.text().catch(() => "");
        if (text) message = text;
      }
      throw new ApiError(message, res.status, code);
    }
    const blob = await res.blob();
    const filename = parseFilename(
      res.headers.get("content-disposition"),
      `sales-report-${new Date().toISOString().slice(0, 10)}.csv`,
    );
    downloadBlob(blob, filename);
  },
};
