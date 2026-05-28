import { api } from "./api";
import { embeddedApi } from "./embeddedApi";

export const firmwareApi = {
  info: () =>
    api.get<{ version: string; build: string; sizeMb: number; note: string }>(
      `${embeddedApi.system}/firmware`,
    ),
  upload: (payload?: { filename?: string; sizeBytes?: number; mimeType?: string }) =>
    api.post<{ ok: boolean; message: string }>(`${embeddedApi.system}/firmware`, payload ?? {}),
};
