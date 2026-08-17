#pragma once

#include <Arduino.h>

// Server-side cache + rate limiter for the Setup Wizard Step 3 Wi-Fi
// discovery result (GET /api/setup/router/wifi/networks).
//
// Why this exists: reopening/reloading/backgrounding the Setup Wizard while
// on Step 3 must NEVER re-trigger a live MikroTik RouterOS discovery more
// than once every WIFI_DISCOVERY_MIN_INTERVAL_MS, and a fresh cache hit
// (age < WIFI_DISCOVERY_CACHE_TTL_MS) must serve instantly without touching
// MikroTik at all. This is what turns "open Step 3 100 times" into "one
// real RouterOS discovery, ~99 cache hits" instead of 100 RouterOS sessions.
//
// Thread-safety: all state is guarded by an internal mutex so it can be read
// from the AsyncTCP/HTTP task and written from the router_worker task
// without any caller-side locking.
namespace WifiDiscoveryCache {

// Persists the outcome of a real discovery run (success or structured
// failure — both are cacheable; a failure still means "we just asked
// MikroTik and this is the answer", so repeating it within the TTL would
// only add load without changing the outcome).
void store(int httpStatus, const String &body);

// True and fills the outputs when a cached result exists and its age is
// below maxAgeMs. Never touches MikroTik.
bool getFresh(uint32_t maxAgeMs, int &httpStatusOut, String &bodyOut);

// True and fills the outputs whenever *any* cached result exists,
// regardless of age (used as a fallback when the worker is busy or the
// caller is rate-limited and a stale-but-real answer beats a hard error).
bool getAny(int &httpStatusOut, String &bodyOut, uint32_t &ageMsOut);

// Rate limiter for *starting a new real discovery attempt*. Returns true
// (and records "attempt started now") only if at least minIntervalMs has
// elapsed since the previous attempt start; otherwise returns false and
// the caller must fall back to getAny()/a busy response instead of
// touching MikroTik.
bool tryBeginAttempt(uint32_t minIntervalMs);

// True once any discovery has ever completed (success or failure).
bool hasAny();

}  // namespace WifiDiscoveryCache
