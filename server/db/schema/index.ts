// Central place to document table names used by the admin dashboard.
// This is intentionally lightweight and purely for maintainability.
export const tables = {
  promo_rates: "promo_rates",
  vouchers: "vouchers",
  sales_transactions: "sales_transactions",
  active_sessions: "active_sessions",
  logs: "logs",
  admin_settings: "admin_settings",
  portal_settings: "portal_settings",
  coin_settings: "coin_settings",
  router_settings: "router_settings",
  sync_batches: "sync_batches",
  sync_events: "sync_events",
  admin_sessions: "admin_sessions",
} as const;
