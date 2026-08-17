# AP Deployment Guide

Renz-Fi stores your AP deployment choice during setup but does **not** automatically configure TP-Link EAP225, Comfast, or generic access points in this version.

## Deployment Modes

| Mode | Meaning |
|------|---------|
| `mikrotik_only` | Guest SSID broadcast from MikroTik hAP lite Wi-Fi (deferred integration phase) |
| `external_only` | Guest SSID broadcast from external AP(s) only |
| `both` | MikroTik Wi-Fi and external AP(s) share the same guest identity |

## Manual External AP Configuration

Configure each external AP in **Access Point mode**:

1. Connect the hAP lite **guest LAN port** to the AP **LAN/uplink** port.
2. Disable DHCP server on the AP.
3. Disable NAT/router mode on the AP.
4. Use the **same guest SSID, password, and security mode** entered in the setup wizard (WPA2-Personal) on subnet **`10.20.20.0/24`**.
5. Assign a management IP via DHCP reservation or static management IP on the guest subnet.
6. Do **not** connect the AP WAN port unless its documentation specifically requires it in AP mode.

## MikroTik Wi-Fi

MikroTik wireless configuration is **not** changed during initial foundation setup. Hotspot, bridge-port attachment, and captive portal redirect are deferred to a later controlled integration phase.

## Compatibility Note

TP-Link EAP225, Comfast, and generic APs are not automatically detected or configured by Renz-Fi in this version.
