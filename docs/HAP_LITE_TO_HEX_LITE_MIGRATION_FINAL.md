# hAP lite → hEX lite Migration — Final Operational Record

**Migration status: SUCCESSFUL AND OPERATIONAL**

**Date of successful physical validation:** 2026-08-29  
**Document type:** Post-migration operational record (documentation only)  
**Does not replace:** [`HAP_LITE_TO_HEX_LITE_MIGRATION_FORENSIC.md`](./HAP_LITE_TO_HEX_LITE_MIGRATION_FORENSIC.md) (pre-migration forensic analysis)

---

## Summary

Renz-Fi production MikroTik service was migrated from a **MikroTik hAP lite (RB941-2nD, RouterOS 7.20.7)** to a **new factory-reset MikroTik hEX lite (RB750r2, RouterOS 7.18.2)**.

The migration **preserved the existing Renz-Fi network architecture** from the operational hAP lite configuration. It did **not** redesign subnets, move the ESP32 onto the guest bridge, or add wireless configuration to the hEX.

Core Renz-Fi operation on the hEX lite was **physically tested and confirmed working**: WAN/Internet, ESP32 management, guest/HotSpot, captive portal, client authentication, and ESP32 Admin Dashboard access from a guest-network client.

---

## Related repository artifacts

| Artifact | Path | Role |
|----------|------|------|
| Migration script (applied) | [`hap-lite-to-hex-lite-migration.rsc`](../hap-lite-to-hex-lite-migration.rsc) | hAP → hEX configuration import |
| Post-migration validation | [`hex-lite-renzfi-validation.rsc`](../hex-lite-renzfi-validation.rsc) | Non-destructive RouterOS checks |
| Pre-migration forensic report | [`HAP_LITE_TO_HEX_LITE_MIGRATION_FORENSIC.md`](./HAP_LITE_TO_HEX_LITE_MIGRATION_FORENSIC.md) | Pre-cutover analysis and command mapping |
| Migration script contract check | [`ESP32_S3_Firmware/tools/routeros-migration-script-contract-check.mjs`](../ESP32_S3_Firmware/tools/routeros-migration-script-contract-check.mjs) | Static syntax/policy check |
| Site migration plan | [`KonekSik-fi Migration/MIGRATION_PLAN.md`](../KonekSik-fi%20Migration/MIGRATION_PLAN.md) | Broader cutover planning |
| hEX setup reference | [`KonekSik-fi Migration/ROUTEROS_HEX_SETUP.md`](../KonekSik-fi%20Migration/ROUTEROS_HEX_SETUP.md) | Generic hEX notes (not used as topology source of truth) |
| Topology reference | [`KonekSik-fi Migration/TOPOLOGY_AND_WIRING.md`](../KonekSik-fi%20Migration/TOPOLOGY_AND_WIRING.md) | Wiring intent |
| Rollback plan | [`KonekSik-fi Migration/ROLLBACK_PLAN.md`](../KonekSik-fi%20Migration/ROLLBACK_PLAN.md) | hAP restoration procedure |
| External AP architecture | [`EXTERNAL_ACCESS_POINT_ARCHITECTURE.md`](./EXTERNAL_ACCESS_POINT_ARCHITECTURE.md) | External AP design guardrails |

**Source hAP export (read-only reference, not modified):** `hap-lite-migration.rsc` (RB941-2nD, RouterOS 7.20.7 export used as architecture source).

---

## 1. Original hAP lite architecture

**Evidence:** VERIFIED from hAP production export and pre-migration forensic analysis.

### ESP32 management network (isolated)

| Parameter | Value |
|-----------|-------|
| Network | `10.10.10.0/24` |
| MikroTik gateway | `10.10.10.1` |
| ESP32 | `10.10.10.2` |
| Interface | `ether2-ESP32` |
| Bridge membership | **Not** in `bridgeGuest` |

### Guest / HotSpot network

| Parameter | Value |
|-----------|-------|
| Network | `10.20.0.0/24` |
| Gateway | `10.20.0.1` |
| Interface | `bridgeGuest` |
| HotSpot server | `hotspot-renzfi` |
| HotSpot profile | `RenzFi-Hotspot` |
| DNS name | `wifi.renz-fi.local` |

### hAP wireless (not migrated)

| Parameter | Value |
|-----------|-------|
| Interface | `wlan1` |
| Mode | AP bridge |
| SSID | **MKonekSik-fi** |
| Guest ingress | `wlan1` member of `bridgeGuest` |

