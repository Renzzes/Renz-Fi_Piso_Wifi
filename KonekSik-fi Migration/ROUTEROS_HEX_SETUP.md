# RouterOS Setup — hEX Refresh

Use hAP lite **export as reference**. Do **not** restore hAP `.backup` onto hEX.

---

## 1. Initial access

1. PC on **ether2** (LAN).
2. Winbox to default IP (often `192.168.88.1` on fresh unit).
3. Upgrade RouterOS v7 if needed → reboot.

---

## 2. WAN (ether1)

```routeros
/interface ethernet set [ find default-name=ether1 ] name=ether1-WAN
/ip dhcp-client add interface=ether1-WAN disabled=no
```

For PPPoE: copy `/interface pppoe-client` from hAP export.

Verify: `/ping 8.8.8.8`

---

## 3. Guest bridge

```routeros
/interface bridge add name=bridge-guest protocol-mode=rstp
/interface bridge port add bridge=bridge-guest interface=ether2
/interface bridge port add bridge=bridge-guest interface=ether3
/interface bridge port add bridge=bridge-guest interface=ether4
/interface bridge port add bridge=bridge-guest interface=ether5
```

Use your hAP bridge name if different.

---

## 4. IP, DHCP, ESP32 lease

**Adjust to match hAP backup:**

```routeros
/ip address add address=10.20.0.1/24 interface=bridge-guest
/ip pool add name=pool-guest ranges=10.20.0.100-10.20.0.250
/ip dhcp-server add name=dhcp-guest interface=bridge-guest address-pool=pool-guest
/ip dhcp-server network add address=10.20.0.0/24 gateway=10.20.0.1 dns-server=10.20.0.1
/ip dhcp-server lease add address=10.10.10.2 mac-address=ESP32_MAC comment="Renz-Fi"
```

---

## 5. DNS & NAT

```routeros
/ip dns set allow-remote-requests=yes servers=8.8.8.8,1.1.1.1
/ip firewall nat add chain=srcnat out-interface=ether1-WAN action=masquerade
```

Copy extra filter/NAT rules from hAP export (skip wlan-only rules).

---

## 6. HotSpot on bridge (required)

```routeros
/ip hotspot profile add name=hsprof-guest hotspot-address=10.20.0.1 \
    html-directory=hotspot dns-name=wifi.local login-by=http-chap,http-pap
/ip hotspot add name=hotspot-guest interface=bridge-guest address-pool=pool-guest profile=hsprof-guest
```

Recreate **user profiles / rate limits** from hAP (e.g. `renzfi-speed-50m-50m`).

Verify: `/ip hotspot print` → `interface=bridge-guest`

---

## 7. Captive portal upload

1. Build: `RENZFI_APPLIANCE_BASE_URL=http://10.10.10.2 npm run build:mikrotik-portal`
2. Upload files to hEX `hotspot` folder (Winbox Files).
3. See `deployment/mikrotik-hotspot/README.md`.

---

## 8. API user for Renz-Fi

```routeros
/user add name=renzfi group=full password="STRONG_PASSWORD"
/ip service set api disabled=no port=8728
```

Match credentials in ESP32 `/config/router.json` or update after migration.

---

## 9. Do NOT migrate from hAP

- `/interface wireless` — use external AP instead
- wlan security profiles on MikroTik
- CAPsMAN (unless you use CAPs)

---

## 10. Save hEX export

```routeros
/export file=hex-production-YYYY-MM-DD
```

Store in `backups/YYYY-MM-DD/`.
