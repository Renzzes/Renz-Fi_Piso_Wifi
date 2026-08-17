# Renz-Fi Production Qualification Report

**Firmware:** ESP32-S3 + W5500 + MikroTik RouterOS  
**Build profile:** `freenove_esp32_s3_wroom` (production)  
**Report date:** 2026-07-19  
**Qualification agent:** Static analysis + instrumentation review (no live appliance in CI)

---

## Executive summary

| Area | Status | Notes |
|------|--------|-------|
| Static architecture guards | **PASS** | Worker isolation, RouterOS gate, CommandResult storage |
| Firmware build | **PASS** | RAM 31.6% (103,572 B) at last successful build |
| Runtime Tests 1–8 (hardware) | **NOT EXECUTED** | Requires serial capture + scripted stress on appliance |
| **Production release** | **HOLD** | Release after hardware qualification PASS |

This report documents **readiness to qualify** and **what must be measured on hardware**. It does not claim PASS for continuous runtime until a serial log meets the thresholds below.

---

## Instrumentation (Test 1)

Every **10 seconds** the firmware emits:

```
[mem] heap=… largest=… minimum=… dma=… largest=… minimum=… jobs=… queue=… sse=… inspection=… wificache=… portal=… roscpu=…
```

| Field | Source |
|-------|--------|
| heap / largest / minimum | Internal 8-bit heap caps |
| dma / largest / minimum | DMA-capable heap (W5500 SPI) |
| jobs / queue | Router Worker busy (0 or 1) |
| sse | EventBus SSE client count |
| inspection | Router provisioning inspection active |
| wificache | WiFi discovery cache populated |
| portal | Active captive portal client session |
| roscpu | Last observed MikroTik CPU % (255 = unknown) |

Optional developer burn-in (`RENZFI_BURN_IN_DIAG=1` in `env:renzfi_developer`):

```
[health] FreeHeap / LargestBlock / MinFreeHeap / task stack HWM / ETH / RouterOS
```

### Capture procedure (≥1 hour)

1. Flash production firmware + SPIFFS.
2. Record serial at 115200 baud for **≥3600 s**.
3. Run qualification analyzer:

```bash
py -3 ESP32_S3_Firmware/tools/production-qualification.py --log serial_capture.txt --min-samples 360
```

**Pass thresholds (analyzer defaults):**

- Zero fatal strings (Guru Meditation, LoadProhibited, W5500 DMA alloc failures)
- `largest` heap block drift ≤ 4096 B
- `largest` DMA block drift ≤ 4096 B
- No sustained degradation of `minimum` in final quartile

---

## Test matrix

### Test 1 — Continuous runtime (≥1 h)

| Criterion | Expected | Verified here |
|-----------|----------|---------------|
| Stable heap | Flat `largest`, stable `minimum` | Instrumentation ready |
| Stable DMA | Flat DMA `largest` | Instrumentation ready |
| No crashes | No Guru / LoadProhibited | Requires log |
| Worker idle metrics | jobs=0 when idle | Requires log |

### Test 2 — Setup wizard stress (≥100 cycles)

Script: manual or browser automation through Owner → Test → Save → WiFi → SSID paths.

Verify serial: no `[mem]` downward trend; `inspection=1` only during inspect jobs; `wificache=1` after first discovery.

### Test 3 — HTTP stress

```bash
py -3 ESP32_S3_Firmware/tools/production-qualification.py --host <APPLIANCE_IP> --http-cycles 50
```

Paths: `/`, `/generate_204`, `/api/setup/status`, `/admin/setup`, `/api/events` (SSE manual).

### Test 4 — RouterOS stress

Code guarantees (static):

- Single session: `RouterApiTransportGate::acquireSession()`
- One command in flight via `RouterOsClient::IoLock`
- CPU tiers in `waitBeforeCommand()`; discovery cache + rate limit on WiFi scan
- Worker queue depth 1

Hardware verify: `roscpu` in `[mem]` stays <85% during discovery; no connect storms in `[router-api]` logs.

### Test 5 — Ethernet stress

Watch for: `setup_dma_priv_buffer`, `Failed to allocate priv TX/RX buffer`, `spi transmit failed`.

Mitigations in place: DMA headroom guard before RouterOS connect; reduced permanent SRAM footprint.

### Test 6 — Power recovery (≥50 boots)

Compare first vs 50th boot `[mem]` baseline after link-up (within 4096 B).

### Test 7 — Recovery

Ethernet pull/replug; MikroTik disable/enable. Expect worker `jobs` return to 0; no reconnect loop in serial.

### Test 8 — Long idle (8 h)

Same as Test 1 with no UI interaction; `sse=0`, `portal=0`, stable DMA.

---

## Static verification (completed)

| Check | Result |
|-------|--------|
| Router worker owns RouterOS path | PASS |
| No stack `CommandResult` in worker chain | PASS (dynamic/small-vector storage) |
| EventBus skip when SSE count=0 | PASS |
| InspectionData lazy allocation | PASS |
| Single worker CommandResult scratch | PASS |
| DMA monitor + connect guard | PASS |
| Build | PASS — RAM 31.6% |

Run locally:

```bash
py -3 ESP32_S3_Firmware/tools/router-test-save-stability-check.py
py -3 ESP32_S3_Firmware/tools/burn-in-diag-check.py
```

---

## Memory baseline (last build)

| Metric | Value |
|--------|-------|
| RAM used | 103,572 B (31.6%) |
| Flash used | ~80.8% |
| Prior optimization baseline | 129,068 B (39.4%) |
| SRAM saved (optimization pass) | **25,496 B** |

---

## Detected regressions

**None in static/build analysis.**

Historical issues (resolved in tree):

- W5500 DMA exhaustion from oversized `CommandResult` + partial init leak
- AsyncTCP LoadProhibited after `req->send()` (RequestTimer fix)
- Router worker stack overflow from stack-allocated `CommandResult`

---

## Recommended production release status

### **HOLD — pending hardware qualification**

Release when **all** are true on a production image:

1. `production-qualification.py` PASS on ≥1 h serial log during mixed workload  
2. 100+ setup wizard cycles without heap/DMA drift  
3. HTTP + Ethernet stress with zero W5500 allocation failures  
4. 50 boot cycles without baseline creep  
5. 8 h idle stable  

**Suggested version tag after PASS:** `0.5.0-w5500-production`

---

## Appendix: qualification command checklist

```text
[ ] Flash freenove_esp32_s3_wroom + uploadfs
[ ] Capture serial ≥1 h (Tests 1 + 8)
[ ] Run 100 setup wizard cycles (Test 2)
[ ] HTTP smoke + concurrent clients (Test 3)
[ ] RouterOS discovery while watching roscpu (Test 4)
[ ] Heavy HTTP/SSE/portal (Test 5)
[ ] 50 power cycles (Test 6)
[ ] ETH + MikroTik fault injection (Test 7)
[ ] py -3 tools/production-qualification.py --log capture.txt
```
