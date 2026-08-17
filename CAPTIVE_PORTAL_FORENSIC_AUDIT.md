# Renz-Fi Captive Portal — Final Forensic Audit

**Audit date:** 2026-08-09  
**Scope:** Repository-wide, read-only source audit  
**Production verdict:** **NOT READY**

## Executive conclusion

The intended editable source of truth is:

```text
portal/
```

It is not yet an effective repository source of truth because the portal source
and pipeline are currently untracked. A clean clone of the current commit cannot
reproduce the inspected portal.

Production guest traffic follows this path:

```text
portal/
  -> scripts/build-mikrotik-portal.mjs
  -> deployment/mikrotik-hotspot/
  -> manual Winbox/FTP upload
  -> MikroTik Files/hotspot/
  -> guest browser
  -> ESP32 /api/portal/* backend
```

The reported captive redirect regression remains unresolved. The strongest
source-supported causes are:

1. MikroTik Hotspot bound to the wireless slave instead of the guest bridge.
2. Incomplete manual upload or an incorrect `html-directory`.
3. Missing RouterOS servlet templates such as `alogin.html` and `redirect.html`.
4. A disabled, missing, or profile-mismatched Hotspot server.

The exact cause cannot be proven from repository source alone. Live RouterOS
configuration, files, host-table state, and an unauthenticated HTTP trace are
required.

No portal folder deletion is approved.

## Critical blockers

- Captive redirect is currently reported broken.
- `portal/` and the new portal build pipeline are untracked.
- `npm run test:portal` fails.
- MikroTik deployment is manual and has no remote checksum verification.
- The generated MikroTik directory is not cleaned before rebuilding.
- The generated bundle omits RouterOS redirect/status servlet templates.
- Admin-generated vouchers are not source-proven to be provisioned to RouterOS.
- Router worker payload publication has a concurrency race.
- Deferred portal work can be dropped when queues are full or unavailable.
- Guest APIs trust a caller-supplied MAC address.
- Portal session persistence has a lost-dirty-update race.
- SPIFFS fallback can cause excessive full-session writes.

## Repository ownership map

| Folder | Purpose | Classification | Decision |
|---|---|---|---|
| `portal/` | Editable guest portal source | Source, currently untracked | **KEEP — intended SSoT** |
| `Captive Portal/` | Deprecated RouterOS sample tree | Legacy, untracked | **KEEP until hardware validation** |
| `deployment/mikrotik-hotspot/` | Upload bundle and deployment instructions | Mixed generated/manual | Keep docs; regenerate assets |
| `ESP32_S3_Firmware/data/portal/` | SPIFFS setup/recovery staging | Generated | Regenerate; do not edit |
| `public/` | Admin SPA assets and portal default inputs | Source | Keep |
| `dist/` | Admin SPA build | Generated | Regenerable |
| `server/data/portal/` | Simulator upload storage | Runtime data | Separate from production portal |

## Runtime architecture

### MikroTik responsibilities

- Intercepts unauthenticated guest HTTP traffic.
- Serves `login.html` and its local assets.
- Expands RouterOS `$(...)` variables.
- Performs CHAP/PAP Hotspot authentication.
- Uses native servlet templates for login, redirect, status, logout, and errors.
- Enforces Hotspot users, active sessions, cookies, profiles, and rate limits.

### ESP32 responsibilities

- Serves `/api/portal/*` session, coin, rates, heartbeat, and branding APIs.
- Serves revisioned banner and music assets.
- Emits `portal.changed` and heartbeat Server-Sent Events.
- Stores authoritative Renz-Fi portal session state.
- Serializes RouterOS mutations through `router_worker`.
- Serves the Admin SPA.
- Serves setup/recovery portal files from SPIFFS.

ESP32 `/portal` is a recovery/development fallback. It is not the production
customer captive portal.

### Guest communication path

```text
Guest browser
  -> MikroTik Hotspot interception
  -> MikroTik-hosted login.html
  -> MikroTik-hosted renzfi-app.js
  -> ESP32 /api/portal/*
  -> PortalSessionManager
  -> router_worker
  -> RouterOS API
```

The MikroTik walled garden must allow the ESP32 appliance address before guest
authentication.

## Build and deployment pipeline

### MikroTik build

`scripts/build-mikrotik-portal.mjs`:

