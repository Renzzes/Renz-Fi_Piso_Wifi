# Phase 6C.2 — Router Experience Implementation Report

**Status:** Complete (frontend only)  
**Date:** 2026-06-29  
**Scope:** Welcome, Router Detection, Driver Selection, Router Connection

---

## 1. Files modified

| File | Change |
|------|--------|
| `src/pages/setup/screens/WelcomeScreen.tsx` | Full implementation |
| `src/pages/setup/screens/RouterDetectionScreen.tsx` | Full implementation |
| `src/pages/setup/screens/DriverSelectionScreen.tsx` | Full implementation |
| `src/pages/setup/screens/RouterConnectionScreen.tsx` | Full implementation |
| `src/pages/setup/SetupWizardPage.tsx` | Self-nav screens hide shell Back/Next |
| `src/pages/setup/routerDraft.ts` | **New** — sessionStorage draft for cross-screen router state |
| `src/pages/setup/routerManifestUtils.ts` | **New** — `unsupportedReason`, merge helpers |
| `src/types/routerProvisioning.ts` | **New** — detect/select/connect response types |
| `src/services/provisioning/provisioningClient.ts` | Typed router endpoints |
| `src/contexts/ProvisioningContext.tsx` | Exposed `setLoading` (existing context only) |
| `src/components/setup/stepRouter.ts` | `resumeScreenForState`, `nextScreenAfterState` |
| `server/services/provisioningSimulator.ts` | Dev detect/select/connect/profiles |
| `server/routes/provisioning.ts` | Wired simulator router endpoints |

**Not modified:** Firmware, wizard shell/components, ProvisioningEngine, contracts.

---

## 2. Router Experience component hierarchy

```
SetupWizardPage
└── ProvisioningProvider (existing)
      └── WizardShell (existing)
            └── SetupCard (existing)
                  ├── WelcomeScreen
                  │     └── SetupForm → SetupFormSection / SetupInfoBanner / SetupActions
                  ├── RouterDetectionScreen
                  │     └── SetupForm → SetupInfoBanner / SetupEmptyState / driver cards
                  ├── DriverSelectionScreen
                  │     └── SetupForm → manifest / firmware fields / SetupInfoBanner
                  └── RouterConnectionScreen
                        └── SetupForm (RHF+Zod) → SetupStatusCard / SetupActions
```

---

## 3. Welcome flow

1. Mount → `bootstrap()` → `GET /installation/resume`
2. If `installation.ready` → redirect `/dashboard`
3. If `resumePrompt` → `ResumeGateModal` (existing)
4. User enters optional installer name
5. **Begin installation** → `POST /installation/begin` → `applyWorkflowData` → `router_detection`
6. **Resume installation** (if session + state ≠ factory) → `resumeScreenForState(state)`
7. **Factory reset** → confirm dialog → `POST /installation/factory-reset` (existing `startOver`)

---

## 4. Router detection flow

1. Mount → `GET /routers/detect`
2. Merge `available[]` + `drivers[]` via `mergeDriverEntries()`
3. Render driver cards (WizardTheme form sections, clickable)
4. Empty → `SetupEmptyState` preset `noRouters` + Retry
5. Card select → `writeRouterDraft({ selectedDriverId, manifest })` → `driver_selection`
6. SSE `installation.progress` updates header progress line

---

## 5. Driver selection flow

1. Load manifest from `routerDraft` (re-fetch detect if missing)
2. Display full `RouterDriverManifest` (vendor, model, firmware, features, capabilities)
3. User enters firmware name/version → client `unsupportedReason()` mirrors firmware
4. If unsupported → `SetupInfoBanner variant="error"`, Continue disabled
5. **Continue** → `POST /routers/select` → on success `applyWorkflowData` → `router_connection`

---

## 6. Router connection flow

1. Mount → `GET /routers/profiles` (when driver selected)
2. React Hook Form + Zod: host, username, password required
3. Profile via `SetupSelect` (if profiles returned) or manual `SetupInput`
4. **Test connection** → `POST /routers/connect`
5. Progress from SSE `installation.progress` (message in banner)
6. Result → `SetupStatusCard` success/error
7. On success → `applyWorkflowData` → `portal_configuration` (placeholder, 6C.3)

---

## 7. API calls used

| Screen | Method | Endpoint |
|--------|--------|----------|
| Bootstrap | GET | `/api/provisioning/installation/resume` |
| Welcome | POST | `/api/provisioning/installation/begin` |
| Welcome | POST | `/api/provisioning/installation/factory-reset` |
| Detection | GET | `/api/provisioning/routers/detect` |
| Driver | POST | `/api/provisioning/routers/select` |
| Connection | GET | `/api/provisioning/routers/profiles` |
| Connection | POST | `/api/provisioning/routers/connect` |

All via `provisioningClient` — no direct `fetch()` in screens.

---

## 8. Validation rules

| Field | Rule |
|-------|------|
| Host | Required (Zod trim + min 1) |
| Username | Required |
| Password | Required |
| Profile | Optional |
| Firmware (driver) | Client `unsupportedReason()` blocks Continue |
| Browser validation | Disabled (`noValidate` on SetupForm) |

Inline errors via `SetupFormField` `error` prop.

---

## 9. Error handling map

| Condition | UI |
|-----------|-----|
| HTTP 401 | Existing auth handler → login |
| HTTP 400 provisioning | `SetupInfoBanner variant="error"` + message from `ApiError` |
| Detect empty | `SetupEmptyState noRouters` |
| Firmware unsupported | `SetupInfoBanner variant="error"` (client + server) |
| Connect failure | `SetupInfoBanner` + `SetupStatusCard status="error"` |
| Connect success | `SetupStatusCard status="success"` |
| Bootstrap failure | Full-page retry panel (existing) |

---

## 10. Resume behavior verification

| State | Resume target screen |
|-------|---------------------|
| `factory` (with session) | `router_detection` |
| `router_selected` | `router_connection` |
| `router_connected` | `portal_configuration` |

`ResumeGateModal` unchanged — Continue dismisses modal; Welcome Resume uses `resumeScreenForState()`.

---

## 11. SSE integration verification

- Existing `useProvisioningEvents` in `ProvisioningContext` — no new events
- `installation.progress` → progress bar percent + message banners
- `installation.state_changed` → updates `installation.progressPercent`
- Screens read `progress?.message` from context during detect/select/connect

---

## 12. Remaining work — Phase 6C.3

| Screen | Work |
|--------|------|
| Portal Configuration | `POST /portal/configure`, branding verify UI |
| Coin Configuration | `POST /coin/configure`, hardware diagnostics form |
| Validation | `POST /validate`, `SetupStatusList` from checks[] |
| Summary | Review + `POST /finish` via **`SetupSummaryModel`** |
| Complete | Redirect dashboard on `installation.completed` |

### Pre-6C.3 additions

| Artifact | Purpose |
|----------|---------|
| `SetupSummaryModel.ts` | Single read model for Summary / Complete / exports |
| `routerManifestUtils.ts` | **Frozen** — all driver compatibility helpers |

---

## Success criteria checklist

| Criterion | Status |
|-----------|--------|
| Welcome functional | ✓ |
| Begin installation | ✓ |
| Resume flow | ✓ |
| Router detection | ✓ |
| Driver cards | ✓ |
| Driver selection | ✓ |
| Firmware compatibility messages | ✓ |
| Connection form | ✓ |
| Connection test | ✓ |
| SSE progress | ✓ |
| State → router_connected | ✓ |
| No placeholders on 4 screens | ✓ |
| No firmware changes | ✓ |
| Admin dashboard unaffected | ✓ |