On the hEX lite, wireless ingress was replaced by **external APs** on Ethernet ports (`ether3`, `ether4`, `ether5`) bridged into `bridgeGuest`.

---

## 2. Why the migration script was created

**Evidence:** VERIFIED (deployment facts).

- The target **hEX lite was new and factory-reset**.
- There was **no pre-existing production hEX configuration** to preserve.
- Blind import of the hAP `.rsc` onto the hEX was **not acceptable** because the hAP export contains `wlan1`, wireless security profiles, and CAP configuration that do not apply to the hEX lite.

The migration was performed by adapting the **actual hAP lite production architecture** into a **hEX-lite-compatible RouterOS script**, not by following a generic hEX tutorial.

### Intentionally preserved from hAP production

- ESP32 management network (`10.10.10.0/24` on `ether2-ESP32`)
- Guest network (`10.20.0.0/24` on `bridgeGuest`)
- DHCP pools, servers, and ESP32 reservation
- HotSpot profile/server behavior
- Firewall intent (ESP32 trust + API)
- NAT (both Renz-Fi masquerade rules)
- RouterOS API restriction (`10.10.10.0/24`, `10.20.0.0/24`, TCP 8728)
- External AP topology (Ethernet guest ingress replacing `wlan1`)

### Intentionally excluded

- `/interface wireless`
- `wlan1`
- CAP / wireless security profiles

---

## 3. Migration script

**File:** [`hap-lite-to-hex-lite-migration.rsc`](../hap-lite-to-hex-lite-migration.rsc)

**Evidence:** VERIFIED (repository + observed import/dry-run history).

The script was written specifically for **factory-reset RB750r2** targets. Phase 0 removes conflicting factory-default objects; Phases 1–12 establish the Renz-Fi topology, DHCP, HotSpot, firewall, NAT, API restriction, walled garden, and managed marker script.

### RouterOS 7.18.2 compatibility issue (Phase 5)

**Evidence:** VERIFIED (dry-run failure and fix).

**Original failing code pattern:**

```routeros
:local wanDhcpId [/ip dhcp-client find where interface="ether1-WAN"]
:if ([:len $wanDhcpId] = 0) do={
  :set wanDhcpId [/ip dhcp-client find where interface="ether1"]
}
```

**Observed error during dry-run on RouterOS 7.18.2:**

```text
syntax error (line 181 column 8)
```

**Cause (VERIFIED on 7.18.2):** `:local` variables are immutable in this scripting context; `:set` cannot reassign a `:local` variable inside an `:if` block.

**Corrected approach (in repository):** nested `:if` blocks with `[find]` — no `:local`/`:set` in the migration script:

1. If DHCP client exists on `ether1-WAN` → reuse and enable.
2. Else if client still on `ether1` → retarget to `ether1-WAN` and enable.
3. Else → create new client on `ether1-WAN`.

Dry-run progressed through Phase 5 after this correction.

### Static contract check

**Evidence:** VERIFIED (tool run during migration work).

```bash
node ESP32_S3_Firmware/tools/routeros-migration-script-contract-check.mjs
```

**Result:** PASS — verified in repository:

- No `:local` / `:set` in migration script
- No `wlan1` executable references
- No `/interface wireless` or CAP commands
- No `/system reset-configuration`

### Validation script

**File:** [`hex-lite-renzfi-validation.rsc`](../hex-lite-renzfi-validation.rsc)

**Evidence:** INFERRED — present in repository; **not modified** as part of the Phase 5 fix (only the migration script was corrected per repository history).

---

## 4. Pre-import backups

**Evidence:** VERIFIED (observed on hEX during migration; backup files not committed to git).

| Backup | Command / name | Observed size | Notes |
|--------|----------------|---------------|-------|
| Pre-import export | `/export file=hex-before-renzfi-import` | ~2664 bytes | `hex-before-renzfi-import.rsc` on hEX |
| Pre-portal replacement | (backup file) | ~29.3 KiB | `hex-before-portal-replacement.backup` |
| Factory reset artifact | (existing on flash) | — | `flash/auto-before-reset.backup` |

**Do not overclaim:** Backup *contents* were not forensically diffed in the repository. Sizes and filenames are **observed facts** only.

---

## 5. Initial hEX factory configuration issue (bridgeLocal / WAN)

**Evidence:** VERIFIED (observed on RouterOS 7.18.2 factory-reset unit during migration validation).

