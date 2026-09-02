# Admin Dashboard + Captive Portal → W5500 DMA Guru (2026-08-31)

**Date:** 2026-08-31  
**Firmware:** `0.5.0-w5500` (Waveshare ESP32-S3-ETH)  
**Crash ELF SHA256 prefix (log + matching build):** `dc7acb65d`  
**Exception:** `Core 1 panic'ed (LoadProhibited)` · `EXCCAUSE=0x1c` · `EXCVADDR=0x00000000`  
**Symptom:** Owner opens Admin Dashboard from a guest client (`10.20.0.244`) while captive portal customers are paying / activating. Device reboots with Guru Meditation.

---

## 1. Verdict (proven)

| Claim | Status |
|-------|--------|
| Crash is **W5500 SPI DMA exhaustion** → ESP-IDF NULL deref | **PROVEN** |
| Crash site is **SPI master priv bounce** (`setup_priv_desc` / `emac_w5500_receive`) | **PROVEN** (serial + addr2line on matching ELF) |
| Trigger is **798 KB Admin SPA JS stream** concurrent with portal coin/activation + storage I/O | **PROVEN** (serial timeline) |
| Failure is **not CPU overload** | **PROVEN** (PSRAM heap ~8.3 MB free throughout; no ping degradation) |
| Failure is **not PSRAM exhaustion** | **PROVEN** |
| SoftAP was **off** at crash (`mgmt_ap=off`) — not required for this incident | **PROVEN** |
| SD missing / SPIFFS fallback adds **concurrent pressure**, not the crash instruction | **PROVEN** |
| Existing HTTP 503 / SSE quiesce **alone** prevent the Guru | **REJECTED** (log shows quiesce **then** RX alloc fail → panic) |
| RouterOS `unknown host IP 10.20.0.252` is a **separate activation bug** that amplified load | **PROVEN** (3 auto-retries + portal-save churn) |

**One-line root cause:** Concurrent Ethernet HTTP (798 KB Admin SPA + portal coin/activation + SPIFFS JSON I/O) exhausts the small **DMA-capable internal SRAM** pool W5500 SPI needs for every TX/RX frame. When `dma_largest` falls below one bounce buffer (~552 B), ESP-IDF still attempts RX → **LoadProhibited @ 0**.

---

## 2. Proven crash site (ELF `dc7acb65d`)

Serial prelude (immediately before Guru):

```text
[spa-stream] path=/assets/s2Zw6sJk.js bytes=798162 inFlight=1 paced=2 dma_largest=8692
[http] 503 reason=ETH_DMA_LOW detail=headroom
[dma-alloc-fail] size=552 caps=0x00000808 fn=heap_caps_aligned_alloc task=async_tcp
                 dma_free=820 dma_largest=276
E (...) spi_master: setup_dma_priv_buffer(...): Failed to allocate priv TX buffer
[dma-alloc-fail] size=552 task=tiT dma_free=540 dma_largest=12
[dma-alloc-fail] size=76  task=w5500_tsk dma_free=588 dma_largest=20
E (...) spi_master: setup_dma_priv_buffer(...): Failed to allocate priv RX buffer
Guru Meditation Error: Core 1 panic'ed (LoadProhibited) EXCVADDR=0x00000000
```

Decoded stack (addr2line on matching `.elf`; ROM frame at `0x40056f5c` is memcpy with null source):

| PC | Symbol / component |
|----|-------------------|
| `0x40056f5c` | ROM copy loop (null pointer → LoadProhibited) |
| `0x4214c745` | `check_trans_valid` (`spi_master.c`) |
| `0x4214cbe3` | `setup_priv_desc` (`spi_master.c`) |
| `0x42139847` | `emac_w5500_receive` (`esp_eth_mac_w5500.c`) |
| `0x42139395` | `emac_w5500_transmit` |
| `0x4213969f` | `emac_w5500_task` |

