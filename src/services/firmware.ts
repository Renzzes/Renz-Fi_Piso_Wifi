import { api, ApiError } from "./api";
import { apiUrl, embeddedApi } from "./embeddedApi";
import { handleUnauthorizedResponse } from "./authSession";

export type FirmwareInfo = {
  version: string;
  build: string;
  sizeMb: number;
  partition?: string;
  note?: string;
};

export type FirmwareProgress = {
  phase: "upload" | "verify" | "complete";
  received?: number;
  md5?: string;
};

export const firmwareApi = {
  info: () => api.get<FirmwareInfo>(`${embeddedApi.system}/firmware`),

  uploadBin: (
    file: File,
    onProgress?: (pct: number, detail?: FirmwareProgress) => void,
  ): Promise<{ ok: boolean; bytes?: number; md5?: string }> => {
    return new Promise((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      const body = file;

      xhr.open("POST", apiUrl(`${embeddedApi.system}/firmware`), true);
      xhr.withCredentials = true;
      xhr.setRequestHeader("Content-Type", "application/octet-stream");

      xhr.upload.onprogress = (event) => {
        if (!event.lengthComputable || !onProgress) return;
        const pct = Math.round((event.loaded / event.total) * 100);
        onProgress(pct, { phase: "upload", received: event.loaded });
      };

      xhr.onload = () => {
        try {
          const json = JSON.parse(xhr.responseText) as {
            success?: boolean;
            data?: { ok?: boolean; bytes?: number; md5?: string };
            error?: string;
            code?: string;
          };
          if (xhr.status === 401) {
            handleUnauthorizedResponse(`${embeddedApi.system}/firmware`);
          }
          if (xhr.status >= 200 && xhr.status < 300 && json.success) {
            onProgress?.(100, { phase: "complete", md5: json.data?.md5 });
            resolve({
              ok: Boolean(json.data?.ok),
              bytes: json.data?.bytes,
              md5: json.data?.md5,
            });
            return;
          }
          reject(
            new ApiError(String(json.error ?? "Firmware upload failed"), xhr.status, json.code),
          );
        } catch (err) {
          reject(err instanceof Error ? err : new Error("Firmware upload failed"));
        }
      };

      xhr.onerror = () => reject(new Error("Network error during firmware upload"));
      xhr.send(body);
    });
  },
};
