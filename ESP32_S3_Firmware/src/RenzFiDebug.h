#pragma once

// ── Renz-Fi compile-time diagnostic flags ────────────────────────────────────
// Override any flag via PlatformIO build_flags, e.g. -DRENZFI_DISABLE_SD_BOOT=1
//
// NOTE: every flag below MUST be spelled exactly "RENZFI_..." (no other
// prefix variant). Earlier revisions of this header accidentally guarded one
// spelling ("RENFZFI_...") while #define-ing a different one
// ("RENZFZFI_..."), which silently made every flag a no-op regardless of
// build_flags. Keep the guard and the #define identifier identical.
//
// Debug logging flags (production defaults all 0):
//   RENZFI_DEBUG_BOOT             — verbose boot-sequence logging
//   RENZFI_DEBUG_SPIFFS           — verbose SPIFFS inventory logging
//   RENZFI_DEBUG_STORAGE          — verbose StorageManager logging
//   RENZFI_DEBUG_ROUTER           — verbose router-driver logging
//   RENZFI_DEBUG_HTTP             — verbose per-request HTTP logging
//   RENZFI_DEBUG_PORTAL           — verbose portal/asset logging
//   RENZFI_NETWORK_DIAG           — exhaustive interface/HTTP/ping diagnostics
//   RENZFI_BURN_IN_DIAG           — periodic heap/task HWM burn-in logging (debug only)
//
// Network-lifecycle bisect flags (production defaults all 0):
//   RENZFI_DISABLE_MGMT_AP_BOOT   — skip ManagementApLifecycle boot policy
//   RENZFI_DISABLE_SD_BOOT        — skip StorageManager::begin() at boot
//   RENZFI_DISABLE_EVENTBUS_SSE   — skip /api/events SSE route registration
//
// AsyncTCP/Management-AP watchdog isolation flag (production default 0):
//   RENZFI_SAFE_AP_HTTP_TEST      — register ONLY GET /healthz and GET /admin
//                                   (tiny inline HTML). No EventBus/SSE,
//                                   AssetServer, PortalServer, StaticFileServer,
//                                   AdminServer SPA assets, captive-portal probe
//                                   routes, or API routes are registered.
//                                   W5500 Ethernet, DHCP, Management AP, and
//                                   AsyncWebServer itself remain fully active.
//                                   Used to isolate AsyncTCP/lwIP coexistence
//                                   issues from application route handlers.
//
// Suggested variants:
//   Production  — all 0 (default below)
//   Installer   — DEBUG_BOOT=1, DEBUG_SPIFFS=1, DEBUG_PORTAL=1
//   Developer   — DEBUG_*=1, RENZFI_BURN_IN_DIAG=1 for overnight stability runs
//   Bisect      — enable one DISABLE_* flag (or SAFE_AP_HTTP_TEST) at a time

#if !defined(RENZFI_DEBUG_BOOT)
#define RENZFI_DEBUG_BOOT 0
#endif

#if !defined(RENZFI_DEBUG_SPIFFS)
#define RENZFI_DEBUG_SPIFFS 0
#endif

#if !defined(RENZFI_DEBUG_STORAGE)
#define RENZFI_DEBUG_STORAGE 0
#endif

#if !defined(RENZFI_DEBUG_ROUTER)
#define RENZFI_DEBUG_ROUTER 0
#endif

#if !defined(RENZFI_DEBUG_HTTP)
#define RENZFI_DEBUG_HTTP 0
#endif

#if !defined(RENZFI_DEBUG_PORTAL)
#define RENZFI_DEBUG_PORTAL 0
#endif

#if !defined(RENZFI_NETWORK_DIAG)
#define RENZFI_NETWORK_DIAG 0
#endif

#if !defined(RENZFI_DEBUG_FINISH)
#define RENZFI_DEBUG_FINISH 0
#endif

#if !defined(RENZFI_BURN_IN_DIAG)
#define RENZFI_BURN_IN_DIAG 0
#endif

#if !defined(RENZFI_DISABLE_MGMT_AP_BOOT)
#define RENZFI_DISABLE_MGMT_AP_BOOT 0
#endif

#if !defined(RENZFI_DISABLE_SD_BOOT)
#define RENZFI_DISABLE_SD_BOOT 0
#endif

#if !defined(RENZFI_DISABLE_EVENTBUS_SSE)
#define RENZFI_DISABLE_EVENTBUS_SSE 0
#endif

#if !defined(RENZFI_SAFE_AP_HTTP_TEST)
#define RENZFI_SAFE_AP_HTTP_TEST 0
#endif

// Warn in the serial log whenever a request handler's synchronous work
// (route lambda entry to req->send()/beginResponse()) exceeds this many
// milliseconds. This is a diagnostic threshold only — it does not abort or
// alter the request in any way.
#if !defined(RENZFI_SLOW_HANDLER_WARN_MS)
#define RENZFI_SLOW_HANDLER_WARN_MS 100
#endif
