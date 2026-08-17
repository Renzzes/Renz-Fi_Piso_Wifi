# Phase 8 — Factory Setup, DHCP-First Network Onboarding & Mobile Discovery

> **Status:** Implemented
> **Scope:** ESP32-S3 + W5500 firmware boot robustness, DHCP-first Ethernet, fixed-SSID
> Management AP with captive-portal detection, extended network API contract, and
> Android factory-setup discovery UX.
> **Supersedes:** SSID/IP details in [MANAGEMENT_AP_ARCHITECTURE.md](./MANAGEMENT_AP_ARCHITECTURE.md)
> (architecture itself — dedicated AP, no NAT, always-on — is unchanged).

---

## 1. Problem statement

Before this change:

- `EthernetManager::begin()` always applied the compile-time static IP
  (`W5500Config.h`, `10.40.0.2`) — there was no DHCP path at all.
- `FirmwareApp::begin()` returned early (halting boot) if `ETH.begin()`
  failed, and `FirmwareApp::loop()` short-circuited on the same flag. A unit
  with no cable, a dead W5500, or a wiring fault would **never** bring up the
  Management AP or web server — it was completely unreachable.
- The Management AP SSID was `RenzFi-Setup-<deviceId>` (prefix + MAC-derived
  suffix), which leaks the device MAC over the air and requires exact-prefix
  matching on the mobile app.
- There was no captive-portal DNS or OS-detection route support on the
  Management AP, so phones/laptops did not automatically prompt "Sign in to
  network" after joining.
- `/` on the Management AP served the customer captive-portal login page
  (`PortalServer` → `login.html`) instead of the admin/setup SPA — installers
  landed on a screen meant for paying customers.
- `/api/system/wifi/config` was a read-only stub describing the compile-time
  static config; there was no way to actually save DHCP/static preference.
- The Android app's factory-detected branch went straight into a blocking
  native onboarding wizard instead of the requested non-blocking prompt.

## 2. Design summary

- **DHCP is now the default and recommended mode** for every appliance,
  factory or already-deployed. Static IP is an explicit, validated,
  opt-in fallback persisted via `NetworkSettingsManager`.
- **Ethernet failure is never fatal.** `EthernetManager::begin()` failing
  only disables Ethernet-dependent features for that boot — Management AP,
  web server, GPIO ISR service (recovery button / CoinManager), and all
  other subsystems still come up.
- **Management AP SSID is fixed:** `Renz-Fi Setup` — identical on every
  unit, no MAC/serial suffix.
- **Captive portal detection** is implemented with a wildcard DNS responder
  plus explicit routes for the OS probe URLs (Android/ChromeOS, iOS/macOS,
  Windows), scoped to Management-AP-origin requests only.
- **`/` is interface-aware:** requests arriving via the Management AP's own
  IP (192.168.4.1) are redirected to `/admin` (the setup/admin SPA);
  Ethernet-side requests are unaffected (the ESP32-hosted portal there is a
  development/recovery fallback only — production customer portal is
  MikroTik-hosted, unchanged).
- **Network settings are a real, validated, persisted model** now consumed
  by `EthernetManager`, not just inert storage.
- **Android factory detection is non-blocking:** joining `Renz-Fi Setup`
  surfaces a dismissible native prompt over the existing "Appliance Not
  Found" screen instead of forcing a wizard.

---

## 3. Files changed (this phase)

### New files

| File | Purpose |
|------|---------|
| `ESP32_S3_Firmware/src/Models.cpp` | `EthernetAddressMode` label/parse helpers |
| `ESP32_S3_Firmware/src/web/CaptivePortalDetectionServer.h/.cpp` | AP-scoped captive-portal OS-probe routes |

### Modified files (ESP32 firmware)

