# External Access Point Architecture

**Status:** Architecture corrected. Stage B registry and Stage C generic reachability remain implemented. No vendor configuration, VLAN, or AP provisioning.  
**Baseline:** Git tag `v0.5.0-fully-operational` (`55a33ac289896a20c8687a5da0b623699eef19a7`)  
**Branch:** `feature/external-access-point`  
**Checkpoints:** Stage B `8ccc09285069e4c4cc87d926779f2caa336401fe`; Stage C `327feeeec2341cc4397fc04278491493a3ea92fb`  
**Firmware:** `0.5.0-w5500`  
**Date:** 2026-08-17

This document is the **source of truth** for External Access Point work. The feature is additive and optional. It must not change the frozen Core.

**Implementation status**

- Stage A: complete (this architecture)
- Stage B: complete — persist/CRUD registry (`stage-b: external-ap persistence-crud`)
- Stage C: complete — generic reachability / management status (`stage-c: external-ap-generic-reachability`)
- Detect: one-time optional MikroTik ARP lookup job (owner-triggered, non-continuous)
- Automatic 45-second monitoring: not implemented
- Vendor AP configuration/provisioning: **will not be implemented**
- VLAN: **out of scope**
- Physical validation: not yet performed

---

## Architectural Guardrails (frozen)

These statements are mandatory. Future work must not weaken them.

1. Renz-Fi does not configure external APs.
2. AP credentials are metadata only and are not used for AP login/configuration.
3. Vendor is informational metadata and is not a driver selector.
4. GenericApDriver is a reachability probe, not a configuration driver.
5. No VLAN is implemented.
6. No AP-side bandwidth enforcement is implemented.
7. MikroTik remains the network authority.
8. Owner configures AP through the AP's own web interface.
9. Open launches the AP's own management interface; ESP32 does not proxy it.
10. Same SSID is allowed but seamless roaming is not guaranteed.
11. Detect is a one-time owner action and must not run as background scanning.
12. ARP reachable from Detect is evidence only; AP status `online` comes from Stage C check.

Intended product model:

> Configure the AP yourself. Plug it into MikroTik. MikroTik handles the network. Register the AP in Renz-Fi so the owner can see and check it.

Not:

> Configure the AP from Renz-Fi.

`IExternalApDriver` / `GenericApDriver` are **reachability probes only**. They are not vendor configuration drivers and must not grow a `vendor → configuration driver` factory.

---

## 1. Purpose

Optional external Wi-Fi coverage extension connected to the MikroTik LAN.

Renz-Fi records that an already-configured AP exists, stores optional management identity, and can check whether that management IP is reachable.

The AP is a Layer-2 Wi-Fi extension. It is **not** a second gateway, not a voucher processor, not a bandwidth enforcer, and not required for Core operation.

If no AP is registered, runtime behavior must remain the v0.5.0 baseline.

---

## 2. Core Principle

External APs are configured independently through their own manufacturer web interface.

**Renz-Fi does not configure them.**

There must be no vendor-specific AP configuration drivers (TP-Link, Ruijie, Tenda, COMFAST, ASUS, Ubiquiti, MikroTik AP, OpenWrt AP, or others).

Renz-Fi must not:

- log in to the AP to push settings
- change SSID, Wi-Fi password, DHCP, NAT, channel, transmit power, VLAN, or IP
- perform HTTP POST/PUT against the AP management UI
- provision, reboot, or firmware-update the AP

The owner configures the AP first. Renz-Fi only knows about it after it is already correctly set up and connected to the MikroTik LAN.

---

## 3. Physical Topology

```
                    INTERNET / ISP
                         |
                         v
                 +----------------+
                 |    MIKROTIK    |
                 |    GATEWAY     |
                 |----------------|
                 | DHCP           |
                 | NAT            |
                 | HotSpot        |
                 | Captive Portal |
                 | Vouchers       |
                 | User Profiles  |
                 | Rate Limits    |
                 | Session Time   |
                 +-------+--------+
                         |
                    MikroTik LAN
                         |
          +--------------+--------------+
          |              |              |
          v              v              v
       ESP32          External AP 1   External AP 2
      Renz-Fi         AP/Bridge       AP/Bridge
          |              |              |
       Admin           Wi-Fi           Wi-Fi
      Dashboard        clients         clients
```

