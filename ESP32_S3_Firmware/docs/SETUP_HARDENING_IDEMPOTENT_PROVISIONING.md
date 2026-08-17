# Renz-Fi v1.0 Setup Wizard Hardening and Idempotent Provisioning

## Scope

This change hardens setup execution without changing captive portal behavior, admin dashboard behavior, RouterOS transport logic, or existing API response schemas.

## Implemented Changes

### 1) Idempotent MikroTik provisioning updates

`RouterProvisioningManager::applyConfiguration()` now performs idempotent apply behavior for the setup-managed foundation objects:

- Bridge (`/interface/bridge`)
- Guest gateway address (`/ip/address`)
- DHCP pool (`/ip/pool`)
- DHCP server (`/ip/dhcp-server`)
- DHCP network (`/ip/dhcp-server/network`)
- ESP32 API firewall allow rule (`/ip/firewall/filter`)

Behavior per resource:

- Detect object existence
- If missing: add object
- If existing and managed target: compare relevant fields
- If different: update only changed fields
- If identical: do nothing
- If conflict with non-managed object: fail safely with conflict response

No duplicate creates are performed for these setup-managed resources.

### 2) Setup Unlock Password protection for re-entry

Added setup lock model in `SetupProvisioningManager`:

- Stores hashed setup unlock password in provisioning metadata
- Uses hash mechanism aligned with owner credential hashing (`AuthCredentials::hashPassword`)
- Uses default factory unlock password `renzfi-setup` when provisioning metadata is absent
- Opens a temporary **in-memory unlock session** (`20 minutes`) after a correct password
- Session stays valid across wizard pages (no re-prompt per step)
- Session ends when:
  - the timer expires, or
  - Finish completes, or
  - Cancel is clicked (`POST /api/setup/lock`)
- On session end, setup locks again and re-entry restores `Ready` if the wizard was reopened
- Provides lock/unlock helpers:
  - `unlockSetup(password)`
  - `lockSetup()` / `closeUnlockedSetup()`
  - `enforceActiveUnlockSession()`
  - `hasActiveSetupUnlockSession()`
  - `requiresSetupUnlock()`

### 3) Setup wizard route protection

`SetupServer` now:

- Serves a locked setup page on `GET /admin/setup` when installation is already configured and unlock session is not active
- Adds `POST /api/setup/unlock` to validate unlock password and open a temporary setup session (~20 minutes)
- Adds `POST /api/setup/lock` to close setup session explicitly (Cancel) and restore Ready
- Reopens setup lifecycle on successful unlock (`InstallationStateManager::reopenSetupWizard()`)
- Wizard UI shows remaining session time and a Cancel control during unlocked re-entry

All setup write routes continue to use existing flow, but are gated by `ensureSetupWizardEnabled(...)`, which now enforces an active unlock session for re-entry (including after the wizard is reopened).

### 4) Owner setup payload extension

Owner creation now accepts:

- `setupUnlockPassword`
- `confirmSetupUnlockPassword`

Validation:

- Minimum length 8
- Confirmation must match

If omitted, setup unlock defaults to owner password behavior at creation time.

### 5) Captive portal stage changed to verification-only (manual / non-blocking)

Finish pipeline in `RouterProvisioningEngine::runFinishPipeline()`:

- Replaced `portal-upload` stage with `portal-verify`
- No `/tool/fetch` execution for portal files
- No portal file upload attempts during finish
- Default portal deployment mode: **MANUAL_EXTERNAL**
  - One bounded `/file/print` inventory (or zero queries when `SKIPPED`)
  - Missing/unconfirmed portal files → `portalStatus=unverified`, **Finish continues**
- `SKIPPED` (Skip for Now): no file enumeration; Finish continues
- `MANAGED` only: verification failure may block Finish
- Essential check (when verifying): `login.html` under profile `html-directory`
- Do not treat ESP32 admin assets as captive portal files

See `SETUP_FINISH_PORTAL_VERIFY_ROOT_CAUSE.md` for the forensic incident write-up.

### 6) Finish hardening and setup re-lock

On successful finish completion:

- Setup unlock session is locked again (`closeUnlockedSetup()`)
- Existing finalization flow remains intact

`POST /api/setup/complete` also locks setup session before reboot completion path.

### 7) Admin handoff URL adjustment

Production handoff dashboard URL now targets:

- `http://<eth-ip>/login`

This aligns setup completion redirect behavior with admin entry standardization.

## Setup Wizard Frontend Updates

`SetupWizardPageHtml.h` updates:

- Owner step now includes setup unlock password fields
- Client-side validation for unlock password
- Finish progress label updated from `portal-upload` to `portal-verify`
- Dashboard URL fallback updated to `/login`

No wizard step count/order changes were introduced.

## Performance and Stability Notes

- No new blocking loops introduced
- Existing async worker model retained
- No high-frequency polling introduced
- No RouterOS flood behavior added
- Existing queue and pacing mechanisms retained

## Validation Checklist Mapping

Implemented and verifiable:

- Setup reruns avoid duplicate foundation objects
- Existing managed objects are updated when drifted
- Setup unlock password required for post-install setup re-entry
- Factory/default unlock fallback is available when provisioning data is absent
- Portal upload is skipped; portal package is verified only
- Finish pipeline remains async and reaches ready path with existing lifecycle controls

## Build Verification

Firmware build verified:

- Environment: `freenove_esp32_s3_wroom`
- Status: `SUCCESS`

