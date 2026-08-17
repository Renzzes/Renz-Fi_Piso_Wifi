# Phase 4B – ESP32 Portal Migration & Portal Hosting

**Status:** Complete — see [PHASE_4B_IMPLEMENTATION_REPORT.md](./PHASE_4B_IMPLEMENTATION_REPORT.md)  
**Prerequisite:** Phase 4A Web Server Foundation + RouteRegistry (complete)  
**HTTP contract (frozen):** [HTTP_ROUTE_CONTRACT.md](./HTTP_ROUTE_CONTRACT.md) — do not change URLs during migration

---

## Goal

Move captive portal hosting from the MikroTik router to the ESP32 appliance. The ESP32 becomes the single web platform for portal HTML, JavaScript, CSS, and resolved media assets.

**This phase is infrastructure-only.** Users must not notice any visual or behavioral change. HTML, JavaScript, CSS, APIs, and UX remain functionally identical — only the serving location changes.

---

## Scope

### Portal Hosting

| Item | Action |
|------|--------|
| `login.html` | Migrate to ESP32; serve via `PortalServer` |
| Portal JavaScript | Migrate; serve from ESP32 |
| Portal CSS | Migrate; serve from ESP32 |
| Route ownership | `PortalServer` owns `/`, `/portal`, `/portal/*` |

### Portal Assets

| Item | Action |
|------|--------|
| Banner / music / future media | Serve via `AssetServer` + `AssetResolver` |
| Storage contract | New uploads use SD contract paths (`/assets/*`) |
| Legacy fallback | Preserve `/www` tier in `AssetResolver` |
| URLs | Keep existing frontend URLs unchanged |

### Router Independence

After migration, MikroTik responsibilities are **only**:

- DHCP
- NAT
- Internet gateway
- Captive redirect (hotspot → ESP32 portal URL)
- Authorization (session allow/deny)

The router **must not** serve portal HTML, JS, CSS, or media files.

---

## Architectural Context

```
WebServerManager
    └── RouteRegistry
            ├── StaticFileServer
            ├── AssetServer
            ├── EventBusRouteProvider
            ├── ApiServer
            ├── PortalServer      ← Phase 4B primary target
            ├── AdminServer
            └── DownloadServer
```

Providers register via `IWebRouteProvider::registerRoutes(WebServerManager&)`. Only providers that need the underlying server call `WebServerManager::routeServer()`.

---

## Constraints (Frozen for 4B)

### MUST NOT change

- Portal appearance
- Portal user flows (coin insert, pause, resume, terminate)
- REST API URLs and JSON schemas
- Admin dashboard
- AssetManager / AssetResolver / PortalConfigManager contracts
- Storage architecture
- Boot order

### MUST preserve

- `/api/portal/*` session APIs (stay in `ApiServer`)
- `/api/portal/assets/*` (stay in `AssetServer`)
- `/api/portal/branding` (stay in `ApiServer`)
- Legacy `/www` fallback for existing deployments
- MikroTik hotspot redirect → ESP32 portal URL

---

## Migration Strategy

### Step 1 – Inventory portal files on router

Document current MikroTik-hosted files: `login.html`, JS bundles, CSS, sound paths, and any hardcoded URLs pointing at router IP.

### Step 2 – Copy assets to ESP32 storage

Place portal static files in SPIFFS and/or SD contract paths without modifying content.

### Step 3 – PortalServer takes ownership

- Remove duplicate handlers in `RenzFiPortalRoutes.h` once `PortalServer` is authoritative
- Route `/portal/*` static files through `PortalServer` + `StaticFileServer` or dedicated portal static root
- Keep `serveStaticOrIndex` behavior identical for existing paths

### Step 4 – AssetServer for portal media

Ensure banner/music/logo URLs in portal JS resolve to existing `/api/portal/assets/*` endpoints (no URL changes).

### Step 5 – MikroTik configuration

Update hotspot profile:

- `html-directory` or equivalent → ESP32 portal URL (redirect only)
- Remove local portal file serving from router
- Verify captive redirect still lands on ESP32

### Step 6 – Verification matrix

| Check | Expected |
|-------|----------|
| Portal loads on hotspot connect | Same UI as before |
| Coin flow | Identical timing and states |
| Banner/music | Same assets via AssetResolver |
| Branding API | Same JSON |
| Session APIs | Unchanged |
| Admin dashboard | Unaffected |
| Legacy `/www` installs | Still work via fallback tier |

---

## Success Criteria

1. Router serves zero portal static files
2. ESP32 serves all portal HTML/JS/CSS
3. AssetResolver fallback chain intact (contract → legacy `/www` → SPIFFS → bundled)
4. No user-visible regression
5. PlatformIO build SUCCESS
6. Field-test on live MikroTik hotspot

---

## Out of Scope (Later Phases)

- Portal UI redesign
- New portal features (ads carousel, video backgrounds)
- Removing legacy `/www` tier (requires migration window + owner notification)
- DownloadServer migration for backup exports

---

## Related Documents

- [PHASE_4A_IMPLEMENTATION_REPORT.md](./PHASE_4A_IMPLEMENTATION_REPORT.md)
- [HTTP_ROUTE_CONTRACT.md](./HTTP_ROUTE_CONTRACT.md) — **frozen HTTP route contract**
- [PORTAL_CONFIG_ARCHITECTURE.md](./PORTAL_CONFIG_ARCHITECTURE.md)
- [ASSET_LIFECYCLE.md](./ASSET_LIFECYCLE.md)