Compact form:

```
ISP
 |
MikroTik
 |
 +---- ESP32
 |
 +---- External AP 1
 |
 +---- External AP 2
```

The external AP is a Layer-2 coverage extension. The MikroTik remains the authoritative gateway. The ESP32 remains the appliance controller. The external AP is **not** another gateway.

---

## 4. Responsibilities

| Layer | Does | Does not |
|-------|------|----------|
| **MikroTik** | DHCP, NAT, Internet gateway, HotSpot, captive portal, voucher authentication, user profiles, promo rates, session duration, bandwidth/rate limits, active users, accounting | Manage vendor AP firmware or AP radio settings |
| **ESP32 / Renz-Fi** | Admin, AP registry, AP metadata, reachability/status, appliance control, vouchers, portal, system management | Become the gateway; configure the AP; poll RouterOS for AP status; proxy the AP web UI |
| **External AP** | Wi-Fi radio, SSID broadcast, Wi-Fi authentication/security, coverage, Ethernet-to-Wi-Fi Layer-2 bridging | DHCP, NAT, routing, HotSpot, vouchers, rate limits |
| **Admin PWA** | Register/list/edit/remove APs, Check, Open `http://{managementIp}` | Store AP passwords in the browser; present AP config controls |

Speed limits stay on MikroTik User Profiles. The radio path does not change voucher speed:

```
Phone
  -> External AP (bridge)
  -> MikroTik LAN
  -> MikroTik HotSpot
  -> Voucher
  -> User Profile
  -> Rate Limit
  -> Internet
```

The same promo applies whether the client is on MikroTik wireless or any correctly bridged external AP.

---

## 5. Installation Workflow

The owner performs these steps **on the AP itself**, then registers it in Renz-Fi.

1. Open the AP’s own manufacturer web interface.
2. Set **AP / Access Point / Bridge** mode. Not Router, Repeater, WISP, or NAT.
3. Disable the AP DHCP server.
4. Disable NAT / router / WAN routing.
5. Assign a management IP on the live MikroTik LAN subnet (owner-chosen; Renz-Fi does not assign it).
6. Set gateway and subnet to match the MikroTik LAN.
7. Configure Wi-Fi SSID and security (may match MikroTik SSID).
8. Apply any manufacturer-specific settings required for bridge/AP operation.
9. Connect the AP **LAN** port to the MikroTik LAN or LAN switch.
10. Verify a client receives a DHCP lease from **MikroTik**, not from the AP.
11. Verify HotSpot / voucher / rate-limit still work through that radio.
12. Register the AP in Renz-Fi Admin → Access Points.

Incorrect (must not be designed for):

```
Phone -> AP NAT/router -> AP DHCP -> private AP subnet -> MikroTik
```

Correct:

```
Phone -> AP bridge -> MikroTik -> HotSpot -> existing voucher/session -> Internet
```

---

## 6. AP Registry

**File:** `/config/access-points.json`  
**Owner:** `ExternalAccessPointManager`  
**Schema version:** `1`  
**Missing file:** empty registry (feature idle).  
**Corrupt file:** empty RAM registry + `registryError`; do **not** auto-delete.

Persisted on create/update/delete only:

- `id` (server-generated)
- `name`
- `enabled`
- `vendor` (informational brand label only: `generic` | `tp-link` | `ruijie` | `tenda` | `other`)
- `model` (informational)
- `managementIp`
- `username`
- `passwordProtected`
- `ssid` (informational label; Renz-Fi does not set the radio SSID)
- `location`
- `notes`

`vendor` is **not** a driver selector. It does not enable configuration APIs.

RAM-only (never written on the check path):

| Field | Notes |
|-------|--------|
| `status` | reachability, not configuration correctness |
| `latencyMs` | measured RTT when available |
| `lastCheck` | last attempt |
| `lastSuccessfulCheck` | last successful reachability |
| `lastError` | non-secret code |
| `capabilities` | last probe: icmp / http / https |
| `hasCredentials` | derived; never `password` |

Limits: max **8** APs. Name 1–32 chars. Duplicate `managementIp` rejected.

The management IP identifies the AP management interface. It is **not** a client IP. Example only (live LAN comes from EthernetManager, never hardcoded):

