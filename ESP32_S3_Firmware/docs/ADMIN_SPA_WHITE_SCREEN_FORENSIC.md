# Admin SPA White Screen — Forensic (CustomerPortal)

**Date:** 2026-08-06  
**Mode:** FORENSIC ONLY — no functional code changes  
**Symptom:** `http://10.10.10.2/admin` from `10.20.0.x` → blank/white Admin UI  
**Separate (out of scope for white-screen):** `http://10.20.0.1/admin` → MikroTik 404

## Verdict (source)

**Exact first failing layer (source-ranked):** React never mounts in `#root` because the **main ES module JS fails to load/execute** (or firmware/SPIFFS on device does not match the staged tree). Bootstrap/API failures alone cannot blank `#root` after React mounts.

**CONFIRMED (workspace staging):** `data/index.html` references `/assets/5fO0OfzB.js`, `/assets/BKTqPgpi.js`, `/assets/63vSE8nA.css` — all present under `data/assets/`.

**SOURCE-PROVEN (current tree):** After Admin Hotspot Access work, CustomerPortal is allowed through `ensureAdminSpaClient` for `/admin` and `/assets/*`. Privileged APIs use `ensureAdminAccess` + AuthManager.

**HARDWARE EVIDENCE REQUIRED:** Prove on-device HTTP status/body for `GET /admin` and `GET /assets/5fO0OfzB.js` from a guest phone (or serial `[http] begin ... access=customer-portal`).

## Update (asset gate fix)

**CONFIRMED root cause of JS `CUSTOMER_PORTAL_RESTRICTED`:** `PortalServer::handleNotFound` applied `ensureManagementClient` before `isPortalUrl`, intercepting `/assets/*` ahead of `StaticFileServer`. Fixed in source — see `ADMIN_SPA_ASSET_GATE_FIX.md`. Flash firmware + SPIFFS.

## /admin path (source)

`WebServerManager` exact `GET /admin` → `ensureAdminSpaClient` → `StaticFileServer::serveStaticOrIndex` → `resolveSpiffsServePath("/admin")` → `/index.html` → MIME `text/html`.

## Why white, not login

`App.tsx` always renders `AuthCheckingScreen` or `AuthPage` after the health `useEffect` settles (success or catch). Pure empty `#root` ⇒ JS did not run. Health 403/401 alone ⇒ login UI, not white.

## Access matrix (current source intent)

| Resource | Anon CustomerPortal | Authed Admin | Expected |
|----------|---------------------|--------------|----------|
| `/admin`, `/login`, `/assets/*` | ALLOW (Spa gate) | ALLOW | HTML/JS/CSS |
| `/api/health` | ALLOW minimal | ALLOW | `{ok,session}` |
| `/api/auth/login` | ALLOW | — | session cookie |
| `/api/status`, `/api/router/*`, … | DENY 401 | ALLOW+RBAC | privileged |

## MikroTik for white-screen

**NO** — direct `10.10.10.2/admin` already paints a document (white). Walled-garden reachability is proven. Fix ESP32 SPA/assets first; `/admin` 404 on gateway is a separate launcher mapping issue.

## CPU

Admin SPA load = ESP32-local HTTP only. 0 RouterOS API commands.
