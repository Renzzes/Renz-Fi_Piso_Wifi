# Phase 6D — Production Hardening & Release Readiness Report

**Status:** Complete  
**Build:** `npm run build` succeeds  
**Firmware:** Unchanged  
**Frozen contracts:** Preserved (no changes to ProvisioningContext, ProvisioningClient, WizardShell, Setup UI Kit, SetupSummaryModel, ValidationResult, InstallationReport)

---

## 1. Architecture verification

| Area | Status |
|------|--------|
| Setup wizard 9-screen flow | Unchanged functionally |
| Provisioning API (`/api/provisioning/*`) | Unchanged contracts |
| Frozen UI kit & context | Not modified |
| Captive portal files | Not modified |
| Firmware architecture | Not modified |

Hardening was limited to **transport layer**, **screen-level UX**, **session recovery**, **code splitting**, and **documentation**.

---

## 2. Files modified

### Network & API

| File | Change |
|------|--------|
| `src/services/api.ts` | `NetworkError`, 60s timeout, `AbortSignal` merge, offline detection |

### Setup hardening utilities

| File | Change |
|------|--------|
| `src/hooks/setup/useOnlineStatus.ts` | **New** — browser online/offline |
| `src/hooks/setup/useSetupSubmitGuard.ts` | **New** — duplicate submission prevention |
| `src/hooks/setup/useFocusRestore.ts` | **New** — focus after async actions |
| `src/hooks/setup/useSetupMountOnce.ts` | **New** — StrictMode-safe mount effects |
| `src/pages/setup/setupErrorMessages.ts` | **New** — consistent error text |
| `src/pages/setup/setupSessionRecovery.ts` | **New** — screen + scroll persistence |

### Wizard orchestration

| File | Change |
|------|--------|
| `src/pages/setup/SetupWizardPage.tsx` | Offline/SSE banners, session recovery, lazy Suspense, step transition |
| `src/pages/setup/screens/index.tsx` | Lazy-loaded screen registry |
| `src/pages/setup/screens/*.tsx` | Error handling, submit guards, mount-once, password clearing |

### App & build

| File | Change |
|------|--------|
| `src/App.tsx` | Lazy `/setup` route with Suspense |
| `vite.config.ts` | `setup-screens` manual chunk |
| `package.json` | `typecheck` script |

### Removed

| File | Reason |
|------|--------|
| `src/components/setup/SetupStepPlaceholder.tsx` | Dead code — no longer referenced |

### Documentation

| File | Change |
|------|--------|
| `docs/PHASE_6D_RELEASE_READINESS_REPORT.md` | This report |
| `docs/PRODUCTION_CHECKLIST.md` | Pre-validation checklist |
| `docs/KNOWN_LIMITATIONS.md` | Real current limitations |

---

## 3. UX improvements

- **Offline banner** on wizard when browser is offline; primary actions disabled where appropriate
- **SSE reconnect banner** when live event channel is disconnected
- **Step loading** via Suspense fallback (`Loading step…`) during lazy screen load
- **Consistent error messages** via `setupErrorMessage()` (network timeout, offline, API errors)
- **Double-submit prevention** on begin, detect, connect, validate, finish flows
- **Focus restoration** after router connection submit
- **Router password cleared** from form after successful connection
- **Retry buttons disabled** while in-flight
- **Bootstrap retry panel** with accessible `role="alert"`

---

## 4. Performance improvements

| Change | Effect |
|--------|--------|
| Lazy setup screens (`React.lazy`) | Smaller initial admin bundle |
| Lazy `SetupWizardPage` route | `/setup` code loaded on demand |
| `setup-screens` Rollup chunk | ~336 KB separate chunk (gzip ~104 KB) |
| Main entry reduced | ~360 KB (gzip ~107 KB) vs prior single ~694 KB JS bundle |
| `useSetupMountOnce` | Avoids duplicate provisioning calls in StrictMode |

No premature `React.memo` added — hot paths are mount/submit guarded instead.

---

## 5. Security improvements

- **Existing route guards** unchanged — `/setup` requires login + no forced password change
- **401 handling** preserved via `handleUnauthorizedResponse`
- **Sensitive field clearing** — router password cleared after successful connect
- **Double-submit prevention** reduces duplicate provisioning mutations
- **Session idle timeout** unchanged (admin-wide)
- **No new auth surfaces** introduced

