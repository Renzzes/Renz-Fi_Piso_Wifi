#pragma once



#include <ArduinoJson.h>



#include "RouterOsClient.h"



class StorageManager;



namespace RouterWireless {



static constexpr const char *kModeExisting = "existing";

static constexpr const char *kModeNew      = "new";

static constexpr const char *kModeExternalAp = "external_ap";



static constexpr const char *kOpenSecurityProfileName = "RenzFi-Open";

static constexpr const char *kManagedWifiIfaceName    = "renzfi-wifi";

static constexpr const char *kManagedWifiConfigName    = "renzfi-setup";



struct WifiSelection {

  String mode;          // existing | new

  String interfaceId;   // RouterOS interface name (internal only)

  String ssid;

  String password;

};



/** Canonical wireless record in RouterProvisioningFile (single source of truth).

 *

 * Today these fields are stored at the provisioning document root. A future

 * schema version should nest them under a single "wireless" object, e.g.

 * { "wireless": { "configured", "mode", "interfaceId", "ssid", "password", ... } }

 * so band/channel/security extensions do not pollute the root document.

 */

struct CanonicalConfig {

  bool   configured = false;

  String mode;

  String interfaceId;

  String ssid;

  String password;

};



struct ListNetworksResult {

  bool   ok = false;

  String code;

  String driver;

  String message;

  String error;

  uint8_t interfaceCount  = 0;

  uint8_t configuredCount = 0;

  uint8_t disabledCount   = 0;

};



bool loadCanonicalConfig(StorageManager *storage, CanonicalConfig &out);

bool saveCanonicalFields(StorageManager *storage, const CanonicalConfig &cfg);



bool listNetworks(RouterOsClient &client, JsonArray out, ListNetworksResult &result);



bool readInterface(RouterOsClient &client, const String &interfaceId, JsonObject out,
                   String &errorOut);

bool updateInterface(RouterOsClient &client, const String &interfaceId,
                     const String &ssid, const String &password, String &errorOut);

/** Admin SSID-only save: targeted pre-read → set → ≤1 light verify (no inventory). */
bool applySsidOnly(RouterOsClient &client, const String &interfaceId,
                   const String &ssid, JsonObject out, String &errorOut);

/** Idempotent Hotspot captive-path repair for bridged production topology.
 *  Ensures Hotspot is bound to the guest bridge (not wireless slave alone),
 *  enables a disabled server, and sets profile html-directory=hotspot when wrong.
 *  Intended for provisioning and explicit Sync — never continuous polling.
 */
bool reconcileCaptiveHotspotPath(RouterOsClient &client,
                                 const String &wirelessIface,
                                 const String &bridgeName,
                                 JsonObject reportOut, String &errorOut);

bool applyWifiSelection(RouterOsClient &client, const WifiSelection &selection,
                        const String &bridgeName, String &errorOut,
                        String &stageOut);



bool parseWifiSelection(JsonObjectConst body, WifiSelection &out, String &errorOut);



void fillWirelessApiJson(const CanonicalConfig &canonical, JsonObject out);



/** Human-readable band label, e.g. "2.4GHz" or "5GHz". */
String formatBandLabel(const String &raw);

/** Derive display band from RouterOS frequency (MHz). Empty if outside known ranges. */
String formatBandFromFrequencyMhz(long freqMhz);

}  // namespace RouterWireless
