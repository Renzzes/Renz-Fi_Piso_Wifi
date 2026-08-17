# Phase 3 — MikroTik Router Provisioning Report

## Summary

Phase 3 adds **Step 5 — Configure MikroTik Router** to the Management AP setup wizard. The ESP32 inspects the saved RouterOS API connection, builds a read-only provisioning plan, and applies **guest bridge + DHCP foundation only** after explicit owner confirmation.

**Phase 3A (this release):** Safe network foundation — bridge, LAN address, DHCP pool/server/network, ESP32 API firewall rule.

**Phase 3B (deferred):** Hotspot activation, bridge port attachment, wireless/AP integration, captive portal walled-garden — shown as `deferred` in Preview only; **no RouterOS writes** to `/ip/hotspot`, `/ip/hotspot/profile`, or `/ip/hotspot/walled-garden`.

**State transition:** `router_configured` → `provisioned` means **base router foundation provisioned** (Hotspot not activated).

## Persistence (`/config/router-provisioning.json`)

```json
{
  "schemaVersion": 2,
  "foundationApplied": true,
  "hotspotActivated": false,
  "clientInterfaceAttached": false,
  "appliedAt": 123456,
  "routerIdentity": "",
  "routerVersion": "",
  "guestBridgeName": "bridge-renzfi",
  "guestNetwork": "10.20.20.0/24",
  "guestGateway": "10.20.20.1",
  "dhcpPool": "10.20.20.10-10.20.20.254",
  "dhcpServerName": "dhcp-renzfi",
  "updatedAt": 123456,
  "createdObjectIds": []
}
```

Future Hotspot defaults are returned in API `defaults.deferredHotspotDefaults` only — not persisted as applied objects.

## RouterOS commands that may execute on Apply

| # | API path | Action |
|---|----------|--------|
| 1 | `/interface/bridge/add` | Create `bridge-renzfi` (`RENZFI: guest bridge`) |
| 2 | `/ip/address/add` | Add `10.20.20.1/24` on bridge (`RENZFI: guest LAN address`) |
| 3 | `/ip/pool/add` | Create `pool-renzfi` (`RENZFI: guest DHCP pool`) |
| 4 | `/ip/dhcp-server/add` | Create `dhcp-renzfi` on bridge (`RENZFI: guest DHCP server`) |
| 5 | `/ip/dhcp-server/network/add` | Network `10.20.20.0/24` (`RENZFI: guest DHCP network`) |
| 6 | `/ip/firewall/filter/add` | Input accept TCP 8728 from ESP32 IP (only if missing) |

**No writes to:** `/ip/hotspot`, `/ip/hotspot/profile`, `/ip/hotspot/walled-garden`, wireless, bridge ports, NAT, or redirect rules.

## Preview deferred actions

- DEFERRED: Create Hotspot server after client-facing interface/AP is selected
- DEFERRED: Attach selected port/wireless/VLAN/external AP to bridge-renzfi
- DEFERRED: Configure Renz-Fi captive portal integration after Hotspot activation
- DEFERRED: Configure guest SSID/security after wireless/AP integration

`canApply` remains `true` when only deferred items exist (no Hotspot conflicts block apply).

## Management AP

Remains running after `provisioned` (shutdown only on `ready` in a later phase).

## Build

```powershell
cd ESP32_S3_Firmware
pio run -e freenove_esp32_s3_wroom
```

## Physical validation (not performed here)

1. Preview shows 6 foundation CREATE/VERIFY actions + 4 DEFERRED items.
2. Apply creates only bridge/DHCP/firewall objects with `RENZFI:` comments.
3. Confirm no `/ip/hotspot` or walled-garden entries created.
4. `router-provisioning.json` has `foundationApplied=true`, `hotspotActivated=false`.
5. Installation state `provisioned`; Management AP still running.
