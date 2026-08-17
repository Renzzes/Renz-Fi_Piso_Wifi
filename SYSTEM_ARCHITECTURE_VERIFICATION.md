# Renz-Fi System Architecture Verification

**Date:** 2026-06-30  
**Scope:** Phases 3A–7A (frozen layers only) — read-only verification, no code changes  
**Objective:** Confirm the appliance forms one coherent system and Phase 7B+ can proceed without revisiting prior layers

---

## 1. Overall Architecture Score

### **84 / 100**

| Dimension | Score | Notes |
|-----------|-------|-------|
| Layer separation & composition root | 92 | `FirmwareApp` is a clear single root; managers wired in dependency order |
| Frozen contract alignment | 85 | Core contracts match runtime; minor doc/runtime gaps |
| End-to-end lifecycle coherence | 82 | Full path works; factory-reset semantics diverge |
| UI ↔ firmware boundary | 88 | Setup wizard respects provisioning-only rule |
| Product philosophy adherence | 90 | Renz-Fi owns business logic; router is infrastructure |
| Fleet autonomy (7A) | 91 | Client-side registry; no ESP32 coupling |
| Technical debt / regression risk | 72 | Legacy dual paths, SessionManager overlap, doc drift |

**Verdict:** Architecture is **stable enough for Phase 7B**. No frozen contract requires redesign. Issues found are **localized**, mostly **transitional legacy**, or **documentation drift**. Two items warrant minimal correction before or early in 7B (system factory reset vs installation state; ready-redirect in setup bootstrap).

---

## 2. Layer-by-Layer Verification

### 2.1 Storage Layer

| Component | Owner | Persistence | Verdict |
|-----------|-------|-------------|---------|
| **StorageManager** | Sole I/O primitive | SD primary, SPIFFS fallback, layout seeding | ✅ Clean |
| **StoragePaths** | Contract (frozen) | Path registry + `StorageOwner` enum | ✅ Authoritative |
| **AssetManager** | Media under `/assets/*` | Writes media metadata in `/config/portal.json` | ✅ Sole writer for assets |
| **AssetResolver** | Internal to AssetManager | Read-only resolution chain | ✅ No duplicate persistence |
| **PortalConfigManager** | Config/branding JSON | `/config/portal.json` (non-media fields + legacy flags) | ⚠️ Shared file with AssetManager (documented) |
| **BackupManager** | Export/restore/wipe | `/backup/*` (legacy), `/backups` (canonical, not fully migrated) | ⚠️ Archive path names diverge from contract |

**Init order** (`FirmwareApp.cpp`): `_storage` → `_assetManager` → `_portalConfig` → `_portalSessions` / `_coin` — correct dependency chain.

**Circular dependencies:** None at compile time. Runtime bidirectional wiring: `CoinManager` ↔ `PortalSessionManager` via `setCoinManager()` — acceptable but tight.

**Issues:**

| File | Issue | Future risk |
|------|-------|-------------|
| `ESP32_S3_Firmware/src/BackupManager.cpp` (lines ~40–54) | Archive keys use `/config/portal-config.json`, flat `/config/sales.json` etc., mapping to runtime paths; asset backup targets legacy `/www/*` not contract `/assets/banner/current.webp` | Restore/export in Phase 7B+ may miss contract-path assets |
| `ESP32_S3_Firmware/src/StoragePaths.h` | Intentional legacy active paths (`/vouchers/vouchers.json`, `/www/*`, `/backup/*`) alongside contract paths | Migration debt; new code must use contract paths only |
| `ESP32_S3_Firmware/src/ApiServer.h` (line ~70) | `BackupManager` owned inside ApiServer, not `FirmwareApp` | Violates documented manager ownership; harder to test/reuse |

**Minimal correction:** Move `BackupManager` to `FirmwareApp` composition root and inject into `ApiServer`; align backup archive paths with `StoragePaths.h` when touching backup code next.

---

### 2.2 Web Platform

**Architecture:**

```
WebServerManager
  └── RouteRegistry
        ├── StaticFileServer   → /assets/*, /static/*
        ├── AssetServer        → GET /api/portal/assets/{banner,music}
        ├── EventBusRouteProvider → GET /api/events
        ├── ApiServer          → /api/* (admin + portal session REST)
        ├── ProvisioningServer → /api/provisioning/*
        ├── PortalServer       → /, /portal, /portal/*
        ├── AdminServer        → /admin, /login, /dashboard, PWA
        └── DownloadServer     → /downloads/* (stub)
```

