# Factory Reset Communication Quiesce — Priority 1 Implementation

**Date:** 2026-08-21  
**Branch:** `feature/waveshare-esp32-s3-eth`  
**Status:** Implemented in source. **NOT PHYSICALLY VALIDATED.**

## 1. FORENSIC ROOT CAUSE (established — not re-litigated)

**PROVEN:** Guru Meditation / LoadProhibited during factory reset is secondary to W5500 SPI DMA allocation failure (`caps=0x00000808`) when `dma_largest` is below the required contiguous block.

**PROVEN lifecycle gap:** `FactoryResetWorker::busy()` suppressed provisioning persist and health snapshots, but Admin/Portal HTTP, SSE, and frontend polling continued — overlapping destructive SD/storage work with W5500 RX/TX.

## 2. EXISTING LIFECYCLE GAP (addressed)

```
FactoryResetWorker (loopTask SD wipe)
  ∥  Admin /api/status, /api/settings/operator, …
  ∥  SSE /api/events + heartbeat
  ∥  Frontend 400ms reset-status + 30s polls
  → dma_largest collapse → W5500 RX fail → Guru
```

## 3. FILES CHANGED

| File | Change |
|------|--------|
| `ESP32_S3_Firmware/src/web/HttpPlaneGate.h/.cpp` | Central `ensureNotFactoryResetting`, allow-list, bind |
| `ESP32_S3_Firmware/src/FirmwareApp.cpp` | `bindFactoryReset` |
| `ESP32_S3_Firmware/src/ApiServer.cpp` | Close SSE on successful enqueue |
| `ESP32_S3_Firmware/src/EventBus.cpp/.h` | Skip heartbeat/emit when busy; refuse connect; `closeAllClients` |
| `src/services/factoryResetQuiesce.ts` | Frontend quiesce flag |
| `src/pages/SystemSettingsPage.tsx` | Enter quiesce; stop status/rgb polls |
| `src/App.tsx` | Disable SSE + health monitor while quiesced |
| `src/hooks/useDashboardEvents.ts` | `fallbackPollMs=false` when SSE intentionally off |
| `src/components/SetupSecurityPanels.tsx` | Disable operator query while quiesced |
| `ESP32_S3_Firmware/tools/factory-reset-contract-check.mjs` | Contract check #25 |
| `docs/FACTORY_RESET_COMMUNICATION_QUIESCE.md` | This document |

## 4. BACKEND REQUEST GATE

Centralized in `HttpPlaneGate::ensureNotFactoryResetting`, called first from:

- `ensureSetupPlane`
- `ensureProductionPlane`
- `ensureAppliancePlane`

All Admin / Setup / Portal / SPA / static plane gates that use those entry points inherit the quiesce.

Rejects **before** endpoint JSON / SD / `beginResponse` work.

## 5. FACTORY RESET ALLOW-LIST

| Method | Path |
|--------|------|
| POST | `/api/system/factory-reset` |
| GET | `/api/system/factory-reset/status` |

(Optional query strings on those paths are allowed.)

## 6. REJECTED REQUEST CLASSES

While `FactoryResetWorker::busy()`:

- All other Admin APIs (`/api/status`, `/api/health`, `/api/settings/*`, `/api/system/*` except allow-list, coin, sales, …)
- Portal APIs (production-plane gated)
- Setup APIs (setup-plane gated)
- Admin SPA / static / asset serves that use production/setup ensure*
- New SSE connects; SSE emit + heartbeat

**HTTP:** `409` + `code=FACTORY_RESET_IN_PROGRESS` (matches existing enqueue-busy contract).

## 7. SSE HANDLING

1. On successful enqueue → `EventBus::closeAllClients()` (already-constructed clients only).
2. **New** SSE while busy → `AsyncEventSource::authorizeConnect` middleware rejects **before** `AsyncEventSourceClient` construction (no `client->close()` in `onConnect`).
3. `heartbeat()` / `emit()` no-op when busy.
4. Frontend closes EventSource via `useDashboardEvents(enabled=false)`.

See also: `docs/FACTORY_RESET_SSE_ONCONNECT_CLOSE_FORENSIC.md` (Priority-1 `onConnect`+`close()` NULL dereference — fixed).

## 8. FRONTEND POLLING CHANGES

1. `setFactoryResetQuiesced(true)` at reset start (before enqueue)
2. Cancel status / rgb / operator queries
3. System Settings status/rgb `enabled: !resetting`
4. App disables SSE + `/api/health` monitor
5. `fallbackPollMs` becomes `false` when SSE disabled → Dashboard hooks stop interval polls

**Kept:** factory-reset status poll loop at 400 ms only.

## 9. DMA INTERACTION

- `ETH_DMA_LOW` / `kMinLargestDmaBlockForEthTx=1536` / `hasEthTransmitHeadroom` **unchanged**
- Quiesce is **lifecycle-based**, not a replacement for DMA gates
- Reject log includes `dma_free` / `dma_largest` for field correlation

## 10. WHAT WAS NOT CHANGED

W5500 pins/SPI, WiFi DMA, SD DMA, AsyncTCP flags, RouterOS commands/worker, Setup Wizard, Portal business logic, Setup Lock, installation state machine, External AP, ETH_DMA_LOW threshold, PSRAM JSON architecture, reset wipe semantics.

## 11. BUILD RESULTS

| Target | Result |
|--------|--------|
| `waveshare_esp32_s3_eth` | **SUCCESS** |
| `freenove_esp32_s3_wroom` | **SUCCESS** |

## 12. TEST RESULTS

| Test | Result |
|------|--------|
| `factory-reset-contract-check.mjs` (25 checks) | **PASS** (incl. #25 quiesce) |
| `npm run test:portal` (portal lifecycle) | **PASS** (30/30) |

## 13. PHYSICAL VALIDATION STATUS

**NOT PHYSICALLY VALIDATED.** Required: Tests A–E from the Priority 1 prompt (Admin closed / open / +portal, DMA observation, post-reset boot).

Do **not** claim fixed until flash tests pass.

## 14. REMAINING RISKS

| Risk | Notes |
|------|--------|
| Multi-tab Admin not on System Settings | Other tabs may still poll until they receive 409; quiesce flag is per-document. Backend gate still rejects. |
| Service worker / cached assets | May attempt fetches; backend rejects non-allow-list. |
| Step 4 / Portal DMA | Separate findings — not in this change. |
| `AsyncEventSource::close()` | Library-supported for **already constructed** clients via `closeAllClients()`. New connections must use `authorizeConnect`, never `client->close()` from `onConnect`. |
