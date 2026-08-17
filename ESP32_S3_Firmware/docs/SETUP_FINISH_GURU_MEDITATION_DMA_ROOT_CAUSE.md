# Setup Finish Guru Meditation / SPI DMA — Root Cause

## 1. Incident

During Finish after **Create Operator**, ESP32 crashed with:

```
spi_master: setup_dma_priv_buffer(...): Failed to allocate priv RX buffer
Guru Meditation Error: Core 0 panic'ed (LoadProhibited)
EXCVADDR: 0x00000000
```

Serial context immediately before the crash:

```
portal mode=MANUAL_EXTERNAL
/ip/hotspot/profile/print → htmlDirectory=hotspot
/file/print
→ SPI DMA allocation failure → LoadProhibited → reboot
```

**Skip for Now** (portal mode=`SKIPPED`) did **not** crash and issued **no** `/file/print`.

## 2. Exact crash location

Crash is inside ESP-IDF `spi_master` while allocating a **DMA-capable** private RX buffer for a W5500 SPI transaction during RouterOS TCP receive/transmit.

Firmware call chain (proven by logs + source):

```
POST /api/setup/finish
→ RouterProvisioningWorker::FinishSetupProvisioning
→ RouterProvisioningEngine::runFinishPipeline
→ portal-verify stage
→ verifyMikroTikCaptivePortal (MANUAL_EXTERNAL)
→ resolveHotspotHtmlDirectory → /ip/hotspot/profile/print
→ /file/print (previously: full filesystem inventory)
→ RouterOsClient::executeCommand → NetworkClient write/read
→ ETH/W5500 SPI → setup_dma_priv_buffer → nullptr → LoadProhibited
```

## 3. Operator vs Skip comparison

| Path | `portalDeploymentMode` | Portal file queries | Crash observed |
|------|------------------------|---------------------|----------------|
| Create Operator → Finish | `manual_external` (default) | yes (`/file/print`) | yes |
| Skip for Now → Finish | `skipped` | **0** | no |

**Proven:** Operator creation is not the crash site. It selects the MANUAL_EXTERNAL Finish path, which previously executed a broad MikroTik `/file/print`.

## 4. Call chain

See §2. Worker parses optional JSON body `portalDeploymentMode` / `skipPortalVerify` and calls `RouterProvisioningEngine::setPortalDeploymentMode(...)` before `runFinishPipeline`.

## 5. DMA measurements

Instrumentation (retained around portal-verify only at high signal points):

```
[dma] before portal-profile ...
[dma] after portal-profile ...
[dma] before portal-file-query ...
[dma] after portal-file-query ...
```

Plus controlled failure path:

```
SPI_DMA_ALLOCATION_FAILED
```

when `DmaMemoryMonitor::hasEthTransmitHeadroom()` is false before a RouterOS command write.

Field DMA numbers must be captured on-device after flash; total heap/PSRAM alone is insufficient (see §7).

## 6. Actual root cause

**ROOT CAUSE (code + log evidence):**

MANUAL_EXTERNAL portal verification previously issued a **full** RouterOS `/file/print` inventory (large TCP RX payload over W5500 SPI). That spike in SPI DMA buffer demand exhausted internal DMA-capable heap (`MALLOC_CAP_DMA`). ESP-IDF then failed `setup_dma_priv_buffer` and proceeded into a null dereference → Guru Meditation.

Contributing factors (evidence-backed):

1. Skip path never touched `/file/print` → no crash (differential proof).
2. Existing `DmaMemoryMonitor` docs already warn that W5500 priv TX/RX buffers need DMA-capable **internal** RAM (~1.5 KB class), independent of large PSRAM free totals.
3. Earlier N× filtered `/file/print` (obsolete multi-file manifest) also created command storms; the post-hardening full inventory was still too heavy for hAP lite + ESP32-S3 DMA pressure during Finish.

## 7. Why total PSRAM was misleading

PSRAM/total heap can look healthy while **DMA-capable internal SRAM** is fragmented or low. W5500 SPI master buffers generally **cannot** live in PSRAM. Large RouterOS replies increase concurrent SPI DMA buffer pressure even when `heap_caps_get_free_size(MALLOC_CAP_8BIT)` looks fine.

## 8. `/file/print` behavior (before → after)

**Before (crash path):**

- Resolve profile `html-directory`
- Full `/file/print` (entire router file table) **or** earlier: one `/file/print` per expected portal asset
- Large reply set → many String attrs (`MAX_ATTRS=24`, `MAX_REPLY_RECORDS=32`)

**After (this fix):**

- `SKIPPED`: **0** file queries
- `MANUAL_EXTERNAL` / `MANAGED`: resolve `html-directory`, then **one** targeted  
  `/file/print ?name=<html-directory>/login.html`
- No content download, no `/tool/fetch`, no obsolete banner/audio manifest

## 9. Parser / reply-limit findings

