# Renz-Fi Router Edition (OpenWrt)

**Status:** Foundation (source tree). Not a replacement for the ESP32 + MikroTik appliance baseline.  
**Companion to:** Appliance Edition (`ESP32_S3_Firmware/`, MikroTik Hotspot).  
**Admin contract:** existing REST + SSE (`/api/health`, `/api/status`, `/api/events`, job APIs).

Router Edition runs **Renz-Fi Core on the router**. There is no ESP32 controller and no MikroTik RouterOS in this product line. The OpenWrt device is the network authority, the application host, and the internet-grant plane.

Appliance Edition is unchanged. This document does not add Setup Wizard steps, change the frozen six-step installer, or move Admin into the operational chain.

---

## Why a second edition

Appliance Edition is the production Piso WiFi product:

```
Internet → MikroTik RouterOS (Hotspot / DHCP / gateway)
                ↓
         ESP32-S3 + W5500 (Renz-Fi Core)
```

That split is correct when the site already has MikroTik. It is the wrong shape when the operator wants a **single OpenWrt router** that already has flash, Ethernet, Wi-Fi, firewall, and GPIO.

Router Edition collapses the stack onto that router:

```
Internet → OpenWrt (firewall / Wi-Fi / captive portal engine)
                ↓
         Renz-Fi Lua Core  (/opt/renzfi/lua)
         Admin PWA         (/www/renzfi)
         Guest portal      (/www/renzfi/portal)
```

Same product behavior (coin, voucher, session, sales, Admin dashboard). Different host.

---

## What Router Edition is

| Layer | Host | Role |
|-------|------|------|
| Network / DHCP / NAT / Wi-Fi | OpenWrt (`netifd`, `dnsmasq`/`odhcpd`, `hostapd`) | Network authority |
| Captive / client isolation | openNDS or nodogsplash (preferred), nftables fallback | Pre-auth splash + MAC grant |
| Renz-Fi Core | Lua on `uhttpd-mod-lua` + `renzfi-tick` | Coin, session, sales, vouchers, EventBus |
| Admin | Same React PWA as Appliance Edition | Optional management client |
| Guest portal | Existing `portal/` sources staged beside Admin | Coin / voucher UI |

Core **must run with Admin closed**. Coin pulses, session expiry, sales persistence, and internet grant do not depend on a browser tab.

---

## What Router Edition is not

- Not an ESP32 firmware port to MIPS/ARM C++.
- Not a new Admin product. The PWA is built with `npm run build:openwrt` and talks to the same `/api/*` envelope.
- Not a MikroTik driver. The ESP32 `OpenWRTDriver` stub in Appliance Edition remains a **client-of-OpenWrt** placeholder for the ESP32 line. Router Edition does not use that driver; Core *is* the router.
- Not a Setup Wizard change. The frozen six-step ESP32 installer is Appliance-only. Router Edition is installed as an OpenWrt package (`openwrt/Makefile`).
- Not a path to copy router passwords into `localStorage`. Auth is a `HttpOnly` session cookie (`renz_session`), same as firmware.

---

## Admin / Core isolation (still binding)

The workspace Admin isolation rules apply to Lua Core with the host names swapped:

| Appliance Edition | Router Edition |
|-------------------|----------------|
| ESP32 Core | Lua Core on OpenWrt |
| MikroTik Hotspot grant | openNDS / nodogsplash / nftables grant |
| SD / SPIFFS | overlay `/etc/renzfi` + `/opt/renzfi/data` (USB bind-mount optional) |
| `GET /api/status` RAM cache | `GET /api/status` from Lua store + ubus observation |
| Stale RouterOS cache → `POST /api/router/cache/sync` worker | Local ubus/uci refresh only. **Never RouterOS.** |

Connect flow is unchanged:

`CONNECT` → `POST /api/auth/login` → `GET /api/status` → existing Dashboard.

Do **not**:

- Open LuCI, SSH, or a vendor API from the browser on every Admin connect.
- Poll the datapath from Admin. Use cached `/api/status` + SSE.
- Add `/api/admin/sync` that talks to the firewall.
- Claim “credentials synchronized” unless a real credential write happened on Core.
- Make coin, session, sale, or internet grant wait for an SSE client. If no Admin is connected, EventBus is a no-op.

Sales order is unchanged: persist first, then emit `sale.created` / `sales.changed`.

---

## Repository layout