Registration order (`WebServerManager.cpp:60–67`) prevents route shadowing. `/assets/*` (bundled admin) vs `/api/portal/assets/*` (dynamic media) namespaces are correctly separated per `HTTP_ROUTE_CONTRACT.md`.

| Check | Result |
|-------|--------|
| HTTP_ROUTE_CONTRACT respected | ✅ No overlapping exact routes found |
| Provisioning routes isolated | ✅ `ProvisioningServer` owns `/api/provisioning/*` |
| Portal vs Admin separation | ✅ Distinct providers |
| `/health` alias | ❌ Reserved in contract, not implemented (only `/api/health`) |
| `/downloads/*` | ❌ Stub only — returns 404 via notFound handler |

**Issue:** `HTTP_ROUTE_CONTRACT.md` lists `/api/*` under **ApiServer**, but provisioning routes are registered by **ProvisioningServer**. Runtime is correct; contract table should note the `/api/provisioning/*` exception (doc-only).

---

### 2.3 Provisioning

| Component | Responsibility | Verdict |
|-----------|----------------|---------|
| **ProvisioningEngine** | Workflow orchestration; no HTTP | ✅ |
| **InstallationStateManager** | Persists `/config/installation.json` | ✅ |
| **InstallationSession** | Wizard session struct | ✅ |
| **ProvisioningServer** | Thin HTTP adapter | ✅ |

**Workflow vs `INSTALLATION_WORKFLOW.md`:**

| Step | API | Engine op | State transition | Match |
|------|-----|-----------|------------------|-------|
| Welcome | `POST .../begin` | `beginInstallation()` | Stays `factory` | ✅ |
| Router Detection | `GET .../routers/detect` | `detectRouters()` | None | ✅ |
| Driver Selection | `POST .../routers/select` | `selectDriver()` | → `router_selected` | ✅ |
| Router Connection | `POST .../routers/connect` | `connectRouter()` | → `router_connected` | ✅ |
| Portal Configuration | `POST .../portal/configure` | `configurePortal()` | → `portal_configured` | ✅ |
| Coin Configuration | `POST .../coin/configure` | `configureCoin()` | → `coin_configured` | ✅ |
| Validation | `POST .../validate` | `validateInstallation()` | → `validation_passed` | ✅ |
| Summary / Complete | `POST .../finish` | `finalizeInstallation()` | → `ready` | ✅ |

All endpoints in `PROVISIONING_API.md` are implemented in `ProvisioningServer.cpp`. No manager leaks into HTTP beyond engine delegation.

**Dual factory reset (documented divergence, incomplete system reset):**

| Endpoint | Owner | Scope |
|----------|-------|-------|
| `POST /api/provisioning/installation/factory-reset` | ProvisioningEngine | Installation state + session only (`InstallationStateManager::resetToFactory()`) |
| `POST /api/system/factory-reset` | BackupManager via ApiServer | Wipes promos, vouchers, sales, router, portal, sessions, auth — **does not reset `installation.json`** |

**Critical gap:** `BackupManager::wipeUserData()` (`BackupManager.cpp:553–583`) does not delete or reset `/config/installation.json`. After system factory reset, appliance can remain `ready` while all operational config is erased — bootstrap may skip setup wizard.

**Minimal correction:** In `BackupManager::performFactoryReset()`, call `InstallationStateManager::resetToFactory()` (inject manager) or delete `StoragePaths::InstallationFile`.

---

### 2.4 Setup Wizard (Browser)

**Location:** `src/pages/setup/` (React admin SPA), **not** root `login.html` / `renzfi-app.js`.

| Screen | ProvisioningClient | ProvisioningContext | Frozen models | Direct manager access |
|--------|-------------------|---------------------|---------------|----------------------|
| Welcome | ✅ | ✅ | — | ❌ None |
| Router Detection | ✅ | ✅ | — | ❌ None |
| Driver Selection | ✅ | ✅ | — | ❌ None |
| Router Connection | ✅ | ✅ | — | ❌ None |
| Portal Configuration | ✅ | ✅ | — | ❌ None |
| Coin Configuration | ✅ | ✅ | — | ❌ None |
| Validation | ✅ | ✅ | ValidationResult | ❌ None |
| Summary | — | props | InstallationReport | ❌ None |
| Complete | ✅ | ✅ | InstallationReport | ❌ None |

