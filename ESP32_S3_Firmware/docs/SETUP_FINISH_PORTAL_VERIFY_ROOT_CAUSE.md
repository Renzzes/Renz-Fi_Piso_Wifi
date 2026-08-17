# Setup Finish — Portal Verify Root Cause Fix

## 1. Incident

Finish (`POST /api/setup/finish`, job `finish-setup-provisioning`) failed after
successful hotspot verification. Installation remained `router_configured` and
the Setup Wizard returned to Step 5 (Administrator & Operator).

## 2. Hardware reproduction

- MikroTik hAP lite RB941-2nD, RouterOS 7.20.7
- Existing-network adoption succeeded
- Finish stages that passed: `ensurePreconditions`, `persistLocalState`,
  `router-connect`, `hotspot-verify` (BRIDGE_FALLBACK PASS)
- Failure stage: `portal-verify` (~3183 ms), then worker finish gate failed
- Serial showed many sequential `/file/print` commands

## 3. Exact root cause

**Function:** `verifyPortalFilesPresent()` in
`RouterProvisioningEngine.cpp` (finish stage `portal-verify`).

**Failing condition:** For every entry in `kPortalFiles[]`, call
`fileExistsOnRouter()` which issues `/file/print ?name=<path>`. Any missing
file returned `false`, which the finish pipeline treated as a **hard failure**
(`PORTAL_FILES_MISSING`, HTTP 400). Installation never advanced to Ready /
Provisioned.

## 4. Evidence from logs

```
[finish-stage] BEGIN portal-verify
/file/print  (repeated)
[finish-stage] WARNING SLOW STAGE
[finish-stage] END portal-verify ... elapsed=3183ms success=false
[router-worker] finished type=finish-setup-provisioning ok=no http=400
```

## 5. Why Step 5 appeared to loop

1. Finish job failed → frontend `failFinalize` / status still `router_configured`
2. `wizardStep=complete` still maps to `panelProvisioned` (Step 5)
3. Operator section reappears; user can click Skip/Finish again → same failure

## 6. Why installation remained `router_configured`

`commitFinishInstallationState()` only runs after portal-verify and later
stages succeed. Portal hard-fail returned before that commit.

## 7. Portal ownership architecture

- Captive portal files live on **MikroTik** (manual/external deploy)
- ESP32 must **not** upload portal files (`/tool/fetch` removed earlier)
- ESP32 Admin Dashboard is unrelated to captive portal verification

## 8. Why portal verification is non-blocking for manual deployment

Default mode is `MANUAL_EXTERNAL`. Verification may report `verified` or
`unverified`, but **must not** block Finish. Only `MANAGED` mode may block.

## 9. Skip-for-now semantics

Previously, **Skip for Now** only skipped operator creation and called the
same Finish path with no portal flag.

Now Skip sends:

```json
{ "portalDeploymentMode": "skipped" }
```

Backend persists that intent for the finish job: **no** `/file/print` inventory;
`portalStatus=skipped`; Finish continues.

Default Finish (after creating an operator) uses `manual_external` (soft verify).

## 10. RouterOS `/file/print` optimization

**Before:** 1 directory check + N expected files ⇒ N+1 `/file/print` calls  
(manifest included obsolete assets: `favicon.ico`, `Default-Banner.png`,
`bg_music.mp3`, `coin.mp3`, `success.mp3`, etc.)

**After:**
- `SKIPPED`: 0 file queries
- `MANUAL_EXTERNAL` / `MANAGED`: resolve `html-directory` from hotspot profile,
  then **one** `/file/print` inventory; compare paths in memory
- If inventory hits the 32-reply cap and `login.html` is missing from the
  partial set, one targeted fallback query is allowed

Essential requirement for soft/managed verify: `login.html` under the profile
`html-directory`.

## 11. State-machine correction

After required gates pass (`hotspot-verify`, etc.) and portal is
verified / unverified(manual) / skipped:

`router_configured` → `provisioned`/`ready` (via existing commit path)

## 12. UI completion behavior

On Finish success with Ready/provisioned:
- Show Installation Complete
- Do not re-open Step 5 operator flow from stale `router_configured`
- Instruct: connect to management network, then open `http://10.10.10.2/login`
- Inability to reach `/login` from Setup AP is **not** an install failure

## 13. Files modified

- `src/RouterProvisioningEngine.h` / `.cpp`
- `src/RouterProvisioningWorker.h` / `.cpp`
- `src/web/SetupServer.cpp`
- `src/web/SetupWizardPageHtml.h`
- `docs/SETUP_FINISH_PORTAL_VERIFY_ROOT_CAUSE.md` (this file)
- `docs/SETUP_HARDENING_IDEMPOTENT_PROVISIONING.md` (portal policy note)

## 14. API compatibility

- `POST /api/setup/finish` still returns `202` + `jobId`
- Optional JSON body: `portalDeploymentMode` (`manual_external` | `skipped` |
  `managed`) or `skipPortalVerify: true`
- Job result may include `portalDeploymentMode`, `portalStatus`,
  `portalBlocking` without removing existing fields

## 15. Idempotency considerations

Hotspot reuse / bridge / DHCP reconciliation unchanged. Portal stage no longer
mutates `html-directory` unless mode is `MANAGED`.

## 16. Performance considerations

Eliminates RouterOS file-print storms on hAP lite during Finish.

## 17. Tests performed

| Test | Result |
|------|--------|
| Firmware build `freenove_esp32_s3_wroom` | See build section |
| Hardware TEST 1–8 | Requires field re-run after flash |

Code-level verification of root cause and non-blocking path completed in-repo.

## 18. Build result

`platformio run -e freenove_esp32_s3_wroom` → **SUCCESS** (42.26s).

## 19. Remaining risks

- Full `/file/print` still capped at `RouterOsClient::MAX_REPLY_RECORDS` (32);
  truncated inventory may mark portal `unverified` (non-blocking in manual mode)
- SD filesystem operations during Finish still log
  `WARNING Operation has NO TIMEOUT` — separate reliability follow-up; not the
  portal-verify root cause
- `MANAGED` portal deployment remains available but is not the production default

## 20. Rollback notes

Revert the files listed in §13. That restores blocking
`verifyPortalFilesPresent` + per-file `/file/print` behavior.
