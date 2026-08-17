# External Access Point Architecture

**Status:** Stage B persistence/CRUD implemented. Reachability (Stage C+) is not included.  
**Baseline:** Git tag `v0.5.0-fully-operational` (`55a33ac289896a20c8687a5da0b623699eef19a7`)  
**Branch:** `feature/external-access-point`  
**Firmware:** `0.5.0-w5500`  
**Date:** 2026-08-17

This document is the design contract for an **optional** External Access Point registry and reachability feature. It does **not** implement the feature.

---

## 1. Purpose

Add owner-managed registration of conventional LAN access points (TP-Link, Ruijie, Tenda, generic router-in-AP-mode) so Renz-Fi can:

- record that an AP exists on the MikroTik LAN
- store AP **management** credentials separately from RouterOS and vouchers
- perform **targeted** reachability checks
- show cached status in Admin

The AP is a Layer-2 Wi-Fi extension. It is **not** a second gateway, not OpenWrt, not a voucher processor, and not required for Core operation.

If no AP is configured, runtime behavior must remain the v0.5.0 baseline.

---

## 2. Current architecture findings

### 2.1 Roles already in production

| Role | Owner today | Must not change |
|------|-------------|-----------------|
| Internet gateway, DHCP, NAT, Hotspot, voucher enforcement, bandwidth | MikroTik RouterOS | Yes |
| Appliance controller, Admin API, coin, sales, portal session, voucher jobs | ESP32 Core | Yes |
| Installer/owner SoftAP at `192.168.4.1` | `ManagementApManager` | Yes — **different feature** |
| Guest Wi-Fi on MikroTik | Router cache + `/api/router/wireless` | Yes |

### 2.2 Naming collision (critical)

`ManagementApManager` / System Settings “Management AP” is the **ESP32 SoftAP** used for setup/recovery. It is **not** an external coverage AP.

This feature must use distinct names everywhere:

| Concept | Name | URL / path |
|---------|------|------------|
| ESP32 setup SoftAP | Management AP | existing `/api/system/...` wifi/AP routes |
| LAN coverage AP | External Access Point | `/api/access-points` |
| MikroTik wireless | Router wireless / SSID | `/api/router/wireless` |

### 2.3 RouterOS / RouterWorker

`RouterProvisioningWorker` is the **single** owner of MikroTikDriver / RouterOsClient.

- Admin router jobs: HTTP 202 + `GET /api/router/jobs/{id}`
- Portal activate/pause/deauth: `tryEnqueue*` Critical priority, fail-fast if busy
- Storage recovery: `503 ROUTER_RECOVERY_IN_PROGRESS` (rejected, not queued)
- Worker busy: `503 ROUTER_WORKER_BUSY`

**Decision:** External AP work **must not** enqueue RouterWorker jobs, must not call RouterOS, and must not add `/ip/hotspot/active/print` polling.

`ExistingNetworkScanner` is a setup-time RouterOS read of bridges/DHCP. It is **not** AP discovery and must not run for AP monitoring.

### 2.4 Credentials

MikroTik setup credentials live in `/config/router-connection.json` as `passwordProtected` via `CredentialProtector` (device-bound AES, `enc:v1:` / `enc:v2:`, key from eFuse MAC). API exposes `hasSavedPassword`, never the secret.

Admin `/config/router.json` is production telemetry/settings. Guest Wi-Fi PSK uses the same protector in wizard config.

**Decision:** Reuse `CredentialProtector` for AP management passwords in a **separate** file. Never write AP secrets into `router.json` or `router-connection.json`.

BackupManager already **omits** `router-connection.json` from export bundles. AP credential file must follow the same exclusion.

### 2.5 Storage / SD

- SD is authoritative. SPIFFS is fallback where designed.
- Remount releases `STORAGE_LOCK` before SD SPI. Do not hold the lock across network I/O.
- `sdRecoveryInProgress()` / `sdIoAllowed()` gate writes.
- Status snapshots must stay in RAM. Do not write SD on every check.

### 2.6 W5500 / Ethernet