On this unit, the factory configuration used **`bridgeLocal`** (not the generic `bridge` name referenced in migration script comments). Critically:

- **`ether1-WAN` was initially a member of `bridgeLocal`**
- A factory DHCP client existed on `bridgeLocal`
- A DHCP client on `ether1-WAN` was **invalid** while `ether1-WAN` remained a bridge slave

**Observed error:**

```text
DHCP client can not run on slave or passthrough interface!
```

### Resolution (observed commands)

```routeros
/interface bridge port remove [find where interface="ether1-WAN"]
/ip dhcp-client disable [find where interface="bridgeLocal"]
/ip dhcp-client enable [find where interface="ether1-WAN"]
```

**Result (VERIFIED):**

- `ether1-WAN` received **`192.168.1.2/24`**
- Gateway **`192.168.1.1`**
- WAN became operational

**Note:** The migration script Phase 0 removes factory bridge ports for `ether2`–`ether5` and targets generic factory bridge names; the **`bridgeLocal` / `ether1-WAN` slave condition** was resolved by the observed manual commands above during live validation on this RouterOS 7.18.2 unit.

---

## 6. Final physical interface topology

**Evidence:** VERIFIED (observed after migration and validation).

```
                    ISP / upstream
                         |
                   ether1-WAN
                   (DHCP client)
                         |
              +----------+----------+
              |    MikroTik hEX     |
              |       lite          |
              +----------+----------+
                    |         |
            ether2-ESP32   bridgeGuest
            10.10.10.1/24   10.20.0.1/24
                 |              |
              ESP32         ether3 ── AP #1
              Renz-Fi       ether4 ── AP #2
                            ether5 ── AP #3
```

| Port | Name | Role | Verified |
|------|------|------|----------|
| ether1 | `ether1-WAN` | WAN / Internet only | VERIFIED |
| ether2 | `ether2-ESP32` | ESP32 management only — **NOT** in `bridgeGuest` | VERIFIED |
| ether3 | `ether3` | `bridgeGuest` — external AP uplink | VERIFIED (guest client observed) |
| ether4 | `ether4` | `bridgeGuest` — external AP | INFERRED (bridge port configured) |
| ether5 | `ether5` | `bridgeGuest` — external AP | INFERRED (bridge port configured) |

**Critical:** The ESP32 is connected to **`ether2-ESP32`**, not to `bridgeGuest`.

---

## 7. ESP32 validation

**Evidence:** VERIFIED (physical test on migrated hEX).

| Check | Observed result |
|-------|-----------------|
| Physical connection | ESP32 on `ether2-ESP32` |
| DHCP lease | `address=10.10.10.2`, `MAC=A2:CB:8F:F8:97:B5`, `status=bound`, `host-name="espressif"` |
| Ping from MikroTik | `/ping 10.10.10.2` → **10/10 received, 0% packet loss** |
| Latency | ~1–2 ms |

**Conclusion:** ESP32 management link is **operational**.

---

## 8. Internet / WAN validation

**Evidence:** VERIFIED (observed on migrated hEX).

| Parameter | Value |
|-----------|-------|
| WAN address | `192.168.1.2/24` |
| Gateway | `192.168.1.1` |
| Default route | `0.0.0.0/0 → 192.168.1.1 via ether1-WAN` |

**Successful tests:**

- `/ping 1.1.1.1` — VERIFIED
- `/ping google.com` — VERIFIED

**Historical note (not a migration failure):** One earlier ping test to `1.1.1.1` showed **20% packet loss** while a later DNS test showed **0% loss**. Subsequent operation and validation succeeded. This is recorded as a transient observation, **not** evidence of failed migration.

---

## 9. HotSpot / captive portal migration

**Evidence:** VERIFIED (observed portal directory and HotSpot print output).

### Portal files

Production captive portal assets from the operational hAP lite environment were transferred to the hEX lite.

**Final working directory:** `flash/hotspot/`

**Observed contents included:**

- `login.html`, `status.html`, `renzfi-app.js`, `renzfi-style.css`
- `rlogin.html`, `redirect.html`, `radvert.html`, `logout.html`, `alogin.html`
- `admin.html`, `error.html`, `connected.html`
- `md5.js`, `script.js`, `style.css`, `api.json`, `errors.txt`
- `favicon.ico`, `Default-Banner.png`, `logo.png`, `bg_music.mp3`
- `xml/`, `img/`, `css/`

