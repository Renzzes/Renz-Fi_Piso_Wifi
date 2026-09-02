# Admin Connect + Idle Captive Portal → W5500 RX DMA Guru Meditation

**Date:** 2026-08-27  
**Firmware:** `0.5.0-w5500` (Waveshare ESP32-S3-ETH)  
**Crash ELF SHA256 prefix (log + matching build):** `22e441524`  
**Exception:** `Core 1 panic'ed (LoadProhibited)` · `EXCCAUSE=0x1c` · `EXCVADDR=0x00000000`  
**Symptom:** Phone idle on captive portal; owner opens Admin / clicks Connect → login can succeed → Guru → reboot. Reproduces on the next Connect while portal keeps polling.

---

## 1. Verdict (proven)

| Claim | Status |
|-------|--------|
| Crash is **DMA-class** (W5500 SPI priv bounce alloc fail → NULL deref) | **PROVEN** |
| Crash site is **W5500 RX** (`emac_w5500_task` → `setup_priv_desc`) | **PROVEN** (nm on matching ELF) |
| Trigger is Admin Connect / dashboard fan-out **while** portal heartbeat/session continues | **PROVEN** (serial timeline) |
| SoftAP still running (`SetupApReady`, `mgmt_ap=running`) lowers resting DMA headroom | **PROVEN** (heartbeat + boot policy) |
| Failure is **not** CPU overload | **PROVEN** (ETH ping 0–1 ms throughout) |
| Failure is **not** PSRAM / total-heap exhaustion | **PROVEN** (`heap≈8.4 MB` free while `dma_largest` → tens of bytes) |
| SD missing / SPIFFS fallback is a **concurrent pressure**, not the crash site | **PROVEN** (mount fails; sales/portal use SPIFFS; crash is W5500 RX) |
| Existing HTTP admit / SSE quiesce **alone** prevent this Guru | **REJECTED** (log shows 503/drop/quiesce **then** RX alloc fail → panic) |

**One-line root cause:** Concurrent Ethernet HTTP (idle portal pollers + Admin Connect/dashboard API burst) exhausts the small **DMA-capable internal SRAM** pool that W5500 SPI needs for every frame; ESP-IDF still attempts RX with a failed priv buffer → **LoadProhibited @ 0**.

---

## 2. Proven crash site (matching ELF `22e441524`)

Serial prelude (both panics in the captured session):

```text
[dma-alloc-fail] size=492|68 caps=0x00000808 fn=heap_caps_aligned_alloc task=w5500_tsk
                 dma_free≈764|456  dma_largest≈340|24
E (...) spi_master: setup_dma_priv_buffer(...): Failed to allocate priv RX buffer
Guru Meditation Error: Core 1 panic'ed (LoadProhibited)
EXCVADDR: 0x00000000
```

`caps=0x00000808` = `MALLOC_CAP_8BIT | MALLOC_CAP_DMA` (W5500 SPI bounce, not WiFi `0x80c`).

Decoded application stack (nm enclosing symbols; `addr2line` debug info on this build is misleading for these PCs):

| PC | Symbol |
|----|--------|
| `0x40056f5c` | ROM copy loop (null source → LoadProhibited) |
| `0x4214862d` | `uninstall_priv_desc` |
| `0x42148db1` | `setup_priv_desc` |
| `0x4214924f` | `spi_device_polling_start` |
| `0x421499a9` | `spi_device_polling_transmit` |
| `0x42135ee7` | `w5500_spi_read` |
| `0x421a3897` | `w5500_read` |
| `0x42134937` | `w5500_read_buffer` |
| `0x42135a35` | `emac_w5500_receive` |
| `0x42135d3f` | `emac_w5500_task` |

Same physical class as `ADMIN_PORTAL_MULTI_CLIENT_DMA_GURU_FORENSIC.md` and `docs/ADMIN_DASHBOARD_DMA_GURU_FORENSIC.md`.

---

## 3. Proven timeline (this incident)

Clients:

| IP | Role |
|----|------|
| `10.20.0.251` | Captive portal phone (idle but polling `/api/portal/heartbeat` + `/api/portal/session`) |
| `10.20.0.250` | Owner Admin (dashboard / Connect) |

### Steady state (before Admin Connect)