```
docs/ROUTER_EDITION.md              ← this file
vite.config.openwrt.ts              ← Admin PWA build (no SPIFFS name limits)
scripts/stage-openwrt-www.mjs       ← dist-openwrt + portal/ → package www/
openwrt/Makefile                    ← OpenWrt package `renzfi`
openwrt/files/etc/init.d/renzfi     ← tick / session-expiry daemon
openwrt/files/etc/config/renzfi     ← UCI package config
openwrt/files/etc/uci-defaults/     ← attach /api Lua handler to uhttpd
openwrt/files/opt/renzfi/lua/       ← Lua Core
openwrt/files/www/renzfi/           ← staged Admin + portal (generated)
```

Appliance trees (`ESP32_S3_Firmware/`, `deployment/mikrotik-hotspot/`, Setup Wizard) are not part of this package.

---

## HTTP surface

Lua Core exposes the same JSON envelope as firmware / the Node simulator:

```json
{ "success": true, "data": { }, "message": "OK" }
```

Errors:

```json
{ "success": false, "error": "…", "code": "INVALID_CREDENTIALS" }
```

### Public (no session)

| Method | Path | Notes |
|--------|------|--------|
| GET | `/api/health` | Inventory + session flag. No secrets. |
| POST | `/api/auth/login` | Sets `renz_session`. |
| POST | `/api/auth/logout` | Clears cookie. |
| GET | `/api/portal/session` | Guest session by `mac`. |
| POST | `/api/portal/start-coin-session` | Open coin window. |
| POST | `/api/portal/done-paying` | Persist sale, then grant internet. |
| POST | `/api/portal/redeem-voucher` | Redeem + grant. |
| GET | `/api/portal/branding` | Banner / music flags. |
| GET | `/api/portal/rates` | Coin promo rates. |

### Authenticated Admin

| Method | Path | Notes |
|--------|------|--------|
| GET | `/api/status` | Authoritative Core snapshot. Local router observation, not RouterOS. |
| GET | `/api/events` | SSE. No-op publisher when zero clients. |
| GET | `/api/sales/*` | Today / week / month / history / records / export. |
| GET/POST | `/api/vouchers` | Generate 1–20 (default 3), delete. |
| GET | `/api/users`, `/api/users/active` | Active sessions. |
| POST | `/api/users/pause`, `resume`, `disconnect` | Session control + deauth. |
| GET/PUT | `/api/coin/settings` | GPIO pin, pulses-per-coin, enable. |
| GET | `/api/router/cache` | Local observation (`stale`, `driverId: openwrt`). |
| POST | `/api/router/cache/sync` | Refresh ubus/uci cache. HTTP 202. Never RouterOS. |
| GET | `/api/system/network` | LAN / WAN / wireless from ubus. |
| POST | `/api/system/reboot` | `reboot` via procd. |

`/api/router/settings` on this edition returns the **local** OpenWrt identity (LAN IP, SSID, driver `openwrt`). It does not accept or return a MikroTik password.

---

## Internet grant

Appliance Edition: Core tells MikroTik Hotspot to authorize a MAC.

Router Edition: Core tells the **local** captive engine:

1. **openNDS / nodogsplash** (preferred): `ndsctl auth <mac>` after a successful persist.
2. **nftables fallback**: add the client MAC to set `renzfi_auth` used by a prerouting accept rule (see `hotspot.lua`).
3. Expiry / pause / disconnect: `ndsctl deauth <mac>` or delete the set element.

Grant happens **after** sales/session persist succeeds. A firewall failure is logged; the sale is not rolled back (same persist-first rule as firmware). Re-grant on tick if the session is still active and the MAC is associated.

---

## Storage

| Path | Persistence | Contents |
|------|-------------|----------|
| `/etc/renzfi/` | overlay (survives reboot) | Admin password hash, settings, coin config |
| `/opt/renzfi/data/` | overlay, bind-mount to USB if present | Sales JSONL, vouchers, sessions |
| `/tmp/renzfi/` | tmpfs | SSE client marks, runtime pid, observational cache |

There is no SPIFFS and no W5500. “SD degraded” in `/api/status` maps to overlay vs USB: if USB was configured and is missing, Core continues on overlay and reports storage degraded. Coin/session/portal/sales must not stop.

JSON writes are rename-atomic (`*.tmp` → target) on the same filesystem.

---

## EventBus

`eventbus.lua` mirrors firmware `EventBus.cpp`:

