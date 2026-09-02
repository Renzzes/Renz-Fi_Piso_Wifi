# Forensic Migration Report — hAP lite → hEX lite (Renz-Fi)

**Date:** 2026-08-29  
**Source file:** `hap-lite-migration.rsc` (read-only; located at user backup path — not modified)  
**Target script:** `hap-lite-to-hex-lite-migration.rsc`  
**Validation script:** `hex-lite-renzfi-validation.rsc`

---

## Executive summary

This migration moves a **proven Renz-Fi split-network topology** from MikroTik **hAP lite (RB941-2nD)** to **hEX lite (RB750r2)**. The hEX lite has **no built-in wireless**. Customer Wi-Fi must enter through an **external AP** in bridge/AP mode on a **guest bridge Ethernet port**, not through `wlan1`.

The source configuration uses:

| Plane | Subnet | Gateway | Interface |
|-------|--------|---------|-----------|
| ESP32 management | `10.10.10.0/24` | `10.10.10.1` | `ether2-ESP32` (isolated) |
| Guest / HotSpot | `10.20.0.0/24` | `10.20.0.1` | `bridgeGuest` |

**Do not blindly import** the hAP export onto the hEX. Wireless, CAP, and hAP bridge MAC settings are hardware-specific and must be removed or replaced.

---

## Hardware and software

| Item | Source (hAP lite) | Target (hEX lite) |
|------|-------------------|-------------------|
| Model | RB941-2nD | RB750r2 |
| RouterOS | 7.20.7 | 7.x recommended (match 7.20.x) |
| Ethernet ports | ether1–4 | ether1–5 |
| Wireless | wlan1 (AP bridge, SSID MKonekSik-fi) | **None** — external AP |
| CPU / RAM | 650 MHz / 32 MiB | 850 MHz / 64 MiB |

---

## Documentation review (mandatory)

### Primary architectural references

| Document | Relevant finding |
|----------|------------------|
| `docs/EXTERNAL_ACCESS_POINT_ARCHITECTURE.md` | External AP is Layer-2 bridge only; MikroTik remains DHCP/NAT/HotSpot authority; Renz-Fi does not configure AP radios. |
| `KonekSik-fi Migration/MIGRATION_PLAN.md` | hEX has no Wi-Fi; HotSpot binds to **bridge**; manual RouterOS setup + Admin sync; do not rely on wizard Step 4 Finish. |
| `KonekSik-fi Migration/TOPOLOGY_AND_WIRING.md` | ether1=WAN, ether2=ESP32, ether3=external AP — **but shows ESP32 on guest bridge** |
| `KonekSik-fi Migration/ROUTEROS_HEX_SETUP.md` | Example hEX setup — **puts ESP32 on guest bridge with lease** |
| `KonekSik-fi Migration/RENZFI_CONFIG_CHANGES.md` | Suggests API `host` may be `10.20.0.1` |
| `ESP32_S3_Firmware/src/SetupRouterConnectionManager.cpp` | Firmware default router host: **`10.10.10.1`** |
| `ESP32_S3_Firmware/src/NetworkDiagnostics.cpp` | Ping target: **`10.10.10.1`** |
| `ESP32_S3_Firmware/docs/ADMIN_CAPTIVE_PORTAL_FINAL_FORENSIC.md` | Guest HotSpot on `bridgeGuest`; walled-garden to ESP32 at `10.10.10.2` |
| `deployment/mikrotik-hotspot/README.md` | Portal build uses `RENZFI_APPLIANCE_BASE_URL=http://10.10.10.2` |

### Documented contradictions (reported, not silently resolved)

| Topic | Source / live hAP config | Some migration docs | Resolution for this migration |
|-------|--------------------------|---------------------|-------------------------------|
| ESP32 port role | **Dedicated management** `ether2-ESP32`, not in `bridgeGuest` | `TOPOLOGY_AND_WIRING.md`, `ROUTEROS_HEX_SETUP.md` place ESP32 on guest bridge | **Preserve source hAP topology** — management isolation is proven and required by user contract |
| RouterOS API host | ESP32 connects to **`10.10.10.1:8728`** | `RENZFI_CONFIG_CHANGES.md` mentions `10.20.0.1` | **Preserve `10.10.10.1`** per source + firmware defaults |
| Guest DHCP pool | `10.20.0.10–10.20.0.254` | `ROUTEROS_HEX_SETUP.md` example `10.20.0.100–250` | **Preserve source pool** |
| Bridge name | `bridgeGuest` | Some docs use `bridge-guest` | **Preserve `bridgeGuest`** (live export name) |
| Target router model | User request: **hEX lite** | Migration folder targets **hEX refresh (E50UG)** | Scripts written for **RB750r2 (5 ports)**; same logical topology applies |

