# Migration Plan — hAP lite → hEX Refresh

## 1. Goals

1. Replace the **hAP lite** gateway with **hEX refresh** for better HotSpot/RouterOS API performance and headroom.
2. Move **customer Wi‑Fi** to one or more **external APs** in bridge/AP mode.
3. Keep the **existing ESP32 appliance** (coins, portal API, sales, Admin) with **no firmware rebuild**.
4. Preserve business continuity: same guest subnet conventions, promos, vouchers, and captive portal UX where possible.
5. Keep a **full hAP lite backup** as reference and rollback.

## 2. Non-goals

- Changing Renz-Fi setup wizard structure (frozen at 6 steps).
- Configuring external APs from Renz-Fi (owner configures AP in manufacturer UI).
- VLAN / multi-SSID enterprise design (out of scope unless you add later).

## 3. Architecture change (before vs after)

### Before — Single-box (hAP lite)

```
Internet → hAP lite (WAN + Wi‑Fi + HotSpot + DHCP + NAT)
              ├── ESP32 (Ethernet)
              └── Phones (Wi‑Fi on hAP lite radio)
```

### After — Router + external AP (hEX refresh)

```
Internet → hEX refresh (WAN + HotSpot + DHCP + NAT) — NO Wi‑Fi
              ├── ESP32 (Ethernet, static/reserved IP)
              ├── External AP #1 (bridge, DHCP/NAT off) → customer Wi‑Fi
              └── (optional) External AP #2
```

**Authority unchanged:** MikroTik remains DHCP, NAT, HotSpot, vouchers, rate limits. AP is Layer-2 only.

## 4. What changes vs what stays the same

### Stays the same

| Item | Notes |
|------|-------|
| ESP32 firmware | Same binary; RouterOS API driver is model-agnostic |
| Coin slot, session logic, sales | Core operation unchanged |
| Admin dashboard (except Wireless SSID page) | Rates, promos, sessions, storage, sales |
| Captive portal **source** | Still built from `portal/*` → `deployment/mikrotik-hotspot/` |
| External AP registry | Admin → Access Points (now **required**, not optional) |
| Guest IP plan | Prefer **same subnet** as today (e.g. `10.20.0.0/24` or `10.10.10.0/24`) |

### Changes

| Item | Action |
|------|--------|
| Physical gateway | hAP lite out → hEX refresh in |
| Customer Wi‑Fi | External AP; SSID/password set in **AP web UI** |
| MikroTik config | New export on hEX; **no Wi‑Fi/wlan** sections |
| HotSpot binding | Must bind to **bridge**, not wlan |
| Router credentials on ESP32 | Update if hEX management IP differs |
| Captive portal files on MikroTik | Re-upload to hEX HotSpot directory |
| Router cache / provisioning JSON | Refresh via Admin **Synchronize Router** |
| Setup wizard Step 4 | **Do not use** for SSID on hEX — see [RENZFI_CONFIG_CHANGES.md](./RENZFI_CONFIG_CHANGES.md) |

### Known software gap (plan around it)

Renz-Fi **Finish / Step 4** still assumes MikroTik has `/interface/wireless`. hEX has none. Migration uses **manual RouterOS setup on hEX** + **Admin credential/portal sync**, not a full re-run of Step 4 Finish.

## 5. Phased approach

### Phase 0 — Backup & document (1–2 hours)

**Do not skip.**

1. Complete [BACKUP_CHECKLIST.md](./BACKUP_CHECKLIST.md).
2. Record current values in the table below (from your live hAP lite).

| Setting | Your hAP lite value |
|---------|---------------------|
| WAN type (DHCP / PPPoE / static) | |
| Guest/LAN subnet | e.g. `10.20.0.0/24` |
| Gateway IP | e.g. `10.20.0.1` |
| ESP32 IP | e.g. `10.10.10.2` or guest LAN IP |
| HotSpot profile name(s) | e.g. `renzfi-speed-50m-50m` |
| HotSpot server name | |
| Bridge name | e.g. `bridge-guest` |
| RouterOS API user (Renz-Fi) | |
| SSID (customer) | |
| DNS servers | |

3. Store backups in this folder under `backups/YYYY-MM-DD/` (create locally; do not commit secrets to git).

### Phase 1 — Procure & bench test (1–3 days)

1. **hEX refresh (E50UG)** + 24 V power adapter.
2. **At least one external AP** (TP-Link EAP/Omada, Ubiquiti, COMFAST, etc.) supporting **AP/Bridge mode**.
3. Short Ethernet cables, optional small switch if multiple LAN devices.
4. Bench-test hEX + AP at desk **before** site cutover:
   - hEX gets internet on WAN
   - Bridge + DHCP + HotSpot on LAN
   - AP bridged to hEX LAN port
   - Phone associates to AP SSID → HotSpot redirect works

### Phase 2 — Build hEX config (parallel to production)