**Note:** Some files may have existed on the hEX before final portal replacement. The **operational fact** is that the final `flash/hotspot/` directory contains the **production Renz-Fi portal assets** and the captive portal **worked on a live client**.

### HotSpot profile (VERIFIED)

| Property | Value |
|----------|-------|
| `name` | `RenzFi-Hotspot` |
| `hotspot-address` | `10.20.0.1` |
| `dns-name` | `wifi.renz-fi.local` |
| `html-directory` | **`flash/hotspot`** (observed on live unit) |
| `login-by` | `cookie,http-chap,http-pap` |

**Note:** The migration script sets `html-directory=hotspot`. The **observed live configuration** uses `flash/hotspot`, consistent with portal files placed under `flash/hotspot/`. This was resolved during portal deployment, not by redesigning the network.

### HotSpot server (VERIFIED)

| Property | Value |
|----------|-------|
| `name` | `hotspot-renzfi` |
| `interface` | `bridgeGuest` |
| `address-pool` | `pool-guest` |
| `profile` | `RenzFi-Hotspot` |
| `ip-of-dns-name` | `10.20.0.1` |
| `proxy-status` | `running` |

---

## 10. DHCP validation

**Evidence:** VERIFIED.

### Pools

| Pool | Range |
|------|-------|
| `pool-guest` | `10.20.0.10` – `10.20.0.254` |
| `pool-mgmt` | `10.10.10.2` – `10.10.10.20` |

### DHCP networks

| Network | Gateway | DNS |
|---------|---------|-----|
| `10.10.10.0/24` | `10.10.10.1` | `10.10.10.1` |
| `10.20.0.0/24` | `10.20.0.1` | `8.8.8.8`, `1.1.1.1`, `10.20.0.1` |

### ESP32 lease

**VERIFIED:** `10.10.10.2` bound to MAC `A2:CB:8F:F8:97:B5`.

---

## 11. HotSpot client validation

**Evidence:** VERIFIED (physical client test).

| Field | Observed |
|-------|----------|
| Client IP | `10.20.0.250` |
| Client MAC | `12:39:31:C8:4C:30` |
| HotSpot Active list | Client present |
| Captive portal | Displayed on phone |

**Portal UI observed:**

- Renz-Fi branding
- Connected / Disconnected state
- IP address and MAC address
- Account credits and remaining time
- Package selection
- **Insert Coin**

**Authentication:** Client successfully authenticated and reached **CONNECTED** state with remaining time displayed.

**Conclusion:** Guest network, HotSpot, and captive portal are **operationally validated**.

---

## 12. ESP32 Admin Dashboard validation

**Evidence:** VERIFIED (after correct test path).

| URL | Result |
|-----|--------|
| `http://10.10.10.2/admin` | Admin Dashboard loaded successfully |

### Troubleshooting event (important)

**Initial failed test:** A laptop **not connected to the Renz-Fi SSID** could not reach the Admin Dashboard.

**Diagnosis (VERIFIED):** Test-path issue — not an application or firmware failure.

**Successful test:** From a device connected to the **Renz-Fi guest SSID** (via external AP → `bridgeGuest` → HotSpot), the Admin Dashboard loaded correctly.

**Architecture path (VERIFIED intent):**

```
Client on 10.20.0.x
        ↓
bridgeGuest / HotSpot
        ↓
ESP32 management network (walled garden / routing)
        ↓
http://10.10.10.2/admin
```

**Conclusion:** ESP32 Admin Dashboard is **operational** from the guest client network.

---

## 13. Firewall validation

**Evidence:** VERIFIED (observed rules on live hEX).

Key rules observed:

| Purpose | Observed configuration |
|---------|------------------------|
| ESP32 trusted | `chain=input`, `action=accept`, `src-address=10.10.10.2` |
| ESP32 API | `chain=input`, `action=accept`, `protocol=tcp`, `src-address=10.10.10.2`, `dst-port=8728` |
| HotSpot exception (dst) | `Renz-Fi ESP32 appliance API`, `dst-address=10.10.10.2` |
| HotSpot exception (src) | `Renz-Fi ESP32 appliance API`, `src-address=10.10.10.2` |

These rules support intended ESP32 appliance/API communication through the HotSpot architecture. Firewall was **not redesigned** during documentation.

---

## 14. NAT validation

**Evidence:** VERIFIED.

| Comment | Configuration |
|---------|---------------|
| `Renz-Fi Internet NAT` | `chain=srcnat`, `action=masquerade`, `out-interface=ether1-WAN` |
| `Renz-Fi Guest Internet NAT` | `chain=srcnat`, `action=masquerade`, `src-address=10.20.0.0/24`, `out-interface=ether1-WAN` |