---

## Interface mapping

### Source hAP lite

```
ether1-WAN     → ISP (DHCP client)
ether2-ESP32   → Renz-Fi ESP32 W5500 (10.10.10.1/24) — NOT in bridgeGuest
ether3         → bridgeGuest member
ether4         → bridgeGuest member
wlan1          → bridgeGuest member (SSID MKonekSik-fi, open AP)
```

### Target hEX lite (proposed)

```
ether1-WAN     → ISP (DHCP client)                    [PORTABLE]
ether2-ESP32   → Renz-Fi ESP32 W5500 (10.10.10.1/24)  [PORTABLE — isolated]
ether3         → bridgeGuest — primary external AP uplink [TARGET-REPLACEMENT for wlan1 ingress]
ether4         → bridgeGuest — spare / AP #2 / switch   [PORTABLE]
ether5         → bridgeGuest — spare (hEX-only port)    [TARGET-REPLACEMENT — extra vs hAP]
wlan1          → REMOVED — configure SSID on external AP [HAP-LITE-ONLY]
```

### Wireless migration decision

**WIRELESS: EXTERNAL AP MAPPING**

- Do **not** add `/interface wireless` on hEX lite.
- External AP: AP/bridge mode, DHCP off, NAT off, LAN uplink to **ether3** (or documented spare port).
- Customer SSID **MKonekSik-fi** (and security) configured in **AP manufacturer UI**, not MikroTik.

---

## Renz-Fi network contract (preserved)

| Parameter | Value | Status |
|-----------|-------|--------|
| ESP32 management subnet | `10.10.10.0/24` | PROVEN (source) |
| MikroTik management IP | `10.10.10.1` | PROVEN |
| ESP32 IP | `10.10.10.2` | PROVEN |
| ESP32 MAC reservation | `A2:CB:8F:F8:97:B5` | PROVEN (verify on unit before cutover) |
| Guest subnet | `10.20.0.0/24` | PROVEN |
| Guest gateway | `10.20.0.1` | PROVEN |
| Guest pool | `10.20.0.10–10.20.0.254` | PROVEN |
| Management pool | `10.10.10.2–10.10.10.20` | PROVEN |
| HotSpot server | `hotspot-renzfi` on `bridgeGuest` | PROVEN |
| HotSpot profile | `RenzFi-Hotspot`, `wifi.renz-fi.local` | PROVEN |
| Walled garden | `10.10.10.2` accept | PROVEN |
| API restriction | `10.10.10.0/24,10.20.0.0/24` port 8728 | PROVEN |

---

## ESP32 firmware provisioning compatibility

Firmware (`RouterProvisioningEngine.cpp`, `RouterProvisioningManager.cpp`) expects:

- HotSpot on guest bridge (bridge fallback when wireless absent) — **compatible** after migration.
- Walled-garden rule for ESP32 IP — migration script adds `10.10.10.2`.
- Managed script `renzfi-hotspot-ready` — migration script adds marker.
- Finish pipeline **wireless-ssid / production-network** stages may fail on hEX — **documented** in `MIGRATION_PLAN.md`; use manual RouterOS + Admin **Synchronize Router**, not wizard Finish.
- API firewall rule from `10.10.10.2:8728` — migration script preserves input accept rules.

**No ESP32 firmware change required** for this migration if management IP and API credentials remain on `10.10.10.1`.

---

## NAT forensics

Source contains two masquerade rules on `ether1-WAN`:

1. **Renz-Fi Internet NAT** — masquerade all traffic out WAN.
2. **Renz-Fi Guest Internet NAT** — masquerade `10.20.0.0/24` out WAN.

