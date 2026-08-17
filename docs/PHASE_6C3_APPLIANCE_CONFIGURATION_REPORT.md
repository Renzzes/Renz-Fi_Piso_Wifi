# Phase 6C.3 — Appliance Configuration Report

**Status:** Complete  
**Build:** `npm run build` passes  
**Firmware:** Unchanged  

---

## 1. Files modified

| File | Change |
|------|--------|
| `src/lib/applianceConfiguration.ts` | **New** — canonical portal/coin config model, defaults, provisioning mappers |
| `src/pages/setup/applianceConfigDraft.ts` | **New** — sessionStorage draft for wizard appliance settings |
| `src/pages/setup/screens/PortalConfigurationScreen.tsx` | Functional portal configuration form + verify flow |
| `src/pages/setup/screens/CoinConfigurationScreen.tsx` | Functional coin configuration form + hardware verify |
| `src/pages/setup/SetupSummaryModel.ts` | Added `portalName`, `theme`, `coinEnabled`, `pricingProfile` |
| `src/pages/setup/SetupWizardPage.tsx` | Self-navigation for portal/coin screens |
| `src/pages/setup/screens/WelcomeScreen.tsx` | Clears appliance draft on factory reset |
| `src/services/provisioning/provisioningClient.ts` | Typed `configurePortal` / `configureCoin` |
| `src/types/routerProvisioning.ts` | `ConfigurePortalResponse`, `ConfigureCoinResponse` |
| `server/services/provisioningSimulator.ts` | Portal/coin configure handlers |
| `server/routes/provisioning.ts` | Wired simulator endpoints |

**Not modified (per phase scope):** firmware, `login.html`, `renzfi-app.js`, `WizardShell`, `ProvisioningContext`, UI kit components, Validation/Summary/Complete screens.

---

## 2. Portal configuration flow

1. User arrives after successful router connection (`router_connected`).
2. Form loads defaults from `applianceConfigDraft` (or `DEFAULT_PORTAL_APPLIANCE_CONFIG`).
3. User edits portal identity and feature toggles:
   - Portal name, welcome message, footer text, theme, language
   - Enable voucher / coin / auto-play music / pause / terminate buttons
4. **Verify portal** saves draft → `POST /api/provisioning/portal/configure` with `{ portal: { … } }`.
5. On success (`verified === true`):
   - `SetupStatusCard` shows revision, banner, and music status
   - Workflow advances to `portal_configured` → navigates to **Coin Configuration**
6. **Restore defaults** resets form to bundled captive portal defaults.

Banner/music uploads remain Admin Dashboard only (Captive Portal page).

---

## 3. Coin configuration flow

1. User arrives after portal verification (`portal_configured`).
2. Form loads from `applianceConfigDraft` (or `RECOMMENDED_COIN_DEFAULTS`).
3. User configures:
   - Coin enabled, pulse count, timeout, coin window (settle ms), minutes per peso
   - Pricing profile shown as read-only info (promo packages managed post-install)
4. **Restore recommended defaults** resets to factory-recommended values.
5. **Save and verify hardware** → `POST /api/provisioning/coin/configure` with `{ coin: { … } }`.
6. `SetupStatusCard` shows hardware diagnostics (`hardwareOk`, `hardware` payload).
7. On success (`hardwareOk` or `skipped`) → `coin_configured` → **Validation** (placeholder in 6C.4).

Coin fields map to the same `CoinManager` shape used by Admin Dashboard `/api/coin/settings` via `coinToAdminSettings()` / `coinFromAdminSettings()`.

---

## 4. API endpoints used

| Method | Endpoint | Purpose |
|--------|----------|---------|
| `POST` | `/api/provisioning/portal/configure` | Verify portal branding; advance to `portal_configured` |
| `POST` | `/api/provisioning/coin/configure` | Save coin settings, run diagnostics; advance to `coin_configured` |

All calls go through `provisioningClient` only. No legacy `/api/settings/*` or `/api/coin/*` from setup screens.

---

## 5. Summary model additions

`SetupSummaryModel` now includes (from `readApplianceConfigDraft()`):

| Field | Source |
|-------|--------|
| `portalConfigured` | Installation state ≥ `portal_configured` |
| `coinConfigured` | Installation state ≥ `coin_configured` |
| `portalName` | `appliance.portal.portalName` |
| `theme` | `appliance.portal.theme` |
| `coinEnabled` | `appliance.coin.enabled` |
| `pricingProfile` | `appliance.coin.pricingProfile` |

Build via `buildSetupSummaryModel()` only — screens do not duplicate summary assembly.

---

## 6. Build status

```
npm run build — SUCCESS (Vite production build)
```

---

## 7. Remaining work — Phase 6C.4

- **Validation screen** — `POST /api/provisioning/validate`, per-check `SetupStatusCard` list
- **Summary screen** — render full `SetupSummaryModel` (router, portal, coin, timing)
- **Complete screen** — `POST /api/provisioning/finish`, redirect to dashboard
- Dev simulator stubs for `/validate` and `/finish`
- Optional: refactor Admin `CoinSettingsPage` to import `applianceConfiguration` mappers (not required for wizard completion)

---

## Architecture notes

- **Single config model:** `src/lib/applianceConfiguration.ts` is the shared source for portal behaviour fields and coin settings. Wizard draft persists choices in sessionStorage until firmware accepts portal body fields.
- **Firmware today:** `configurePortal` verifies bundled/custom banner+music only (`body` ignored). Portal form values are sent in the provisioning payload for forward compatibility; captive portal HTML/JS unchanged.
- **No duplicate systems:** Coin wizard uses the same field semantics as `CoinManager` / Admin coin settings. Portal media uploads stay on Admin Captive Portal page.