| File | Change |
|------|--------|
| `src/Models.h` | `NetworkSettings` extended: `addressMode`, `staticIp/Gateway/SubnetMask/DnsPrimary/DnsSecondary`, `provisioned`. New `EthernetAddressMode` enum. |
| `src/NetworkSettingsManager.h/.cpp` | Tolerant load/save (new + legacy `ip/gateway/subnet/dns` JSON keys), IPv4 + subnet-mask validation, `loadNvsOnly()` (early-boot, no SD), `validateStaticConfig()`, safe fallback-to-DHCP on any invalid/corrupt data. |
| `src/EthernetManager.h/.cpp` | `begin(const NetworkSettings&)` — DHCP by default (no `ETH.config()` call), static mode only when `provisioned && Static && valid`; never blocks on DHCP lease; `addressModeLabel()`, `isStaticMode()`, `hasIp()` exposed; failure path returns `false` without side effects. |
| `src/FirmwareApp.h/.cpp` | Boot no longer halts on ETH failure. Loads `NetworkSettingsManager::loadNvsOnly()` before Ethernet init. GPIO ISR install is now unconditional. Falls back to `DeviceIdentity::stableChipMacAddress()` for deviceId/AP naming when Ethernet is absent. `loop()` no longer early-returns on ETH failure. |
| `src/DeviceIdentity.h/.cpp` | Added `stableChipMacAddress()` — eFuse-based per-chip MAC, independent of Ethernet/WiFi driver state. |
| `src/ManagementApConfig.h` | `SSID_PREFIX` → fixed `SSID = "Renz-Fi Setup"`. |
| `src/ManagementApManager.h/.cpp` | `buildSsid()` returns the fixed SSID (no MAC suffix). Added wildcard `DNSServer` (start on AP start, stop on AP stop, polled in `loop()`). |
| `src/web/PortalServer.h/.cpp` | `isManagementApRequest()` — redirects `/` and `/portal` to `/admin` when the request's local endpoint IP is the Management AP IP. |
| `src/web/WebServerManager.cpp` | Registers the new `CaptivePortalDetectionServer` provider. |
| `src/NetworkStatusModel.cpp` | `ethernet` block gains `driverReady`, `hasIp`, `mode` (`dhcp`/`static`). |
| `src/ApiServer.h/.cpp` | New `NetworkSettingsManager*` dependency. `/api/health` gains a non-secret `ethernet` block + top-level `installationState`. `/api/system/wifi/config` GET returns real persisted settings + live driver state + a MikroTik RouterOS DHCP-reservation example using the live MAC; POST/PUT validate and persist real settings (`rebootRequired: true` in the response — see §7). |
| `ESP32_S3_Firmware/platformio.ini` | `w5500_minimal` env additionally builds `Models.cpp` (needed by `EthernetManager.cpp`'s address-mode label helper). |
| `MANAGEMENT_AP_ARCHITECTURE.md` | SSID references updated to the fixed `Renz-Fi Setup` name; note pointing to this document for the Phase 8 delta. |

### Modified files (Android)

See §8.

---

## 4. Network settings schema

```cpp
enum class EthernetAddressMode : uint8_t { Dhcp = 0, Static = 1 };

struct NetworkSettings {
  EthernetAddressMode addressMode = EthernetAddressMode::Dhcp;
  String staticIp             = "10.40.0.2";   // static-mode only
  String staticGateway        = "10.40.0.1";
  String staticSubnetMask     = "255.255.255.0";
  String staticDnsPrimary     = "10.40.0.1";
  String staticDnsSecondary   = "";
  bool   provisioned          = false;  // true only after an explicit save
  bool   managementApKeepEnabledAfterSetup = false;  // unchanged (Phase 7C.2)
};
```

Persistence: NVS namespace `renz-network` (unchanged) mirrored to
`settings.json` → `"network"` (unchanged file/key). Legacy `ip`/`gateway`/
`subnet`/`dns` JSON keys are still read (mapped onto the new `static*`
fields) so pre-Phase-8 `settings.json` files keep working without a schema
version bump — new keys are only written going forward.

**Validation** (`NetworkSettingsManager::validateStaticConfig`, also
duplicated as a lightweight standalone check inside `EthernetManager.cpp`
for the `w5500_minimal` recovery build):

- All four required static fields (`staticIp`, `staticGateway`,
  `staticSubnetMask`, `staticDnsPrimary`) must parse as IPv4 addresses.
- `staticSubnetMask` must additionally be a contiguous-bits mask
  (e.g. `255.255.255.0` — rejects e.g. `255.0.255.0`).
- `staticDnsSecondary` is validated only if non-empty.
- Validation is a no-op (always passes) when `addressMode == Dhcp`.

**Fallback behavior:** any invalid/corrupt/partial static config — at NVS
load, at settings.json reconciliation, or at `save()` time — resolves to
DHCP rather than blocking boot or silently applying a broken static config.

---

## 5. Boot state machine

```
Phase 0  RecoveryManager.runBootCheck()          (unchanged)
Phase 1  NetworkSettingsManager::loadNvsOnly()    (NEW — NVS only, no SD)
         EthernetManager::begin(settings)
           ├─ ETH.begin() succeeds
           │    ├─ provisioned && Static && valid → ETH.config(static)
           │    └─ otherwise                      → DHCP (no ETH.config call)
           │    IP arrival (either mode) observed asynchronously in loop()
           └─ ETH.begin() fails → _ethBootOk=false, boot CONTINUES (no halt)
         GpioIsrService::ensureInstalled()         (NEW — now unconditional)
Phase 2  SPIFFS.begin()                            (unchanged)
Phase 3  StorageManager (SD)                       (unchanged)
Phase 4  Subsystems (auth, installation, promos, vouchers, router, coin, …)
         NetworkSettingsManager::begin(&storage)   (reconciles NVS ⇄ settings.json)
         ManagementApManager.begin() → AP starts   (SSID: "Renz-Fi Setup", always)
         ManagementApLifecycle.applyBootPolicy()
         startNetworkServices() if (eth ready) OR (mgmt AP running)  (unchanged condition,
                                                                       now reachable even
                                                                       when eth never came up)
loop()   _eth.loop() / _mgmtAp.loop() / _mgmtApLifecycle.loop()   — unconditional, no
         early return on ETH boot failure (REMOVED the old `if (!_ethBootOk) return;`)
```

Key guarantee: **every code path reaches `ManagementApManager.begin()` and
`startNetworkServices()`**, regardless of W5500/DHCP/Internet outcome. The
appliance never hangs or reboot-loops on a network fault.

---

## 6. Management AP & captive portal

| Setting | Value |
|---------|-------|
| SSID | `Renz-Fi Setup` (fixed, exact, no suffix) |
| AP IP / Gateway | `192.168.4.1` |
| Setup URL | `http://192.168.4.1` |
| Security | Open (unchanged) |
| DNS | Wildcard captive responder (`DNSServer`, port 53, `*` → `192.168.4.1`) — **NEW** |

Captive-portal OS-detection routes (`CaptivePortalDetectionServer`,
AP-scoped only — Ethernet-side/LAN requests to these paths get a plain 404,
identical to pre-Phase-8 behavior):

| Path | OS |
|------|-----|
| `/generate_204`, `/gen_204` | Android, ChromeOS |
| `/hotspot-detect.html`, `/library/test/success.html` | iOS, macOS |
| `/connecttest.txt`, `/ncsi.txt` | Windows |
| `/fwlink` | Windows (legacy) |

All respond `302 → http://192.168.4.1` when the request arrived via the
Management AP's own IP; this — combined with the wildcard DNS — is what
triggers each OS's native "Sign in to network" prompt.

`GET /` and `GET /portal` are interface-aware: on the Management AP they
redirect to `/admin`; on the Ethernet/LAN interface, behavior is completely
unchanged (dev/recovery fallback captive portal; production customer portal
remains MikroTik-hosted per `docs/HTTP_ROUTE_CONTRACT.md`).

---

## 7. API contract changes

All changes are **additive** — no existing field was removed or renamed at
the JSON level (NVS/JSON key renames are internal implementation details
with tolerant legacy-key fallback, see §4).

### `GET /api/health` (public, unauthenticated — unchanged auth)

New fields:

```json
{
  "installationState": "needs_setup",
  "ethernet": {
    "driverReady": true,
    "link": true,
    "hasIp": true,
    "mode": "dhcp",
    "ip": "10.40.0.2",
    "gateway": "10.40.0.1",
    "mac": "AA:BB:CC:DD:EE:FF"
  }
}
```

No router credentials or other secrets are exposed here — Ethernet
link/address state is not sensitive and mirrors what's already visible on
the physical LAN.

### `GET /api/system/network` (auth required — unchanged auth)

`interfaces.ethernet` / top-level `ethernet` gain `driverReady`, `hasIp`,
`mode` (`"dhcp"` | `"static"`).

### `GET /api/system/wifi/config` (Session-level auth — was previously a
read-only static-config stub)

