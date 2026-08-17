# Admin SPA CustomerPortal Asset Gate Fix

**Date:** 2026-08-06  
**Status:** SOURCE FIXED — HARDWARE VALIDATION REQUIRED

## Hardware-proven failure

`GET /assets/<main>.js` from CustomerPortal returned `CUSTOMER_PORTAL_RESTRICTED` while HTML/CSS could appear loaded (CSS often from immutable browser cache).

## Root cause

`PortalServer::handleNotFound` (notFound priority **35**) called `ensureManagementClient` **before** `isPortalUrl()`.

Dispatch order:

1. AdminServer (10) — passes CustomerPortal for `/assets/*`, returns false  
2. DownloadServer (30) — skips  
3. **PortalServer (35) — Management gate rejects ALL CustomerPortal notFound → JSON**  
4. StaticFileServer (40) — never reached for guest `/assets/*.js`

## Fix

1. `PortalServer::handleNotFound` — `isPortalUrl` first; Management gate only for portal recovery URLs  
2. `AdminServer::handleNotFound` — claim only `/admin/*`  
3. Bounded `[admin-spa]` serial logs for CustomerPortal allow/deny  

Privileged APIs unchanged: `ensureAdminAccess` + AuthManager/RBAC.

## Flash both

Firmware **and** SPIFFS (`uploadfs`) after `npm run build:esp32`.
