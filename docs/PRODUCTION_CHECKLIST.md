# Production Checklist — Renz-Fi Admin Dashboard

Use this checklist before Phase 4C hardware validation and field deployment.

## GENERAL

- [ ] `npm run build` succeeds
- [ ] `npm run typecheck` passes (or known pre-existing issues documented)
- [ ] `npm run lint` passes on `src/` (exclude build artifacts)
- [ ] Production bundle generated in `dist/`
- [ ] PWA icons generated (`public/icons/`)

## WIZARD

- [ ] Welcome — begin, resume, factory reset
- [ ] Router Detection — detect, retry, driver selection
- [ ] Driver Selection — firmware compatibility, continue
- [ ] Router Connection — credentials, profile, test connection
- [ ] Portal Configuration — verify portal, defaults
- [ ] Coin Configuration — settings, hardware verify, restore defaults
- [ ] Validation — auto-checks, retry, go to step, continue
- [ ] Summary — read-only review from `SetupSummaryModel`
- [ ] Complete — finish, dashboard redirect, restart setup

## PROVISIONING

- [ ] Resume (`GET /api/provisioning/installation/resume`)
- [ ] Abort (`POST /api/provisioning/installation/abort`)
- [ ] Factory Reset (`POST /api/provisioning/installation/factory-reset`)
- [ ] Progress (`installation.progress` SSE)
- [ ] State changed (`installation.state_changed` SSE)
- [ ] Finish (`POST /api/provisioning/finish`)
- [ ] Completed (`installation.completed` SSE)

## SESSION RECOVERY

- [ ] Browser refresh restores active screen (when state permits)
- [ ] Scroll position restored after refresh
- [ ] Router/appliance/validation drafts preserved in sessionStorage
- [ ] Offline banner shown; progress preserved during disconnect

## PORTAL

- [ ] `login.html` unchanged
- [ ] `renzfi-app.js` unchanged
- [ ] `renzfi-style.css` unchanged
- [ ] Portal configuration reuses `applianceConfiguration` model
- [ ] Banner/music uploads remain in Admin Captive Portal page

## ROUTER

- [ ] MikroTik driver — full provisioning flow
- [ ] Driver detection and manifest display
- [ ] Firmware compatibility check
- [ ] Connection test with profile
- [ ] Retry and recovery after failure

## COIN

- [ ] Recommended defaults applied
- [ ] Diagnostics / hardware status displayed
- [ ] Settings map to `CoinManager` field semantics

## ASSETS

- [ ] Banner handled by Admin Dashboard only
- [ ] Music handled by Admin Dashboard only
- [ ] AssetManager / firmware unchanged

## SECURITY

- [ ] `/setup` requires authenticated session
- [ ] Password change gate enforced
- [ ] 401 redirects to login
- [ ] Session idle timeout active
- [ ] Router password cleared from form after successful connect
- [ ] Double-submit prevention on provisioning actions

## AUTH

- [ ] Login flow works against appliance
- [ ] Connection lost overlay on admin API failure
- [ ] Logout clears session

---

**Sign-off:** _________________ **Date:** _________________