Frozen UI kit used: `WizardShell`, `WizardTheme`, `SetupForm`, `SetupInfoBanner`, `SetupStatusCard`, `navigationGuards`, `stepRouter`.

**Contract deviations:**

| File | Issue | Future risk |
|------|-------|-------------|
| `src/pages/setup/SetupWizardPage.tsx` (line ~433) | `ProvisioningProvider` receives `onAborted` but **not** `onReady` | Resume when `installation.ready === true` does not redirect to dashboard per `SETUP_WIZARD_UI_CONTRACT.md` §2.1 |
| `src/pages/setup/screens/CompleteScreen.tsx` (lines 100–101) | Prefers `finishSummary?.firmwareVersion` over `InstallationReport` | Minor read-model bypass; inconsistent summary source |

**Minimal correction:** Pass `onReady={handleExit}` to `ProvisioningProvider` in `SetupWizardPage.tsx`.

---

### 2.5 Portal

| Concern | Owner | Verdict |
|---------|-------|---------|
| Captive portal HTML | **PortalServer** | ✅ |
| Portal template injection | **PortalTemplate** | ✅ |
| Dynamic banner/music | **AssetServer** + **AssetResolver** | ✅ |
| Portal config JSON | **PortalConfigManager** | ✅ |
| Guest sessions | **PortalSessionManager** | ✅ Authoritative for captive portal |
| Coin hardware → session credit | **CoinManager** → `PortalSessionManager::onCoinInserted()` | ✅ |
| Router hotspot hooks | **RouterPlatform** (optional) | ✅ Infrastructure only |

Router does **not** own portal logic. `PortalServer.cpp` serves from SPIFFS with serve-time templating — Phase 4B migration complete. `RenzFiPortalRoutes.h` deleted.

**Stale comments:** `PortalSessionManager.h` still references MikroTik-hosted portal JS — documentation drift only.

**Overlap:** `SessionManager` (legacy admin users + sales) vs `PortalSessionManager` (captive portal). Portal path is authoritative for guest flow. Admin coin test (`ApiServer.cpp:1799–1805`) uses `SessionManager.grantCoinSession()` — bypasses portal coin-window model (admin-only diagnostic path).

---

### 2.6 Router Platform

| Component | Role | Verdict |
|-----------|------|---------|
| **RouterPlatform** | Facade; sole dependency for upper layers | ✅ |
| **IRouterDriver** | Vendor contract | ✅ |
| **MikroTikDriver** | RouterOS transport + hotspot hooks | ✅ No business logic |
| **GenericAPDriver** | No-op router API; appliance-side sessions | ✅ Correct fallback |
| **RouterDriverManifest** | Capability flags | ✅ |

`MikroTikManager` deleted; logic in `MikroTikDriver`. Upper layers never include vendor headers.

**Residual naming:** `/api/status` exposes `mikrotik.*` keys (`ApiServer.cpp:~516`) for admin dashboard backward compatibility — not a logic leak, but couples UI to legacy shape.

**Product rule satisfied:** Appliance operates with GenericAP (no router API). MikroTik unlocks enhanced integration only.

---

### 2.7 Fleet (Phase 7A)

| Rule | Implementation | Verdict |
|------|----------------|---------|
| Each appliance autonomous | No ESP32-to-ESP32 comms | ✅ |
| Registry client-side only | Browser `localStorage`, Android DataStore | ✅ |
| Keyed by `deviceId` | `RF-` + MAC suffix via `DeviceIdentity.cpp` | ✅ |
| Fleet switch = API base URL only | `setRuntimeApiBaseUrl()` in browser | ✅ |
| No shared DB/sessions | Per-appliance cookies and SD | ✅ |
| DeviceProfile frozen | `GET /api/health` → `data.device` | ✅ |

Firmware has no fleet mode — `capabilities.fleet = true` signals client registry support only. Phase 7B FleetHealth is spec-only (`PHASE_7B_FLEET_HEALTH.md`).

---

### 2.8 Mobile Application