**Analysis:** Rule 2 is a subset of rule 1. For guest-only traffic, rule 2 alone would suffice; rule 1 also covers management subnet (`10.10.10.0/24`) Internet if ever needed. HotSpot may add its own NAT rules dynamically.

**Decision:** **Preserve both** — no silent removal. Project has not proven dependency on only one rule; removing either could change behavior for management-plane Internet or HotSpot interaction order.

---

## Routes forensics

| Source route | Classification | Migrate? | Reason |
|--------------|----------------|----------|--------|
| `10.10.10.2/32 gateway=10.10.10.2%ether2-ESP32` | DUPLICATE | No | Redundant on connected `10.10.10.0/24`; likely provisioning artifact |
| `10.10.10.2/32 gateway=ether2-ESP32` | DUPLICATE | No | Same destination; duplicate syntax |
| `10.10.10.20/32 gateway=bridgeGuest` | UNKNOWN | Manual review | Management-pool address with guest-bridge gateway — contradicts split topology; may be erroneous or site-specific AP mgmt hack |

**ROUTES: MANUAL REVIEW** — migration script omits static routes; connected subnet routing should suffice for ESP32 API.

---

## Risks

| Risk | Impact | Mitigation |
|------|--------|------------|
| Factory default bridge still owns ether3–5 | Guest bridge / HotSpot broken | Remove ports from default bridge before import (documented in migration script) |
| WinBox session drop when `10.10.10.1` applied | Locked out temporarily | Connect PC to `ether2-ESP32`; reconnect at `10.10.10.1` |
| External AP not bridged | No customer Wi-Fi | Bench-test AP before cutover |
| Portal files not uploaded | Broken captive portal UI | Rebuild + upload `deployment/mikrotik-hotspot/` to `Files/hotspot/` |
| API user not in export | ESP32 cannot authenticate | Create/verify API user manually; match `router.json` |
| ESP32 MAC mismatch | Wrong IP after migration | Verify W5500 MAC before applying lease |
| Wizard Finish on hEX | Failure on wireless stages | Skip Finish; use manual migration + Admin sync |
| Document vs source topology conflict | Wrong port wiring | Follow **source hAP export**, not simplified hEX setup doc |

---

## Unresolved questions

1. **`10.10.10.20/32 via bridgeGuest` route** — purpose unknown; not migrated automatically.
2. **HotSpot operational users** (voucher MAC users, test user) — not in safe migration script; restore from secured backup if needed.
3. **RouterOS API username/password** — not present in export; must exist from prior provisioning or be created manually.
4. **`html-directory=hotspot`** — absent from export but required by firmware finish; migration script sets it; portal files must be uploaded separately.
5. **Exact external AP model and management IP** — register in Admin → Access Points after cutover.

---

## Validation procedure

### A. Import and static checks

1. Backup hEX: `/export file=hex-pre-renzfi-migration`
2. Optional: detach ether3–5 from factory default bridge
3. Upload `hap-lite-to-hex-lite-migration.rsc` → `/import file-name=hap-lite-to-hex-lite-migration.rsc`
4. Run `hex-lite-renzfi-validation.rsc`
5. Create/verify API user; upload hotspot portal files
6. Wire: ISP→ether1, ESP32→ether2, AP→ether3

### B. Post-migration acceptance tests (physical)

| # | Test | Expected |
|---|------|----------|
| 1 | WAN obtains IP | DHCP on ether1-WAN |
| 2 | ESP32 uses `10.10.10.2` | DHCP reservation or static |
| 3 | MikroTik management | `10.10.10.1` on ether2-ESP32 |
| 4 | ESP32 → MikroTik ping | OK |
| 5 | ESP32 → API `:8728` | Login success |
| 6 | Guest DHCP | `10.20.0.x` |
| 7 | Guest gateway | `10.20.0.1` |
| 8 | HotSpot redirect | Portal/login.html |
| 9 | HotSpot auth | Coin/voucher session |
| 10 | Guest Internet | After auth |
| 11 | Guest NAT | Egress via WAN |
| 12 | ESP32 mgmt not HotSpot-trapped | No redirect on 10.10.10.x |
| 13 | Walled garden | `/api/portal/*` reaches ESP32 |
| 14 | API not global | Refused from WAN/untrusted nets |
| 15 | Renz-Fi provisioning | Connect, auth, sync, activate |
| 16 | External AP | Phones on AP SSID → guest bridge → HotSpot |

