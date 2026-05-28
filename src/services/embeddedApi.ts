const DEFAULT_EMBEDDED_HOST = "10.10.10.1";

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
  return `${apiBaseUrl}${path.startsWith("/") ? path : `/${path}`}`;
}

export function getEmbeddedHost() {
  return window.location.hostname || DEFAULT_EMBEDDED_HOST;
}

export function getDefaultAdminAddress() {
  const host = getEmbeddedHost();
  return host === "localhost" || host === "127.0.0.1" ? DEFAULT_EMBEDDED_HOST : host;
}