Live LAN identity comes from `EthernetManager`: `ip()`, `gateway()`, `subnet()`. `NetworkSettings` static defaults (`10.40.0.2` / `10.40.0.1`) are **not** the live LAN and must not be hardcoded as AP validation.

`NetworkDiagnostics` ICMP ping exists only behind `RENZFI_NETWORK_DIAG`, targets a hardcoded `10.10.10.1` every 10 s, and is **not** reusable as production AP monitoring.

### 2.7 Portal / voucher / coin

`PortalSessionManager` grants Internet through RouterWorker hotspot jobs. Voucher jobs run on `voucher_worker` (HTTP 202, single-flight, batched history). Coin does not talk to RouterOS.

External AP clients are normal MikroTik LAN/hotspot clients. No voucher or portal code on the AP.

### 2.8 Admin / HTTP conventions

- Admin is an optional client (`GET /api/status`, SSE, existing job APIs).
- Exact URI matching is required for collection vs sub-routes (voucher `/api/vouchers` vs `/bulk-delete` lesson).
- Owner-only for credentialed network config (`ROLE_PERMISSION_MATRIX.md`).
- JSON in HTTP callbacks uses heap/PSRAM documents (`JsonHeap.h`), not large INTERNAL DMA buffers.
- Setup wizard is **frozen** at six steps. AP registration is **Admin-only**, not a wizard step.

### 2.9 Existing product copy (not a registry)

Setup already documents optional coverage APs (`OPTIONAL_ACCESS_POINTS`, `docs/AP_DEPLOYMENT_GUIDE.md`, deferred action `deferred-external-ap`). That copy is educational. There is **no** persisted AP inventory today.

---

## 3. Proposed architecture

```
Internet
   │
MikroTik  (gateway, DHCP, NAT, Hotspot, vouchers, bandwidth)
   │
   ├──────── ESP32 Renz-Fi (controller / Admin / coin / voucher jobs)
   │
   └──────── LAN / switch
                ├─ AP-01 (bridge / AP mode, DHCP off, NAT off)
                ├─ AP-02
                └─ Wi-Fi clients ──► same MikroTik Hotspot domain
```

### 3.1 Responsibilities

| Layer | Does | Does not |
|-------|------|----------|
| MikroTik | DHCP, NAT, Hotspot, vouchers, rate-limit, Internet | Manage vendor AP firmware |
| ESP32 | Registry, credential store, targeted check, Admin API | Become gateway; poll RouterOS for AP; proxy AP UI |
| External AP | Broadcast SSID, bridge clients to LAN | DHCP, NAT, captive portal, vouchers |
| Admin PWA | CRUD UI, poll cached status, open `http://{managementIp}` in the browser | Store AP passwords; proxy vendor UI |

### 3.2 Optional-if-empty rule

When `accessPoints.length == 0`:

- no ICMP, no TCP probes, no vendor HTTP
- no periodic timer work
- no SD writes from the AP subsystem
- Core (coin, portal, vouchers, RouterWorker, SD recovery) unchanged

### 3.3 Configured vs manageable

| State | Meaning |
|-------|---------|
| Registered | Owner saved name + management IP (required). Credentials optional for generic ping. |
| Network reachable | ICMP echo succeeded |
| Management reachable | TCP/HTTP to management port succeeded |
| Vendor-manageable | Driver reports login capability **and** auth succeeded (Stage E+, hardware-verified) |

Generic driver never claims vendor-manageable.

---

## 4. Data model

**File:** `/config/access-points.json`  
**Owner:** new `ExternalAccessPointManager`  
**Schema version:** `1`  
**Fallback:** missing file = empty registry (feature idle). Corrupt file = empty RAM registry + `lastError` visible in list; do not auto-delete.

Persisted (SD, on create/update/delete only):

```json
{
  "schemaVersion": 1,
  "accessPoints": [
    {
      "id": "ap_a1b2c3d4",
      "name": "AP-01",
      "enabled": true,
      "vendor": "generic",
      "model": "Archer C6",
      "managementIp": "10.10.10.10",
      "username": "",
      "passwordProtected": "",
      "ssid": "RENZ-FI-EXT",
      "location": "Counter",
      "notes": ""
    }
  ]
}
```