- SoftAP **on**: `lifecycle=SetupApReady`, `mgmt_ap=running`, SSID `Renz-Fi Setup`
- Installation: `router_configured` (22%) — **not** `Ready` / `Provisioned`, so `ManagementApLifecycle::processSetupCompletion()` does **not** stop SoftAP
- SD: **missing** → SPIFFS fallback; portal-save to `/fb/ps.json`
- Resting DMA after SoftAP + ETH: roughly **`dma≈19–20 KB`, `largest≈15–16 KB`** (boot after W5500 was ~45 KB free / ~43 KB largest)
- Portal alone is survivable at this floor

### Collapse (first panic ~48–61 s uptime)

1. Admin: `/dashboard`, SPA assets, repeated `/api/health`, login OK, `/api/status`, `/api/coin/diagnostics`, `/api/system/health`, …
2. `sse=1` appears; sales chart enters under already-low DMA
3. Alloc fails on `async_tcp` / `tiT` / `w5500_tsk` (TX then RX)
4. Firmware reacts: `503 ETH_DMA_LOW`, `drop reason=api-json-admit`, `[http-quiesce] closing SSE`
5. **Still:** `w5500_tsk` RX `setup_dma_priv_buffer` fails → Guru → reboot

### Second panic (after reboot, ~204 s)

Same pattern, faster: Connect / login → coin/diagnostics / system/health → DMA critical → **RX fail → Guru** again while portal `10.20.0.251` keeps heartbeating.

---

## 4. Why it keeps happening (mechanism)

```
SoftAP WiFi stack still resident (setup incomplete)
  + portal ETH pollers (heartbeat/session)
  + Admin Connect fan-out (health × N, status, coin, system, SSE, optional chart)
  → many concurrent W5500 SPI frames need DMA bounce buffers
  → dma_largest collapses below one RX frame (~68–500 B)
  → IDF emac_w5500_task cannot allocate priv RX → NULL → LoadProhibited
```

**Why gates did not stop the reboot**

| Protection | What it does | Gap in this log |
|------------|--------------|-----------------|
| HTTP admit floor (`largest ≥ 3072`) | Rejects **new** ApiServer JSON | Requests already admitted / in flight still TX; portal + health liveness use a **weaker** TX floor (`≥ 1536`) and skip paced slots |
| Paced HTTP slots (max 2) | Caps concurrent SPA/JSON bodies | Does not apply to `/api/health` liveness; does not stop W5500 **RX** of inbound frames |
| SSE quiesce on critical | Closes Admin EventSource | Runs **after** DMA is already critical; RX can still fail on the next frame |
| Emergency flag on alloc-fail | Blocks further HTTP admit | Hook fires **on** the failed alloc; panic can race on that same RX path |

So: **reactive** protection sheds Admin load, but **does not make failed W5500 RX safe**. Once `dma_largest` is tens of bytes, any inbound Ethernet frame can Guru.

**Why SoftAP matters even when Admin uses Ethernet**

- SoftAP is not “serving” the Admin SPA here, but it permanently taxes INTERNAL/DMA (WiFi buffers).
- SoftAP only auto-stops when `InstallationStateManager::isReady()` (`Ready` or `Provisioned`). At `router_configured`, SoftAP stays up by design → resting DMA headroom stays thin before Admin opens.

**Not the root cause**

- “CPU busy” — rejected (ping stays 0–1 ms)
- “Out of RAM” in the PSRAM sense — rejected (~8 MB free)
- Admin “breaking” Core coin/session logic — rejected (Core continues until the whole chip reboots from ETH DMA)
- Opening Admin *by itself* without concurrent ETH load — not proven necessary; this incident proves the **combination**

---

## 5. What not to do (operations)

Avoid stacking these while customers are on the portal:

1. **Do not** leave Management SoftAP running for long after Ethernet production is in use — finish setup to Ready/Provisioned (or stop SoftAP / use temporary maintenance AP only when needed).
2. **Do not** open Admin Connect and hammer refresh while several phones leave the captive portal tab open (heartbeat/session keep hitting ETH).
3. **Do not** treat brief `503 ETH_DMA_LOW` as “try harder” with parallel retries from many tabs — that deepens the DMA hole.
4. **Do not** run without SD seated if sales/history paths will allocate large INTERNAL JSON under load (secondary pressure; this crash was W5500 RX, but SPIFFS + remount attempts add contention).
5. **Do not** assume SSE quiesce or 503 means the device is safe — if Serial already shows `dma_largest` in the hundreds, stop adding Admin traffic until it recovers.