- MikroTik `10.10.10.1`
- ESP32 `10.10.10.2`
- External AP `10.10.10.20`
- Clients: MikroTik DHCP `10.10.10.x`

---

## 7. Reachability

Stage C is **External AP Reachability / Management Status**, not an External AP Configuration Driver.

**Owner Check** (Admin **Check** button) asks **MikroTik** whether the **saved** `managementIp` is present:

1. Load registered AP and its saved management IP (never hardcode)
2. RouterOS `/ip/arp/print` for that IP — `status=reachable` → **Online** (`method: arp`)
3. If ARP is missing/stale/incomplete/failed/inconclusive → RouterOS `/ping` to the same IP
4. Ping success → **Online** (`method: ping`)
5. Ping fail → one ARP re-query (ping often refreshes the neighbor even when ICMP fails); `reachable` → **Online** (`method: arp`); otherwise **Offline** (`method: arp_ping`)

**Proven field note (first Check Offline / second Online):** MikroTik often returns ARP `stale` on a cold/idle management IP. Check falls through to `/ping`; many APs do not answer ICMP on the management address, so ping fails and Admin shows Offline. That same ping still refreshes RouterOS ARP, so a second Check sees `reachable` and shows Online. Firmware now re-queries ARP once in the same Check job after ping failure.

Path: `ESP32 → MikroTik → AP`. The ESP32 must **not** ICMP/TCP directly to the AP for Check.

`GenericApDriver` (ESP32 ICMP + TCP 80/443) remains only for legacy Sync probe paths. Check does not use it.

HTTP:

- `POST /api/access-points/{id}/check` → **202** `{ jobId, accessPointId, state: "queued" }` (Router Worker)
- `GET /api/access-points/jobs/{jobId}` → job snapshot including `online`, `status` (`online`|`unreachable`), `method`, `ipAddress`

Worker: existing `RouterProvisioningWorker` (same exception as Detect). Not `async_tcp`. Not `loopTask`. Not ESP32 LAN scan.

If Router Worker is busy: `503 ROUTER_WORKER_BUSY`.  
Empty / disabled registry entry: rejected — no RouterOS probe.

Status means MikroTik-confirmed presence, not “the AP is correctly configured”:

| Status | Meaning |
|--------|---------|
| `online` | MikroTik ARP reachable **or** RouterOS ping success for the saved IP |
| `unreachable` | ARP inconclusive and RouterOS ping failed/timeout (**Offline** in Admin) |
| `disabled` | Registry entry disabled; no probe |
| `unknown` | MikroTik/RouterOS unavailable or check failed to run |

`auth_failed` is not produced. There is no AP authentication feature.

One-time Detect is separate from Check:

- `POST /api/access-points/detect` queues one bounded RouterWorker job (HTTP 202)
- `GET /api/access-points/detect/jobs/{jobId}` polls only that queued job
- Detect may return ARP data (IP/MAC/interface/bridge-port/hostname/status)
- Detect does not auto-create AP entries
- Check uses the **saved** registered IP only; Detect finds candidates

---

## 8. Credentials

The dashboard may collect username and password. They are **optional** for registration and reachability.

They are stored as management identity metadata. **They do not mean Renz-Fi configures the AP.** Stage C does not use them to log in.

| Store | Content |
|-------|---------|
| `/config/access-points.json` | `username` + `passwordProtected` via `CredentialProtector` (device-bound) |
| API request | `password` on write; blank preserves |
| API response | `hasCredentials` only |
| Logs | name, IP, status, latency — never password, cookies, tokens |
| Browser | never localStorage / IndexedDB secrets |
| Backup ZIP | **exclude** this file |
| Factory reset | **delete** this file |

AP credentials are separate from MikroTik RouterOS credentials and from Wi-Fi PSK.

---

## 9. Same SSID

Same SSID is allowed. Example: MikroTik `RENZ-FI` and External AP `RENZ-FI` with the same security.

That can let clients see one network name across the coverage area.

Renz-Fi **does not** implement 802.11k/v/r, controller roaming, vendor mesh, or fast roaming. Client roaming stays with the phone and the radios.

Renz-Fi only requires that the AP is bridged into the same MikroTik-controlled network. Voucher sessions remain MikroTik HotSpot sessions on either radio.

---