Same physical class as prior forensics: `ADMIN_PORTAL_MULTI_CLIENT_DMA_GURU_FORENSIC.md`, `ADMIN_CONNECT_IDLE_PORTAL_DMA_GURU_FORENSIC.md`, `W5500_RX_DMA_EMERGENCY_QUIESCE.md`.

---

## 3. Proven timeline (this incident)

### Topology / clients

| IP | Role |
|----|------|
| `10.20.0.244` | Owner Admin Dashboard (opened from guest HotSpot network) |
| `12:39:31:C8:4C:30` | Portal client — activation retries (RouterOS trap) |
| `CA:2C:DC:D5:EC:EB` | Portal client — coin insert + successful activation |

Router: MikroTik **hEX lite** (migrated from hAP lite). Guest HotSpot: `10.20.0.0/24`. ESP32 ETH: `10.10.10.2`.

### Steady state (before Admin open)

- `mgmt_ap=off`, `lifecycle=ProductionReady`, `install=owner_created`
- Resting DMA: **`dma≈27–31 KB`, `largest≈17 KB`, `minimum=88`**
- PSRAM heap: **`~8.39 MB` free** — healthy, irrelevant to W5500
- SD card: **mount failed** → SPIFFS fallback; repeated `SPIFFS fallback quota exceeded`
- Portal activations failing: `unknown host IP 10.20.0.252` on `/ip/hotspot/active/login` (3 auto-retries)

### Load ramp (coin + activation, no Admin yet)

1. Portal activation retries for `12:39:31:C8:4C:30` — router worker jobs, portal-save, sd-readJson
2. Coin window + PHP 10 for `CA:2C:DC:D5:EC:EB` — done-paying, deferred activation during router COOLDOWN
3. Health probe recovery → activation success for `CA:2C:DC:D5:EC:EB` (~6.8 s router job including 6 s cooldown wait)
4. DMA drifts down to **`dma≈24–27 KB`** — tight but above prior crash thresholds

### Collapse (Admin open → Guru ~234 s uptime)

1. `GET /admin` from `10.20.0.244` — HTML + CSS OK
2. **`GET /assets/s2Zw6sJk.js` — 798,162 bytes** — `[spa-stream] inFlight=1 paced=2 dma_largest=8692`
3. Concurrent: portal-save, sd-readJson, sales SD read/write, favicon/icons/png fan-out
4. W5500 PHY read error (`read PHY register failed`) — symptom of SPI stress, not root cause
5. `dma_largest` collapses: **8692 → 4340 → 1844 → 172**
6. Firmware reacts: `503 ETH_DMA_LOW`, `[http] drop reason=headroom`, `[dma-emergency]`
7. **Still:** `w5500_tsk` RX `setup_dma_priv_buffer` fails → Guru → reboot

Post-reboot: `[crash-report] Unexpected reset reason=PANIC`.

---

## 4. Why it happens (mechanism)

```
Portal coin/activation (router worker + portal-save + sd-readJson)
  + Admin SPA 798 KB stream (W5500 SPI TX per chunk)
  + Admin asset fan-out (icons, PNG, favicon)
  + 2 s health snapshot storage probes (SPIFFS capacity)
  → many concurrent W5500 SPI frames need DMA bounce buffers (caps 0x808)
  → dma_largest collapses below one frame (~552 B)
  → IDF emac_w5500_task cannot allocate priv RX/TX
  → NULL deref in setup_priv_desc → LoadProhibited EXCVADDR=0
```

### Why prior gates did not prevent reboot

| Protection | What it does | Gap in this log |
|------------|--------------|-----------------|
| HTTP admit floor (`largest ≥ 3072`) | Rejects new responses when low | 798 KB JS **admitted** at `dma_largest=8692` — legal but insufficient margin under concurrent portal load |
| Paced HTTP slots (max 2) | Caps concurrent body streams | Portal-save + sd-readJson + router worker still consume INTERNAL/DMA; RX has **no app gate** |
| SSE quiesce / 503 ETH_DMA_LOW | Sheds Admin load reactively | Fires **after** collapse; W5500 RX can still panic on next frame |
| Emergency alloc-fail hook | Blocks new HTTP for 8 s | Hook races with in-flight RX on `w5500_tsk` — **does not make failed RX safe** |