```json
{
  "addressMode": "dhcp",
  "provisioned": false,
  "staticIp": "10.40.0.2",
  "staticGateway": "10.40.0.1",
  "staticSubnetMask": "255.255.255.0",
  "staticDnsPrimary": "10.40.0.1",
  "staticDnsSecondary": "",
  "current": { "mode": "dhcp", "ip": "10.40.0.2", "gateway": "10.40.0.1", "mac": "AA:BB:CC:DD:EE:FF" },
  "dhcpReservation": {
    "mac": "AA:BB:CC:DD:EE:FF",
    "routerOsExample": "/ip dhcp-server lease add mac-address=AA:BB:CC:DD:EE:FF address=<desired-ip> server=<dhcp-server-name>"
  }
}
```

### `POST` / `PUT /api/system/wifi/config` (FullAccess auth — was previously
a hardcoded `405 STATIC_CONFIG` stub)

Request body (all fields optional; omitted fields keep their current
value):

```json
{
  "addressMode": "static",
  "staticIp": "10.40.0.50",
  "staticGateway": "10.40.0.1",
  "staticSubnetMask": "255.255.255.0",
  "staticDnsPrimary": "10.40.0.1"
}
```

- Validates via `NetworkSettingsManager::save()`; returns `400
  INVALID_NETWORK_CONFIG` with a human-readable message on failure — no
  partial/invalid config is ever persisted.