**Model:** Android Owner App is a **native fleet shell** + **WebView** to `http://{ip}/admin` — setup wizard parity is via the same React SPA, not duplicated native provisioning.

| Capability | Browser | Android | Parity |
|------------|---------|---------|--------|
| LAN discovery | `deviceDiscovery.ts`, `/api/health` | `DeviceRepository.kt` subnet scan | ✅ Functional |
| Device registry | `DeviceRegistryContext` | `DevicePreferences` DataStore | ✅ |
| Fleet switching | `setRuntimeApiBaseUrl(ip)` | New WebView URL per device | ✅ Equivalent |
| Setup wizard | Full `/setup` | WebView SPA | ✅ |
| Session management | Cookie + `sessionGate` | WebView cookies per origin | ✅ |
| Route on switch | Preserves route if authed | Always `dashboard/{id}` | ⚠️ UX difference |
| Frozen provisioning models | Native in SPA | Not in Kotlin | ✅ Acceptable (SPA canonical) |

Root `renzfi-app.js` hardcodes `API_BASE = "http://10.40.0.2/api/portal"` — correct for captive portal on appliance LAN; not part of admin/fleet architecture.

---

## 3. Product Philosophy Verification

| Renz-Fi owns | Verified owner | Status |
|--------------|----------------|--------|
| Sessions | `PortalSessionManager` (+ legacy `SessionManager` for admin) | ✅ |
| Coins | `CoinManager` | ✅ |
| Vouchers | VoucherManager (storage) + ApiServer routes | ✅ |
| Portal | `PortalServer`, `PortalSessionManager`, `PortalConfigManager` | ✅ |
| Reports / Sales | `PortalSessionManager`, `SessionManager` → `/sales/sales.json` | ⚠️ Dual writers |
| Assets | `AssetManager` | ✅ |
| Configuration | `PortalConfigManager`, `NetworkSettingsManager`, settings JSON | ✅ |
| Fleet | Client registry only | ✅ |
| Setup | `ProvisioningEngine` + React wizard | ✅ |
| Mobile / Admin | AdminServer + React SPA; Android WebView | ✅ |

| Router owns | Verified | Status |
|-------------|----------|--------|
| Internet / Gateway / DHCP / Wi-Fi | Router drivers configure via `RouterPlatform` | ✅ |
| Optional API enhancements | Hotspot disconnect, status probes | ✅ Never owns sessions/coins/vouchers |

**Rule 3 — Router APIs optional:** `GenericAPDriver` proves appliance operates without router API. MikroTik integration enhances but does not gate core flows.

---

## 4. Ownership Verification

### Clean ownership (confirmed)

```
FirmwareApp (composition root)
├── StorageManager          → all file I/O
├── InstallationStateManager → /config/installation.json
├── ProvisioningEngine      → setup workflow (internal managers only)
├── PortalSessionManager    → /sessions/portal_sessions.json
├── CoinManager             → GPIO + coin settings
├── AssetManager            → /assets/*
├── PortalConfigManager     → portal.json (config fields)
├── RouterPlatform          → /config/router.json + driver I/O
└── WebServerManager        → HTTP demux only

HTTP adapters (no business logic):
├── ProvisioningServer → ProvisioningEngine
├── PortalServer       → SPIFFS + PortalTemplate
├── AssetServer        → AssetManager/AssetResolver
├── AdminServer        → React SPA shell
└── ApiServer          → manager REST (⚠️ also embeds BackupManager)
```

### Ownership violations / blurs

| Issue | Files | Severity |
|-------|-------|----------|
| BackupManager inside ApiServer | `ApiServer.h`, `ApiServer.cpp` | Low |
| Sales recorded from two managers | `PortalSessionManager.cpp`, `SessionManager.cpp` | Medium |
| System factory reset skips installation state | `BackupManager.cpp` | **High** |
| ApiServer god-object (~2266 lines, 15+ manager pointers) | `ApiServer.cpp` | Medium (maintainability) |

---

## 5. Flow Verification (Customer Lifecycle)