---

## 6. Network stability

| Feature | Implementation |
|---------|----------------|
| Request timeout | 60s default in `apiFetch` |
| AbortSignal | Caller can pass `signal` in `RequestInit` |
| Offline detection | Pre-fetch check + `NetworkError` on failure |
| Duplicate submissions | `useSetupSubmitGuard` on key screens |
| Progress preservation | sessionStorage drafts + backend installation state |

**Note:** `ProvisioningClient` was not modified (frozen). Timeout/offline apply to all `api.*` consumers including provisioning calls.

---

## 7. Session recovery

| Scenario | Behaviour |
|----------|-----------|
| Browser refresh | `resume` bootstrap + restore persisted screen if allowed by navigation guards |
| Tab reload | Same as refresh |
| Scroll position | Saved/restored via `setupSessionRecovery` |
| Draft data | router, appliance, validation drafts in sessionStorage |
| Factory reset / restart | Clears persisted screen + scroll |

---

## 8. Code cleanup

- Removed unused `SetupStepPlaceholder.tsx`
- Removed unused imports from several screens
- Centralized setup error messaging
- Added `npm run typecheck` script

**Pre-existing (not fixed in 6D — out of scope):**

- `tsc` errors in unused `carousel.tsx` / `chart.tsx` (missing optional deps)
- `ProvisioningContext` SET_PROGRESS payload typing (frozen file)
- ESLint CRLF warnings on Windows legacy paths

---

## 9. Build verification

```
npm run build — SUCCESS

dist/assets/index-*.js          ~360 KB (gzip ~107 KB)
dist/assets/setup-screens-*.js  ~336 KB (gzip ~104 KB)
dist/assets/SetupWizardPage-*.js ~6 KB (gzip ~2 KB)
```

No circular chunk warning after manual chunk adjustment.

---

## 10. Production checklist

See [`docs/PRODUCTION_CHECKLIST.md`](PRODUCTION_CHECKLIST.md).

---

## 11. Known limitations

See [`docs/KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md).

---

## 12. Known risks

| Risk | Mitigation |
|------|------------|
| Non-MikroTik drivers are stubs | Validate on MikroTik hardware first |
| Portal wizard fields not yet applied to captive portal HTML | Documented; banner/music via Admin |
| Validation 400 on firmware hides per-check details | Retry + go-to-step; simulator shows full checks |
| SSE 3s fixed reconnect | Acceptable for LAN; monitor during field test |

---

## 13. Recommendations before hardware validation (Phase 4C)

1. Run through [`PRODUCTION_CHECKLIST.md`](PRODUCTION_CHECKLIST.md) on a real ESP32 + MikroTik bench.
2. Verify coin acceptor diagnostics on hardware with `ENABLE_COIN_MANAGER` build.
3. Confirm SD storage check passes with card inserted.
4. Test browser refresh at each wizard step (router → complete).
5. Test LAN disconnect mid-step — confirm offline banner and successful retry.
6. Flash production bundle via `npm run build:esp32` and verify `/setup` on appliance.
7. Do **not** attempt TP-Link / Ruijie / OpenWRT installation until driver protocols are implemented.

---

## 14. Readiness assessment

| Criterion | Result |
|-----------|--------|
| Functionality unchanged | ✓ |
| UX more polished | ✓ |
| Stability improved | ✓ |
| Performance optimized (meaningful paths) | ✓ |
| Security reviewed | ✓ |
| Dead code removed | ✓ |
| Build succeeds | ✓ |
| Documentation created | ✓ |
| Ready for Phase 4C hardware validation | **Yes** (MikroTik + coin path) |

---

## 15. Remaining work (post-6D, not in scope)

- Phase 4C hardware validation on bench
- Diagnostics / Health pages (reuse `ValidationResult`)
- PDF / support bundle (reuse `InstallationReport`)
- Admin coin page refactor to shared `applianceConfiguration` mappers
- Fix pre-existing strict TypeScript issues in unused UI components
- CI: add `typecheck` + lint scoped to `src/` excluding build output
