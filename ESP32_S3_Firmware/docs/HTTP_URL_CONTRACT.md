# Renz-Fi HTTP URL Contract (Frozen — Phase 4A)

> **Superseded by [HTTP_ROUTE_CONTRACT.md](./HTTP_ROUTE_CONTRACT.md)** — the canonical permanent HTTP contract. This file is retained for Phase 4A history; do not edit except to fix broken links.
>
> **Production hosting correction:** the customer-facing captive portal (`/`, `/portal`, `/portal/*` below) is deployed to **MikroTik Hotspot storage** in production — see `deployment/mikrotik-hotspot/README.md`. These ESP32 URLs are unchanged and remain registered for local development, factory setup, management-AP access, and field recovery only; see `HTTP_ROUTE_CONTRACT.md` for the current statement.

This document is the **permanent HTTP contract** for the ESP32 firmware web appliance. It is the HTTP equivalent of [STORAGE_ARCHITECTURE.md](./STORAGE_ARCHITECTURE.md) / `StoragePaths.h`.

**Authoritative runtime:** `WebServerManager` + `RouteRegistry` + `IWebRouteProvider` implementations  
**Default port:** `80` (`RenzFiConfig::HTTP_PORT`)  
**Status:** Frozen before Phase 4B portal migration. **Do not change URLs unless absolutely necessary.**

**Companion docs:** [PHASE_4A_IMPLEMENTATION_REPORT.md](./PHASE_4A_IMPLEMENTATION_REPORT.md), [PHASE_4B_PORTAL_MIGRATION.md](./PHASE_4B_PORTAL_MIGRATION.md), [PORTAL_CONFIG_ARCHITECTURE.md](./PORTAL_CONFIG_ARCHITECTURE.md)

---

## Design principles

1. **Prefix ownership** — each top-level URL prefix has exactly one owning route provider.
2. **Stable URLs** — clients (browser portal, admin React app, owner Android app, scripts) depend on these paths; breaking changes require a major firmware version and migration notes.
3. **Separation of concerns** — static bundles, dynamic media, REST APIs, and SPA shells are different prefixes even when names overlap conceptually.
4. **Public vs admin** — captive portal and portal session APIs are public; owner/admin APIs require session authentication.
5. **Cache by content class** — use `CacheManager` policies; do not invent ad-hoc `Cache-Control` strings in handlers.

---

## Top-level route map (frozen)

```
/                 Captive portal entry (alias → portal shell)
────────────────────────────────────────────────────────────
/portal           Captive portal alias + static subtree (dev/recovery fallback; production is MikroTik)
/portal/*         Portal HTML, JS, CSS, sounds (dev/recovery fallback; production is MikroTik)
────────────────────────────────────────────────────────────
/admin            React admin dashboard SPA shell
/admin/*          Admin deep links (React Router fallback)
/login            Admin login entry (legacy alias → admin SPA)
/dashboard        Admin dashboard alias (legacy → admin SPA)
────────────────────────────────────────────────────────────
/api/*            REST API, SSE, portal session API, resolved media
────────────────────────────────────────────────────────────
/assets/*         Bundled frontend resources (React admin chunks)
────────────────────────────────────────────────────────────
/static/*         Firmware-shipped static resources (reserved)
────────────────────────────────────────────────────────────
/downloads/*      Owner download endpoints (reserved)
────────────────────────────────────────────────────────────
/manifest.webmanifest   Admin PWA manifest
/sw.js                  Admin service worker
/favicon.svg            Admin favicon
/favicon.ico            Admin favicon
────────────────────────────────────────────────────────────
/health           Reserved — top-level health alias (not implemented)
/metrics          Reserved — future telemetry
/docs             Reserved — future API / operator documentation
```

> **Note:** Operational health is served today at **`GET /api/health`** (canonical). Top-level `/health` is reserved for future load-balancer or monitoring aliases; do not remove `/api/health`.

---

## Prefix contract summary

