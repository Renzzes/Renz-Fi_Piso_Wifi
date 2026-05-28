#pragma once

#include <Arduino.h>

namespace RenzFiConfig {

static constexpr const char *AP_SSID = "Renz-Fi";
static constexpr const char *AP_PASSWORD = "";
static constexpr const char *MDNS_NAME = "renz-fi";

static const IPAddress AP_IP(10, 10, 10, 1);
static const IPAddress AP_GATEWAY(10, 10, 10, 1);
static const IPAddress AP_SUBNET(255, 255, 255, 0);

static constexpr uint16_t HTTP_PORT = 80;
static constexpr uint16_t DNS_PORT = 53;

static constexpr int PIN_SD_CS = 10;
static constexpr int PIN_SD_SCK = 12;
static constexpr int PIN_SD_MISO = 13;
static constexpr int PIN_SD_MOSI = 11;

static constexpr int PIN_COIN = 4;
static constexpr int PIN_INSERT_COIN_LED = 5;

static constexpr uint32_t COIN_DEBOUNCE_MS = 35;
static constexpr uint32_t COIN_SETTLE_MS = 450;
static constexpr uint32_t SESSION_TTL_SECONDS = 8UL * 60UL * 60UL;
static constexpr uint32_t SSE_HEARTBEAT_MS = 15000;
static constexpr uint32_t CLEANUP_INTERVAL_MS = 30000;
static constexpr uint32_t LED_TICK_MS = 150;

static constexpr size_t JSON_DOC_SMALL = 2048;
static constexpr size_t JSON_DOC_MEDIUM = 8192;
static constexpr size_t JSON_DOC_LARGE = 24576;

static constexpr const char *DEFAULT_ADMIN_PASSWORD = "admin";
static constexpr const char *SESSION_COOKIE = "renz_session";

static constexpr const char *WWW_ROOT = "/www";
static constexpr const char *SETTINGS_FILE = "/config/settings.json";
static constexpr const char *PROMOS_FILE = "/config/promos.json";
static constexpr const char *ROUTER_FILE = "/config/router.json";
static constexpr const char *VOUCHERS_FILE = "/vouchers/vouchers.json";
static constexpr const char *SALES_FILE = "/sales/sales.json";
static constexpr const char *LOGS_FILE = "/logs/logs.json";
static constexpr const char *USERS_FILE = "/sessions/users.json";
static constexpr const char *ADMIN_SESSIONS_FILE = "/sessions/admin.json";

}  // namespace RenzFiConfig
