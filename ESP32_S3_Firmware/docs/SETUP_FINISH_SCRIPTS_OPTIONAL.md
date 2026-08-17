# Setup Finish — Optional Scripts Stage

**Date:** 2026-08-16  
**Incident:** Finish job failed after required gates passed because `/system/script/print` read failed under MikroTik CPU 100%.

## Exact failure path

```
POST /api/setup/finish
  → finish-setup-provisioning (worker)
  → runFinishPipeline()
  → hotspot-verify PASS
  → portal-verify SKIPPED/PASS (non-blocking)
  → hotspot-profile PASS
  → walled-garden PASS
  → scripts: ensureManagedScript()
       → /system/script/print  READ FAILED
       → SCRIPT_ENSURE_FAILED (blocking return)
  → worker finish gate FAILED
  → HTTP 400
  → installationState remains router_configured
```

## Classification

| Stage | Required? | Notes |
|-------|-----------|--------|
| persistLocalState | REQUIRED | |
| router-connect / auth | REQUIRED | |
| hotspot-verify | REQUIRED | |
| portal-verify | OPTIONAL (mode-dependent) | Already non-blocking for MANUAL/SKIPPED |
| hotspot-profile | REQUIRED | |
| walled-garden | REQUIRED | Guest API reachability |
| **scripts** (`renzfi-hotspot-ready`) | **OPTIONAL** | Log-only marker; not used by coin/portal/session |
| api-verify | REQUIRED | |
| production-network | REQUIRED | |

## Fix (minimal)

`RouterProvisioningEngine::runFinishPipeline` scripts stage:

- If `RouterApiTransportGate::cpuUnderPressure()` → skip discovery, `blocking=false`, continue
- If `/system/script/print` (or add) fails → log OPTIONAL skip, `blocking=false`, continue
- Do **not** return `SCRIPT_ENSURE_FAILED`
- No extra retries, no new RouterOS session, no CPU pacing removal

## Untouched

Coin, Captive Portal, Setup wizard steps, FactoryResetWorker, Admin isolation, StorageManager, MikroTikDriver, W5500, TWDT, required finish gates.

## Validation

- BUILD: `pio run -e freenove_esp32_s3_wroom`
- Contract: `setup-wizard-finish-provisioning-check.py`
- HARDWARE: not claimed until flash + finish under high RouterOS CPU
