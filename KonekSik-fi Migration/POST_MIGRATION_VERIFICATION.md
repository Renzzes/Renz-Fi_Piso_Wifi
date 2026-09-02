# Post-Migration Verification

Run within **2 hours** of cutover, then again after **24 hours**.

---

## A. Infrastructure

| # | Test | Pass |
|---|------|------|
| A1 | hEX WAN online (`/ping 8.8.8.8` from RouterOS) | ☐ |
| A2 | ESP32 Ethernet link up (Admin dashboard / serial `eth_link=up`) | ☐ |
| A3 | ESP32 reachable from technician PC on guest LAN | ☐ |
| A4 | External AP online (Admin → Access Points → Check) | ☐ |
| A5 | Phone associates to **AP SSID** (not MikroTik — hEX has no Wi‑Fi) | ☐ |

---

## B. HotSpot & portal

| # | Test | Pass |
|---|------|------|
| B1 | Phone gets DHCP IP from guest pool | ☐ |
| B2 | Browser redirects to captive portal (HotSpot) | ☐ |
| B3 | Portal UI loads (banner, coin modal) | ☐ |
| B4 | Browser devtools: API calls reach ESP32 (`/api/portal/session`) | ☐ |
| B5 | No CORS / wrong IP in `renzfi-app.js` | ☐ |

---

## C. Coin & session (production)

| # | Test | Pass |
|---|------|------|
| C1 | Insert coin → credits increase on portal | ☐ |
| C2 | Done Paying → RouterOS user created/updated | ☐ |
| C3 | Internet access granted | ☐ |
| C4 | Session timer counts down | ☐ |
| C5 | Sale recorded in Admin → Sales (Today) | ☐ |
| C6 | Add-time (second coin same session) works | ☐ |

---

## D. RouterOS API (Renz-Fi ↔ hEX)

| # | Test | Pass |
|---|------|------|
| D1 | Admin → Dashboard MikroTik connectivity **Online** | ☐ |
| D2 | Synchronize Router completes (202 → success) | ☐ |
| D3 | Serial: `[ros-health] state=HEALTHY` after jobs | ☐ |
| D4 | Activate latency acceptable (vs old hAP baseline) | ☐ |

---

## E. Admin dashboard

| # | Test | Pass |
|---|------|------|
| E1 | Login to Admin | ☐ |
| E2 | `/api/status` loads (no persistent ETH_DMA_LOW) | ☐ |
| E3 | Active users count reasonable | ☐ |
| E4 | Storage health SD **Ready** (if SD present) | ☐ |
| E5 | No unresolved SPIFFS conflicts (after SD sync) | ☐ |

---

## F. Load spot-check (optional)

| # | Test | Pass |
|---|------|------|
| F1 | 3+ phones online simultaneously | ☐ |
| F2 | Coin insert during active sessions | ☐ |
| F3 | Admin refresh during portal use | ☐ |

---

## Sign-off

| Role | Name | Date | Notes |
|------|------|------|-------|
| Owner | | | |
| Installer | | | |

**Issues found:**

```
_________________________________________________________________
_________________________________________________________________
```

**Migration accepted:** ☐ Yes  ☐ No — rollback per ROLLBACK_PLAN.md
