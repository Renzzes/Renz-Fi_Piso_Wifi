# Setup Step 4 — ROUTER_WORKER_BUSY False Failure (No Guru)

**Date:** 2026-08-27  
**Board / build:** Waveshare ESP32-S3-ETH SoftAP setup (`192.168.4.1`)  
**Status:** **PROVEN** from serial + UI. Fix applied (wizard poll + idempotent job join).

---

## Symptom

On Step 4 (Wi-Fi Configuration → Finish / adopt existing network), the phone shows:

> **Configuration failed.**  
> RouterOS returned: `ROUTER_WORKER_BUSY: Router worker is busy`

Installer Retry/Back. Device does **not** reboot.

---

## Guru Meditation?

**No.** This session did **not** Guru Meditation / crash-reboot.

Evidence from the same log:

- SoftAP Wi‑Fi/coex DMA pressure: `[dma-alloc-fail] … class=wifi-coex-internal-dma (not W5500 spi bounce)` with a backtrace — classified as SoftAP INTERNAL alloc fail, **not** a fatal W5500 Store/LoadProhibited path.
- Worker continued and logged **`[setup] ADOPTION COMPLETE`** then  
  `[router-worker] finished type=configure-existing-network ok=yes http=200`.
- Heartbeats and Ethernet ping to `10.10.10.1` kept succeeding after the UI error.

So: **UI false failure** while the router job actually succeeded.

---

## Proven root cause

### Causal chain (this log)

1. `POST /api/setup/router/existing-network/configure` → **202** `jobId=4`, worker starts `configure-existing-network`.
2. Concurrent SoftAP traffic + SD remount/sync leave DMA tight (`dma free` dips; status polls hammer SoftAP).
3. `GET /api/setup/router/jobs/4` returns **503 `ETH_DMA_LOW`** (`detail=json-string`) while job 4 is still running.
4. Wizard `pollRouterJob()` treated non-`queued`/`running` poll bodies as **terminal failure** and called `handleAdoptionResult`.
5. `handleAdoptionResult` saw 503 / DMA and **re-POSTed** `executeAdoption()` (intended “retry after memory frees”).
6. Worker still busy on job 4 → enqueue rejected → **503 `ROUTER_WORKER_BUSY`**.
7. UI mapped that to **Configuration failed** — even though job 4 later completed successfully (`ADOPTION COMPLETE`).

### Why it looks like “RouterOS returned”

The failure modal always prefixes `RouterOS returned:` for any adoption error string.  
`ROUTER_WORKER_BUSY` is an **ESP32 worker-queue** code, not a MikroTik API trap.

---

## Fix (implemented)

| Layer | Change |
|-------|--------|
| `SetupWizardPageHtml.h` `pollRouterJob` | On `503` / `ETH_DMA_LOW` / `ROUTER_WORKER_BUSY` (and soft network blips), **keep polling the same `jobId`** instead of aborting. |
| `SetupWizardPageHtml.h` `handleAdoptionResult` | Treat `ROUTER_WORKER_BUSY` like DMA busy: soft wait + retry (up to 8). |
| `RouterProvisioningWorker::enqueueInternal` | If worker is busy on the **same** `configure-existing-network` / `finish-setup` type, **join** the in-flight `jobId` (202) instead of BUSY. |

---

## Related (not this bug)

- SoftAP captive `/generate_204` storm + `wifi-coex-internal-dma` pressure remains a real stressor (see `docs/SETUP_STEP4_SOFTAP_WIFI_DMA_GURU_FORENSIC.md`).
- Historical Finish Guru paths are separate DMA/W5500 forensics; this BUSY modal is independent.

---

## Verification

1. SoftAP setup → Step 4 Finish with existing SSID.  
2. Confirm no “Configuration failed” when serial shows `ADOPTION COMPLETE`.  
3. If job poll briefly 503s, overlay should say waiting for memory / worker, then success.  
4. No Guru / no unexpected reboot.