```
Factory Appliance
    ↓  SD seeded: installation.json → factory, layout dirs, default auth
Boot
    ↓  FirmwareApp: ETH → SPIFFS → SD → subsystems → WebServerManager
    ↓  RecoveryManager: GPIO hold → L1 password / L2 network (no full wipe)
Installation Wizard
    ↓  Admin login → /setup → resume → linear screens
    ↓  ProvisioningClient → /api/provisioning/* only
Router Selection → Connection
    ↓  ProvisioningEngine → RouterPlatform → IRouterDriver
    ↓  Credentials → /config/router.json
Portal Configuration → Coin Configuration
    ↓  configurePortal / configureCoin → PortalConfigManager, CoinManager
Validation → Summary → Finish
    ↓  validate → validation_passed; finish → ready + SSE installation.completed
Ready
    ↓  Captive portal live; admin dashboard fully functional
Customer Uses Piso WiFi
    ↓  PortalServer → guest UI; /api/portal/* session APIs
    ↓  CoinManager ISR → PortalSessionManager; optional RouterPlatform authorize
Fleet Management (owner)
    ↓  Client discovers via /api/health; registry by deviceId
    ↓  Switch device → retarget API base (browser) or WebView URL (Android)
Updates
    ↓  POST /api/system/firmware (OTA via ApiServer); SD path /firmware/update.bin reserved
Factory Reset
    ↓  Wizard: provisioning factory-reset (setup only)
    ↓  System settings: /api/system/factory-reset (data wipe — ⚠️ installation state gap)
```

**Resume / recovery:** `GET /api/provisioning/installation/resume` + `workflowStep` mapping in `stepRouter.ts` align with firmware state machine. Session recovery via `setupSessionRecovery.ts`. Power-loss mid-wizard: state monotonic, UI back button does not regress firmware state — ✅ per contract.

---

## 6. Browser vs Mobile Parity

| Area | Parity | Notes |
|------|--------|-------|
| Discovery | ✅ | Same `/api/health` + subnet default `192.168.88` |
| Provisioning flow | ✅ | Android uses same SPA in WebView |
| Fleet device switch | ✅ | Different mechanism, same outcome |
| Portal/coin setup screens | ✅ | Identical via WebView |
| Validation / summary | ✅ | Identical via WebView |
| Session re-auth on switch | ⚠️ | Browser preserves route; Android resets to dashboard |
| Fleet mode flag | ⚠️ | Browser explicit; Android implicit (multi-device) |
| Native frozen models in Kotlin | N/A | By design — SPA is canonical |

**Conclusion:** Functional parity achieved. UX differences are acceptable for Phase 7B; document in FleetHealth spec if route preservation matters on mobile.

---

## 7. Frozen Contract Verification

| Contract | Runtime alignment | Contradictions |
|----------|-------------------|----------------|
| **HTTP_ROUTE_CONTRACT** | Routes match providers; `/health` and `/downloads/*` unimplemented | Minor — reserved/stub |
| **HTTP_URL_CONTRACT** | Lists MikroTikManager as non-route owner (deleted) | Doc stale |
| **DEVICE_PROFILE_CONTRACT** | `DeviceIdentity.cpp` fills frozen fields | ✅ |
| **INSTALLATION_WORKFLOW** | Engine + UI step mapping | ✅ |
| **PROVISIONING_API** | All endpoints in ProvisioningServer | ✅ |
| **SETUP_WIZARD_UI_CONTRACT** | Mostly aligned | Ready redirect missing |
| **STORAGE_ARCHITECTURE** | StoragePaths + owners | Legacy paths active (documented) |
| **ASSET_LIFECYCLE** | AssetManager pipeline | Backup uses legacy paths |
| **PORTAL_CONFIG_ARCHITECTURE** | Split in progress | Shared portal.json (documented) |
| **ContractVersions.h** | v1 for device profile, storage, HTTP | ✅ |

**Contract versions:** `ContractVersions.h` + `scripts/contract-versions.mjs` — keep in sync when bumping.

---

## 8. Risks for Future Phases

| Risk | Phase impact | Likelihood |
|------|--------------|------------|
| System factory reset leaves `installation.json` at `ready` | 7B FleetHealth may show "ready" appliances with empty config | **High** |
| Dual SessionManager / PortalSessionManager sales paths | 7B aggregated sales misleading if both paths used | Medium |
| Backup archive path mismatch | 7B export/download features | Medium |
| ApiServer size / BackupManager embedding | 7B `/downloads/*`, fleet export endpoints | Medium |
| `/api/status` mikrotik-shaped JSON | 7B FleetHealth router status normalization | Low |
| Captive portal hardcoded API_BASE in shipped `renzfi-app.js` | Deployments on non-default IP subnets | Low (portal uses relative paths in SPIFFS build ideally) |
| DownloadServer stub | 7B owner downloads | Expected — implement when needed |

