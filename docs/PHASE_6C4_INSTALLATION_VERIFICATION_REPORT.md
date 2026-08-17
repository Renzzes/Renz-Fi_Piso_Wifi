# Phase 6C.4 — Installation Verification & Completion Report

**Status:** Complete  
**Build:** `npm run build` passes  
**Firmware:** Unchanged  

---

## 1. Files modified

| File | Change |
|------|--------|
| `src/models/ValidationResult.ts` | **New** — canonical validation read model + parsers |
| `src/models/InstallationReport.ts` | **New** — `InstallationReport` + `buildInstallationReport()` |
| `src/pages/setup/validationDraft.ts` | **New** — sessionStorage for validation results |
| `src/pages/setup/screens/ValidationScreen.tsx` | Installation verification UI |
| `src/pages/setup/screens/SummaryScreen.tsx` | Installation summary UI |
| `src/pages/setup/screens/CompleteScreen.tsx` | Finish + completion UI |
| `src/pages/setup/SetupWizardPage.tsx` | Self-nav for validation/complete; dashboard redirect via Complete screen |
| `src/pages/setup/screens/WelcomeScreen.tsx` | Clears validation draft on factory reset |
| `src/services/provisioning/provisioningClient.ts` | Typed `validate` / `finish` |
| `src/types/routerProvisioning.ts` | `ValidateInstallationResponse`, `FinishInstallationResponse` |
| `server/services/provisioningSimulator.ts` | `validateInstallation`, `finalizeInstallation` |
| `server/routes/provisioning.ts` | Wired validate/finish routes |

**Not modified (per phase scope):** firmware, `SetupSummaryModel`, `ProvisioningContext`, `WizardShell`, Setup UI Kit, installation workflow docs, HTTP contracts.

---

## 2. Validation flow

1. User arrives at **Installation Verification** after coin configuration (`coin_configured`).
2. Screen auto-runs `POST /api/provisioning/validate` on mount (or resumes cached draft).
3. Backend returns `checks[]` with `id`, `passed`, `detail` (optional `severity`).
4. `parseValidationResults()` maps checks into the seven-category catalog:
   - Router, Portal, Coin, Storage, Assets, Network, Firmware
5. Results render in `SetupStatusList` / `SetupStatusCard` (success, warning, error, pending).
6. On failure: `SetupInfoBanner` with actionable guidance; **Retry** and **Go to step** (router/portal/coin).
7. On success (`passed === true`): workflow advances to `validation_passed`; user continues to **Summary** (auto via `applyWorkflowData` or **Continue**).
8. Resume: if state ≥ `validation_passed`, skips to Summary.

---

## 3. Summary flow

1. User reviews **Installation Summary** after validation passes.
2. Screen calls `buildInstallationReport()` only — no screen-level field assembly.
3. Displays installer, elapsed time, router, driver, firmware, portal, coin, progress, completed steps via `SetupReadOnlyValue` and format helpers from `InstallationReport.ts`.
4. Warning banner if validation incomplete (state < `validation_passed`).
5. Wizard shell **Finish setup** navigates to **Complete** (requires `validation_passed` per navigation guards).

---

## 4. Completion flow

1. **Installation Complete** screen mounts after Summary **Finish setup**.
2. Calls `POST /api/provisioning/finish` once (skipped if already `ready`).
3. On success: shows success status, elapsed time, router/portal/coin/firmware summary from `SetupSummaryModel`.
4. **Go to dashboard** → `/dashboard` via React Router.
5. **Restart setup** (optional) → factory reset + clear drafts.
6. On failure: **Retry** or **Back to validation**.

Dashboard auto-redirect on SSE was removed from `SetupWizardPage` so the Complete screen is shown before the user chooses **Go to dashboard**.

---

## 5. ValidationResult model

```typescript
interface ValidationResult {
  id: string;
  title: string;
  description: string;
  passed: boolean;
  severity: "info" | "warning" | "error";
}
```

**Location:** `src/models/ValidationResult.ts`

**Helpers:** `parseValidationResults()`, `validationResultToSetupStatus()`, `validationGuidanceFor()`, `validationCheckTargetScreen()`, `partitionValidationResults()`

Reusable by future Diagnostics, Health, and Support Bundle features.

---

## 6. InstallationReport model

```typescript
interface InstallationReport {
  summary: SetupSummaryModel;
  validation: ValidationResult[];
  warnings: ValidationResult[];
  recommendations: ValidationResult[];
  generatedAt: string;
}
```

**Location:** `src/models/InstallationReport.ts`

**Builder:** `buildInstallationReport()` — composes `buildSetupSummaryModel()` + validation draft.

**Format helpers:** `formatReportValue()`, `formatRouterSummaryLine()`, etc. — display only, no summary assembly.

---

## 7. API endpoints used

| Method | Endpoint | Purpose |
|--------|----------|---------|
| `POST` | `/api/provisioning/validate` | Run installation checks; advance to `validation_passed` when all pass |
| `POST` | `/api/provisioning/finish` | Finalize installation; advance to `ready`; emit completion |

Both via `provisioningClient` only.

---

## 8. SSE verification

Existing `ProvisioningContext` subscription unchanged:

| Event | Use in 6C.4 |
|-------|-------------|
| `installation.progress` | Validate/finish progress messages in `SetupInfoBanner` |
| `installation.state_changed` | Context state merge (unchanged) |
| `installation.completed` | Emitted by simulator on finish; Complete screen shows finalized state |
| `installation.aborted` | Unchanged |

No new events or context providers added.

---

## 9. Build status

```
npm run build — SUCCESS
```

---

## 10. Remaining work — Phase 6C.5

Suggested follow-ups (not in scope for 6C.4):

- **Post-setup polish:** first-run dashboard tour, empty-state hints for Captive Portal uploads
- **Diagnostics page:** reuse `ValidationResult` + `buildInstallationReport()` for live health checks
- **PDF / support bundle export:** serialize `InstallationReport` (no UI in 6C.4)
- **Production validate UX:** firmware returns HTTP 400 on failed validate — consider surfacing `checks` from error envelope if firmware adds them later
- **Admin coin page:** optional refactor to import `applianceConfiguration` mappers

---

## Setup Wizard status

All nine setup screens are implemented. The wizard architecture remains frozen; Phase 6C.4 completes the installation workflow end-to-end.