| Prefix | Owner | Auth | Cache policy | Content type | Consumer |
|--------|-------|------|--------------|--------------|----------|
| `/` | **PortalServer** | Public | NoCache / ShortCache† | `text/html` | Captive portal browser (dev/recovery; prod is MikroTik) |
| `/portal` | **PortalServer** | Public | NoCache / ShortCache† | `text/html` | Captive portal browser (dev/recovery; prod is MikroTik) |
| `/portal/*` | **PortalServer** + StaticFileServer fallback | Public | ShortCache | html, css, js, audio | Captive portal browser (dev/recovery; prod is MikroTik) |
| `/admin` | **AdminServer** | Public‡ | NoCache | `text/html` | Owner browser |
| `/admin/*` | **AdminServer** (onNotFound SPA fallback) | Public‡ | NoCache | `text/html` | Owner browser |
| `/login` | **AdminServer** | Public‡ | NoCache | `text/html` | Owner browser |
| `/dashboard` | **AdminServer** | Public‡ | NoCache | `text/html` | Owner browser |
| `/api/*` | **ApiServer** (+ **AssetServer** for portal media GET) | Mixed§ | NoCache | `application/json` | Admin app, portal JS, automation |
| `/api/events` | **EventBusRouteProvider** | Public | NoCache | `text/event-stream` | Admin dashboard (SSE) |
| `/assets/*` | **StaticFileServer** | Public | **Immutable** | js, css, png, svg, … | Admin React bundle |
| `/static/*` | **StaticFileServer** | Public | ShortCache | varies | Firmware resources (reserved) |
| `/downloads/*` | **DownloadServer** | TBD | NoCache | binary / json | Owner browser (reserved) |
| `/manifest.webmanifest` | **AdminServer** | Public | NoCache | manifest+json | Admin PWA |
| `/sw.js` | **AdminServer** | Public | NoCache | javascript | Admin PWA |
| `/favicon.*` | **AdminServer** | Public | NoCache | svg / ico | Browser |
| `/health` | *Reserved* | — | — | — | Monitoring (future) |
| `/metrics` | *Reserved* | — | — | — | Monitoring (future) |
| `/docs` | *Reserved* | — | — | — | Operators (future) |