---

## 9. Technical Debt

| Category | Items |
|----------|-------|
| **Legacy paths** | `/vouchers/vouchers.json`, `/www/*`, `/backup/*` vs contract paths |
| **Deleted code references** | `src/README.md`, `HTTP_URL_CONTRACT.md` still mention MikroTikManager |
| **Parallel session systems** | SessionManager (admin) vs PortalSessionManager (portal) |
| **Incomplete migrations** | PortalConfigManager config-only split (Phase 3B target); DownloadServer; `/health` alias |
| **Documentation** | Phase 4A reports reference RenzFiPortalRoutes (deleted) |
| **UI contract gap** | Setup ready redirect; CompleteScreen firmware field |
| **Composition** | BackupManager owned by ApiServer |

No blocking dead code in runtime path. `DownloadServer` is intentional stub. `MikroTikManager` fully removed from build.

---

## 10. Recommendations Before Phase 7B

Prioritized **minimal** corrections — no refactors, no feature work.

### P0 — Fix before FleetHealth UI

1. **System factory reset must reset installation state**  
   - **File:** `ESP32_S3_Firmware/src/BackupManager.cpp` (`performFactoryReset` / `wipeUserData`)  
   - **Why:** Prevents "ready" ghost state after full wipe  
   - **Fix:** Inject `InstallationStateManager*` and call `resetToFactory()`, or delete `StoragePaths::InstallationFile`

2. **Wire setup ready redirect**  
   - **File:** `src/pages/setup/SetupWizardPage.tsx`  
   - **Why:** Matches frozen `SETUP_WIZARD_UI_CONTRACT.md` §2.1  
   - **Fix:** `<ProvisioningProvider enabled onReady={handleExit} onAborted={handleExit}>`

### P1 — Doc hygiene (no runtime change)

3. Update `ESP32_S3_Firmware/src/README.md` and `HTTP_URL_CONTRACT.md` — remove MikroTikManager references; add ProvisioningServer to route ownership table in `HTTP_ROUTE_CONTRACT.md`

### P2 — Defer to when touching those areas

4. Align `BackupManager` archive paths with `StoragePaths.h` contract paths  
5. Move `BackupManager` to `FirmwareApp` composition root  
6. `CompleteScreen.tsx`: use `summary.applianceFirmwareVersion` only (remove `finishSummary` bypass)  
7. Implement `/health` alias or mark explicitly deferred in contract  
8. Normalize `/api/status` router block to driver-agnostic shape (keep mikrotik as optional nested key for compat)

---

## Regression Search Summary

| Pattern searched | Finding |
|------------------|---------|
| Duplicate logic | Sales recording in PortalSessionManager + SessionManager |
| Duplicate storage | portal.json shared by AssetManager + PortalConfigManager (intentional) |
| Duplicate APIs | Two factory-reset endpoints (different scope — second incomplete) |
| Duplicate managers | SessionManager vs PortalSessionManager (legacy parallel) |
| Dead / obsolete code | MikroTikManager, RenzFiPortalRoutes — removed; docs stale |
| Unused routes | DownloadServer stub only |
| Duplicate models | None in setup wizard frozen stack |
| Hidden coupling | CoinManager ↔ PortalSessionManager runtime cycle |
| Manager leaks in UI | None in setup wizard |
| Contract violations | Ready redirect; system factory reset scope |

---

## Final Assessment

The Renz-Fi appliance architecture is **coherent, layered, and contract-driven**. The composition root, provisioning boundary, web platform demux, router abstraction, and client-side fleet model form a **stable foundation** for Phase 7B FleetHealth and beyond.

**Phase 7B can proceed** without reopening Phases 3–7A design. Address **P0 items** (installation state on system factory reset; setup ready redirect) early in 7B to avoid fleet status false positives and wizard edge cases.

No frozen contracts require redesign. No working code requires refactoring for verification purposes.

---

*Generated by architecture verification pass — no firmware or application code was modified.*
