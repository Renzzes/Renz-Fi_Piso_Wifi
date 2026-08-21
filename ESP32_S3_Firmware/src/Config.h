#pragma once

#include <cstddef>
#include <cstdint>

#include "StoragePaths.h"

namespace RenzFiConfig {

static constexpr const char *FIRMWARE_VERSION = "0.5.0-w5500";
#if defined(RENZFI_BOARD_WAVESHARE_ESP32_S3_ETH)
static constexpr const char *HARDWARE_REVISION = "ESP32-S3-ETH-WAVESHARE";
#else
static constexpr const char *HARDWARE_REVISION = "ESP32-S3-W5500-N8R8";
#endif

// ── Optional hardware ───────────────────────────────────────────────────────
// Set true when Universal Coin Slot is wired (pulse input + RGB LEDs).
static constexpr bool ENABLE_COIN_MANAGER = true;

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
// Proven-working budget for RouterOsClient::login()/executeCommand(), used
// unchanged by MikroTikDriver (foundation apply / ongoing router
// management) since before this session's Setup Wizard work began.
static constexpr uint32_t  ROUTEROS_CONNECT_TIMEOUT_MS  = 5000;
static constexpr uint32_t  ROUTEROS_IO_TIMEOUT_MS       = 8000;

// CONFIRMED REGRESSION (diagnosed with the temporary login instrumentation
// in RouterOsClient::login()/readByte()/readWord() — see LOGIN TX WORDS /
// LOGIN RX / LOGIN RX TIMEOUT serial blocks):
//
// SETUP_ROUTER_CONNECT_TIMEOUT_MS / SETUP_ROUTER_IO_TIMEOUT_MS previously
// carried a 2000ms/2000ms budget for the *same* RouterOsClient::login() call
// used by the Setup Wizard's Test Connection / Save Connection / Existing
// Network Scan flows (via RouterSession in RouterProvisioningManager.cpp and
// SetupRouterValidator.cpp). That is a 4x shorter read window than the one
// physically proven to work above. On a real MikroTik hAP lite, TCP connect
// and the login sentence write always completed, but the router's API reply
// consistently did not arrive inside 2000ms — surfacing as
// ROUTEROS_API_READ_TIMEOUT with reason=sentence_deadline_exceeded before
// the scan ever reached its first inspection command.
//
// Fix: align to the proven ROUTEROS_*_TIMEOUT_MS values above. Kept as
// separate named constants (not merged into one shared symbol) because they
// gate conceptually different call sites — the interactive Setup Wizard
// (a user is actively waiting on a button) vs. the autonomous
// MikroTikDriver (background router management) — and are allowed to
// diverge again later without coupling one caller's UX budget to the
// other's. Every RouterOsClient::setTimeouts() call site currently in the
// firmware, and the policy it uses:
//   RouterProvisioningManager.cpp   RouterSession ctor            SETUP_ROUTER_*
//   RouterProvisioningWorker.cpp    TcpDiagnostic job              SETUP_ROUTER_*
//   RouterProvisioningWorker.cpp    ApiProtocolDiagnostic job       SETUP_ROUTER_*
//   SetupRouterValidator.cpp        testConnection/saveConnection   SETUP_ROUTER_*
//   router/drivers/MikroTikDriver.cpp  foundation apply etc.        ROUTEROS_*
// All Setup Wizard call sites already share one policy; only the driver
// uses the other, by design (see above).
static constexpr uint32_t  SETUP_ROUTER_CONNECT_TIMEOUT_MS = 5000;
static constexpr uint32_t  SETUP_ROUTER_IO_TIMEOUT_MS      = 8000;
static constexpr uint32_t  ROUTER_API_SENTENCE_TIMEOUT_MS    = 2000;

// RouterOS worker — serialized off async_tcp (see RouterProvisioningWorker).
// FreeRTOS stack size is in words (StackType_t), not bytes: words * 4 on ESP32.
static constexpr uint16_t  RENZFI_ROUTER_WORKER_STACK_WORDS = 12288;  // 48 KB
// Voucher generate/persist worker — long SD transactions must not run on
// loopTask (TWDT). Stack sized for generate() call frames; JSON docs are heap.
static constexpr uint16_t  RENZFI_VOUCHER_WORKER_STACK_WORDS = 8192;  // 32 KB
static constexpr int32_t   VOUCHER_WORKER_CORE_AFFINITY = 0;  // keep CPU1 free
// This is the *overall* per-job deadline enforced by
// RouterApiTransportGate::jobExpired() — a second, independent gate that
// readByte()/readWord() consult before their own per-sentence io timeout.
// It must comfortably exceed connect + login + every RouterOS command a
// job can issue, or a slow-but-legitimate login reply gets cut off by this
// job-level gate instead of the (correct) sentence-level one, even after
// SETUP_ROUTER_IO_TIMEOUT_MS above is raised. Timing budget for one
// Existing Network Scan job (connect, login, then up to 7 sequential
// read-only RouterOS print commands + JSON serialization):
//
//   TCP connect:                 <= 5000 ms  (SETUP_ROUTER_CONNECT_TIMEOUT_MS)
//   RouterOS login:               <= 8000 ms  (SETUP_ROUTER_IO_TIMEOUT_MS worst case)
//   First inspection command:     <= 3000 ms  (subsequent print commands are
//                                              read-only list queries and are
//                                              typically much faster than
//                                              login on real hardware, since
//                                              login also does credential
//                                              verification/hashing)
//   JSON serialization:           <= 1000 ms
//   Safety margin:                   3000 ms
//   ------------------------------------------
//   TOTAL:                          20000 ms
//
// Previously 6000ms — no headroom once the login io timeout was corrected
// from 2000ms to 8000ms above.
static constexpr uint32_t  ROUTER_WORKER_JOB_TIMEOUT_MS = 20000;
static constexpr uint32_t  ROUTER_WORKER_JOB_TTL_MS     = 120000;
static constexpr uint8_t   ROUTER_WORKER_QUEUE_DEPTH      = 1;
static constexpr uint32_t  ROUTER_API_MIN_CONNECT_INTERVAL_MS = 5000;
static constexpr uint32_t  ROUTER_API_BACKOFF_INITIAL_MS      = 10000;
static constexpr uint32_t  ROUTER_API_BACKOFF_MAX_MS          = 60000;

// RouterOS health FSM (stability remediation 2026-08-15). Non-blocking;
// owned by RouterApiTransportGate. Does NOT poll RouterOS while idle.
static constexpr uint8_t   ROUTER_HEALTH_FAILS_TO_UNAVAILABLE = 2;
static constexpr uint32_t  ROUTER_HEALTH_RECOVERY_DWELL_MS    = 15000;
static constexpr uint32_t  ROUTER_HEALTH_PROBE_MIN_INTERVAL_MS = 15000;
// After successful HotSpot activate, skip VerifyActive for this window.
static constexpr uint32_t  ROUTER_ACTIVATE_TRUST_WINDOW_MS    = 120000;

// Wi-Fi discovery — tighter per-operation ceilings (see listNetworks instrumentation).
static constexpr uint32_t  ROUTER_WIFI_DISCOVERY_CONNECT_MS = 3000;
static constexpr uint32_t  ROUTER_WIFI_DISCOVERY_CMD_MS       = 5000;
static constexpr uint32_t  ROUTER_WIFI_OPTIONAL_CMD_MS      = 2000;

// ── Wi-Fi discovery cache / rate limiting (setup wizard Step 3 safety) ─────────
// Reopening the setup wizard (or refreshing/backgrounding the browser) must
// never re-trigger a live MikroTik discovery more often than this. A cache
// hit costs zero RouterOS commands. See WifiDiscoveryCache.
static constexpr uint32_t  WIFI_DISCOVERY_CACHE_TTL_MS      = 30000;
// Even on a cache miss, at most one real discovery attempt may start within
// this window; concurrent/rapid callers receive the last known result
// (fresh or stale) instead of touching MikroTik again.
static constexpr uint32_t  WIFI_DISCOVERY_MIN_INTERVAL_MS   = 5000;

// ── MikroTik CPU protection (RouterApiTransportGate command pacing) ───────────
// Minimum spacing enforced between any two RouterOS commands issued by this
// firmware, regardless of caller. Adaptive 5-tier table: widens as the last
// observed /system/resource/print cpu-load sample (only ever read
// opportunistically, never polled for this purpose) climbs. Above the pause
// threshold, Low-priority (optional/background inventory) jobs stop issuing
// commands entirely and simply wait — see RouterApiTransportGate::RouterJobPriority.
// Setup-essential discovery (list-wifi-networks, existing-network-scan) runs at
// Normal: paced by tier delays under load, but never paused into job timeout.
static constexpr uint8_t   ROUTER_CPU_TIER1_MAX_PERCENT      = 30;   // <30%  -> tier1 delay
static constexpr uint8_t   ROUTER_CPU_TIER2_MAX_PERCENT      = 50;   // 30-50 -> tier2 delay
static constexpr uint8_t   ROUTER_CPU_TIER3_MAX_PERCENT      = 70;   // 50-70 -> tier3 delay
static constexpr uint8_t   ROUTER_CPU_TIER4_MAX_PERCENT      = 85;   // 70-85 -> tier4 delay
// >= ROUTER_CPU_TIER4_MAX_PERCENT (>85%): tier5 delay for Critical/Normal
// jobs (includes Setup wireless/SSID discovery). Low-priority optional
// inventory may pause (no command sent) until CPU recovers or the job's
// own deadline expires.
static constexpr uint32_t  ROUTER_CMD_DELAY_TIER1_MS         = 100;
static constexpr uint32_t  ROUTER_CMD_DELAY_TIER2_MS         = 200;
static constexpr uint32_t  ROUTER_CMD_DELAY_TIER3_MS         = 350;
static constexpr uint32_t  ROUTER_CMD_DELAY_TIER4_MS         = 500;
static constexpr uint32_t  ROUTER_CMD_DELAY_TIER5_MS         = 500;
// Retained for any legacy call sites/log messages that still refer to a
// single "safe" threshold (Task 5/14 CPU-pressure gates elsewhere in the
// codebase, e.g. RouterWirelessAdapter::listNetworks()).
static constexpr uint8_t   ROUTER_CPU_SAFE_THRESHOLD_PERCENT = ROUTER_CPU_TIER3_MAX_PERCENT;
static constexpr uint8_t   ROUTER_CPU_PAUSE_THRESHOLD_PERCENT = ROUTER_CPU_TIER4_MAX_PERCENT;
// A CPU-load sample older than this is considered unknown (treated as safe,
// not as busy) so a single stale high reading can't permanently throttle.
static constexpr uint32_t  ROUTER_CPU_SAMPLE_MAX_AGE_MS      = 60000;

#if RENZFI_BURN_IN_DIAG
// Overnight stability sampling — loopTask only; no RouterOS login on this cadence.
static constexpr uint32_t  BURN_IN_DIAG_INTERVAL_MS           = 8000;
static constexpr uint32_t  BURN_IN_ROUTER_PROBE_INTERVAL_MS   = 60000;
#endif

// Router cache — dashboard reads local metadata; stale after this many hours.
static constexpr uint32_t  ROUTER_CACHE_STALE_THRESHOLD_HOURS = 24;
// Existing-network scan decisions expire after this window; adoption requires a fresh scan.
static constexpr uint16_t  EXISTING_NETWORK_SCAN_DECISION_TTL_MINUTES = 10;
static constexpr uint16_t  SETUP_ROUTER_CONNECTION_TEST_TTL_MINUTES   = 10;

#ifndef RENZFI_ROUTER_TCP_DIAGNOSTIC
#define RENZFI_ROUTER_TCP_DIAGNOSTIC 0
#endif
#ifndef RENZFI_ROUTER_API_PROTOCOL_DIAGNOSTIC
#define RENZFI_ROUTER_API_PROTOCOL_DIAGNOSTIC 0
#endif
#ifndef RENZFI_BURN_IN_DIAG
#define RENZFI_BURN_IN_DIAG 0
#endif
// Safe debug mode: logs RouterOS API sentence word counts and encoded wire
// lengths only (never word content), so it can never leak credentials.
// Disabled by default to keep production serial output quiet.
#ifndef RENZFI_ROUTER_API_LOG_SENTENCE_WORDS
#define RENZFI_ROUTER_API_LOG_SENTENCE_WORDS 0
#endif
// When 0, RouterOS scan/login paths emit milestone logs only (LOGIN SUCCESS,
// SCAN COMPLETE, ADOPTION COMPLETE, FAILURE). When 1, full diagnostics.
#ifndef RENZFI_VERBOSE_ROUTER_API
#define RENZFI_VERBOSE_ROUTER_API 0
#endif
// Task affinity for the RouterOS worker task. -1 = tskNO_AFFINITY (let the
// FreeRTOS scheduler place the task on whichever core is free). lwIP's tiT
// task and WiFi are hard-pinned to CPU0 on this chip, and AsyncTCP is pinned
// to CPU1 (see platformio.ini CONFIG_ASYNC_TCP_RUNNING_CORE). Forcibly
// pinning this worker to either core makes it contend with one of those
// fixed-affinity tasks for the entire duration of a blocking RouterOS I/O
// call. Leaving it unpinned lets the scheduler balance it against whichever
// core is momentarily less busy. Override only after physical soak testing
// proves a specific pinned core is safe and beneficial.
static constexpr int32_t   ROUTER_WORKER_CORE_AFFINITY = -1;
// Hard ceiling on a single RouterOS API "word" payload. RouterOS login/trap
// replies are always tiny (well under 1 KB); this bound exists purely to
// reject a desynchronized/corrupt byte stream before it can request a
// multi-hundred-KB String allocation.
static constexpr size_t    ROUTER_API_MAX_WORD_LEN     = 4096;

// Compile-time debug-only transport diagnostics (disabled in production).
#if RENZFI_ROUTER_TCP_DIAGNOSTIC
static constexpr uint8_t   ROUTER_TCP_DIAG_ITERATIONS  = 20;
#endif

// ── Hardware pins ─────────────────────────────────────────────────────────────
//  W5500 uses a dedicated SPI3_HOST bus — see W5500Config.h.
//  SD card uses a separate FSPI bus (pins below).
//  Board pin maps are selected by RENZFI_BOARD_WAVESHARE_ESP32_S3_ETH.
static constexpr int PIN_SD_SCK  = 7;
static constexpr int PIN_SD_MISO = 5;
static constexpr int PIN_SD_MOSI = 6;
#if defined(RENZFI_BOARD_WAVESHARE_ESP32_S3_ETH)
// Waveshare: onboard TF CS is GPIO4 (coin must move off GPIO4).
static constexpr int PIN_SD_CS   = 4;
static constexpr int PIN_COIN    = 18;
#else
// Freenove: external SD CS GPIO18; coin pulse on GPIO4.
static constexpr int PIN_SD_CS   = 18;
static constexpr int PIN_COIN    = 4;
#endif
// SD library runtime SPI frequency (card init uses 400 kHz internally).
static constexpr uint32_t SD_SPI_FREQ_HZ = 1000000;

// GPIO 35–37 are OPI PSRAM data lines — do not use.
// Stage 1 RGB: keep discrete common-negative LEDs on 38/39/40 (WS2812 deferred).
static constexpr int PIN_RGB_LED_RED   = 38;
static constexpr int PIN_RGB_LED_GREEN = 39;
static constexpr int PIN_RGB_LED_BLUE  = 40;
// Recovery button (header or dedicated PCB switch): active LOW, INPUT_PULLUP.
static constexpr int PIN_RECOVERY = 2;

// ── Recovery timing ───────────────────────────────────────────────────────────
static constexpr uint32_t RECOVERY_BOOT_WINDOW_MS   = 5000;
static constexpr uint32_t RECOVERY_LEVEL1_HOLD_MS   = 10000;
static constexpr uint32_t RECOVERY_LEVEL2_HOLD_MS   = 20000;
static constexpr uint32_t RECOVERY_RGB_FLASH_MS     = 200;
static constexpr uint8_t  RECOVERY_RGB_FLASH_COUNT  = 6;

// ── NVS namespaces (auth + network — no second settings system) ───────────────
static constexpr const char *NVS_AUTH_NS     = "renz-auth";
static constexpr const char *NVS_NETWORK_NS  = "renz-network";

// ── Timing ───────────────────────────────────────────────────────────────────
// TEMPORARY calibration defaults for hardware testing without the production
// external 10k ohm pull-up on the coin signal line (PIN_COIN). These values only
// mitigate noisy/floating-input symptoms in software; they are not a
// substitute for the external pull-up, which remains required for production
// reliability. Revert toward the original values once the resistor is fitted.
static constexpr uint32_t COIN_DEBOUNCE_MS       = 100;  // TEMP: was 35
static constexpr uint32_t COIN_SETTLE_MS         = 200;  // TEMP: was 450
// TEMP: ignore ISR edges for this long right after a pulse group is
// finalized, so electrical ringing cannot start a phantom second group.
static constexpr uint32_t COIN_POST_GROUP_GUARD_MS = 250;
// Physical safety ceiling: finalized groups above this are rejected immediately.
// Valid denominations are resolved separately via the pulse→PHP mapping table.
static constexpr uint32_t COIN_MAX_PULSES_PER_GROUP = 20;
static constexpr uint32_t COIN_INSERT_TIMEOUT_SEC = 60;
static constexpr uint32_t COIN_NO_ACTIVITY_TIMEOUT_SEC = 5UL * 60UL;
static constexpr uint32_t SESSION_TTL_SECONDS  = 8UL * 60UL * 60UL;
static constexpr uint32_t SSE_HEARTBEAT_MS     = 15000;
static constexpr uint32_t CLEANUP_INTERVAL_MS  = 30000;
static constexpr uint32_t LED_TICK_MS          = 150;
static constexpr uint32_t RGB_LED_ACCEPTED_MS  = 2000;
static constexpr uint32_t RGB_BOOT_YELLOW_MS   = 3000;

// ── JSON document sizes ───────────────────────────────────────────────────────
static constexpr size_t JSON_DOC_SMALL  = 2048;
static constexpr size_t JSON_DOC_MEDIUM = 8192;
static constexpr size_t JSON_DOC_LARGE  = 24576;

// Short-lived server-side cache for /api/sales/chart/* (avoids SD re-read on
// rapid dashboard navigation). Invalidated on new sales or TTL expiry.
static constexpr uint32_t SALES_CHART_CACHE_MS = 4000;

// ── Auth / session ────────────────────────────────────────────────────────────
static constexpr const char *DEFAULT_ADMIN_PASSWORD = "admin";
static constexpr const char *SESSION_COOKIE         = "renz_session";
// ESP32 NVS key names must be <= 15 characters.
static constexpr const char *NVS_KEY_OP_USER        = "op_user";
static constexpr const char *NVS_KEY_OP_HASH        = "op_hash";
static constexpr const char *NVS_KEY_OP_PERMS       = "op_perms";

// ── File paths (aliases — canonical definitions in StoragePaths.h) ───────────
static constexpr const char *WWW_ROOT              = StoragePaths::LegacyWww;
static constexpr const char *SETTINGS_FILE         = StoragePaths::SettingsFile;
static constexpr const char *PROMOS_FILE           = StoragePaths::PromosFile;
static constexpr const char *ROUTER_FILE           = StoragePaths::RouterFile;
static constexpr const char *WIFI_CONFIG_FILE      = StoragePaths::WifiConfigFile;
static constexpr const char *VOUCHERS_FILE         = StoragePaths::VouchersFile;
static constexpr const char *SALES_FILE            = StoragePaths::SalesFile;
static constexpr const char *LOGS_FILE             = StoragePaths::LogsFile;
static constexpr uint32_t LOGS_QUOTA_KB            = 256;  // nominal SD log budget for status UI
static constexpr const char *USERS_FILE            = StoragePaths::UsersFile;
static constexpr const char *ADMIN_SESSIONS_FILE   = StoragePaths::AdminSessionsFile;
static constexpr const char *PORTAL_SESSIONS_FILE  = StoragePaths::PortalSessionsFile;
static constexpr const char *PORTAL_CONFIG_FILE    = StoragePaths::PortalConfigFile;
static constexpr const char *INSTALLATION_FILE     = StoragePaths::InstallationFile;

// Captive portal branding assets (SD primary, SPIFFS fallback)
static constexpr const char *PORTAL_BANNER_SD      = StoragePaths::LegacyPortalBanner;
static constexpr const char *PORTAL_MUSIC_SD       = StoragePaths::LegacyPortalMusic;
static constexpr const char *PORTAL_BANNER_SPIFFS  = StoragePaths::Spiffs::PortalCustomBanner;
static constexpr const char *PORTAL_MUSIC_SPIFFS   = StoragePaths::Spiffs::PortalCustomMusic;
static constexpr const char *PORTAL_BANNER_DEFAULT = StoragePaths::Spiffs::PortalDefaultBanner;
static constexpr const char *PORTAL_MUSIC_DEFAULT  = StoragePaths::Spiffs::PortalDefaultMusic;
static constexpr size_t      PORTAL_BANNER_MAX_BYTES = 2U * 1024U * 1024U;  // 2 MiB
static constexpr size_t      PORTAL_MUSIC_MAX_BYTES  = 2U * 1024U * 1024U;  // 2 MiB
static constexpr size_t      LOG_RAM_BUFFER_SIZE    = 500;

// ── Portal session timing ──────────────────────────────────────────────────────
static constexpr uint32_t COIN_WINDOW_SECS       = 60;   // insert-coin window length
static constexpr uint32_t PORTAL_SAVE_INTERVAL_MS = 30000; // periodic SD persist for timers
static constexpr uint32_t PORTAL_IDLE_TTL_SEC       = 900;   // 15 min — drop idle portal records
static constexpr uint32_t PORTAL_HEARTBEAT_STALE_SEC = 120;  // drop active rows without heartbeat

// ── NVS namespace ─────────────────────────────────────────────────────────────
static constexpr const char *NVS_WIFI_NS = "renzfi_wifi";  // kept for legacy NVS reads

// ── SPIFFS fallback storage (SD unavailable) ─────────────────────────────────
static constexpr const char *FB_MANIFEST        = StoragePaths::Spiffs::FbManifest;
static constexpr const char *FB_SETTINGS        = StoragePaths::Spiffs::FbSettings;
static constexpr const char *FB_PROMOS          = StoragePaths::Spiffs::FbPromos;
static constexpr const char *FB_ROUTER          = StoragePaths::Spiffs::FbRouter;
static constexpr const char *FB_VOUCHERS        = StoragePaths::Spiffs::FbVouchers;
// Short path: SPIFFS max object name is 32 bytes; ".tmp" atomic writes need headroom.
static constexpr const char *FB_PORTAL_SESSIONS = StoragePaths::Spiffs::FbPortalSessions;
static constexpr const char *FB_SALES           = StoragePaths::Spiffs::FbSales;
static constexpr const char *FB_PORTAL_CONFIG   = StoragePaths::Spiffs::FbPortalConfig;
static constexpr const char *FB_INSTALLATION       = StoragePaths::Spiffs::FbInstallation;
static constexpr const char *FB_PROVISIONING       = StoragePaths::Spiffs::FbProvisioning;
static constexpr const char *FB_ROUTER_CONNECTION   = StoragePaths::Spiffs::FbRouterConnection;
static constexpr const char *FB_ROUTER_PROVISIONING = StoragePaths::Spiffs::FbRouterProvisioning;
static constexpr const char *FB_ROUTER_CACHE        = StoragePaths::Spiffs::FbRouterCache;
static constexpr const char *FB_EXISTING_NETWORK_SCAN =
    StoragePaths::Spiffs::FbExistingNetworkScan;
static constexpr const char *FB_SETUP_WIZARD        = StoragePaths::Spiffs::FbSetupWizard;

static constexpr size_t FB_LIMIT_SETTINGS         = 8 * 1024;
static constexpr size_t FB_LIMIT_PROMOS           = 32 * 1024;
static constexpr size_t FB_LIMIT_ROUTER           = 8 * 1024;
static constexpr size_t FB_LIMIT_VOUCHERS         = 64 * 1024;
static constexpr size_t FB_LIMIT_PORTAL_SESSIONS  = 128 * 1024;
static constexpr size_t FB_LIMIT_SALES            = 128 * 1024;
static constexpr size_t FB_LIMIT_PORTAL_CONFIG    = 1024;
static constexpr size_t FB_LIMIT_INSTALLATION       = 4 * 1024;
static constexpr size_t FB_LIMIT_PROVISIONING       = 4 * 1024;
static constexpr size_t FB_LIMIT_ROUTER_CONNECTION   = 8 * 1024;
static constexpr size_t FB_LIMIT_ROUTER_PROVISIONING = 8 * 1024;
static constexpr size_t FB_LIMIT_ROUTER_CACHE        = 8 * 1024;
static constexpr size_t FB_LIMIT_EXISTING_NETWORK_SCAN = 24 * 1024;
static constexpr size_t FB_LIMIT_SETUP_WIZARD        = 4 * 1024;
static constexpr size_t FB_LIMIT_MANIFEST            = 8 * 1024;

static constexpr size_t FB_SOFT_LIMIT_BYTES    = 256 * 1024;
static constexpr size_t FB_HARD_LIMIT_BYTES    = 320 * 1024;
static constexpr size_t SPIFFS_MIN_FREE_BYTES   = 128 * 1024;

static constexpr uint32_t FB_WRITE_MIN_INTERVAL_MS = 30000;
static constexpr uint32_t STORAGE_HEALTH_POLL_MS   = 60000;
// After initial remount retries fail, continue low-power presence checks.
static constexpr uint32_t STORAGE_WATCH_POLL_MS    = 300000;  // 5 minutes
static constexpr uint32_t STORAGE_LOCK_TIMEOUT_MS  = 5000;
// refreshRuntimeSnapshot() is called from loopTask; throttle heavy SPIFFS walks
// to avoid starving async_tcp on shared CPU1.
static constexpr uint32_t STORAGE_SNAPSHOT_HEAVY_INTERVAL_MS = 30000;
static constexpr uint8_t SD_RECOVERY_MAX_ATTEMPTS  = 3;
static constexpr uint8_t STORAGE_CONFLICT_CAP      = 4;
static constexpr uint8_t STORAGE_REPLAY_FILE_CAP   = 8;
// The identical payload is attempted at most twice before bounded fallback.
static constexpr uint8_t STORAGE_WRITE_ATTEMPTS    = 2;

}  // namespace RenzFiConfig
