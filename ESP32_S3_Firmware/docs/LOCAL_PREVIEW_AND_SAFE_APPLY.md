# Local Preview and Safe Apply

## Local Preview (`GET /api/setup/router-plan`)

- Returns **HTTP 200 immediately** with a deterministic plan built from local wizard state.
- Does **not** open a RouterOS TCP socket, log in, enqueue `RouterProvisioningWorker`, or call any RouterOS print command.
- Logs: `[router-plan] local preview start` / `local preview complete` only.
- Never returns passwords, credential blobs, or secrets.

### Plan Actions

**CREATE (foundation):**

- `bridge-renzfi`
- Guest gateway address/subnet (`10.20.20.1/24` on `10.20.20.0/24`)
- Guest DHCP pool (`10.20.20.10-10.20.20.254`), server, network
- Scoped TCP/8728 firewall allow rule for ESP32 only (`RENZFI: ESP32 appliance API access`)

**VERIFY / SAFETY:**

- Apply preflight uses targeted queries (`?name=`, `?address=`, scoped firewall filter) only.
- `CONFLICT_DETECTED` if a non-`RENZFI:` object occupies the same name or exact guest subnet address (`10.20.20.0/24` / `10.20.20.1/24`).

## Guest Subnet Validation (local)

- Default guest subnet is **`10.20.20.0/24`** (not MikroTik's common `192.168.88.0/24` LAN).
- Local preview rejects guest subnets that overlap the ESP32 Ethernet subnet or the known MikroTik management/router subnet.
- When the router subnet is not known locally, preview adds a warning that Apply will stop safely on MikroTik address conflicts.

**DEFER:**

- MikroTik Hotspot, wireless SSID, bridge-port attachment, external AP configuration, captive portal redirect, NAT, vouchers, bandwidth enforcement

## Apply (`POST /api/setup/router-apply`)

- Queued `RouterProvisioningWorker` job (RouterOS-backed).
- Requires confirmation phrase: `APPLY RENZ-FI CONFIGURATION`
- Idempotent: skips existing matching Renz-Fi objects; never modifies non-Renz-Fi configuration.

## UI

- Button: **Show Planned Configuration**
- Router Identity / RouterOS Version: *Not checked in local preview*
- Apply disabled until confirmation phrase is typed exactly.