## 10. Bandwidth / Voucher Enforcement

MikroTik User Profiles remain authoritative.

Do not configure speed limits on the external AP. Do not add per-AP QoS or vendor rate-limit APIs.

```
Phone
 -> External AP
 -> MikroTik
 -> HotSpot
 -> Voucher
 -> User Profile
 -> Rate Limit
 -> Internet
```

Existing voucher session, service expiration, user profile, rate limit, captive portal, and active-user logic remain untouched.

---

## 11. VLAN

**VLAN is intentionally OUT OF SCOPE.**

Renz-Fi does not add VLAN UI, VLAN assignment per AP, VLAN provisioning, VLAN schema, or VLAN-specific speed limits.

The owner configures the AP as a bridge and plugs it into an available MikroTik LAN port.

If VLAN-aware deployments are ever needed, that is a separate MikroTik segmentation project. Do not mix it into this feature.

---

## 12. Security

- Owner-only AP registry and check/job routes.
- No plaintext password in API responses, logs, SSE, or diagnostics.
- Device-bound `CredentialProtector`.
- No ESP32 reverse-proxy of the AP web UI. **Open** is `window.open("http://"+managementIp)` only.
- Generic reachability does not attempt login (avoids lockouts and destructive POSTs).

---

## 13. Performance

- No LAN scan, ARP sweep, or subnet discovery.
- No high-frequency or 45-second automatic probes in the current implementation.
- No RouterOS / RouterWorker traffic for AP checks.
- No SD read/write, `SD.begin`/`SD.end`, remount, or `STORAGE_LOCK` during probes.
- Dedicated `ap_check_worker`, single-flight.
- Persist only on CRUD.
- JSON for HTTP uses heap/PSRAM documents, not large INTERNAL DMA buffers.

---

## 14. Failure Handling

| Condition | Behavior |
|-----------|----------|
| No AP registered | Idle; v0.5.0 Core unchanged |
| Check while another check runs | `503 ACCESS_POINT_CHECK_BUSY` |
| SD recovery in progress | `503 STORAGE_RECOVERY_IN_PROGRESS`; do not enqueue |
| Ethernet has no IP | Job completes with `unknown` / `ETHERNET_NOT_READY`; do not call the AP offline |
| AP disabled | `disabled`; no ICMP/TCP |
| ICMP only | `network_reachable` |
| TCP only | `management_reachable` |
| Neither | `unreachable`; Core continues |
| AP not found | `404 ACCESS_POINT_NOT_FOUND` |
| MikroTik down | AP may still ping; hotspot/vouchers degrade independently |
| RouterWorker busy | Irrelevant; AP checks do not use it |

IP validation (live `EthernetManager` only; never hardcoded site subnets such as
`10.10.10.x` or `192.168.88.x`):

- valid unicast IPv4
- Ethernet has a usable IP
- not ESP32 SoftAP range `192.168.4.0/24`
- not ESP32 IP or live gateway
- not network/broadcast of the live Ethernet subnet (when candidate is on-subnet)
- same Ethernet subnet **or** any other RFC1918 private address (routed via MikroTik)
- not duplicate AP IP
- Sync/Check prove reachability; validation does not ping or scan

---

## 15. Relationship to ManagementApManager

| Concept | Name | Role |
|---------|------|------|
| ESP32 setup/recovery SoftAP | Management AP (`ManagementApManager`) | ESP32-owned, `192.168.4.1`, installer/owner recovery. **Not** a customer coverage AP |
| LAN coverage AP | External Access Point | Optional radio on the MikroTik LAN. Configured by the owner on the device itself |
| MikroTik wireless | Router wireless / SSID | Existing `/api/router/wireless` |

Do not call the ESP32 SoftAP an External Access Point.

---

## 16. Future Scope

Possible later work (explicitly **not** this implementation):

- periodic reachability monitoring
- vendor-specific management (if ever justified with real hardware and a separate architecture)
- VLAN-aware deployments
- MikroTik network segmentation

Do not implement AP configuration, vendor login, VLAN, or roaming now.

Historical Stage B/C checkpoints must not be rewritten. `v0.5.0-fully-operational` must not be moved.

---

## 17. RouterOS Worker Ownership Exception — External AP Detect + Check

