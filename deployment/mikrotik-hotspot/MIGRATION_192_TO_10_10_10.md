# Migrate hAP lite guest / Hotspot LAN: 192.168.88.0/24 → 10.10.10.0/24

This guide moves the MikroTik hAP lite **guest Hotspot network** to `10.10.10.0/24`
while keeping the ESP32 appliance on **DHCP** (recommended: static DHCP lease by
Ethernet MAC). **WinBox will disconnect** when the router LAN address changes —
plan a reconnect path before step 3.

## Target addressing

| Role | Address |
|------|---------|
| Hotspot gateway (bridge-lan) | `10.10.10.1/24` |
| DHCP pool | `10.10.10.100` – `10.10.10.254` |
| ESP32 (DHCP reservation — example) | `10.10.10.2` |
| Hotspot DNS name (optional) | `wifi.renz-fi.local` |
| Manual captive portal URL | `http://10.10.10.1/login` |
| ESP32 management AP (unchanged) | `Renz-Fi Setup` @ `192.168.4.1` |

The portal JavaScript **must not** hardcode the ESP32 IP. Build the MikroTik bundle
with the reservation you configure:

```bash
RENZFI_APPLIANCE_BASE_URL=http://10.10.10.2 npm run build:mikrotik-portal
```

## Ordered migration steps

1. **Backup** — `/export file=renzfi-pre-migration` (download from Files).
2. **Prepare reconnect** — note current admin access; after migration use `10.10.10.1`.
3. **Change bridge gateway** — set `bridge-lan` / LAN interface to `10.10.10.1/24`.
4. **Change DHCP network/pool** — network `10.10.10.0/24`, pool `10.10.10.100-254`.
5. **Update Hotspot profile/server** — Hotspot address on `10.10.10.1`, profile
   `html-directory=hotspot` (applied by `upload-hotspot-files.rsc`).
6. **ESP32 DHCP reservation** — `/ip dhcp-server lease add mac-address=<W5500-MAC> address=10.10.10.2` (adjust server name/interface to match your config).
7. **Build portal** — `RENZFI_APPLIANCE_BASE_URL=http://10.10.10.2 npm run build:mikrotik-portal`.
8. **Upload hotspot files** — Winbox → Files → `hotspot/` (all files from
   `deployment/mikrotik-hotspot/`, including default audio if present in `portal/`).
9. **Import deployment script** — edit `renzfiApplianceIp` in
   `upload-hotspot-files.rsc` if needed, upload, then `/import file-name=upload-hotspot-files.rsc`.
10. **Reconnect WinBox** at `10.10.10.1`.
11. **Test** — unauthenticated captive portal, voucher login, API reachability,
    coin flow (Insert Coin → Done Paying), pause/resume, custom vs default assets.

## Example RouterOS migration script (review before import)

**Warning:** importing changes live addresses and drops your WinBox session.
Adjust interface names (`bridge-lan`, `dhcp1`, `hotspot1`) to match your unit.

Save as `migrate-guest-subnet.rsc`, upload to Files, then `/import` only after backup.

```
# --- review interface names before running ---
:local newGateway "10.10.10.1"
:local newPool "10.10.10.100-10.10.10.254"
:local newNetwork "10.10.10.0/24"

/ip address remove [find interface=bridge-lan address~"192.168.88."]
/ip address add address=($newGateway . "/24") interface=bridge-lan

/ip pool set [find name~"hotspot"] ranges=$newPool
/ip dhcp-server network set [find address~"192.168.88."] address=$newNetwork gateway=$newGateway dns-server=$newGateway

/ip hotspot set [find] address=$newGateway
/ip hotspot profile set [find] html-directory=hotspot

:put "Subnet migration applied — reconnect WinBox at 10.10.10.1"
```

## Captive portal pop-up expectations

- Android/iOS captive network assistants **vary by device and OS version** — auto pop-up
  is not guaranteed on every phone.
- HTTP detection requests should redirect when the OS performs captive checks.
- **HTTPS** cannot always be transparently intercepted (certificate pinning/HSTS).
- Manual fallback: **`http://10.10.10.1/login`** or `http://wifi.renz-fi.local/login`
  when DNS is configured.

## What is not automated

- Changing the router LAN subnet (operator must import/run migration script).
- Creating the ESP32 DHCP reservation (requires the unit’s W5500 MAC).
- Uploading binary portal assets (`Default-Banner.png`, `*.mp3`) — use Winbox Files.
- Reflashing ESP32 firmware (only needed if upgrading API/CORS/branding fixes).

## ESP32 network notes

- Factory default Ethernet mode is **DHCP** (`NetworkSettingsManager`).
- Static mode remains optional via admin/provisioning.
- Firmware must not assume `192.168.88.2`, `10.40.0.2`, or `10.10.10.2` as compile-time
  appliance addresses; only the MikroTik **build-time** `RENZFI_APPLIANCE_BASE_URL`
  and DHCP reservation document the expected production IP.
