#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

class EthernetManager;
class RouterPlatform;
class StorageManager;

namespace DeviceIdentity {

String formatDeviceId(const String &macAddress);
String readFriendlyName(StorageManager *storage);

// Stable per-chip identifier that does not depend on the W5500/Ethernet
// driver being present or successfully initialized. Reads the ESP32's
// factory-programmed base MAC directly from eFuse (no WiFi/ETH radio is
// started). Used as the deviceId/AP-naming source whenever Ethernet init
// fails, so a unit with no cable/faulty W5500 still gets a stable, unique
// identity instead of a generic zeroed-out fallback.
String stableChipMacAddress();

// Random token generated once, lazily, on first call and held for the
// lifetime of this boot (never persisted, never derived from anything
// that survives a reboot). Exposed via /api/setup/status so setup-wizard
// frontends can positively confirm whether the device actually restarted
// between two points in time, instead of inferring a restart from a single
// ambiguous signal (a failed poll, a missing job id, a slow mobile
// reconnect, etc.). Changes on every boot; identical for the entire
// lifetime of a given boot.
String bootInstanceId();

/** Loop / lifecycle: refresh cached profile fields (may read storage). */
void refreshRuntimeProfile(EthernetManager *eth, StorageManager *storage,
                           RouterPlatform *router, bool coinEnabled);

/** Mark profile cache stale after settings change (HTTP sets flag only). */
void invalidateRuntimeProfile();

/** Health snapshot: serialize cached profile only — no I/O. */
void fillRuntimeProfile(JsonObject out);

void fillCapabilities(JsonObject caps, RouterPlatform *router, bool coinEnabled);
void fillProfile(JsonObject out, EthernetManager *eth, StorageManager *storage,
                 RouterPlatform *router, bool coinEnabled);

}  // namespace DeviceIdentity