- On success, sets `provisioned = true` and persists to NVS + settings.json.
- Response includes `"rebootRequired": true` — **by design, the new
  Ethernet configuration takes effect on next boot**, not live. This avoids
  fragile in-place W5500/lwIP reconfiguration and matches how virtually all
  embedded network appliances handle IP changes. The setup wizard should
  prompt the installer to reboot (existing `/api/system/reboot`-style
  endpoints, if present, are unchanged by this phase).

---

## 8. Android changes

| File | Change |
|------|--------|
| `util/Constants.kt` | New `MANAGEMENT_AP_SSID = "Renz-Fi Setup"` (exact, fixed). Old `MANAGEMENT_AP_SSID_PREFIX` kept but `@Deprecated` — no longer referenced anywhere in source. |
| `util/ManagementApNetworkUtils.kt` | `isSetupSsid()` now does an exact (trimmed, case-insensitive) match against `MANAGEMENT_AP_SSID` instead of a prefix match. `setupSsidHint()` returns the fixed name. Doc comments clarify SSID is a hint only — `MANAGEMENT_AP_IP` probing is always authoritative. |
| `ui/screens/onboarding/AddApplianceScreen.kt` | Copy updated from `RenzFi-Setup-…` to `"Renz-Fi Setup"`. |
| `ui/screens/onboarding/OnboardingConnectScreen.kt` | Copy updated from `RenzFi-Setup-XXXXXX` to the fixed SSID. |
| `viewmodel/OnboardingViewModel.kt` | `setupSsidHint` field and error copy updated to the fixed SSID. |
| `viewmodel/MainViewModel.kt` | Mode-A factory-detected branch (`NewAppliance` + `needsSetup=true`) no longer routes directly into the onboarding wizard. It now sets `StartupRoutingResult.NoDevicesApplianceNotFound` (same screen as "no appliance found") **and** `StartupDiscovery.NewAppliance`, which renders the existing non-blocking `AlertDialog` overlay — identical behavior to `scheduleBackgroundDiscovery()`. Removed the now-unreachable `NoDevicesNewAppliance` sealed variant and its dead auto-navigation branch. |
| `ui/navigation/NavGraph.kt` | Removed the dead `NoDevicesNewAppliance` auto-navigation branch. `StartupDiscovery.NewAppliance` dialog copy changed to title **"Renz-Fi setup network detected"**, confirm button **"Set Up Appliance"**, dismiss button **"Not Now"** (previously "New Renz-Fi Appliance Detected" / "Begin Setup" / "Later"). |

**Resulting factory-setup UX:** joining the `Renz-Fi Setup` SSID (whether
detected at cold-start with an empty registry, or via the existing
background poll while already using the app) always lands the user on a
normal screen first (Appliance Not Found or My Vendo) with a dismissible
native dialog offered on top — never a forced wizard. Tapping **"Set Up
Appliance"** opens the existing native onboarding flow which probes
`http://192.168.4.1`; if unreachable, the existing `WifiInstructions` error
state is shown (unchanged code path). Registered-device behavior, Admin
Login, and WebView-based `/admin` access are all untouched.