**Production hAP lite stays online** until Phase 3.

1. Follow [ROUTEROS_HEX_SETUP.md](./ROUTEROS_HEX_SETUP.md) on the **new hEX** using hAP lite export as **reference** (not blind import).
2. Configure external AP per manufacturer docs (DHCP off, NAT off, bridge to hEX).
3. Wire ESP32 to hEX LAN (same IP plan as production if possible).
4. Deploy captive portal: `deployment/mikrotik-hotspot/README.md` with correct `RENZFI_APPLIANCE_BASE_URL`.
5. Point ESP32 `router.json` at hEX (test IP) — see [RENZFI_CONFIG_CHANGES.md](./RENZFI_CONFIG_CHANGES.md).
6. Run verification on **bench** using [POST_MIGRATION_VERIFICATION.md](./POST_MIGRATION_VERIFICATION.md).

### Phase 3 — Cutover window (30–90 minutes)

Schedule low-traffic time. Have hAP lite labeled and ready for rollback.

| Step | Action | Owner |
|------|--------|-------|
| 1 | Announce brief Wi‑Fi downtime | Owner |
| 2 | Power off hAP lite; disconnect ISP if needed | Tech |
| 3 | Install hEX: ISP → `ether1` (WAN); LAN devices → guest bridge ports | Tech |
| 4 | Confirm RouterOS boot, WAN online, bridge up | Tech |
| 5 | Connect ESP32 Ethernet to hEX LAN | Tech |
| 6 | Power ESP32; confirm link + ping ESP32 IP | Tech |
| 7 | Update Renz-Fi router credentials if IP changed | Owner/Admin |
| 8 | Admin → **Synchronize Router** | Owner |
| 9 | Test: coin → portal → activate → internet | Owner |
| 10 | Register external AP(s) in Admin → Access Points | Owner |

If any critical step fails → [ROLLBACK_PLAN.md](./ROLLBACK_PLAN.md).

### Phase 4 — Verification & soak (24–72 hours)

1. Complete [POST_MIGRATION_VERIFICATION.md](./POST_MIGRATION_VERIFICATION.md).
2. Monitor: coin activations, RouterOS login latency, Admin dashboard, sales recording.
3. Watch ESP32 serial for `[router-budget]`, `[activate]`, storage health.
4. Sign off when stable.

### Phase 5 — Decommission hAP lite

1. Label hAP lite **SPARE / ROLLBACK**; store with backup USB note.
2. Update site documentation with new gateway model and AP list.
3. Optional: add photo + IP diagram to this folder.

## 6. IP addressing strategy (recommended)

Use **the same guest subnet** as hAP lite to minimize portal/credential churn.

Example (adjust to your backup):

| Device | IP | Notes |
|--------|-----|-------|
| hEX gateway (bridge) | `10.20.0.1/24` | HotSpot + DHCP server |
| ESP32 (Renz-Fi) | `10.10.10.2/24` **or** guest LAN IP | Must match `RENZFI_APPLIANCE_BASE_URL` in portal build |
| DHCP pool | `10.20.0.100–10.20.0.250` | Phones |
| External AP mgmt | e.g. `10.20.0.10` | Static; register in Admin AP page |

**Important:** Captive portal `renzfi-app.js` must call the ESP32 URL the phones can reach. Rebuild portal if ESP32 IP changes.

## 7. Risk register

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| No Wi‑Fi after cutover (forgot AP) | Medium | High | Bench-test AP before cutover |
| HotSpot not on bridge | Medium | High | Follow ROUTEROS_HEX_SETUP; verify `/ip/hotspot` interface = bridge |
| Portal can’t reach ESP32 API | Medium | High | Correct `RENZFI_APPLIANCE_BASE_URL`; firewall allow ESP32:80 |
| Setup wizard Step 4 confusion | High | Low | Document: skip Step 4 SSID; use AP UI |
| RouterOS API slow under load | Low | Medium | hEX refresh reduces vs hAP lite |
| Wrong port wiring (WAN/LAN swap) | Medium | High | Label ether1 = WAN before power-on |

## 8. Roles

| Role | Responsibility |
|------|----------------|
| **Owner** | Approves downtime, promos unchanged, AP SSID/password, final sign-off |
| **Installer / tech** | Physical wiring, hEX RouterOS, portal upload |
| **Renz-Fi Admin user** | Credentials, sync router, AP registry, sales check |

## 9. Future firmware improvement (optional backlog)

Track separately — **not required** for migration:

- Setup wizard **bridge-only gateway** mode (skip `/interface/wireless`).
- Finish pipeline: `wirelessQueryRequired = false` when scan detects HotSpot-on-bridge only.
- Admin: hide or relabel “Wireless SSID” when gateway has no radio.

## 10. Approval

| Name | Role | Date | Signature |
|------|------|------|-----------|
| | Owner | | |
| | Installer | | |