External AP Detect and Check need RouterOS data (`/ip/arp/print`, optional
bridge/lease lookups, and Check fallback `/ping`), so they must run on the
existing asynchronous RouterOS job owner.

Approved exception:

- `RouterProvisioningWorker` remains the sole asynchronous RouterOS job owner.
- External AP Detect and Check may reuse this existing job infrastructure for
  one-time, bounded, read-only RouterOS operations (Check may issue `/ping`
  to the **saved** management IP only).
- This exception does not authorize AP configuration, RouterOS configuration
  writes (other than none), periodic scanning, or creation of another RouterOS
  worker/client.

Why this is required:

1. Detect and Check need MikroTik-side evidence; the ESP32 must not probe the
   AP directly across split L2 segments.
2. Reusing the existing worker avoids competing RouterOS clients/queues and
   duplicated recovery/auth/session ownership.
3. Both remain asynchronous (`POST` enqueue -> `202` -> job poll), so no
   blocking RouterOS work runs inside HTTP handlers.
4. Both remain one-time and owner-triggered only; no timers, daemons, or
   continuous ARP monitoring.
5. Detect is read-only and bounded; Check is ARP + optional `/ping` only.
6. Detect cannot auto-create AP entries; owner confirmation is required.
7. Neither may configure the AP, alter MikroTik topology, or control AP power.
8. Legacy Sync may still use `ap_check_worker` / `GenericApDriver`; Admin Check
   uses Router Worker MikroTik ARP + ping.

Allowed Detect/Check RouterOS operations:

- `/ip/arp/print`
- optional `/interface/bridge/host/print` (Detect)
- optional `/ip/dhcp-server/lease/print` (Detect)
- `/ping` to the saved AP management IP only (Check fallback)

Detect/Check are prohibited from RouterOS writes and network-control mutations.

---

## 18. Files and frozen-contract notes

Implemented:

| File | Role |
|------|------|
| `docs/EXTERNAL_ACCESS_POINT_ARCHITECTURE.md` | This design |
| `ESP32_S3_Firmware/src/ExternalAccessPointTypes.h` | Types, IP validation, reachability classification |
| `ESP32_S3_Firmware/src/ExternalAccessPointManager.h/.cpp` | Registry, persist, `ap_check_worker` |
| `ESP32_S3_Firmware/src/ap/IExternalApDriver.h` | Reachability probe contract (not a config driver) |
| `ESP32_S3_Firmware/src/ap/GenericApDriver.cpp/.h` | ICMP + TCP reachability |
| `src/pages/AccessPointsPage.tsx` | Owner Admin UI |
| `src/services/accessPoints.ts` | Client API + job poll |

Core freeze applies to: RouterWorker, MikroTikDriver, RouterOsClient,
VoucherManager, PortalSessionManager, CoinManager, ManagementApManager,
EthernetManager, W5500 SPI, SD remount, STORAGE_LOCK, TWDT, partition table,
sales chart, setup wizard, router credential recovery.

RouterProvisioningWorker exception:

- Keep a single RouterOS async owner.
- External AP Detect may add only one-time bounded read-only job handling.
- No additional RouterOS worker/client is allowed.
- No RouterOS/AP configuration writes are allowed through this exception.

Setup wizard remains frozen at six steps. AP registration is Admin-only.

**BackupManager:** do not add `/config/access-points.json` to export entries.

---

## 19. Absolute rule

```
OWNER CONFIGURES AP
        |
        v
AP IS PUT IN AP/BRIDGE MODE
        |
        v
AP CONNECTED TO MIKROTIK LAN
        |
        v
MIKROTIK PROVIDES NETWORK SERVICES
        |
        +--> DHCP
        +--> NAT
        +--> HotSpot
        +--> Voucher
        +--> User Profile
        +--> Rate Limit
        +--> Session Time
        |
        v
EXTERNAL AP PROVIDES ONLY WIFI COVERAGE
        |
        v
RENZ-FI ADMIN REGISTERS / MONITORS AP
```

Renz-Fi does **not** configure the AP, VLAN, or AP speed limits. Renz-Fi does **not** replace MikroTik. MikroTik remains the single network authority. The external AP is an optional Wi-Fi coverage extension on the MikroTik LAN.

`v0.5.0-fully-operational` remains the operational rollback tag.