- Reads files from `portal/`.
- Replaces `__RENZFI_APPLIANCE_BASE_URL__`.
- Writes the upload bundle to `deployment/mikrotik-hotspot/`.
- Copies login HTML, JavaScript, CSS, MD5 support, favicon, banner, and optional
  audio.
- Generates `admin.html`.

Limitations:

- It does not clean the destination first.
- Removed optional source files can remain in output.
- Unmanaged files can remain beside generated files.
- It does not upload anything to MikroTik.
- It does not compare local and remote hashes.
- It does not generate the complete RouterOS servlet set.

### MikroTik configuration script

`deployment/mikrotik-hotspot/upload-hotspot-files.rsc`:

- Sets `html-directory=hotspot`.
- Adds a walled-garden exception for the ESP32 appliance.

It does **not**:

- Upload portal files.
- Validate remote file hashes.
- Repair Hotspot interface topology.
- Confirm that guests are intercepted.
- Restore missing RouterOS servlet templates.

### ESP32 staging

`scripts/stage-esp32-data.mjs`:

- Recreates `ESP32_S3_Firmware/data/`.
- Stages the Admin SPA.
- Stages the portal subset needed for setup/recovery.
- Excludes large portal audio from SPIFFS.

This pipeline is safer because its destination is recreated. Build metadata and
Vite timestamps still prevent fully byte-identical output.

## Redirect regression analysis

### 1. Wrong Hotspot interface

If DHCP and the guest gateway are attached to a bridge but the Hotspot server is
attached only to a wireless slave interface, guests can receive network access
without entering the Hotspot interception path.

This best explains a condition where files remain present but captive redirect
stops entirely.

Required evidence:

```routeros
/ip hotspot print detail
/ip hotspot host print detail
/interface bridge port print detail
/ip dhcp-server print detail
/ip address print detail
```

### 2. Manual deployment drift

The repository cannot establish which files are installed on the router. The
deployment process requires a human to upload files using Winbox or FTP.

Required evidence:

```routeros
/file print detail
/ip hotspot profile print detail
```

Verify that the active profile points to the directory containing the current
`login.html`, JavaScript, CSS, MD5 helper, and RouterOS servlet templates.

### 3. Missing RouterOS servlet templates

The current builder does not emit:

- `alogin.html`
- `redirect.html`
- `status.html`
- `logout.html`
- `error.html`
- `rlogin.html`
- `radvert.html`
- `errors.txt`
- WISPr XML templates

If an operator replaced the router's complete `hotspot/` directory with only the
current generated bundle, post-authentication redirect and status/error behavior
may fail.

### 4. Disabled or mismatched Hotspot

The `.rsc` deployment helper changes profile settings but does not repair a
disabled, missing, or incorrectly bound Hotspot server.

### 5. Captive assistant versus HTTP redirect

OS captive-assistant popup behavior and MikroTik HTTP interception are separate.
HTTPS interception is unreliable. Validation must include a plain HTTP request
from an unauthenticated client.

### Not primary causes of initial interception failure

These can break the page after it loads but do not normally prevent MikroTik
from intercepting the first HTTP request:

- Incorrect ESP32 API base URL.
- Missing ESP32 walled-garden rule.
- ESP32 API/CORS failure.
- Missing banner or music.
- ESP32 management-AP captive-probe handler.

## Legacy `Captive Portal/` file classification