---

## 9. Backward compatibility

### Factory / new appliances

DHCP-first boot is the intended default. A brand-new or factory-reset unit
has `provisioned = false` and always requests a DHCP lease — this matches
the Phase 8 spec and is the correct behavior for new installs.

### Deployed units that currently use a static ESP32 IP

Units already running pre-Phase-8 firmware obtained their Ethernet address
from compile-time `W5500Config.h` static config (e.g. `10.40.0.2`). That
address was never stored with a `provisioned = true` flag in NVS.

On first boot with Phase 8 firmware, those units will **not** continue
forcing the old static IP. Legacy `ip`/`gateway`/`subnet`/`dns` values in
NVS or `settings.json` are migrated into the new `static*` fields, but
because `provisioned` defaults to `false`, **Ethernet boots into DHCP
mode** unless an operator has explicitly saved network settings through the
new API before rebooting.

**Production impact:** for any deployed unit whose portal, admin URL, or
fleet tooling depends on a known fixed ESP32 address, the first Phase 8
boot can assign a different address via DHCP. That can break the existing
portal base URL (and any hard-coded references to the old static IP) until
the address is stable again.

**Required rollout step — do this before flashing Phase 8 to a deployed
unit:**

1. Note the unit's W5500 MAC address (serial log, admin UI, or
   `/api/health` → `ethernet.mac`).
2. On the MikroTik hAP lite, create a **DHCP reservation** (static lease)
   for that MAC at the address the unit currently uses (e.g. `10.40.0.2`).
   Example RouterOS:
   ```
   /ip dhcp-server lease add mac-address=<W5500-MAC> address=10.40.0.2 server=<dhcp-server-name>
   ```
3. Flash Phase 8 firmware and reboot. The unit will request DHCP and should
   receive the same address via the reservation — portal base URL and
   admin access remain unchanged.

Alternatively, after flashing (via Management AP at `192.168.4.1`), save
static mode through `POST /api/system/wifi/config` with the previous
address and reboot — but the DHCP-reservation-first approach avoids any
address gap during the transition.

The Management AP (`Renz-Fi Setup` @ `192.168.4.1`) remains available
throughout regardless of which Ethernet path is chosen, so the unit is
never unreachable for recovery or reconfiguration.

### API consumers

All pre-existing JSON fields are preserved; only new fields were added.
The `/api/system/wifi/config` 405 stub behavior is replaced with working
functionality — any client that depended on the old 405 response will need
updating, but no client could have depended on the old GET's static-only
payload being authoritative since it was never wired to actual hardware
state.

### SSID

The AP SSID changing from `RenzFi-Setup-<id>` to the fixed `Renz-Fi Setup`
is a deliberate breaking change to the over-the-air name; any external
tooling matching the old prefix must update (the Android app in this same
repo is updated as part of this phase).

---

## 10. Security

- No router/MikroTik credentials are exposed by any endpoint touched in
  this phase (`/api/health`, `/api/system/network`, `/api/system/wifi/config`
  all remain scoped to non-secret network/link metadata).
- `/api/system/wifi/config` write access still requires `FullAccess`
  session auth (same as all other provisioning/config write endpoints);
  read access uses the existing `Session`-level requirement, matching the
  precedent already established by `/api/system/management-ap/*`.
- Captive-portal detection routes and the DNS wildcard responder are
  strictly scoped to the Management AP's own IP — they cannot be triggered
  from the Ethernet/LAN side and never touch customer-portal or router
  logic.
- Static IP input is validated server-side (IPv4 parse + proper subnet-mask
  shape) before being persisted or applied — no unvalidated user input
  reaches `ETH.config()`.
- No secrets are logged; all new `Serial.print*` calls emit only IP/MAC/mode
  information already visible on the network.

---

## 11. Build commands & results

```bash
cd ESP32_S3_Firmware
pio run -e freenove_esp32_s3_wroom   # SUCCESS — RAM 32.3%, Flash 65.1%
pio run -e w5500_minimal             # SUCCESS — RAM 14.0%, Flash 32.9%
```