- `emit(name, json)` returns immediately when `/tmp/renzfi/sse/` has no client marks.
- `/api/events` registers a client mark, streams `text/event-stream`, heartbeats `event: ping`, unregisters on disconnect.
- Sales: persist → `sale.created` + `sales.changed`. Admin patches UI from `sale.created` when connected.

uhttpd Lua workers are request-scoped. Client presence is therefore a tmpfs file, not an in-process list. That is intentional so API workers and the SSE worker share one truth.

---

## Coin hardware

Optional GPIO via `/sys/class/gpio` or `gpioctl`, configured in UCI:

```
config coin 'coin'
    option enabled '1'
    option gpio_pin '18'
    option pulses_per_coin '1'
    option active_low '1'
```

`renzfi-tick` (procd, `opt/renzfi/lua/tick.lua`) polls the pin, debounces, credits the open coin window, and never talks to Admin. No coin hardware: voucher-only mode; `/api/health` reports `coin.enabled=false`.

RGB / W5500 / Management AP are Appliance-only. Router Edition `/api/system/rgb` returns `enabled: false`. Management AP is the router’s existing admin SSID or LAN — not `192.168.4.1` ESP32 AP.

---

## Build and install

### Admin + portal payload

```bash
npm run build:openwrt
```

This builds the PWA with `vite.config.openwrt.ts` (readable hashed names; OpenWrt overlay is not SPIFFS) and stages:

- `dist-openwrt/` → `openwrt/files/www/renzfi/`
- `portal/` required HTML/JS/CSS (+ recommended banner) → `openwrt/files/www/renzfi/portal/`

Generated www files are gitignored. Sources stay in `src/` and `portal/`.

### OpenWrt package

From an OpenWrt SDK / buildroot with this tree as a feed package:

```bash
echo "src-link renzfi /path/to/Renz-Fi_Piso_Wifi" >> feeds.conf
./scripts/feeds update renzfi
./scripts/feeds install renzfi
make package/renzfi/compile
```

On a running router (after `opkg install` of the `.ipk` plus `uhttpd-mod-lua`, `lua`, `libuci-lua`):

```bash
/etc/init.d/uhttpd restart
/etc/init.d/renzfi enable
/etc/init.d/renzfi start
```

Default listen: existing `uhttpd` instance, Lua prefix `/api` → `/opt/renzfi/lua/main.lua`, document root files overlayed under `/www/renzfi`. LuCI, if installed, should be moved off `/` (package default: LuCI on port 8080) so guests and Admin share port 80 with the captive portal.

Default Admin password: `admin` (`mustChangePassword: true` until changed).

---

## Runtime loop (`renzfi-tick`)

Every 5 seconds, without Admin:

1. Read coin GPIO (if enabled).
2. Expire sessions whose `expiresAt` has passed → persist → deauth MAC.
3. Re-assert nftables/openNDS auth for still-active MACs (idempotent).
4. Refresh observational WAN/LAN cache from ubus into `/tmp/renzfi/router-cache.json`.
5. EventBus heartbeat (no-op if no SSE clients).

This is the OpenWrt equivalent of firmware `loopTask`. Watchdog: procd `respawn`.

---

## Relationship to Appliance `OpenWRTDriver`

`ESP32_S3_Firmware/src/router/drivers/OpenWRTDriver` is a **foundation stub** so an ESP32 appliance could one day *control* a separate OpenWrt AP. That is a different product (ESP32 Core + OpenWrt radio).

Router Edition inverts that: OpenWrt **is** Core. Do not wire the ESP32 driver into this package. Do not share MikroTik credential files. Do not run both editions on one site as a single control plane.

---

## Out of scope (this foundation)

- Building a full OpenWrt disk image / `imagebuilder` profile for a named board.
- Porting the frozen ESP32 Setup Wizard to LuCI.
- Cloud / fleet / remote management (same limitation as Appliance Edition).
- TP-Link / Ruijie / MikroTik as underlays of Router Edition.
- Claiming field validation on a physical OpenWrt board until flashed and tested.

---

## Verification (host)

Lua Core is loadable without a router:

```bash
lua -e 'package.path="./openwrt/files/opt/renzfi/lua/?.lua;"..package.path; require("json"); require("store")'
```

Admin pipeline (needs Node deps):

```bash
npm run build:openwrt
test -f openwrt/files/www/renzfi/index.html
```

Do not treat those checks as device certification.
