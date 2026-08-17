# Admin Hotspot Access — Implementation

**Date:** 2026-08-06  
**Status:** IMPLEMENTED — HARDWARE VALIDATION REQUIRED  
**Forensic basis:** `ADMIN_GATEWAY_10_20_0_1_FORENSIC.md`

## Final URL contract

| URL | Behavior |
|-----|----------|
| `http://10.20.0.1/login` | MikroTik Hotspot captive portal (unchanged) |
| `http://10.20.0.1/admin` or `/admin.html` | Hotspot static launcher → redirect |
| `http://10.10.10.2/admin` | ESP32 Admin SPA (address bar after hop — expected) |

Do **not** reverse-proxy the React SPA through RouterOS. Do **not** trust `10.20.0.0/24` as Management.

## Request flow

```
Owner phone (10.20.0.x)
  → http://10.20.0.1/admin[.html]
  → MikroTik hotspot/admin.html (static)
  → HTTP redirect to http://10.10.10.2/admin
  → ESP32 Admin SPA (CustomerPortal allowed for SPA surface)
  → POST /api/auth/login (appliance plane)
  → HttpOnly session cookie (same-origin 10.10.10.2)
  → Privileged /api/* via ensureAdminAccess (session OR Management LAN)
```

## ESP32 access policy

| Surface | Gate |
|---------|------|
| Admin SPA (`/admin`, `/login`, `/dashboard`, `/assets/*`, PWA icons) | `ensureAdminSpaClient` — Management **or** CustomerPortal |
| Privileged Admin APIs (`RENZFI_PROD_GATE`) | `ensureAdminAccess` — Management **or** authenticated session |
| `/api/health` (CustomerPortal) | Minimal `{ ok, session }` only — no eth/router/coin inventory |
| `/api/auth/login|logout|change-password` | `ensureAppliancePlane` (unchanged) + auth rules |
| `/api/portal/*` | `ensureProductionPlane` (unchanged) |
| ESP32 `/` root, `/static/*`, PortalServer recovery, downloads | `ensureManagementClient` (Management LAN only) |

Guest subnet is **never** classified as Management.

## Authentication / RBAC

- Cookie: HttpOnly, `Path=/`, `SameSite=Lax` (AuthManager)
- Owner vs Operator: existing `requireAuth` / `requireOwnerAuth` unchanged
- Admin login does **not** touch Hotspot `/ip/hotspot/active|user|cookie`
- Opening `/admin` does **not** authorize Internet

## MikroTik

- New file: `portal/admin.html` → built to `deployment/mikrotik-hotspot/admin.html`
- Redirect target substituted from `RENZFI_APPLIANCE_BASE_URL` (same as portal JS)
- Walled-garden `10.10.10.2/32` unchanged — sufficient for Admin SPA/API reachability
- No RouterOS API / proxy / Hotspot topology changes

### Path note

RouterOS Hotspot typically serves `admin.html` as `/admin.html`. Bare `/admin` may map to the same file on some builds — **hardware validation required**. Prefer documenting both; no firewall hacks.

## RouterOS command budget

| Action | RouterOS API cmds |
|--------|-------------------|
| Open `/admin` launcher | 0 |
| Admin login | 0 |
| Admin idle | 0 |
| Explicit router ops | existing worker only |

## Deployment

```bash
RENZFI_APPLIANCE_BASE_URL=http://10.10.10.2 npm run build:mikrotik-portal
```

Upload to MikroTik `Files → hotspot/`:

- **New:** `admin.html`
- Existing portal files only if also rebuilt

Flash ESP32 firmware + SPIFFS (`npm run build:esp32` then PlatformIO).

## Hardware validation

1. Guest SSID → `http://10.20.0.1/login` still captive portal  
2. `http://10.20.0.1/admin` and/or `/admin.html` → lands on ESP32 Admin login  
3. Anonymous: privileged `/api/router/*` → 401  
4. Wrong password → no session  
5. Owner/Operator login → dashboard; roles preserved  
6. No Hotspot active entry from Admin login alone  
7. Captive auto-redirect / WAN / DNS still PASS  