`vendor` enum: `generic` | `tp-link` | `ruijie` | `tenda` | `other`. Unknown values coerce to `generic`.

RAM-only (never written on the check path):

| Field | Notes |
|-------|--------|
| `status` | see §7 |
| `latencyMs` | last successful probe RTT, else omitted |
| `lastCheckMs` | `millis()` of last attempt |
| `lastSuccessfulCheckMs` | last success |
| `lastError` | non-secret reason code/message |
| `capabilities` | `{ reachability, vendorApi }` |
| `hasCredentials` | derived; never `password` |

Limits: max **8** APs. Name 1–32 chars. Duplicate `managementIp` rejected. `id` generated server-side.

---

## 5. Credential storage

Reuse `CredentialProtector::protectSecret` / `unprotectSecret`.

| Store | Content |
|-------|---------|
| `/config/access-points.json` | `username` (non-secret) + `passwordProtected` blob |
| API request | `password` accepted on write; blank means preserve |
| API response | `hasCredentials: boolean` only |
| Logs | name, IP, vendor, status, latency — never password, cookies, tokens |
| Browser | never localStorage / IndexedDB secrets |
| Backup ZIP | **exclude** this file (same policy as `router-connection.json`) |
| Factory reset | **delete** this file (add to `FactoryResetWorker` `kResetFiles`) |

AP credentials are **management** credentials. They are not Wi-Fi PSK, not RouterOS, not vouchers.

---

## 6. API contract

All routes: production plane, **owner-only**, exact URI matchers for collection routes.

JSON via PSRAM/heap documents. No RouterOS. No SD I/O on `async_tcp` beyond existing request parsing; persist/check run off the HTTP task.

| Method | Path | Result |
|--------|------|--------|
| GET | `/api/access-points` | 200 list + RAM status overlay |
| POST | `/api/access-points` | 201 created (no password in body) |
| GET | `/api/access-points/{id}` | 200 one record |
| PUT | `/api/access-points/{id}` | 200 updated |
| DELETE | `/api/access-points/{id}` | 200 `{ ok: true, id }` |
| POST | `/api/access-points/{id}/check` | **202** `{ jobId }` |
| GET | `/api/access-points/jobs/{id}` | job snapshot (terminal: completed/failed) |

`POST .../refresh` is an alias of `/check` if implemented; do not add a second job type.

List response (no secrets):

```json
{
  "ok": true,
  "schemaVersion": 1,
  "accessPoints": [
    {
      "id": "ap_a1b2c3d4",
      "name": "AP-01",
      "enabled": true,
      "vendor": "generic",
      "model": "Archer C6",
      "managementIp": "10.10.10.10",
      "hasCredentials": false,
      "ssid": "RENZ-FI-EXT",
      "location": "Counter",
      "notes": "",
      "status": "unknown",
      "latencyMs": null,
      "lastCheck": null,
      "lastSuccessfulCheck": null,
      "lastError": null,
      "capabilities": {
        "reachability": true,
        "vendorApi": "NOT_SUPPORTED"
      }
    }
  ]
}
```

Validation errors: `400` + codes `INVALID_REQUEST`, `INVALID_IP`, `IP_RESERVED`, `IP_NOT_ON_LAN`, `DUPLICATE_IP`, `LIMIT_REACHED`.  
Storage recovery: `503 STORAGE_RECOVERY_IN_PROGRESS` on persist (not on GET of RAM snapshot).  
Check while busy: `503 ACCESS_POINT_CHECK_BUSY` (rejected, not queued).

SSE: emit `access-points.changed` only on CRUD or **completed** check — not on a 45 s tick if Admin is closed. EventBus no-ops with zero clients.

---

## 7. Status model

| Status | Meaning |
|--------|---------|
| `unknown` | Never checked, or Ethernet down so check skipped |
| `disabled` | `enabled: false`; monitor skipped |
| `online` | ICMP **and** management TCP/HTTP succeeded |
| `network_reachable` | ICMP succeeded; management port did not |
| `management_reachable` | Management port succeeded; ICMP failed/filtered |
| `auth_failed` | Vendor login attempted and rejected (Stage E+) |
| `unreachable` | Both probes failed (offline / wrong IP) |