Both rules from the hAP production intent were observed on the hEX lite.

---

## 15. Routing validation

**Evidence:** VERIFIED (observed routing table during operation).

| Destination | Via |
|-------------|-----|
| `0.0.0.0/0` | `192.168.1.1` via `ether1-WAN` |
| `10.10.10.0/24` | `ether2-ESP32` |
| `10.20.0.0/24` | `bridgeGuest` |
| `192.168.1.0/24` | `ether1-WAN` |

Routing was verified **operationally** (Internet + guest + management paths working).

---

## 16. External access points

**Evidence:** VERIFIED (guest Wi-Fi path) / NOT TESTED (per-port AP inventory).

| Port | Intended role | Status |
|------|---------------|--------|
| ether3 | External AP uplink | **VERIFIED** — real HotSpot client observed through AP/guest path |
| ether4 | External AP | **NOT TESTED** individually — bridge port configured |
| ether5 | External AP | **NOT TESTED** individually — bridge port configured |

The hEX lite has **no built-in wireless radio**. Wireless service previously provided by hAP `wlan1` is replaced by **external APs** in bridge/AP mode on `bridgeGuest` ports.

**SSID (production):** **MKonekSik-fi** (configured on external AP, not on MikroTik).

Guest network remains **`10.20.0.0/24`** — external APs extend L2 coverage without changing Renz-Fi IP architecture.

---

## 17. What was not migrated

**Evidence:** VERIFIED (by design).

| Item | Reason |
|------|--------|
| hAP lite wireless radio config | hEX lite has no equivalent radio |
| `wlan1` | hAP-only interface |
| CAP / wireless security profiles | hAP-only |
| Operational HotSpot user passwords from export | Not placed in migration script (security / operational data) |
| RouterOS API user credentials | **MANUAL** — not in hAP export; created/verified separately on hEX |
| `10.10.10.20/32 via bridgeGuest` static route | **MANUAL IF NEEDED** — only if AP management IP confirmed as `10.10.10.20` |

---

## 18. Migration issues discovered and resolved

### A. WAN DHCP client invalid while `ether1-WAN` was a bridge slave

- **Symptom:** `DHCP client can not run on slave or passthrough interface!`
- **Cause:** `ether1-WAN` member of `bridgeLocal` on factory-reset 7.18.2 unit
- **Resolution:** Removed `ether1-WAN` from `bridgeLocal`; disabled bridge DHCP client; enabled WAN DHCP on `ether1-WAN`
- **Result:** WAN `192.168.1.2/24`, gateway `192.168.1.1` — **operational**

### B. Migration script Phase 5 syntax error (`:local`/`:set`)

- **Symptom:** `syntax error (line 181 column 8)` on dry-run
- **Cause:** RouterOS 7.18.2 rejects `:set` reassignment of `:local` variables
- **Resolution:** Rewrote Phase 5 using nested `[find]` checks (no `:local`/`:set`)
- **Result:** Dry-run passed Phase 5; static contract check PASS

### C. Admin Dashboard test from wrong network

- **Symptom:** Laptop could not load Admin Dashboard
- **Cause:** Laptop not on Renz-Fi guest SSID
- **Resolution:** Retest from phone/client on guest SSID
- **Result:** `http://10.10.10.2/admin` — **operational**

### D. Captive portal replacement backed up

- **Action:** `hex-before-portal-replacement.backup` created before portal file replacement
- **Result:** Production portal restored to `flash/hotspot/` — **operational**

---

## 19. Final verdict

### STATUS: **SUCCESSFUL AND OPERATIONAL**

The **hAP lite → hEX lite** migration is complete and the Renz-Fi system is **operating on the hEX lite**.

### Verified operational components

| Component | Status |
|-----------|--------|
| WAN | VERIFIED |
| Internet access | VERIFIED |
| ESP32 management network | VERIFIED |
| ESP32 DHCP lease | VERIFIED |
| ESP32 ping/connectivity | VERIFIED |
| Guest network (`10.20.0.0/24`) | VERIFIED |
| DHCP (guest + management) | VERIFIED |
| External AP guest path | VERIFIED |
| MikroTik HotSpot | VERIFIED |
| Captive portal | VERIFIED |
| HotSpot authentication | VERIFIED |
| Client addressing | VERIFIED |
| Client Internet access | VERIFIED |
| ESP32 Admin Dashboard | VERIFIED |
| Firewall / API exceptions | VERIFIED |
| NAT | VERIFIED |
| Routing | VERIFIED |

