# Topology & Wiring — hEX Refresh + External AP

## Port roles (hEX refresh E50UG)

| Port | Role | Connected to |
|------|------|--------------|
| **ether1** | WAN | ISP modem / ONT |
| **ether2** | LAN (guest bridge) | ESP32 W5500 |
| **ether3** | LAN (guest bridge) | External AP #1 |
| **ether4–5** | LAN | Spare / AP #2 / switch |

## Target diagram

```
   ISP ──► ether1 [ hEX refresh ] bridge-guest
                    ├── ESP32 Renz-Fi
                    ├── External AP → customer phones
                    └── (optional AP #2)
```

## External AP (manufacturer UI)

| Setting | Value |
|---------|-------|
| Mode | AP / Bridge (not Router) |
| DHCP | Off |
| NAT | Off |
| Uplink | LAN → hEX guest port |
| Management IP | Static on guest subnet |

Register AP in **Admin → Access Points**.

## ESP32

- W5500 Ethernet to guest bridge.
- IP must match `RENZFI_APPLIANCE_BASE_URL` in captive portal build.

## hAP lite vs hEX

| hAP lite | hEX + AP |
|----------|----------|
| ISP → hAP WAN | ISP → hEX ether1 |
| ESP32 → hAP LAN | ESP32 → hEX LAN port |
| Phones → hAP Wi‑Fi | Phones → external AP |