| File | Purpose | Classification |
|---|---|---|
| `README.md` | Deprecation marker | Keep until migration completes |
| `login.html` | Old captive login | Legacy |
| `renzfi-app.js` | Old portal API client | Legacy |
| `renzfi-style.css` | Old portal stylesheet | Legacy |
| `md5.js` | CHAP helper | Legacy; identical to canonical |
| `Default-Banner.png` | Old large banner | Legacy media |
| `bg_music.mp3` | Local fallback music | Legacy media |
| `favicon.ico` | Old icon | Legacy |
| `piso_portal_logo.png` | Old logo | Legacy |
| `alogin.html` | RouterOS post-login servlet | Retain for compatibility validation |
| `redirect.html` | RouterOS redirect servlet | Retain for compatibility validation |
| `status.html` | RouterOS status servlet | Retain for compatibility validation |
| `logout.html` | RouterOS logout servlet | Retain for compatibility validation |
| `error.html` | RouterOS error servlet | Retain for compatibility validation |
| `rlogin.html` | WISPr redirect servlet | Retain for compatibility validation |
| `radvert.html` | Advertisement redirect | Retain for compatibility validation |
| `api.json` | RouterOS captive API template | Retain for compatibility validation |
| `errors.txt` | RouterOS error dictionary | Retain for compatibility validation |
| `css/style.css` | Servlet-page stylesheet | Retain with servlet templates |
| `img/user.svg` | Legacy icon | Legacy |
| `img/password.svg` | Legacy icon | Legacy |
| `xml/alogin.html` | WISPr servlet | Retain for compatibility validation |
| `xml/error.html` | WISPr servlet | Retain for compatibility validation |
| `xml/flogout.html` | WISPr servlet | Retain for compatibility validation |
| `xml/login.html` | WISPr servlet | Retain for compatibility validation |
| `xml/logout.html` | WISPr servlet | Retain for compatibility validation |
| `xml/rlogin.html` | WISPr servlet | Retain for compatibility validation |
| `xml/WISPAccessGatewayParam.xsd` | WISPr schema | Retain with XML templates |

## Branding pipeline

```text
Admin upload
  -> /api/settings/portal/banner or music
  -> AssetManager
  -> SD canonical storage
  -> SPIFFS fallback
  -> portal metadata revision
  -> portal.changed SSE
  -> guest reloads revisioned asset URL
```

Findings:

- Branding is not copied to MikroTik by Router Sync.
- Custom banner and music are served by ESP32.
- MikroTik-hosted media are offline/default fallbacks.
- Branding metadata uses no-store behavior.
- Asset URLs are revision-busted.
- If SSE is missed, the portal also refreshes branding on browser lifecycle
  events.
- A low residual stale-cache risk remains if a revision value is reused after
  reboot.

## Portal session and RouterOS communication

Guest-facing endpoints include:

- `GET /api/portal/branding`
- `GET /api/portal/session`
- `GET /api/portal/rates`
- `POST /api/portal/start-coin-session`
- `POST /api/portal/done-paying`
- `POST /api/portal/pause`
- `POST /api/portal/resume`
- `POST /api/portal/terminate`
- `POST /api/portal/heartbeat`
- `GET /api/portal/assets/banner`
- `GET /api/portal/assets/music`
- `GET /api/events`

RouterOS mutations are queued through one worker to avoid concurrent RouterOS API
sessions. This is directionally correct but does not eliminate all races.

## Performance and integrity findings

### Router worker publication race

The worker payload can be written before exclusive dispatch ownership is fully
secured. Two callers can overwrite shared payload state or cause one caller to
report failure while its payload is executed by another dispatch.

Potential impact:

- Wrong client authorization.
- Wrong MAC or profile mutation.
- Incorrect completion callback.

### Deferred-work loss

Several portal flows enqueue cleanup or RouterOS actions without consistently
handling queue failure. A full or unavailable queue can leave local and RouterOS
state divergent.

### Caller-supplied MAC trust

Open guest endpoints use a MAC supplied by the request. Without binding it to
the observed client identity, one guest may be able to inspect or mutate another
guest's portal session if its MAC is known.

### Session persistence race

Persistence copies state, writes it, then clears the dirty flag. A concurrent
mutation during the write window can be incorrectly marked clean.

### SPIFFS write amplification

When SD storage is unavailable, session persistence can write the full session
document to SPIFFS more frequently than intended, increasing flash-wear risk.

### Coin pulse-boundary race

Pulse resolution and ISR updates can occur near the same boundary, creating a
risk that a pulse is attributed to the wrong group.

### Existing protections

- Global RouterOS session gate.
- RouterOS command pacing.
- Cooldown and backoff.
- Job deadlines.
- DMA availability checks.
- Mutex-protected portal mutations.
- Done Paying idempotency recheck.
- Purchased time preserved when activation fails.
- Idle portal/dashboard polling does not itself issue RouterOS commands.

## Voucher integration gap

The guest login form posts credentials to MikroTik Hotspot.
`VoucherManager` stores generated vouchers in ESP32 JSON storage.

No source-proven path was found that automatically creates matching MikroTik
Hotspot users. Therefore, an Admin-generated voucher is not proven usable by the
production MikroTik login page.

