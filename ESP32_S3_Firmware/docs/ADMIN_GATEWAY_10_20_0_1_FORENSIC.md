# Admin Gateway `10.20.0.1/admin` — Forensic

**Date:** 2026-08-06  
**Mode:** Forensic only — **no functional source changes**  
**Constraint:** Do not disturb working captive portal / WAN / Hotspot path

## Product intent

| URL | Role |
|-----|------|
| `http://10.20.0.1/login` | Customer captive portal (MikroTik Hotspot) |
| `http://10.20.0.1/admin` | Owner Admin Dashboard entry |

ESP32 may remain at `10.10.10.2` internally. Owner should not need that address as the *primary* entry bookmark.

## Proven current ownership

| Path | Owner today |
|------|-------------|
| `10.20.0.1/` / `/login` | **MikroTik Hotspot** (`html-directory=hotspot`, wproxy) |
| `10.20.0.1/admin` | **Not implemented** — no Hotspot `admin` servlet file in deployment |
| `10.10.10.2/admin`, `/login`, `/dashboard`, `/assets/*` | **ESP32** Admin SPA (SPIFFS) |
| `10.10.10.2/api/portal/*` | **ESP32** (allowed for Hotspot guests via walled-garden) |
| `10.10.10.2/api/*` (privileged) | **ESP32** + `RENZFI_PROD_GATE` = Management LAN only |
| `10.10.10.2/api/auth/login` | **ESP32** + appliance plane only (not Management-IP gated) |

## Why `10.10.10.2` from guest SSID fails Admin

Hardware: guest gets `CUSTOMER_PORTAL_RESTRICTED`.

Source:

- `HttpPlaneGate::classifyAccess` — remote outside ESP32 ETH subnet → `CustomerPortal`
- `HttpPlaneGate::ensureManagementClient` → `rejectCustomerPortal`
- Callers: `WebServerManager` `/admin`, `StaticFileServer`, `RENZFI_PROD_GATE` privileged APIs

This is intentional guest isolation, not a routing failure. ESP32 is reachable (walled-garden `10.10.10.2/32`).

## Admin SPA / API (source facts)

- Vite: **no** `base: '/admin/'` — assets `/assets/*`, React paths `/login`, `/dashboard` (BrowserRouter, no basename)
- Direct Mode API: relative `/api/*` same-origin (`embeddedApi.ts`, empty `VITE_API_BASE`)
- Auth: HttpOnly cookie, `Path=/`, `SameSite=Lax` (`AuthManager::cookieHeader`)
- Fetch: `credentials: "include"` (`src/services/api.ts`)
- CORS: `Access-Control-Allow-Origin: *` (`WebResponse::addCorsHeaders`) — **incompatible** with credentialed cross-origin cookies

## Reverse proxy

RouterOS Hotspot/www is **not** an nginx-style reverse proxy for `/admin/*` → ESP32. Using Web Proxy for SPA+API would risk CPU and is not supported by current Renz-Fi architecture. **Not recommended.**

## Serving full SPA from Hotspot `/admin`

Conditional only:

- Would need Vite `base: '/admin/'`, upload bundles to MikroTik Files
- Cross-origin API to `10.10.10.2` breaks cookie auth under current SameSite/CORS
- Larger change than a launcher + gate fix

## Recommended smallest architecture (do not implement in this pass)

**Option A + auth-based ESP32 access (not subnet trust):**

1. MikroTik Hotspot file: `admin.html` (or equivalent) served as owner entry at `10.20.0.1/admin` — static redirect/meta-refresh to `http://10.10.10.2/admin` (or `/login`)
2. Ensure Hotspot serves that local page without infinite redirect to `/login` (hardware validate)
3. ESP32: allow **CustomerPortal** remotes to load Admin SPA + call privileged APIs **only after** `AuthManager` session (replace IP Management gate with auth for those routes; do **not** mark `10.20.0.0/24` as trusted Management)
4. Keep Hotspot Internet auth separate (walled-garden ≠ Hotspot login)

Address bar after hop: `10.10.10.2/...` — entry bookmark remains `10.20.0.1/admin`. Strict “URL always stays on 10.20.0.1” requires reverse proxy or full Hotspot-hosted SPA + auth redesign — higher risk.

## Implementation status

See **`ADMIN_HOTSPOT_ACCESS_IMPLEMENTATION.md`** — Option A + auth-gated Admin APIs implemented in firmware/portal build. Hardware validation still required.

## CPU / stability

Recommended path: static Hotspot HTML + normal ESP32 HTTP. **0** RouterOS API commands for Admin page loads. No Hotspot host polling.

## Security

- Reach `/admin` ≠ authenticated Admin  
- Admin login ≠ Hotspot Internet authorization  
- Privileged APIs must remain behind AuthManager (Owner/Operator roles)

## Hardware validation required

- Unauthenticated GET `http://10.20.0.1/admin` (or `/admin.html`) — served vs forced to `/login`
- After ESP32 gate change: guest-SSID owner can log in; customer without credentials cannot call privileged APIs
- Captive `/login` and auto-redirect still PASS
