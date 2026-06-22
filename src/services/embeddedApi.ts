const DEFAULT_EMBEDDED_HOST = "192.168.30.252";

export const apiBaseUrl = (import.meta.env.VITE_API_BASE ?? "").replace(/\/$/, "");

export const embeddedApi = {
  health: "/api/health",
  status: "/api/status",
  system: "/api/system",
  promos: "/api/promos",
  vouchers: "/api/vouchers",
  users: "/api/users",
  sales: "/api/sales",
  settings: "/api/settings",
  logs: "/api/logs",
  coin: "/api/coin",
  router: "/api/router",
  auth: "/api/auth",
  events: "/api/events",
};

export function apiUrl(path: string) {
  if (/^https?:\/\//i.test(path)) return path;
  return `${apiBaseUrl}${path.startsWith("/") ? path : `/${path}`}`;
}

/** Resolve portal branding asset URLs from settings (relative or absolute). */
export function resolvePortalAssetUrl(url?: string | null): string {
  if (!url) return "";
  return apiUrl(url);
}

export function getEmbeddedHost() {
  return window.location.hostname || DEFAULT_EMBEDDED_HOST;
}

export function getDefaultAdminAddress() {
  const host = getEmbeddedHost();
  return host === "127.0.0.1" || host === "::1" ? DEFAULT_EMBEDDED_HOST : host;
}