ICMP failure alone is **not** offline if HTTP/TCP management succeeds.

---

## 8. IP validation

Derive expected LAN from **live** `EthernetManager` (`ETH.localIP`, `gatewayIP`, `subnetMask`). If Ethernet has no IP, reject save with `ETHERNET_NOT_READY` (do not use compile-time `10.10.10.x` or static NVS defaults as the LAN).

Reject:

- invalid IPv4
- network address and broadcast
- ESP32 management IP
- MikroTik gateway IP (live gateway)
- another registered AP IP
- ESP32 Management SoftAP subnet (`192.168.4.0/24`) as a coverage-AP management IP

Do not LAN-sweep. Do not auto-change MikroTik DHCP/reservations.

---

## 9. Monitoring and worker strategy

### 9.1 Why not RouterWorker

AP checks would contend with hotspot activate/deauth and Admin router jobs. That violates Admin/Core isolation and the single-RouterWorker rule.

### 9.2 Why not loopTask / async_tcp

TCP connect and vendor HTTP are blocking. ICMP session create is short, but waiting for ping completion on `async_tcp` is forbidden.

### 9.3 Chosen pattern (same family as `VoucherManager`)

`ExternalAccessPointManager` owns:

- RAM registry + status overlay
- persist/load of `/config/access-points.json` (never during a probe)
- a **dedicated** single-flight worker task `ap_check_worker`
- HTTP 202 job records with a **separate job mutex** (GET job never waits on I/O)

`FirmwareApp::loop()` may only decide “interval elapsed → tryEnqueueCheck” when:

- at least one enabled AP exists
- Ethernet has IP
- SD recovery is **not** in progress (skip interval; do not write)
- worker is idle

Interval: **45 seconds**. Manual Check Now: immediate enqueue of **that** AP only. Background pass: round-robin **one AP per interval** (never blast all eight).

Generic probe (Stage C):

1. ICMP echo count=1, timeout ≤ 2000 ms (`esp_ping_*`, not NetworkDiagnostics)
2. Non-blocking-wait via `vTaskDelay` on the AP worker only
3. TCP connect to port 80 (fallback 443), timeout ≤ 2000 ms
4. Classify status; update RAM; complete job

No vendor login in Stage C. No large JSON. No STORAGE_LOCK during probes.

If the worker cannot be added without touching TWDT/stack budgets, **stop** and report measured stack/DMA before merging.

---

## 10. Vendor drivers (Stage E, later)

```
IExternalApDriver
  ├── GenericApDriver     (reachability only)     — Stage C
  ├── TpLinkApDriver      (stub NOT_SUPPORTED)    — Stage E when hardware exists
  ├── RuijieApDriver      (stub)
  └── TendaApDriver       (stub)
```

Unsupported capability returns `NOT_SUPPORTED`, not an error. Do not push vendor config (SSID, DHCP, channel) to unknown devices.

---

## 11. Admin UI

Do **not** invent a new “Network” nav tree (would redesign frozen Admin IA).

Add **one** owner-only item:

- Path: `/access-points`
- Label: **Access Points**
- Placement: after System Configuration
- Permission: owner-only (credentials)

Page: table/cards with Name, Vendor, Model, Management IP, SSID (label), Status, Last check, Clients (hidden unless vendor capability exists), actions Check / Edit / Remove / Open Management (`window.open("http://"+ip)` — no ESP32 proxy).

Empty state: explain AP mode, DHCP off, NAT off, LAN-to-LAN, MikroTik remains gateway. Link mentally to existing setup copy; **do not** add a wizard step.

SSID field is **metadata**. ESP32 does not change the AP SSID unless a verified vendor driver exists (not in first implementation).

---

## 12. Same SSID and roaming

Owners may set AP SSID equal to MikroTik SSID or different (`RENZ-FI` vs `RENZ-FI-EXT`). Both are valid Layer-2 designs if the AP is bridged and DHCP/NAT are off.

Renz-Fi **does not** implement 802.11k/v/r or fake roaming. Document:

> Same SSID and security can allow clients to move between radios. True seamless roaming depends on the client and AP, not on ESP32.

Clients on either radio remain MikroTik Hotspot users. Vouchers and bandwidth stay centralized.

---

## 13. Hardware topology (first validation)

Required AP mode: **Access Point / Bridge**. Not Router, Repeater, WISP, or NAT.

1. Disable AP DHCP and NAT.
2. Connect AP **LAN** to MikroTik LAN or LAN switch (not AP WAN unless vendor AP-mode requires it).
3. Management IP via MikroTik DHCP or static on the **same** subnet as ESP32.
4. MikroTik remains `*.1` gateway. ESP32 remains its current LAN IP. AP is a different host (example only: `10.10.10.10` on a `10.10.10.0/24` site).

---

## 14. Files

### 14.1 New files

| File | Role |
|------|------|
| `docs/EXTERNAL_ACCESS_POINT_ARCHITECTURE.md` | This design (Stage A) |
| `ESP32_S3_Firmware/src/ExternalAccessPointManager.h/.cpp` | Registry, persist, jobs, monitor tick |
| `ESP32_S3_Firmware/src/ExternalAccessPointTypes.h` | Status/vendor enums |
| `ESP32_S3_Firmware/src/ap/IExternalApDriver.h` | Driver interface |
| `ESP32_S3_Firmware/src/ap/GenericApDriver.cpp/.h` | ICMP + TCP probe |
| `src/pages/AccessPointsPage.tsx` | Admin UI |
| `src/services/accessPoints.ts` | Client API |
| `scripts/test-access-point-ip-validation.mjs` | Host-side IPv4/LAN tests |

Vendor drivers beyond Generic: **not** in the first implementation.

### 14.2 Existing files to touch (additive only)

| File | Change |
|------|--------|
| `ESP32_S3_Firmware/src/StoragePaths.h` | Add `AccessPointsFile = "/config/access-points.json"` |
| `ESP32_S3_Firmware/src/FirmwareApp.h/.cpp` | Construct/begin; loop tick only |
| `ESP32_S3_Firmware/src/ApiServer.h/.cpp` | Exact routes; owner auth; inject manager |
| `ESP32_S3_Firmware/src/FactoryResetWorker.cpp` | Delete access-points file |
| `ESP32_S3_Firmware/docs/HTTP_ROUTE_CONTRACT.md` | Document new routes (additive) |
| `ESP32_S3_Firmware/docs/ROLE_PERMISSION_MATRIX.md` | Owner-only AP routes |
| `src/App.tsx` | Route |
| `src/components/AdminLayout.tsx` | Nav item |
| `src/services/embeddedApi.ts` | Path helper |
| `README.md` | Short pointer after implementation |

### 14.3 Must not modify

RouterWorker, MikroTikDriver, VoucherManager job/history architecture, PortalSessionManager grant path, CoinManager, SD remount owner, W5500 SPI pins, TWDT, partition table, Setup wizard step list, `ManagementApManager` behavior, sales chart DMA path.

---

## 15. Frozen-contract extensions (explicit)

These contracts are labeled frozen against **breaking** changes. This feature only **adds**:

1. **StoragePaths** — new file path; existing paths unchanged.  
2. **HTTP_ROUTE_CONTRACT** — new `/api/access-points*` URLs; existing URLs unchanged.  
3. **FactoryResetWorker `kResetFiles`** — one extra delete target so reset does not leave AP secrets.  
4. **ApiServer::begin** — one additional pointer (same pattern as `mgmtAp` / `factoryReset`).

If any of those four cannot be accepted, stop before coding and choose an alternative (e.g. nest AP JSON under an existing config file — rejected here because it would mix AP secrets with unrelated documents).

**BackupManager:** do **not** add this file to `kJsonEntries` (credentials, device-bound key).

---

## 16. Performance impact