```bash
cd RenzFi-Owner-App
./gradlew.bat assembleDebug           # BUILD SUCCESSFUL
```

All three builds passed with zero compile errors. No new linter errors were
introduced in any touched ESP32 file.

---

## 12. Manual test matrix

| # | Scenario | Steps | Expected result |
|---|----------|-------|------------------|
| 1 | Factory unit, no Ethernet cable | Power on with nothing plugged into W5500 | Boots fully; Serial shows `ETH.begin() = false` then boot continues; `Renz-Fi Setup` AP is broadcasting within a few seconds; `http://192.168.4.1` loads the admin/setup SPA |
| 2 | Factory unit, cable connected, DHCP-capable router | Power on with Ethernet connected to a router handing out DHCP | `Renz-Fi Setup` AP still broadcasts; Ethernet acquires a DHCP lease in the background (Serial: `[ETH] IP acquired (dhcp): ...`); `/api/health` `ethernet.hasIp` becomes `true` without a reboot |
| 3 | Factory unit, cable connected, no DHCP server | Power on with Ethernet connected to an unconfigured switch/router | Boots normally; AP still available; Ethernet never gets an IP but nothing hangs; `ethernet.hasIp` stays `false` |
| 4 | Join Management AP from phone | Connect Wi-Fi to `Renz-Fi Setup` | OS shows "Sign in to network" / captive-portal prompt automatically (Android/iOS/Windows) |
| 5 | Root URL on Management AP | Browse to `http://192.168.4.1/` while on the AP | Redirects to `/admin`, not the customer login page |
| 6 | Root URL on Ethernet/dev fallback | Browse to the ESP32's Ethernet IP directly (`/`) | Unchanged — still serves the dev/recovery captive portal login page |
| 7 | Save static IP via API | `POST /api/system/wifi/config` with a valid static config while authenticated | `200` with `rebootRequired: true`; reboot; unit comes up with the new static IP applied |
| 8 | Save invalid static IP | `POST` with `staticIp: "not-an-ip"` | `400 INVALID_NETWORK_CONFIG`, nothing persisted, unit keeps previous config |
| 9 | Corrupt/garbage NVS static data | Manually corrupt the `renz-network` NVS static fields, reboot | Falls back to DHCP automatically; boot completes normally; AP available |
| 10 | Recovery reset (Level 2) | Trigger existing recovery-button NVS reset | Network settings reset to DHCP + `provisioned=false`; next boot is DHCP |
| 11 | `/api/health` unauthenticated | `curl http://<device-ip>/api/health` | Contains `ethernet` block and `installationState`; no credentials present |
| 12 | Existing MikroTik-hosted customer portal | Connect a customer device to the Wi-Fi/hotspot served by MikroTik (unrelated to the ESP32's own Wi-Fi) | Completely unaffected — customer portal is served by MikroTik, not touched by this phase |
| 13 | Android: join `Renz-Fi Setup` | With the Owner app installed, join the SSID | Native, dismissible "Renz-Fi setup network detected" prompt appears over the existing screen; "Not Now" dismisses without side effects; "Set Up Appliance" opens the existing setup flow at `http://192.168.4.1` |
| 14 | Android: 192.168.4.1 unreachable | Join `Renz-Fi Setup`-named SSID that isn't actually the appliance (or AP not yet up) | App shows its existing native "unavailable" state, no crash |
| 15 | Android: registered device unaffected | Use the app normally with an already-registered appliance on Ethernet | No change in behavior; Admin Login / dashboard flow untouched |

---

## 13. Explicit non-goals (this phase)

- No changes to `RouterPlatform`, `IRouterDriver` implementations, or the
  MikroTik-hosted customer captive portal content/logic.
- No changes to `ProvisioningEngine`/`ProvisioningServer` business logic
  beyond the new network-settings dependency wiring.
- No live/hot re-application of Ethernet config without a reboot — changing
  static/DHCP mode always requires a reboot to take effect (documented,
  intentional).
- No WPA2/password option added to the Management AP (still open, as
  before).
- No changes to voucher/coin/session logic.
- Android: no WebView-based setup flow was introduced — the existing native
  Compose onboarding is reused and extended, per the smallest-safe-change
  principle (WebView remains used only for the post-registration `/admin`
  dashboard, unchanged).