Logs: `reply attr limit reached (max=24)`.

**Evidence from `RouterOsClient::ReplyRecord::addAttr`:** when `attrCount >= MAX_ATTRS`, addAttr returns false; caller sets `replyLimitReached` and **stops adding attrs** for that reply. This is a **safe truncate**, not a proven crash root.

**Separate bug fixed (allocation safety):** on overflow `ReplyRecord` allocation failure, `appendReply()` previously returned a **prior live reply**, so subsequent `addAttr` could mutate the wrong record. It now returns a **discard sink** when allocation fails or the reply cap is hit.

Label for attr-limit ↔ DMA panic link: **HYPOTHESIS only** (correlated under memory pressure, not proven causal).

## 10. Allocation-safety findings

| Location | Behavior |
|----------|----------|
| `addAttr` overflow | `new (nothrow)`; failure → false (truncate) |
| `appendReply` overflow | `new (nothrow)`; failure → discard sink + `replyLimitReached` (**fixed**) |
| `readWord` payload | `String::reserve` checked |
| RouterOS command write | DMA headroom precheck → `SPI_DMA_ALLOCATION_FAILED` (no continue) |
| Portal MANUAL_EXTERNAL | transport/DMA/query failure → `portalStatus=unverified`, **non-blocking** |

## 11. Code changes

- `RouterProvisioningEngine.cpp/.h` — targeted portal verify; modes; DMA snapshots around portal stage; non-blocking MANUAL_EXTERNAL
- `RouterOsClient.cpp` — DMA precheck before command TX; safe discard sink on reply alloc failure
- `RouterProvisioningWorker.cpp/.h` — finish request JSON selects portal mode
- `SetupServer.cpp` — `/api/setup/finish` accepts body, forwards to worker
- `SetupWizardPageHtml.h` — Skip → `skipped`; Operator/default → `manual_external`; completion UI does not treat status-unreachable as install failure
- This document

## 12. API compatibility

- Existing `POST /api/setup/finish` still returns `202` + `jobId`
- Optional body: `{ "portalDeploymentMode": "manual_external"|"skipped"|"managed" }` or `{ "skipPortalVerify": true }`
- Job result may include `portalDeploymentMode`, `portalStatus`, `portalBlocking`

## 13. Portal deployment semantics

| Mode | File queries | Missing login.html / query fail | Blocks Finish? |
|------|--------------|----------------------------------|----------------|
| SKIPPED | 0 | n/a → `skipped` | no |
| MANUAL_EXTERNAL (default) | 1 targeted | `unverified` + warning logs | **no** |
| MANAGED | 1 targeted | fail `PORTAL_FILES_MISSING` | yes |

Captive portal remains on MikroTik only. ESP32 never uploads portal files in this path.

## 14. Performance impact

- Removes full-filesystem `/file/print` during Finish
- Removes N-file verification storms
- Lowers W5500 SPI DMA pressure during portal-verify
- Skip path remains zero portal RouterOS file I/O

## 15. Regression tests

| ID | Case | Expected | Result |
|----|------|----------|--------|
| 1 | Skip for Now | mode=SKIPPED, 0 `/file/print`, no panic | code-level PASS; hardware re-flash required |
| 2 | Create Operator | mode=MANUAL_EXTERNAL, targeted login.html only, no panic | code-level PASS; hardware re-flash required |
| 3 | login.html present | `portalStatus=verified` | code-level |
| 4 | login.html missing | `unverified`, blocking=false, Finish continues | code-level |
| 5 | RouterOS query / DMA fail | controlled unverified / error code, no panic | code-level |
| 6 | Alloc failure path | nothrow not used as live reply storage | code-level (discard sink) |
| 7 | Repeat Finish | no duplicate workers; bounded queries | code-level |
| 8 | Existing provisioning paths | unchanged outside portal-verify policy | code review |

## 16. Build result

`platformio run -e freenove_esp32_s3_wroom` → **SUCCESS** (27.59s).

## 17. Remaining risks

- ESP-IDF SPI master can still fail if DMA is exhausted by **other** concurrent SPI users (SD + W5500). Portal stage no longer intentionally triggers large RX.
- SD Finish ops still log `WARNING Operation has NO TIMEOUT` — separate reliability finding; not this crash’s proven root cause.
- `MANAGED` mode still blocks on missing `login.html` (intentional, not production default).

## 18. Separate finding — wireless running

Observed independently in some Finish runs:

```
WIRELESS ENABLE COMMAND SENT=no
WIRELESS ENABLE VERIFIED=yes
WIRELESS RUNNING=no
reason=interface-not-running
```

**Not changed** in this patch. Track as a separate production-network acceptance issue.

## Separate finding — SD filesystem

Finish still performs SD/LittleFS writes that FinishTrace flags as having **no timeout**. Document for a follow-up; do not wrap with unsafe task cancellation here.