This must be fixed or explicitly waived and documented before production release.

## Safe cleanup decision

### Keep

- `portal/`
- Deployment documentation and `.rsc` helper.
- Admin SPA source/public assets.
- Firmware portal APIs and session logic.
- Setup/recovery portal support.
- RouterOS servlet templates until hardware validation completes.

### Generated

- `deployment/mikrotik-hotspot/` generated portal assets.
- `ESP32_S3_Firmware/data/`.
- `dist/`.

Generated files should not become competing editable sources.

### Legacy

- `Captive Portal/`.
- Unmanaged deployment launchers such as extensionless `admin` and `index.html`.

Legacy does not mean safe to delete. The current redirect regression makes the
RouterOS servlet files operationally relevant evidence.

### Deletion candidates

Obvious junk such as `New Text Document.txt` may be removable after:

1. Capturing a repository checkpoint.
2. Archiving the live router file tree.
3. Confirming no operator procedure depends on it.

## Proposed final structure

Do not implement this structure until redirect and compatibility validation are
complete.

```text
portal/                         # only editable guest portal source
deployment/mikrotik-hotspot/    # generated upload artifact + operator docs
legacy/mikrotik-stock/          # archived RouterOS servlet compatibility set
scripts/                        # build, stage, validation and deployment tooling
ESP32_S3_Firmware/data/         # generated SPIFFS staging
docs/                           # current contracts and archived reports
```

## Implementation risk plan

### Safe after a repository checkpoint

- Track `portal/`, portal scripts, deployment documentation, and tests atomically.
- Fix the failing portal contract test.
- Add build manifests and local hash verification.
- Document exactly which RouterOS files are required.
- Remove only proven junk.

### Medium risk

- Clean the generated MikroTik destination before building.
- Archive duplicate media.
- Retire unmanaged admin launcher experiments.
- Consolidate stale portal documentation.

### High risk

- Delete or move `Captive Portal/`.
- Change the RouterOS servlet file set.
- Alter guest portal routes.
- Remove SPIFFS setup/recovery assets.
- Automate router uploads without transactional rollback.

## Rollback package required before cleanup

1. Archive each representative router's complete `Files/hotspot/` tree and file
   hashes.
2. Export Hotspot, profile, host, user, cookie, walled-garden, bridge, DHCP, DNS,
   NAT, and route configuration.
3. Preserve the currently deployed firmware binary and SPIFFS image.
4. Preserve ESP32 SD configuration and dynamic portal assets.
5. Create one atomic Git checkpoint containing all replacement portal source,
   scripts, tests, and documentation.
6. Preserve the legacy RouterOS servlet templates as a restore bundle.

Rollback should restore MikroTik files and configuration first. Restore firmware
and SPIFFS only if the failure is proven to originate on ESP32.

## Mandatory hardware validation

- RouterOS v7 and the oldest supported RouterOS v6 version.
- Fresh router and upgraded/adopted router.
- DHCP, DNS, and plain HTTP interception while unauthenticated.
- `/ip hotspot host` registration.
- OS captive assistant separately from plain HTTP redirect.
- Login, alogin, redirect, status, logout, error, and `api.json`.
- CHAP and PAP voucher flows.
- Wrong-voucher handling.
- Admin-generated voucher provisioning.
- Coin pulse groups and Done Paying.
- Add-time, pause, resume, terminate, and expiry.
- Router worker busy/failure activation recovery.
- Android, iOS/macOS, and Windows.
- Fresh, cached, and private browser sessions.
- Branding upload, deletion, SSE, revision, and offline fallback.
- Setup Finish in managed, manual, and skipped modes.
- Factory recovery and `/portal` recovery.
- SD removal and power loss during coin, sale, and activation.
- DMA, heap, watchdog, and RouterOS CPU soak tests.
- Verification that idle UI operation causes zero RouterOS commands.

## Final recommendation

**Do not delete, move, or consolidate portal files yet.**

First:

1. Capture live RouterOS evidence and reproduce the redirect failure.
2. Restore correct interception and post-login behavior.
3. Track the intended source and pipeline atomically.
4. Fix the failing portal test.
5. Resolve or formally waive the session, worker, voucher, and persistence
   blockers.
6. Complete the hardware matrix and preserve rollback artifacts.

Only then should legacy cleanup proceed in small, independently reversible steps.
