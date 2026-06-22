#pragma once

#include <Arduino.h>

namespace RenzFiConfig {

static constexpr const char *FIRMWARE_VERSION = "0.5.0-w5500";

// ── Network backend ───────────────────────────────────────────────────────────
// Backend networking is now handled by W5500 wired Ethernet (VLAN40).
// See W5500Config.h for pin map and static IP configuration.
// WiFi STA mode is no longer used for backend connectivity.

// ── mDNS ─────────────────────────────────────────────────────────────────────
// Resolves as renzfi.local on the VLAN40 management network
static constexpr const char *MDNS_NAME = "renzfi";

// ── HTTP ─────────────────────────────────────────────────────────────────────
static constexpr uint16_t HTTP_PORT = 80;

// ── MikroTik RouterOS API (binary API over W5500 Ethernet) ────────────────────
static constexpr uint16_t  ROUTEROS_API_PORT            = 8728;
static constexpr uint32_t  ROUTEROS_CONNECT_TIMEOUT_MS  = 5000;
static constexpr uint32_t  ROUTEROS_IO_TIMEOUT_MS       = 8000;

// ── Hardware pins ─────────────────────────────────────────────────────────────
//  W5500 uses a dedicated HSPI bus — see W5500Config.h.
//  SD card uses a separate FSPI bus (pins below).
static constexpr int PIN_SD_CS   = 18;
static constexpr int PIN_SD_SCK  = 7;
static constexpr int PIN_SD_MISO = 5;
static constexpr int PIN_SD_MOSI = 6;
// SD library runtime SPI frequency (card init uses 400 kHz internally).
static constexpr uint32_t SD_SPI_FREQ_HZ = 1000000;

static constexpr int PIN_COIN          = 4;
// GPIO 35–37 are OPI PSRAM data lines on N8R8 — do not use.
// GPIO 38–40 are safe user GPIOs with no internal peripheral conflicts.
static constexpr int PIN_RGB_LED_RED   = 38;
static constexpr int PIN_RGB_LED_GREEN = 39;
static constexpr int PIN_RGB_LED_BLUE  = 40;

// ── Timing ───────────────────────────────────────────────────────────────────
static constexpr uint32_t COIN_DEBOUNCE_MS       = 35;
static constexpr uint32_t COIN_SETTLE_MS         = 450;
static constexpr uint32_t COIN_INSERT_TIMEOUT_SEC = 60;
static constexpr uint32_t SESSION_TTL_SECONDS  = 8UL * 60UL * 60UL;
static constexpr uint32_t SSE_HEARTBEAT_MS     = 15000;
static constexpr uint32_t CLEANUP_INTERVAL_MS  = 30000;
static constexpr uint32_t LED_TICK_MS          = 150;
static constexpr uint32_t RGB_LED_ACCEPTED_MS  = 1200;

// ── JSON document sizes ───────────────────────────────────────────────────────
static constexpr size_t JSON_DOC_SMALL  = 2048;
static constexpr size_t JSON_DOC_MEDIUM = 8192;
static constexpr size_t JSON_DOC_LARGE  = 24576;

// ── Auth / session ────────────────────────────────────────────────────────────
static constexpr const char *DEFAULT_ADMIN_PASSWORD = "admin";
static constexpr const char *SESSION_COOKIE         = "renz_session";

// ── File paths ────────────────────────────────────────────────────────────────
static constexpr const char *WWW_ROOT            = "/www";
static constexpr const char *SETTINGS_FILE       = "/config/settings.json";
static constexpr const char *PROMOS_FILE         = "/config/promos.json";
static constexpr const char *ROUTER_FILE         = "/config/router.json";
static constexpr const char *WIFI_CONFIG_FILE    = "/config/wifi.json";
static constexpr const char *VOUCHERS_FILE       = "/vouchers/vouchers.json";
static constexpr const char *SALES_FILE          = "/sales/sales.json";
static constexpr const char *LOGS_FILE           = "/logs/logs.json";
static constexpr uint32_t LOGS_QUOTA_KB          = 256;  // nominal SD log budget for status UI
static constexpr const char *USERS_FILE           = "/sessions/users.json";
static constexpr const char *ADMIN_SESSIONS_FILE  = "/sessions/admin.json";
static constexpr const char *PORTAL_SESSIONS_FILE = "/sessions/portal_sessions.json";
static constexpr const char *PORTAL_CONFIG_FILE     = "/config/portal.json";

// Captive portal branding assets (SD primary, SPIFFS fallback)
static constexpr const char *PORTAL_BANNER_SD       = "/www/portal-banner.webp";
static constexpr const char *PORTAL_MUSIC_SD        = "/www/portal-bg-music.mp3";
static constexpr const char *PORTAL_BANNER_SPIFFS   = "/portal/custom/banner.webp";
static constexpr const char *PORTAL_MUSIC_SPIFFS    = "/portal/custom/bg-music.mp3";
static constexpr const char *PORTAL_BANNER_DEFAULT  = "/portal/Default-Banner.png";
static constexpr const char *PORTAL_MUSIC_DEFAULT   = "/portal/bg_music.mp3";
static constexpr size_t      PORTAL_MUSIC_MAX_BYTES = 1024 * 1000;  // 1000 KiB hard limit
static constexpr size_t      LOG_RAM_BUFFER_SIZE    = 500;

// ── Portal session timing ──────────────────────────────────────────────────────
static constexpr uint32_t COIN_WINDOW_SECS       = 60;   // insert-coin window length
static constexpr uint32_t PORTAL_SAVE_INTERVAL_MS = 30000; // periodic SD persist for timers
static constexpr uint32_t PORTAL_IDLE_TTL_SEC       = 900;   // 15 min — drop idle portal records
static constexpr uint32_t PORTAL_HEARTBEAT_STALE_SEC = 120;  // drop active rows without heartbeat

// ── NVS namespace ─────────────────────────────────────────────────────────────
static constexpr const char *NVS_WIFI_NS = "renzfi_wifi";  // kept for legacy NVS reads

// ── SPIFFS fallback storage (SD unavailable) ─────────────────────────────────
static constexpr const char *FB_MANIFEST        = "/fallback/.manifest.json";
static constexpr const char *FB_SETTINGS        = "/fallback/settings.json";
static constexpr const char *FB_PROMOS          = "/fallback/promos.json";
static constexpr const char *FB_ROUTER          = "/fallback/router.json";
static constexpr const char *FB_VOUCHERS        = "/fallback/vouchers.json";
// Short path: SPIFFS max object name is 32 bytes; ".tmp" atomic writes need headroom.
static constexpr const char *FB_PORTAL_SESSIONS = "/fb/ps.json";
static constexpr const char *FB_SALES           = "/fb/sales.json";
static constexpr const char *FB_PORTAL_CONFIG     = "/fb/pcfg.json";

static constexpr size_t FB_LIMIT_SETTINGS         = 8 * 1024;
static constexpr size_t FB_LIMIT_PROMOS           = 32 * 1024;
static constexpr size_t FB_LIMIT_ROUTER           = 8 * 1024;
static constexpr size_t FB_LIMIT_VOUCHERS         = 64 * 1024;
static constexpr size_t FB_LIMIT_PORTAL_SESSIONS  = 128 * 1024;
static constexpr size_t FB_LIMIT_SALES            = 128 * 1024;
static constexpr size_t FB_LIMIT_PORTAL_CONFIG    = 1024;

static constexpr size_t FB_SOFT_LIMIT_BYTES    = 256 * 1024;
static constexpr size_t FB_HARD_LIMIT_BYTES    = 320 * 1024;
static constexpr size_t SPIFFS_MIN_FREE_BYTES   = 128 * 1024;

static constexpr uint32_t FB_WRITE_MIN_INTERVAL_MS = 30000;
static constexpr uint32_t STORAGE_HEALTH_POLL_MS   = 60000;
static constexpr uint8_t SD_RECOVERY_MAX_ATTEMPTS  = 3;

}  // namespace RenzFiConfig