The migration **preserved the Renz-Fi network architecture** from the hAP lite production configuration rather than adopting a generic hEX layout that would have placed the ESP32 on the guest bridge.

---

## 20. Final configuration table

| Component | Final configuration | Status |
|-----------|---------------------|--------|
| **WAN** | `ether1-WAN`, DHCP, observed `192.168.1.2/24`, gw `192.168.1.1` | **PASS** |
| **ESP32 mgmt** | `ether2-ESP32`, `10.10.10.1/24`, ESP32 `10.10.10.2` | **PASS** |
| **Guest** | `bridgeGuest`, `10.20.0.1/24` | **PASS** |
| **AP / ether3** | External AP uplink on `bridgeGuest` | **PASS** (guest client verified) |
| **AP / ether4** | `bridgeGuest` member | **NOT TESTED** (configured) |
| **AP / ether5** | `bridgeGuest` member | **NOT TESTED** (configured) |
| **HotSpot** | `hotspot-renzfi` on `bridgeGuest` | **PASS** |
| **Portal** | `flash/hotspot/` production assets | **PASS** |
| **ESP32 Admin** | `http://10.10.10.2/admin` from guest client | **PASS** |
| **RouterOS API** | TCP 8728 restricted to mgmt/guest subnets | **INFERRED** (configured; full API job suite not exhaustively retested) |
| **Coin / sales / voucher flows** | Portal showed Insert Coin; client CONNECTED | **PARTIALLY VERIFIED** (full production burn-in NOT TESTED) |

---

## 21. Evidence labels and known limitations

### Labels used in this document

| Label | Meaning |
|-------|---------|
| **VERIFIED** | Directly observed via RouterOS output or physical test during migration validation |
| **INFERRED** | Logically derived from configuration or repository artifacts |
| **NOT TESTED** | No direct evidence during migration validation |

### Known limitations (do not overclaim)

| Topic | Status |
|-------|--------|
| Long-term stability / soak test | NOT TESTED |
| Every Renz-Fi application function (all Admin pages, sales, vouchers, coin hardware) | NOT TESTED exhaustively |
| Every RouterOS API provisioning operation | NOT TESTED exhaustively |
| Each external AP on ether4 and ether5 individually | NOT TESTED |
| WAN address permanence (`192.168.1.2/24` is upstream DHCP — may change) | OBSERVED at validation time only |
| hAP lite decommission / rollback drill | NOT TESTED during this validation window |

Despite these limitations, **core Renz-Fi operation on the hEX lite was physically tested and is functioning**.

---

## 22. Rollback artifacts

| Artifact | Location | Purpose |
|----------|----------|---------|
| `hex-before-renzfi-import.rsc` | hEX (observed) | Pre-migration export |
| `hex-before-portal-replacement.backup` | hEX (observed) | Pre-portal-replacement backup |
| `flash/auto-before-reset.backup` | hEX flash | Factory reset artifact |
| hAP lite production config | Original hAP unit / export | Full service rollback per [`ROLLBACK_PLAN.md`](../KonekSik-fi%20Migration/ROLLBACK_PLAN.md) |

---

## Appendix — Source vs destination hardware

| | Source | Destination |
|---|--------|-------------|
| Model | MikroTik hAP lite **RB941-2nD** | MikroTik hEX lite **RB750r2** |
| RouterOS | **7.20.7** (production) | **7.18.2** (migrated) |
| Wireless | Built-in `wlan1` (MKonekSik-fi) | **None** — external APs |
| Renz-Fi role | Production gateway | **Current production gateway** |

---

## Appendix — Validation summary

| Phase | Result |
|-------|--------|
| Migration script dry-run (post Phase 5 fix) | VERIFIED — proceeded without syntax errors |
| Static contract check | VERIFIED — PASS |
| Migration import on factory-reset hEX | VERIFIED — operational |
| `hex-lite-renzfi-validation.rsc` | INFERRED / optional — live validation performed via observed tests documented above |
| Physical acceptance (WAN, ESP32, HotSpot, portal, Admin) | **VERIFIED** |

---

**Migration status: SUCCESSFUL AND OPERATIONAL**  
**Validation date: 2026-08-29**  
**Document author: Renz-Fi migration record (post-cutover)**  
**Configuration/firmware/scripts: unchanged by this document**
