# Setup Wizard Navigation & Router Edit UX

## Summary

Production UX update for the inline PROGMEM setup wizard (`SetupServer`). Adds persistent Back navigation on every wizard step, a Step 4 saved-state hub with **Edit Router Connection**, and blank-password preserve for router test/save when encrypted credentials already exist.

No changes to MikroTik provisioning apply logic, firewall rules, credential encryption format, installation-state downgrade paths, DNS/AP lifecycle, or GET-only router-plan preview.

## Wizard navigation (client-side only)

| Step | Panel | Back target |
|------|-------|-------------|
| 1 | Device Check | — |
| 2 | Owner Account | Step 1 |
| 3 | Router Connection Check | Step 2 |
| 4 form | MikroTik credentials | Step 3 (first entry) or Step 4 saved (edit) |
| 4 saved | Connection saved hub | Step 3 |
| 5 | Configure MikroTik | Step 4 saved (`Back to Router Connection`) |

Back navigation uses `showPanel()` only. It does not call setup APIs, downgrade `InstallationStateManager`, or clear persisted credentials.

## Step 4 saved state

When `router_configured` / wizard step `router_complete`, the saved panel shows:

1. **Configure MikroTik Router** → Step 5
2. **Edit Router Connection** → Step 4 form prefilled from `GET /api/setup/router-config`
3. **Back** → Step 3

## Blank-password preserve

| Endpoint | Blank password behavior |
|----------|-------------------------|
| `GET /api/setup/router-config` | Adds `hasSavedPassword` (boolean); never returns password |
| `POST /api/setup/router/test` | Uses saved encrypted password via `CredentialProtector` |
| `POST /api/setup/router/save` | Validates with saved password; re-encrypts only when a new password is supplied |

Password input in the wizard always starts blank. UI note: *Leave blank to keep the saved password.*

## Files changed

- `src/web/SetupServer.cpp` — wizard HTML/JS
- `src/SetupRouterConnectionManager.h` — `hasSavedPassword`, `resolvePasswordForRequest`
- `src/SetupRouterConnectionManager.cpp` — blank-password test/save
- `docs/HTTP_ROUTE_CONTRACT.md`
- `docs/NETWORK_PLANE_ARCHITECTURE.md`
- `docs/MOBILE_APP_API_CONTRACT.md`
- `docs/SETUP_WIZARD_NAVIGATION_REPORT.md` (this file)

## Build

```bash
pio run -e freenove_esp32_s3_wroom
```