### Not the root cause

| Ruled out | Evidence |
|-----------|----------|
| CPU exhaustion | No TWDT; router jobs complete; ETH service continues until DMA collapse |
| Customer count limit | Crash with **2 portal clients** + 1 Admin — architectural DMA pool limit, not headcount |
| hEX lite migration failure | Router jobs succeed when DMA available; issue is ESP32-side DMA pool |
| Admin opening RouterOS on connect | Admin uses cached `/api/status` + SSE per isolation rules |

### Contributing amplifier (separate bug)

**RouterOS `unknown host IP 10.20.0.252`** on `/ip/hotspot/active/login`:

- MikroTik cannot authorize the client's IP in `/ip/hotspot/active` at login time (ARP/timing — client not yet visible to RouterOS HotSpot host table).
- Causes 3 auto-retries + portal-save + router worker churn, keeping DMA under pressure **before** Admin opens.
- Fix belongs in activation path (wait for ARP / retry with backoff / skip active login when IP unknown) — **not** the Guru crash site, but reduces load.

---

## 5. Prevention strategy (without limiting customers)

Goal: **Core portal + coin + activation must survive Admin Dashboard open.** Admin may degrade (503 Retry-After, SSE pause) but must not Guru.

### A. Proven-safe operational mitigations (no firmware)

1. Open Admin from **`http://10.10.10.2/admin`** on the LAN (ether2 path) when possible — reduces concurrent HotSpot + Admin traffic on same bridge, but **does not eliminate** DMA competition on ESP32.
2. Ensure **SD card seated** — SPIFFS fallback adds lock contention and quota warnings visible in this log.
3. Fix RouterOS **unknown host IP** for activations — reduces retry storms.

### B. Firmware hardening (targeted, Core-safe)

| Change | Purpose |
|--------|---------|
| **Portal-aware large SPA gate** — require `dma_largest ≥ 12 KB` before admitting ≥32 KB assets when portal operational load is active | Prevents 798 KB stream from starting during coin/activation |
| **Defer 2 s health snapshot storage work** when DMA headroom is below HTTP-admit floor | Stops SPIFFS capacity probes competing with W5500 during storms |
| **`hasOperationalPortalLoad()`** — detect Activating / WaitingCoin / Active, not only Active | Fixes `portal=0` blind spot during activation retries |
| Keep existing: paced slots, 503 ETH_DMA_LOW, SSE quiesce, emergency hook | Reactive shedding — necessary but not sufficient alone |

### C. What we must NOT do

- Limit captive portal customer count as a "fix"
- Remove portal heartbeat / session polling (Core depends on them)
- Open RouterOS from Admin connect (violates admin-core isolation)
- Block coin / activation when Admin is open

---

## 6. Expected behavior after hardening

When portal customers are paying or activating and DMA is under pressure:

- Admin `/admin` HTML/CSS may load
- Large JS bundle returns **`503 ETH_DMA_LOW` + `Retry-After: 2`** until DMA recovers
- Portal coin, session, activation **continue** on Core path
- SSE may quiesce briefly; Admin reconnects automatically
- **No Guru** — W5500 RX never races into NULL deref because large streams defer proactively

---

## 7. Related documents

- `ADMIN_PORTAL_MULTI_CLIENT_DMA_GURU_FORENSIC.md`
- `ADMIN_CONNECT_IDLE_PORTAL_DMA_GURU_FORENSIC.md`
- `W5500_RX_DMA_EMERGENCY_QUIESCE.md`
- `STOREPROHIBITED_SALES_DMA_GURU_FORENSIC.md`