---

## 6. Preferred prevention (normal ops: many portal clients + owner Admin)

### A. Operational (immediate, no firmware change)

| Action | Why |
|--------|-----|
| Complete installation to **Ready/Provisioned** so SoftAP auto-stops | Recovers a large permanent DMA tax |
| Seat a working SD card on the Waveshare onboard TF slot (GPIO4) | Removes remount churn + fallback I/O under load |
| Prefer one Admin client; close idle portal tabs when testing Admin | Cuts concurrent ETH pollers |
| On `ETH_DMA_LOW` / connection blips: wait / single retry — do not open multiple dashboards | Avoids fan-out storms |

### B. Product / firmware (preferred engineering direction)

Order by leverage vs risk (Admin remains optional; Core must survive without Admin):

1. **Proactive ETH quiesce before RX starvation**  
   Close SSE and reject new Admin JSON when `dma_largest` approaches a soft floor (e.g. well above `kCriticalDmaFloorForW5500Rx=768`), not only after `w5500_tsk` alloc fails. Goal: never let RX attempt with `largest < ~500`.

2. **Stop SoftAP when production ETH plane is active and setup SoftAP has no installer clients**  
   Today SoftAP stays up for all `needsSetup()` states including `router_configured`. Prefer: stop SoftAP once production HTTP is registered **unless** SoftAP has associated stations or maintenance mode is explicit. (Product decision — do not strand active SoftAP installers.)

3. **Throttle Admin Connect / Dashboard fan-out (UI)**  
   After login: sequence `/api/status` then stagger coin/system/chart; keep health liveness sparse when SSE is up (already partially done). Avoid parallel burst of 6+ GETs on first paint.

4. **Tighten `/api/health` under SoftAP+ETH**  
   Liveness currently bypasses the 3072 admit floor and paced slots. Under SoftAP-active production, either share the admit floor or rate-limit health responses when `largest` is thin.

5. **Hard fail-safe (last resort)**  
   If W5500 SPI bounce alloc fails repeatedly, pause ETH RX briefly / force HTTP silence until `largest` recovers — better than reboot. Must not block coin GPIO or loopTask forever.

### C. Explicitly out of scope / wrong fixes

- Opening RouterOS on every Admin Connect (violates Admin/Core isolation)
- Claiming “credentials synchronized” for a DMA crash
- Disabling WDT / ignoring Guru
- Merging SoftAP with External Access Point feature
- Adding setup-wizard steps solely for DMA (wizard freeze)

---

## 7. Expected healthy behaviour after prevention

Under 1+ idle portal phones + owner Admin Connect:

- Possible brief `503 ETH_DMA_LOW` / SSE reconnect / Admin “Connecting…” retry
- SoftAP off (or maintenance-only) once production is the daily path
- **No** `Failed to allocate priv RX buffer` → **no** LoadProhibited reboot
- Portal session Core continues; Admin degrades gracefully

---

## 8. Validation checklist

1. Flash build with SoftAP stopped in production (or finish setup to Ready).
2. Leave one phone on captive portal (heartbeat alive).
3. Owner Connect → Dashboard; watch Serial.
4. Pass: may see admit 503 / SSE quiesce; must **not** see Guru / `priv RX buffer`.
5. Confirm resting `[dma] periodic-dma largest` stays comfortably above ~3 KB with SoftAP off; SoftAP-on resting ~15 KB is already the danger zone for Admin bursts.

---

## 9. Related docs

- `ESP32_S3_Firmware/docs/ADMIN_PORTAL_MULTI_CLIENT_DMA_GURU_FORENSIC.md`
- `ESP32_S3_Firmware/docs/W5500_RX_DMA_EMERGENCY_QUIESCE.md`
- `ESP32_S3_Firmware/docs/STOREPROHIBITED_SALES_DMA_GURU_FORENSIC.md`
- `docs/ADMIN_DASHBOARD_DMA_GURU_FORENSIC.md`
- `docs/SETUP_STEP4_SOFTAP_WIFI_DMA_GURU_FORENSIC.md`