---

## Rollback plan (hEX → restore previous state)

Use if migration fails verification or site cannot operate.

### Prerequisites

- [ ] hAP lite hardware available with intact config or backup
- [ ] `hap-lite-migration.rsc` or `.backup` from source unit
- [ ] ESP32 SD **not** factory-reset
- [ ] ISP cabling documented

### Rollback steps

| Step | Action |
|------|--------|
| 1 | Announce service restore |
| 2 | Power off hEX; disconnect ISP and LAN |
| 3 | Reconnect ISP → hAP lite WAN (`ether1-WAN`) |
| 4 | Connect ESP32 → hAP lite `ether2-ESP32` |
| 5 | Reconnect customer Wi-Fi path (hAP wlan1 or prior wiring) |
| 6 | Power hAP lite; wait for boot |
| 7 | Verify `10.10.10.1` and `10.20.0.1` on hAP |
| 8 | Restore ESP32 `/config/router.json` from backup if edited for hEX |
| 9 | Reboot ESP32 |
| 10 | Admin → MikroTik Online; test coin → portal → internet |
| 11 | Document failure; keep hEX for bench retest |

### hEX-only partial rollback (keep hardware, undo Renz-Fi config)

If you must revert hEX without restoring hAP:

1. `/export file=hex-before-rollback` (current broken state)
2. `/import file-name=hex-pre-renzfi-migration` (if taken before migration)
3. Or `/system reset-configuration no-defaults=yes` **only if explicitly approved** — destructive; requires full manual reconfiguration

**Prefer:** restore pre-migration export over factory reset.

See also: `KonekSik-fi Migration/ROLLBACK_PLAN.md` (hAP lite restoration path).

---

## OUTPUT 3 — Source-to-target command mapping

Complete inventory of `hap-lite-migration.rsc` (RouterOS 7.20.7 export, 101 lines).

