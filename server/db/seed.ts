import { db } from "./connection.js";

const promoCount = db.prepare("SELECT COUNT(*) as c FROM promo_rates").get() as { c: number };
if (promoCount.c === 0) {
  const insertPromo = db.prepare(
    `INSERT INTO promo_rates (name, coin, minutes, speed, devices) VALUES (?, ?, ?, ?, ?)`,
  );
  const promos = [
    ["1 Peso", 1, 15, 5, 1],
    ["5 Pesos", 5, 90, 10, 1],
    ["10 Pesos", 10, 240, 10, 2],
    ["20 Pesos", 20, 600, 15, 2],
  ] as const;
  for (const p of promos) insertPromo.run(...p);
}

const voucherCount = db.prepare("SELECT COUNT(*) as c FROM vouchers").get() as { c: number };
if (voucherCount.c === 0) {
  const insertVoucher = db.prepare(
    `INSERT INTO vouchers (code, amount, minutes, status, expires) VALUES (?, ?, ?, ?, ?)`,
  );
  const vouchers = [
    ["R8K2-PMQX", 10, 240, "unused", "2026-06-01"],
    ["T4N9-LZBV", 20, 600, "active", "2026-05-28"],
    ["W1HC-X7DR", 5, 90, "expired", "2026-05-10"],
    ["Q3FE-2YML", 5, 90, "unused", "2026-06-15"],
  ] as const;
  for (const v of vouchers) insertVoucher.run(...v);
}

const logCount = db.prepare("SELECT COUNT(*) as c FROM logs").get() as { c: number };
if (logCount.c === 0) {
  const insertLog = db.prepare(
    `INSERT INTO logs (level, message, created_at) VALUES (?, ?, datetime('now', ?))`,
  );
  const logs = [
    ["INFO", "Voucher T4N9-LZBV activated for 10.10.10.34", "-2 minutes"],
    ["OK", "Coin accepted: ₱5 from slot", "-4 minutes"],
    ["WARN", "Hotspot user session timeout 10.10.10.58", "-7 minutes"],
    ["INFO", "MikroTik connection healthy (32ms)", "-9 minutes"],
    ["INFO", "ESP32 boot complete - 3d 4h uptime", "-12 minutes"],
    ["ERR", "Failed login attempt for user 'admin'", "-17 minutes"],
  ] as const;
  for (const l of logs) insertLog.run(l[0], l[1], l[2]);
}

const salesCount = db.prepare("SELECT COUNT(*) as c FROM sales_transactions").get() as {
  c: number;
};
if (salesCount.c === 0) {
  const insertSale = db.prepare(
    `INSERT INTO sales_transactions (amount, sessions, recorded_at) VALUES (?, ?, date('now', ?))`,
  );
  const sales = [
    [248, 32, "0 days"],
    [195, 28, "-1 days"],
    [312, 41, "-2 days"],
    [110, 19, "-3 days"],
  ] as const;
  for (const s of sales) insertSale.run(s[0], s[1], s[2]);
}

function setDefault(table: string, key: string, value: string) {
  db.prepare(`INSERT INTO ${table} (key, value) VALUES (?, ?) ON CONFLICT(key) DO NOTHING`).run(
    key,
    value,
  );
}

setDefault("admin_settings", "username", "admin");
setDefault("admin_settings", "password_hash", "");
setDefault("portal_settings", "portal_name", "Renz-Fi Hotspot");
setDefault("portal_settings", "banner", "");
setDefault("portal_settings", "welcome_message", "Welcome! Insert coins or use a voucher.");
setDefault("portal_settings", "announcement", "");
setDefault("portal_settings", "primary_color", "#1e293b");
setDefault("coin_settings", "pulse_width_ms", "100");
setDefault("coin_settings", "calibration", "1");
setDefault("coin_settings", "timeout_seconds", "30");
setDefault("coin_settings", "last_pulse", "0");
setDefault("coin_settings", "total_today", "248");
setDefault("coin_settings", "errors", "0");
setDefault("coin_settings", "state", "Ready");
setDefault("router_settings", "host", "10.40.0.1");
setDefault("router_settings", "username", "admin");
setDefault("router_settings", "password", "");
setDefault("router_settings", "profile", "default");
setDefault("router_settings", "ssid", "RenzFi_PesoWifi");
setDefault("router_settings", "connected", "1");
