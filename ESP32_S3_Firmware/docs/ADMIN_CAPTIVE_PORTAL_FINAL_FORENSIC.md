# Captive Portal Final Forensic — Serving / Routing / Security

## Hardware-proven layers (do not reopen)

```
SSID → wireless → bridgeGuest → DHCP → Hotspot (hotspot-renzfi)
→ /ip/hotspot/host shows unauthenticated clients (H, not A)
```

ESP32 appliance LAN: `10.10.10.2`  
Guest Hotspot LAN: `10.20.0.0/24` (`hotspot-address=10.20.0.1`)  
`html-directory=hotspot`, `login-by=cookie,http-chap,http-pap`

## Exact break point

**Hotspot interception path can work; the remaining source-proven defects are:**

1. **ESP32 Admin-plane exposure to Hotspot guests** (PROVEN)  
   Guests reach `10.10.10.2` via MikroTik (walled-garden).  
   `HttpPlaneGate` classified only by ESP32 *local* interface (`via=ETH` → `plane=production`).  
   Both admin laptops on `10.10.10.0/24` and guests on `10.20.0.0/24` therefore received Admin SPA (`/index.html`) and Admin APIs.

2. **Customer portal must be served by MikroTik `hotspot/login.html`**, not ESP32 Admin.  
   Source `portal/login.html` does **not** redirect to `10.10.10.2` or `/index.html`.  
   It renders the customer UI and loads local `renzfi-app.js` / `renzfi-style.css` / `md5.js`, preserving RouterOS `$(...)` tokens.

3. **Portal API base URL (second stage)**  
   Official build substitutes `__RENZFI_APPLIANCE_BASE_URL__` → `http://10.10.10.2`.  
   If the file on MikroTik still has the placeholder, JS falls back to `window.location.origin` (MikroTik) and `/api/portal/*` fails after the page opens.

## MikroTik login.html analysis (source)

| Question | Answer |
|----------|--------|
| Renders portal itself? | **Yes** |
| Redirect to 10.10.10.2? | **No** |
| Redirect to /index.html? | **No** |
| Loads renzfi-app.js locally? | **Yes** (`<script src="renzfi-app.js">`) |
| Uses placeholder? | In `portal/renzfi-app.js` source yes; build must substitute |
| RouterOS tokens preserved? | **Yes** — `$(ip)`, `$(mac)`, `$(link-login-only)`, `$(link-orig)`, `$(chap-id)`, `$(chap-challenge)`, `$(if chap-id)` |

## ESP32 routing analysis (source)

```
Guest 10.20.0.251 → MikroTik → ESP32 ETH 10.10.10.2
HttpPlaneGate: localIP == ETH → Production
StaticFileServer / ApiServer: served Admin SPA + Admin APIs
```

**Security finding:** Customer clients had unintended Admin-plane exposure (hardware log: `/index.html`, `/api/router/*`, `/api/system/*`, auth login success).

## Fix applied

`HttpPlaneGate::AccessClass`:

| Access | Rule |
|--------|------|
| Setup | local = 192.168.4.1 |
| Management | production plane + remote in ESP32 ETH subnet |
| CustomerPortal | production plane + remote **outside** ETH subnet |

- Admin SPA / privileged APIs → `ensureManagementClient`
- `/api/portal/*`, portal assets CORS OPTIONS → `ensureProductionPlane` (guests allowed)
- `/api/health` denied for CustomerPortal

No Hotspot topology changes. No RouterOS polling.

## Customer access policy (after fix)

**Allow (guest):** `/api/portal/*`, `/api/portal/assets/*`, `OPTIONS /api/`, `/api/events` (SSE if registered)

**Deny (guest):** `/`, `/index.html`, `/login`, `/dashboard`, `/admin`, `/assets/*` (admin SPA), `/api/router/*`, `/api/system/*`, `/api/coin/*`, `/api/auth/*`, other privileged APIs, `/api/health`

**Management (`10.10.10.0/24`):** normal Admin SPA/API access

## Captive redirect vs OS popup

- **Plain HTTP:** MikroTik Hotspot intercepts → `html-directory=hotspot` → `login.html`
- **OS captive popup:** MikroTik should intercept OS probes; ESP32 `/generate_204` etc. are Management-AP only and return 404 on ETH — by design

## Hardware validation still required

Confirm `http://neverssl.com` serves MikroTik `login.html`, guest cannot open Admin `/index.html`, portal API works with substituted base URL, admin still works from `10.10.10.0/24`.