| # | Source command | Classification | Target command / action | Reason |
|---|----------------|----------------|-------------------------|--------|
| 1 | `/interface bridge add ... name=bridgeGuest admin-mac=D0:EA:11:24:57:D6 auto-mac=no` | TARGET-REPLACEMENT | `/interface bridge add name=bridgeGuest auto-mac=yes` | Bridge name portable; hAP admin-mac is hardware-specific |
| 2 | `/interface ethernet set ether1 name=ether1-WAN` | PORTABLE | Same | WAN port naming preserved |
| 3 | `/interface ethernet set ether2 name=ether2-ESP32` | PORTABLE | Same | Management port naming preserved |
| 4 | `/interface list add name=RENZFI_PRODUCTION` | PORTABLE | Same (comment updated) | List object portable |
| 5 | `/interface wireless security-profiles set default supplicant-identity` | HAP-LITE-ONLY | **Omit** | hEX has no wireless |
| 6 | `/interface wireless security-profiles add RenzFi-Open` | HAP-LITE-ONLY | **Omit** | Open AP profile was for wlan1 |
| 7 | `/interface wireless set wlan1 mode=ap-bridge ssid=MKonekSik-fi security-profile=RenzFi-Open` | HAP-LITE-ONLY | **Omit** | No wlan1 on hEX; SSID on external AP |
| 8 | `/ip hotspot profile add RenzFi-Hotspot dns-name=wifi.renz-fi.local hotspot-address=10.20.0.1 login-by=cookie,http-chap,http-pap` | PORTABLE (+ inferred) | Same + `html-directory=hotspot` | Profile portable; html-directory required by firmware but missing from export |
| 9 | `/ip hotspot user profile add test1 rate-limit=5M/5M` | PORTABLE | Same (idempotent add) | Rate profile portable |
| 10 | `/ip hotspot user profile add renzfi-speed-10m-10m rate-limit=10M/10M` | PORTABLE | Same | Rate profile portable |
| 11 | `/ip hotspot user profile add renzfi-speed-15m-15m rate-limit=15M/15M` | PORTABLE | Same | Rate profile portable |
| 12 | `/ip hotspot user profile add renzfi-speed-50m-50m rate-limit=50M/50M` | PORTABLE | Same | Rate profile portable |
| 13 | `/ip pool add pool-guest ranges=10.20.0.10-10.20.0.254` | PORTABLE | Same | Guest pool preserved |
| 14 | `/ip pool add pool-mgmt ranges=10.10.10.2-10.10.10.20` | PORTABLE | Same | Management pool preserved |
| 15 | `/ip dhcp-server add dhcp-guest interface=bridgeGuest pool-guest` | PORTABLE | Same | Guest DHCP on bridge |
| 16 | `/ip dhcp-server add dhcp-mgmt interface=ether2-ESP32 pool-mgmt` | PORTABLE | Same | Management DHCP isolated on ESP32 port |
| 17 | `/ip hotspot add hotspot-renzfi interface=bridgeGuest pool-guest profile=RenzFi-Hotspot` | PORTABLE | Same | HotSpot on bridge — core Renz-Fi design |
| 18 | `/interface bridge port add bridge=bridgeGuest interface=ether3` | PORTABLE | Same | LAN guest member |
| 19 | `/interface bridge port add bridge=bridgeGuest interface=ether4` | PORTABLE | Same | LAN guest member |
| 20 | `/interface bridge port add bridge=bridgeGuest interface=wlan1` | HAP-LITE-ONLY | `/interface bridge port add bridge=bridgeGuest interface=ether3` (AP uplink) | wlan1 replaced by external AP Ethernet ingress |
| 21 | `/interface list member add list=RENZFI_PRODUCTION interface=wlan1` | TARGET-REPLACEMENT | `interface=ether3` | Marks guest ingress port instead of radio |
| 22 | `/interface wireless cap set bridge=bridgeGuest interfaces=wlan1` | HAP-LITE-ONLY | **Omit** | CAP/wireless controller not applicable |
| 23 | `/ip address add 10.10.10.1/24 interface=ether2-ESP32` | PORTABLE | Same | Management gateway |
| 24 | `/ip address add 10.20.0.1/24 interface=bridgeGuest` | PORTABLE | Same | Guest gateway |
| 25 | `/ip dhcp-client add interface=ether1-WAN` | PORTABLE | Same + `add-default-route=yes` | WAN DHCP; export noted interface inactive during backup |
| 26 | `/ip dhcp-server lease add 10.10.10.2 mac-address=A2:CB:8F:F8:97:B5` | PORTABLE | Same (verify MAC) | ESP32 reservation critical |
| 27 | `/ip dhcp-server network add 10.10.10.0/24 gateway=10.10.10.1 dns=10.10.10.1` | PORTABLE | Same | Management DHCP network |
| 28 | `/ip dhcp-server network add 10.20.0.0/24 gateway=10.20.0.1 dns=8.8.8.8,1.1.1.1,10.20.0.1` | PORTABLE | Same | Guest DHCP network |
| 29 | `/ip dns set allow-remote-requests=yes servers=1.1.1.1,8.8.8.8` | PORTABLE | Same | DNS forwarding |
| 30 | `/ip firewall filter add input accept established,related,untracked` | PORTABLE | Same | Baseline input acceptance |
| 31 | `/ip firewall filter add forward accept established,related` | PORTABLE | Same | Return traffic |
| 32 | `/ip firewall filter add input accept src=10.10.10.2` | PORTABLE | Same | ESP32 trusted |
| 33 | `/ip firewall filter add input accept icmp src=10.10.10.2` | PORTABLE | Same | ESP32 ICMP |
| 34 | `/ip firewall filter add input accept tcp/8728 src=10.10.10.2` | PORTABLE | Same | ESP32 API access |
| 35 | `/ip firewall filter add passthrough unused-hs-chain (disabled)` | PORTABLE | **Omit** (HotSpot creates as needed) | Placeholder only |
| 36 | `/ip firewall nat add passthrough unused-hs-chain (disabled)` | PORTABLE | **Omit** | Placeholder only |
| 37 | `/ip firewall nat add masquerade out=ether1-WAN comment=Renz-Fi Internet NAT` | PORTABLE | Same | General WAN NAT |
| 38 | `/ip firewall nat add masquerade out=ether1-WAN src=10.20.0.0/24 comment=Renz-Fi Guest Internet NAT` | PORTABLE | Same | Guest-specific NAT (redundant but preserved) |
| 39 | `/ip hotspot user add test password=1234` | UNSAFE | **Manual / omit** | Test credential; do not publish in migration script |
| 40 | `/ip hotspot user add voucher MAC user password=... profile=renzfi-speed-50m-50m` | UNSAFE | **Manual / omit** | Operational voucher user; restore from secured backup if needed |
| 41 | `/ip hotspot walled-garden add disabled placeholder` | PORTABLE | **Omit** | Disabled placeholder |
| 42 | `/ip hotspot walled-garden ip add dst=10.10.10.2 action=accept` | PORTABLE | Same | Captive portal API access to ESP32 |
| 43 | `/ip route add 10.10.10.2/32 gateway=10.10.10.2%ether2-ESP32` | DUPLICATE | **Omit** | Redundant connected route |
| 44 | `/ip route add 10.10.10.20/32 gateway=bridgeGuest` | UNKNOWN | **Manual review** | Unclear purpose; contradicts management subnet design |
| 45 | `/ip route add 10.10.10.2/32 gateway=ether2-ESP32` | DUPLICATE | **Omit** | Duplicate of #43 |
| 46 | `/ip service set api address=10.10.10.0/24,10.20.0.0/24` | PORTABLE | Same + `disabled=no port=8728` | API restriction preserved; export did not show enable state |
| 47 | `/system script add renzfi-hotspot-ready source=":log info ..."` | PORTABLE | Same | Renz-Fi managed marker |

