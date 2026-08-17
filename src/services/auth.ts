import { api } from "./api";
import { apiUrl, embeddedApi } from "./embeddedApi";
import type { OperatorPermission } from "@/lib/operatorPermissions";

const REMEMBER_IP_KEY = "renz_admin_ip";

export type AuthRole = "owner" | "operator" | "none";

export const authApi = {
  login: (password: string) =>
    api.post<{
      authenticated: boolean;
      rememberIp: boolean;
      mustChangePassword?: boolean;
      firstBootCompleted?: boolean;
      role?: AuthRole;
      permissions?: OperatorPermission[];
    }>(`${embeddedApi.auth}/login`, {
      password,
    }),

  logout: () => api.post<{ success: boolean; message: string }>(`${embeddedApi.auth}/logout`),

  changePassword: (payload: { oldPassword: string; newPassword: string }) =>
    api.post<{ ok: boolean; mustChangePassword?: boolean; firstBootCompleted?: boolean }>(
      `${embeddedApi.auth}/change-password`,
      payload,
    ),

  health: () =>
    fetch(apiUrl(embeddedApi.health), { credentials: "include" }).then(async (res) => {
      if (!res.ok) throw new Error(`Health check failed: ${res.status}`);
      const json = (await res.json()) as {
        success?: boolean;
        data?: {
          ok: boolean;
          session?: {
            authenticated?: boolean;
            mustChangePassword?: boolean;
            firstBootCompleted?: boolean;
            role?: AuthRole;
            permissions?: OperatorPermission[];
          };
        };
        ok?: boolean;
        session?: {
          authenticated?: boolean;
          mustChangePassword?: boolean;
          firstBootCompleted?: boolean;
          role?: AuthRole;
          permissions?: OperatorPermission[];
        };
      };
      return json.data ?? { ok: Boolean(json.ok), session: json.session };
    }),
};

export function getRememberedIp(): string | null {
  try {
    return localStorage.getItem(REMEMBER_IP_KEY);
  } catch {
    return null;
  }
}

export function setRememberedIp(ip: string) {
  try {
    localStorage.setItem(REMEMBER_IP_KEY, ip);
  } catch {
    // ignore
  }
}

export function clearRememberedIp() {
  try {
    localStorage.removeItem(REMEMBER_IP_KEY);
  } catch {
    // ignore
  }
}
