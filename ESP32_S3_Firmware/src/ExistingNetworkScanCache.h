#pragma once

#include <Arduino.h>

// Server-side cache for the Setup Wizard "Compatible Network" scan result
// (POST /api/setup/router/existing-network/scan).
//
// Why this exists: RouterOS inspection for this step (bridges, DHCP, pool,
// firewall, hotspot) must happen ONLY ONCE per Save. Reopening/reloading the
// wizard, or navigating Back/Next across steps, must reuse the cached result
// instead of re-running the full inspection against MikroTik. A new live
// scan is only performed when the cache is empty, the user explicitly
// presses "Rescan", or the router connection is re-saved (credentials
// changed), which calls clear() below.
//
// Thread-safety: guarded by an internal mutex so it can be read from the
// AsyncTCP/HTTP task and written from the router_worker task without any
// caller-side locking.
namespace ExistingNetworkScanCache {

// Persists the outcome of a real scan run (success or structured failure).
void store(int httpStatus, const String &body);

// True and fills the outputs whenever a cached result exists.
bool getAny(int &httpStatusOut, String &bodyOut, uint32_t &ageMsOut);

// True once a scan has ever completed (success or failure).
bool hasAny();

// Invalidates the cached result. Must be called whenever the router
// connection is re-saved (credentials may have changed) or the user
// explicitly requests a Rescan.
void clear();

}  // namespace ExistingNetworkScanCache