### hEX-only additions (not in source)

| Target command | Reason |
|----------------|--------|
| `/interface bridge port add bridge=bridgeGuest interface=ether5` | hEX lite fifth port — guest bridge member for expansion |
| `html-directory=hotspot` on profile | Required by firmware; inferred from provisioning code |

---

## Deployment checklist

- [ ] Copy `hap-lite-to-hex-lite-migration.rsc` to hEX Files
- [ ] Export backup before import
- [ ] Verify ESP32 W5500 MAC matches lease
- [ ] Import migration script
- [ ] Run validation script
- [ ] Create API user (if missing)
- [ ] Upload captive portal to `hotspot/`
- [ ] Configure external AP (bridge mode, DHCP/NAT off)
- [ ] Wire ports per mapping table
- [ ] Run physical acceptance tests 1–16
- [ ] Admin → Synchronize Router
- [ ] Register external AP in Admin → Access Points
- [ ] Export final config: `/export file=hex-lite-production-YYYY-MM-DD`

---

## Final status block

```
FORENSIC STATUS:
    PROVEN — split management/guest topology, IP plan, HotSpot-on-bridge, ESP32 API path, walled garden
    INFERRED — html-directory=hotspot, ether3 as primary AP port, static /32 routes unnecessary
    UNKNOWN — 10.10.10.20/32 via bridgeGuest route purpose

MIGRATION STATUS:
    READY WITH MANUAL REVIEW

HARDWARE MAPPING:
    hAP lite (RB941-2nD) → hEX lite (RB750r2)

WIRELESS:
    EXTERNAL AP MAPPING (ether3 primary guest ingress; SSID on AP UI)

ESP32 MANAGEMENT:
    VERIFIED (in source export and migration script — physical test pending)

GUEST NETWORK:
    VERIFIED (bridgeGuest 10.20.0.0/24 — physical test pending)

HOTSPOT:
    VERIFIED (hotspot-renzfi / RenzFi-Hotspot — portal upload manual)

ROUTEROS API:
    VERIFIED (restriction preserved; API user creation manual)

FIREWALL:
    VERIFIED (ESP32 trust rules preserved)

NAT:
    VERIFIED (both masquerade rules preserved)

ROUTES:
    MANUAL REVIEW (duplicates omitted; 10.10.10.20 route unexplained)

SCRIPT:
    CREATED — hap-lite-to-hex-lite-migration.rsc

VALIDATION:
    NOT RUN (script created; apply on device)

PHYSICAL VALIDATION:
    NOT DONE — scripts not yet applied to hEX lite hardware
```
