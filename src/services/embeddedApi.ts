const DEFAULT_EMBEDDED_HOST = "192.168.30.252";

/** Build-time override (dev proxy target). */
const envBaseUrl = (import.meta.env.VITE_API_BASE ?? "").replace(/\/$/, "");

/** Runtime fleet target — empty string means same-origin (Direct Mode). */
let runtimeApiBaseUrl: string | null = null;

export function getRuntimeApiBaseUrl(): string {
  if (runtimeApiBaseUrl != null) return runtimeApiBaseUrl;
  return envBaseUrl;
}

export function setRuntimeApiBaseUrl(base: string | null): void {
  runtimeApiBaseUrl = base == null ? null : base.replace(/\/$/, "");
}

/** @deprecated Use getRuntimeApiBaseUrl — kept for existing imports. */
export const apiBaseUrl = envBaseUrl;

export function isDirectMode(): boolean {
  return getRuntimeApiBaseUrl() === "";
}

export function isFleetTargetActive(): boolean {
  return getRuntimeApiBaseUrl() !== "";
}

export function buildDeviceBaseUrl(ip: string): string {
  const trimmed = ip.trim();
  if (!trimmed) return "";
  if (/^https?:\/\//i.test(trimmed)) return trimmed.replace(/\/$/, "");
  return `http://${trimmed}`;
}

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
  const base = getRuntimeApiBaseUrl();
  return `${base}${path.startsWith("/") ? path : `/${path}`}`;
}

/** Resolve portal branding asset URLs from settings (relative or absolute). */
export function resolvePortalAssetUrl(url?: string | null): string {
  if (!url) return "";
  return apiUrl(url);
}

export function getEmbeddedHost() {
  if (isFleetTargetActive()) {
    try {
      return new URL(getRuntimeApiBaseUrl()).hostname;
    } catch {
      return window.location.hostname || DEFAULT_EMBEDDED_HOST;
    }
  }
  return window.location.hostname || DEFAULT_EMBEDDED_HOST;
}

export function getDefaultAdminAddress() {
  const host = getEmbeddedHost();
  return host === "127.0.0.1" || host === "::1" ? DEFAULT_EMBEDDED_HOST : host;
}
