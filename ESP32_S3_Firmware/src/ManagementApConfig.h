#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  ManagementApConfig.h — Installer / owner Wi-Fi access point constants
//
//  The Management AP is a dedicated ESP32 soft-AP for setup, admin, diagnostics,
//  and maintenance. It does NOT carry customer traffic and does NOT share
//  Internet with connected clients.
// ─────────────────────────────────────────────────────────────────────────────

#include <IPAddress.h>

namespace ManagementApConfig {

static const IPAddress IP     (192, 168, 4, 1);
static const IPAddress GATEWAY(192, 168, 4, 1);
static const IPAddress SUBNET (255, 255, 255, 0);

// Exact, fixed SSID — no MAC/serial suffix. A per-unit suffix would leak the
// device MAC over the air and make the mobile app's SSID match fragile; a
// single well-known name is simpler for installers and matches the mobile
// app's exact-match detection.
static constexpr const char *SSID         = "Renz-Fi Setup";
static constexpr const char *PORTAL_URL  = "http://192.168.4.1";
static constexpr const char *SETUP_PATH  = "/admin/setup";
static constexpr const char *SETUP_URL   = "http://192.168.4.1/admin/setup";
static constexpr uint8_t     CHANNEL       = 6;
static constexpr uint8_t     MAX_CLIENTS   = 4;

// Maintenance mode auto-stop after all admin sessions end (Phase 7C.2).
static constexpr uint32_t MAINTENANCE_TIMEOUT_SECONDS = 600;

}  // namespace ManagementApConfig
