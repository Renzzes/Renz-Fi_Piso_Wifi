/** Sidebar / route permission keys assignable to Operator accounts. */
export const OPERATOR_PERMISSION_KEYS = [
  "dashboard",
  "promo-rates",
  "vouchers",
  "active-users",
  "sales-reports",
  "captive-portal",
  "coin-settings",
  "system-configuration",
  "logs",
  "firmware",
] as const;

export type OperatorPermission = (typeof OPERATOR_PERMISSION_KEYS)[number];

export const DEFAULT_OPERATOR_PERMISSIONS: OperatorPermission[] = [
  "dashboard",
  "promo-rates",
  "captive-portal",
  "coin-settings",
];

export const OPERATOR_PERMISSION_LABELS: Record<OperatorPermission, string> = {
  dashboard: "Dashboard",
  "promo-rates": "Promo Rates",
  vouchers: "Vouchers",
  "active-users": "Active Users",
  "sales-reports": "Sales Reports",
  "captive-portal": "Captive Portal",
  "coin-settings": "Coin Settings",
  "system-configuration": "System Configuration",
  logs: "Logs",
  firmware: "Update",
};

export function pathPermission(path: string): OperatorPermission | null {
  const map: Record<string, OperatorPermission> = {
    "/dashboard": "dashboard",
    "/promo-rates": "promo-rates",
    "/vouchers": "vouchers",
    "/active-users": "active-users",
    "/sales-reports": "sales-reports",
    "/captive-portal": "captive-portal",
    "/coin-settings": "coin-settings",
    "/system-configuration": "system-configuration",
    "/logs": "logs",
    "/firmware": "firmware",
  };
  return map[path] ?? null;
}

export function normalizeOperatorPermissions(
  raw: unknown,
): OperatorPermission[] {
  if (!Array.isArray(raw)) return [...DEFAULT_OPERATOR_PERMISSIONS];
  const allowed = new Set<string>(OPERATOR_PERMISSION_KEYS);
  const out: OperatorPermission[] = [];
  for (const item of raw) {
    if (typeof item !== "string") continue;
    if (!allowed.has(item)) continue;
    if (!out.includes(item as OperatorPermission)) {
      out.push(item as OperatorPermission);
    }
  }
  return out.length > 0 ? out : [...DEFAULT_OPERATOR_PERMISSIONS];
}