† Portal shell via `StaticFileServer::serveStaticOrIndex`; `/portal/*` static files use ShortCache (`max-age=86400`).  
‡ Shell is public; **API calls from the SPA require admin session** via `/api/*`.  
§ See [Authentication model](#authentication-model) below.

---

## Important distinction: `/assets` vs portal media

These are **different URL namespaces** and must not be merged.

| URL | Purpose | Owner | Storage |
|-----|---------|-------|---------|
| `/assets/*` | React admin **build artifacts** (Vite/Webpack chunks) | StaticFileServer | SPIFFS `/assets/` |
| `/api/portal/assets/banner` | Owner-uploaded **portal banner** | AssetServer | SD contract → legacy `/www` → SPIFFS → bundled |
| `/api/portal/assets/music` | Owner-uploaded **portal music** | AssetServer | SD contract → legacy `/www` → SPIFFS → bundled |

Portal JavaScript must continue to use **`/api/portal/assets/*`** for dynamic media. Phase 4B does not change these URLs.

---

## Authentication model

| Level | Meaning | Enforced by |
|-------|---------|-------------|
| **Public** | No session cookie required | — |
| **Session** | Valid admin session; password change in progress allowed | `ApiServer::requireAuth(Session)` |
| **Admin** | Valid admin session + full access (blocks if password change required) | `ApiServer::requireAuth(FullAccess)` |

Cookie-based session via `AuthManager`. JSON 401/403 responses use envelope `{ success: false, error, code }`.

---

## Cache policy reference

Defined in `CacheManager` (`src/web/CacheManager.h`):

| Policy | Header | Used for |
|--------|--------|----------|
| **NoCache** | `no-store` | All `/api/*` JSON, SPA shells, errors, SSE |
| **ShortCache** | `max-age=86400` | `/portal/*` static files, `/static/*` foundation |
| **LongCache** | `public, max-age=31536000` | Available for future use |
| **Immutable** | `public, max-age=31536000, immutable` | `/assets/*` admin bundles |

MIME types resolved by `MimeResolver` (`src/web/MimeResolver.h`).

---

## `/api/*` — ApiServer contract

All paths below are under prefix **`/api`**. Unless noted, methods require **Admin** authentication.

### Public endpoints (no auth)

| Method | Path | Content-Type | Consumer | Notes |
|--------|------|--------------|----------|-------|
| OPTIONS | `/api/` | — | Browsers | CORS preflight |
| GET | `/api/health` | `application/json` | Monitoring, admin | Lightweight health; canonical health URL |
| POST | `/api/auth/login` | `application/json` | Admin app | Creates session |
| POST | `/api/auth/logout` | `application/json` | Admin app | Clears session |
| GET | `/api/portal/branding` | `application/json` | Portal JS | Title, theme, asset URLs |
| GET | `/api/portal/session` | `application/json` | Portal JS | Query: `mac`, `ip` |
| GET | `/api/portal/rates` | `application/json` | Portal JS | Promo rates modal |
| POST | `/api/portal/start-coin-session` | `application/json` | Portal JS | Coin flow |
| POST | `/api/portal/done-paying` | `application/json` | Portal JS | Coin flow |
| POST | `/api/portal/pause` | `application/json` | Portal JS | Session control |
| POST | `/api/portal/resume` | `application/json` | Portal JS | Session control |
| POST | `/api/portal/cancel-modal` | `application/json` | Portal JS | UI state |
| POST | `/api/portal/reset` | `application/json` | Portal JS | Session reset |
| POST | `/api/portal/terminate` | `application/json` | Portal JS | User terminate |
| POST | `/api/portal/heartbeat` | `application/json` | Portal JS | Keepalive |

### Public media (AssetServer)

| Method | Path | Content-Type | Consumer | Cache |
|--------|------|--------------|----------|-------|
| GET | `/api/portal/assets/banner` | image/webp, image/jpeg, … | Portal JS | Resolver MIME; no explicit long cache |
| GET | `/api/portal/assets/music` | audio/mpeg, … | Portal JS | Resolver MIME |

### Session-only endpoints

| Method | Path | Notes |
|--------|------|-------|
| POST | `/api/auth/change-password` | **Session** level (allowed during forced password change) |

### Admin endpoints (representative groups)

| Group | Path prefix | Consumer |
|-------|-------------|----------|
| Dashboard status | `/api/status` | Admin React |
| Storage | `/api/storage/*` | Admin React |
| System | `/api/system/*` | Admin React |
| RGB / coin hardware | `/api/rgb/*`, `/api/coin/*`, `/api/system/coin` | Admin React |
| Promos | `/api/promos`, `/api/promos/{id}` | Admin React |
| Vouchers | `/api/vouchers`, `/api/vouchers/{code}` | Admin React |
| Users / sessions | `/api/users/*` | Admin React |
| Sales | `/api/sales/*`, `/api/sales/chart/{period}` | Admin React |
| Settings | `/api/settings/*` | Admin React |
| Portal admin uploads | `/api/settings/portal/*` | Admin React |
| Backup / restore | `/api/settings/backup`, `/api/settings/restore` | Admin React |
| Firmware OTA | `/api/system/firmware` | Admin React |
| Logs | `/api/logs`, `/api/logs/export` | Admin React |
| Router | `/api/router/*` | Admin React |
| Network | `/api/system/network`, `/api/system/wifi*` | Admin React (wifi = backward-compat alias) |

Parameterized routes registered via `ApiServer::handleNotFound`:

- `PUT` / `DELETE` `/api/promos/{id}`
- `GET` / `DELETE` `/api/vouchers/{code}`
- `GET` `/api/sales/chart/daily|weekly|monthly`

### Server-sent events

| Method | Path | Owner | Content-Type | Consumer |
|--------|------|-------|--------------|----------|
| GET | `/api/events` | EventBusRouteProvider | `text/event-stream` | Admin dashboard live updates |

---

## Route provider ownership (frozen)

| Provider | Registers | onNotFound priority |
|----------|-----------|---------------------|
| **StaticFileServer** | `/assets/*`, `/static/*`; SPA static resolution | 40 |
| **AssetServer** | `/api/portal/assets/banner`, `/api/portal/assets/music` | — |
| **EventBusRouteProvider** | `/api/events` | — |
| **ApiServer** | `/api/*` REST + portal session API | 20 |
| **PortalServer** | `/`, `/portal`, `/portal/*` (dev/recovery fallback; production is MikroTik) | — |
| **AdminServer** | `/admin`, `/login`, `/dashboard`, PWA icons | 10 |
| **DownloadServer** | `/downloads/*` (reserved) | 30 |

Registration order (first match wins): StaticFileServer → AssetServer → EventBus → ApiServer → PortalServer → AdminServer → DownloadServer.

**Managers that never own HTTP routes:** AssetManager, PortalConfigManager, StorageManager, CoinManager, PortalSessionManager, MikroTikManager.

---

## Intended consumers

| Consumer | Primary prefixes | Auth |
|----------|------------------|------|
| **Captive portal browser** | `/`, `/portal/*`, `/api/portal/*` | Public |
| **Owner admin browser** | `/admin/*`, `/login`, `/assets/*`, `/api/*` | API: Admin session |
| **Owner Android app** | `/api/*` | Admin session |
| **Portal JavaScript (`renzfi-app.js`)** | `/api/portal/*` | Public |
| **Monitoring / scripts** | `/api/health` (today); `/health` (future) | Public |
| **Firmware / CI** | `/static/*` (future) | Public |

---

## Production portal hosting (supersedes the old "Phase 4B" plan below)

The production customer captive portal is generated from `portal/` and deployed to **MikroTik Hotspot storage** (see `deployment/mikrotik-hotspot/README.md`, built via `npm run build:mikrotik-portal`). This is the opposite direction from the Phase 4B plan once described here (ESP32 hosting, MikroTik redirecting) — that plan was superseded before implementation. This change does **not** alter:

- Any URL in this document
- Portal HTML/JS/CSS behavior or appearance
- `/api/portal/*` JSON schemas
- `/api/portal/assets/*` URLs

`/`, `/portal`, and `/portal/*` remain **PortalServer** owned on the ESP32, but only serve development, factory-setup, management-AP, and field-recovery traffic — not production customers. The client-side base URL used to reach `/api/portal/*` from the MikroTik-hosted portal is resolved by `renzfi-app.js` (see `RENZFI_APPLIANCE_BASE_URL`); the JSON schemas and paths themselves are unchanged.

---

## Reserved / future routes

| Path | Intended purpose | Status |
|------|------------------|--------|
| `/health` | Top-level health probe alias | Reserved — use `/api/health` today |
| `/metrics` | Prometheus-style or appliance metrics | Reserved |
| `/docs` | Operator or API documentation | Reserved |
| `/static/*` | Firmware-bundled non-portal resources | Foundation registered; no assets migrated |
| `/downloads/*` | Backup exports, report downloads | Foundation registered; routes TBD |

Adding a reserved route requires updating this document **before** implementation.

---

## Change policy

1. **Frozen URLs** — treat breaking URL changes like breaking `StoragePaths` changes.
2. **New endpoints** — prefer extending existing prefix groups (`/api/settings/...`, `/api/portal/...`) over new top-level prefixes.
3. **New top-level prefix** — requires new `IWebRouteProvider`, RouteRegistry entry, and an update to this document.
4. **Deprecation** — keep old URLs functional for at least one major firmware version; document in release notes.
5. **External clients** — admin React app, `renzfi-app.js`, login.html, and owner Android app are compatibility-sensitive; verify all four on any URL change.

---

## Quick reference card

```
Browser portal (prod)     →  MikroTik Hotspot storage (deployment/mikrotik-hotspot/)
Browser portal (dev/rec)  →  /  /portal/*             (ESP32 fallback only)
Appliance API (all)       →  /api/portal/*             (called from either origin)
Owner admin               →  /admin/*  /assets/*  /api/*  (session cookie)
Dynamic media             →  /api/portal/assets/banner|music   (NOT /assets/*)
Admin bundles             →  /assets/*
Health (today)            →  GET /api/health
Live updates              →  GET /api/events  (SSE)
```

**Frozen:** Phase 4A. The Phase 4B ESP32-hosting plan described in this file's history was superseded by MikroTik production hosting — see `HTTP_ROUTE_CONTRACT.md` and `deployment/mikrotik-hotspot/README.md` for the current state. Amend only with explicit architecture review.