| Risk | Mitigation |
|------|------------|
| MikroTik 100% CPU | Zero RouterOS traffic from this feature |
| async_tcp WDT | 202 + worker; no probe in HTTP callback |
| STORAGE_LOCK / SD remount | Persist only on CRUD; skip persist/check writes during recovery |
| DMA fragmentation | No large INTERNAL JSON; probes are ICMP/TCP; PSRAM for any JSON |
| Extra FreeRTOS task | One task, small stack, idle when registry empty |
| Ethernet loss | Checks no-op without ETH IP; Core continues |
| Voucher slowdown | Separate worker; no shared RouterWorker queue |

---

## 17. Security

- Owner-only API.
- No password in responses, logs, or SSE.
- Device-bound `CredentialProtector` (restore of the JSON onto another ESP32 cannot decrypt).
- No ESP32 reverse-proxy of vendor admin UI (avoids cookie/session capture on the appliance).
- Generic driver does not attempt login (prevents lockouts / destructive POSTs).

---

## 18. Failure modes

| Condition | Behavior |
|-----------|----------|
| No AP configured | Idle; v0.5.0 Core unchanged |
| AP unreachable | Status `unreachable`; Core continues |
| Wrong vendor password | Stage C: ignore (no login). Stage E: `auth_failed` |
| MikroTik down | AP may still ping; Renz-Fi does not crash; hotspot/vouchers already degraded independently |
| SD remounting | Skip AP persist and skip checks that would write; RAM status may go `unknown` |
| RouterWorker busy | Irrelevant; AP checks do not use it |
| Vendor unsupported | Generic reachability still works |

---

## 19. Implementation stages

| Stage | Scope | Gate |
|-------|-------|------|
| **A** | This audit + architecture doc | Done when design accepted |
| **B** | Persist/list/create/update/delete; empty = idle | `pio run` + IP validation tests; no probes |
| **C** | Generic check job 202; ICMP+TCP; no vendor login | Unreachable AP must not WDT/DMA/RouterOS |
| **D** | Admin page | No passwords in browser storage |
| **E** | Vendor adapters | Only with real hardware |
| **F** | 45 s round-robin monitor | Disabled when registry empty |
| **G** | Physical AP-mode router | Tests 1–16 in the feature request |

Do not combine E/F/G into the first merge if C/D are not stable.

---

## 20. Test plan (implementation later)

Host tests (Stage B/C): IPv4 parse, reject network/broadcast/self/gateway/duplicate, empty registry JSON, password stripped from serialized API objects.

Firmware/build: `npm run build:esp32`, `pio run -e freenove_esp32_s3_wroom`, existing scripts (`test-sales-chart-buckets`, `test-storage-health-semantics`, `test-sales-uptime-aggregation`, portal tests).

Physical (Stage G): no AP; unreachable AP; reachable AP; unplug; replug; two APs; client on MikroTik SSID; client on AP SSID; same SSID honesty; AP disabled; SD removed; RouterOS down; voucher generate during monitor; sales 7/28/180 during monitor; long soak (no WDT, no Guru Meditation, no DMA collapse).

---

## 21. Regression risks

| Risk | Severity | Notes |
|------|----------|--------|
| Confusing Management AP vs External AP in UI | Medium | Distinct labels required |
| Accidental RouterWorker reuse | High | Forbidden by this spec |
| Check on async_tcp | High | 202-only |
| SD write on status tick | High | RAM overlay only |
| Backup exporting AP secrets | High | Exclude file |
| Factory reset leaving AP file | Medium | Add to `kResetFiles` |
| Hardcoded `10.10.10.x` | Medium | Live ETH LAN only |
| Vendor auto-config | High | Not in generic driver |
| Nav redesign / wizard step | Medium | Forbidden |
| Extra task stack vs TWDT | Medium | Measure before merge |

---

## 22. What this feature does not guarantee

Renz-Fi can guarantee: optional registration, targeted reachability, centralized MikroTik hotspot/voucher/bandwidth for bridged clients, Core operating with zero APs.

Renz-Fi cannot guarantee: zero-second roaming, universal vendor statistics, remote SSID/channel changes, or vendor login on arbitrary firmware.

---

## 23. Absolute rule

`v0.5.0-fully-operational` remains the rollback tag. This feature is additive and optional. The AP must not replace MikroTik, process vouchers, become a second gateway, require OpenWrt, or overload ESP32/MikroTik.
