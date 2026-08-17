# WAN Readiness / Upstream Internet Provisioning — Forensic

## 1. FORENSIC RESULT

Hardware proved captive-portal auto-redirect works once WAN DHCP is bound and an **active** default route exists.

Firmware audit: Renz-Fi **never created** MikroTik WAN DHCP clients, never renamed `ether1-WAN`, and never installed `0.0.0.0/0 → 192.168.60.1` (“Temporary upstream default route”). Those objects were **outside** this codebase (manual / factory / QuickSet).

Guest Hotspot/LAN provisioning is independent of WAN and correctly does **not** require Internet.

## 2. ROOT CAUSE (hardware incident)

Inactive/absent upstream default route + no WAN DHCP binding → MikroTik could not resolve/reach Internet → OS connectivity probes failed → no automatic captive UI.

Hotspot itself was healthy (`host` present, manual `10.20.0.1/login` worked).

## 3. PREVIOUS WAN BEHAVIOR

| Area | Behavior |
|------|----------|
| `/api/status.internet` | Stub: `known=false` |
| Admin WAN card | Hidden when unknown |
| Finish | No WAN gate; does not fail without Internet |
| Foundation apply | Guest DHCP **server** only; NAT deferred; no WAN DHCP **client** |

## 4. CURRENT WAN PROVISIONING PATH (after minimal fix)

On **Synchronize Router** / **Test** only (`collectCacheSnapshot` / `testSettings`), same open RouterOS session:

1. Observe `ether1-WAN` link  
2. Observe `/ip/dhcp-client` for that interface  
3. Ensure DHCP client **only if** interface exists, no client, and no static address  
4. Observe active `0.0.0.0/0`  
5. Remove **only** route with exact comment `Temporary upstream default route` when DHCP is bound and another active default exists  
6. One bounded `/ping address=8.8.8.8 count=1` when default route is active  

Result cached under `observation.wan` and exposed on `/api/status.wan` + `internet.known`.

## 5. DHCP CLIENT CONTRACT

Target (automatic WAN mode):

```
/ip dhcp-client
interface=ether1-WAN
add-default-route=yes
use-peer-dns=yes
comment=RENZFI: WAN DHCP client
```

- Idempotent: correct existing client → no churn  
- Static address on `ether1-WAN` → **do not** create DHCP client  
- Disabled client with `RENZFI:` comment → enable only  

## 6. DEFAULT ROUTE CONTRACT

- Prefer DHCP-owned dynamic default (`add-default-route=yes`)  
- Never hardcode `192.168.60.1` / `192.168.50.1` / `192.168.1.1`  
- Stale temporary static removed only by exact comment match  

## 7. TEMPORARY ROUTE FINDING

| Question | Answer |
|----------|--------|
| Who created it in firmware? | **Nobody** — zero source hits |
| Why on hardware? | External/manual leftover |
| Can firmware recreate it? | **No** (never implemented) |
| What changed? | Sync/Test may **remove** that exact-comment route when DHCP owns routing |

## 8. WAN STATE MODEL

`observation.wan` / `/api/status.wan`:

`known`, `interface`, `link`, `dhcp`, `ip`, `gateway`, `defaultRoute`, `internet`, `dns`, `note`

Hotspot `observation.hotspotStatus` remains independent.

## 9. HOTSPOT VS WAN SEPARATION

Hotspot AVAILABLE even if WAN offline. Diagnostics must not say “Captive Portal Failed” for upstream outage.

## 10. ROUTEROS COMMAND BUDGET

| Context | Commands |
|---------|----------|
| Idle Admin / Portal | **0** |
| Sync/Test (added) | ~3–6 in **existing** session |
| Continuous WAN poll | **None** |

## 11. CPU / STABILITY

No idle polling, no heartbeat WAN checks, no portal/session WAN checks, no reconnect loops.

## 12. FILES CHANGED

See implementation report section in the chat response.

## 13. NO-CHANGE AREAS

Captive portal UI, coin/promo/session, wireless SSID path, worker gate/cooldown/pacing, W5500, Finish Hotspot stages (still not Internet-gated).

## 14–17. BUILD / VALIDATION / VERDICT

See chat final report. Hardware validation required after Sync.
