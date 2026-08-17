# Setup Wizard Redesign (8 Steps)

The Management AP setup wizard at `192.168.4.1/admin/setup` guides installers through first-time Renz-Fi configuration on the ESP32-S3 + W5500 appliance.

## Steps

| Step | ID | Purpose |
|------|-----|---------|
| 1 | `owner` | Create Owner / Super Admin account |
| 2 | `ethernet` | ESP32 W5500 DHCP or static management IP |
| 3 | `router` | MikroTik API connection (test + save) |
| 4 | `guest_wifi` | Guest SSID, WPA2-Personal password, portal display name |
| 5 | `ap_deployment` | Where guest Wi-Fi is broadcast (MikroTik / external / both) |
| 6 | `coin` | Coin rates (₱→minutes), abuse protection thresholds |
| 7 | `operator` | Limited dashboard operator account |
| 8 | `review` | Local router plan preview + Apply with confirmation phrase |

## Persistence

| File | Contents |
|------|----------|
| `/config/provisioning.json` | Owner metadata + password hash |
| `/config/setup-wizard.json` | Ethernet flag, guest Wi-Fi (encrypted password), AP mode, coin setup, operator metadata |
| `/config/router-connection.json` | MikroTik host/port/username + encrypted API password |
| `/config/settings.json` | Coin rate table + abuse settings (mirrored from wizard step 6) |
| NVS `renz-network` | Ethernet mode/static fields |
| NVS `renz-auth` | Owner + operator password hashes, owner username |

## Setup API Endpoints

All on the Management AP plane (`192.168.4.1`):

- `GET /api/setup/status` — device + wizard progress
- `POST /api/setup/owner`
- `POST /api/setup/ethernet`
- `POST /api/setup/router/test`, `POST /api/setup/router/save`
- `POST /api/setup/guest-wifi`
- `POST /api/setup/ap-deployment`
- `POST /api/setup/coin`
- `POST /api/setup/operator`
- `GET /api/setup/router-plan` — local-only plan (HTTP 200, no RouterOS socket)
- `POST /api/setup/router-plan` — HTTP 405
- `POST /api/setup/router-apply` — queued RouterOS foundation apply

## Design Rules

- No Wi-Fi scanning, AP discovery, or automatic external AP configuration.
- Guest Wi-Fi and AP deployment choices are stored locally; MikroTik wireless and external APs are not changed during foundation apply.
- Back navigation is client-side only and does not erase saved wizard data.
- Passwords and credential blobs never appear in API responses or logs.

## Guest Network Defaults

Foundation apply uses a dedicated guest subnet separate from typical MikroTik LANs:

| Field | Default |
|-------|---------|
| Guest network | `10.20.20.0/24` |
| Gateway | `10.20.20.1` |
| Gateway CIDR | `10.20.20.1/24` |
| DHCP pool | `10.20.20.10-10.20.20.254` |

Local validation rejects guest subnets that overlap the ESP32 Ethernet subnet or the known MikroTik management subnet.
