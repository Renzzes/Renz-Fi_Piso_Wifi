import { apiUrl } from "./embeddedApi";
import { ApiError, api } from "./api";
import { embeddedApi } from "./embeddedApi";

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

export type SetupUnlockStatus = {
  configured: boolean;
  recoverable?: boolean;
  password?: string;
};

export type OperatorAccountStatus = {
  configured: boolean;
  username?: string;
  permissions?: string[];
};

export const settingsApi = {
  admin: () => api.get<{ adminIp?: string }>(`${embeddedApi.settings}/admin`),
  updateAdmin: (payload: { password?: string }) =>
    api.put<{ ok: boolean }>(`${embeddedApi.settings}/admin`, payload),

  setupUnlock: () =>
    api.get<SetupUnlockStatus>(`${embeddedApi.settings}/setup-unlock`),
  changeSetupUnlock: (payload: {
    currentPassword: string;
    newPassword: string;
    confirmPassword: string;
  }) =>
    api.post<{ ok: boolean; configured?: boolean }>(
      `${embeddedApi.settings}/setup-unlock`,
      payload,
    ),

  operator: () =>
    api.get<OperatorAccountStatus>(`${embeddedApi.settings}/operator`),
  createOperator: (payload: {
    username: string;
    password: string;
    confirmPassword: string;
    permissions?: string[];
  }) =>
    api.post<{
      ok: boolean;
      configured?: boolean;
      username?: string;
      permissions?: string[];
    }>(`${embeddedApi.settings}/operator`, payload),
  updateOperatorPermissions: (permissions: string[]) =>
    api.post<{
      ok: boolean;
      configured?: boolean;
      username?: string;
      permissions?: string[];
    }>(`${embeddedApi.settings}/operator`, { permissions }),

  backup: async () => {
    const res = await fetch(apiUrl(`${embeddedApi.settings}/backup`), {
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
        /* ignore */
      }
      throw new ApiError(message, res.status, code);
    }
    const blob = await res.blob();
    const filename = parseFilename(
      res.headers.get("content-disposition"),
      "renzfi-backup.zip",
    );
    downloadBlob(blob, filename);
  },

  restoreFile: async (file: File) => {
    if (file.name.toLowerCase().endsWith(".json")) {
      const text = await file.text();
      const backup = JSON.parse(text) as unknown;
      return api.post<{ rebooting?: boolean }>(`${embeddedApi.settings}/restore`, backup);
    }
    const form = new FormData();
    form.append("backup", file, file.name);
    return api.upload<{ rebooting?: boolean }>(`${embeddedApi.settings}/restore`, form);
  },
};
