# Setup Step 4 SoftAP / WiFi DMA / Phone “Next” — Forensic

**Date:** 2026-08-22  
**Board / build:** Waveshare ESP32-S3-ETH, firmware `0.5.0-w5500`, ELF SHA256 prefix `6ec2953e5`  
**Status:** Root causes classified. **No product fix in this pass.** Wizard structure unchanged.

---

## Symptoms (installer-reported)

| # | Surface | Symptom |
|---|---------|---------|
| A | Laptop SoftAP wizard | Step 4 “Use Existing SSID” — SSIDs take a long time (or fail) to appear |
| B | Laptop SoftAP wizard | “Create New SSID” then continue → Guru Meditation → ESP32 reboot |
| C | Phone SoftAP | After unlock (done on laptop), can open setup, but **Next** on “Redirect Setup” does nothing |

Supporting evidence: serial log + browser console pasted by installer (same session).

---

## Verdict (one line each)

| # | Root cause | Confidence |
|---|------------|------------|
| A | SoftAP + captive probes deplete INTERNAL/DMA; Wi‑Fi SSID list is async worker + 202 retries; failures surface as **HTTP 502** on `/api/setup/router/wifi/networks` | **PROVEN** (console 502 + serial `wifi-coex-internal-dma` + known list-wifi 502 path) |
| B | Same DMA collapse during `configure-existing-network` (RouterOS `/interface/wireless/print` in flight) → SPI priv RX alloc fails → **LoadProhibited EXCVADDR=0** (secondary W5500 crash) | **PROVEN** (serial chain) |
| C | Phone captive portal / SoftAP entry: GET `/` is **notFound** / plane-restricted; OS “redirect / sign-in” Next is not our wizard Next. Secondary: Owner **Next** no-ops when `ownerCreated` | **STRONGLY INDICATED** (serial + code); exact OS chrome not captured |

Shared underlying pressure: **SoftAP WiFi/coex INTERNAL+DMA (caps `0x80c`) exhaustion**, not “MikroTik is down” (Ethernet ping to `10.10.10.1` stays SUCCESS; existing-network **scan** job completed in ~2.8s).

---

## Physical timeline (this session)

### 1. SoftAP captive storm (phone `192.168.4.4`)

Repeated:

- `GET /generate_204` → `CaptivePortalDetection` (~1–2 ms)
- Concurrent `[dma-alloc-fail] size=584|1626 caps=0x0000080c class=wifi-coex-internal-dma` on `tiT` / `async_tcp`
- `GET /` → `WebServer/notFound` (setup plane — **no HTML redirect to `/admin/setup`**)
- Later `GET /admin/setup` succeeds (~30–34 ms)

Laptop / second client `192.168.4.2` similarly hits `/connecttest.txt`.

### 2. Status at failure window

`/api/setup/status` (browser console):

- `installationState=router_configured`, `wizardStep=review` (UI Step 4 Wi‑Fi)
- `setupLocked=false`, unlock session open
- `wifiSelectionConfigured=true`, hint `Test2 Piso Wifi`
- Ethernet `10.10.10.2`, link up

### 3. Slow / missing SSIDs (symptom A)

Browser:

```text
/api/setup/router/wifi/networks → 502 Bad Gateway
```

Code path (`RouterProvisioningWorker` `ListWifiNetworks`): **502** on connect failure **or** `RouterWireless::listNetworks` failure. UI (`loadWifiNetworks`) treats non-success as “Unable to load SSIDs” / push toward Create New; **202 busy** retries up to 8× every 2s → long “Loading…” even when healthy.

Steady-state SoftAP DMA in this log often recovers to `dma≈20k largest≈11k`, but dips hard under job + SoftAP traffic.

### 4. Rescan OK, then configure Guru (symptom B)

- Existing-network **scan** job completed: `compatible_candidate`, `scanDurationMs=2788`
- User continued → `POST …/existing-network/configure` → job `2`
- Worker: RouterOS connect/login OK → `/system/resource/print` OK → `/interface/wireless/print` started
- Parallel SoftAP: job polls + `sd-readJson` + captive probes
- DMA collapses: e.g. `dma_free=256 dma_largest=56` while needing ~625–678 / 1626-byte `0x80c` allocs
- Firmware log: `spi_master … Failed to allocate priv RX buffer` then:

```text
Guru Meditation Error: Core 1 panic'ed (LoadProhibited)
EXCVADDR: 0x00000000
```

Objdump of ELF `6ec2953e5` places backtrace near **`uninstall_priv_desc`** / SPI priv-desc teardown (`0x4213fca4`…), not application JSON. Naive `addr2line` wrongly attributed ArduinoJson (debug mismatch); **serial + objdump are authoritative**.

After reboot: browser `GET …/jobs/2` → `ERR_CONNECTION_RESET` (expected after crash).

**Create New vs Existing:** Finish on Step 4 always calls `executeAdoption()` → same `configure-existing-network` worker job (SSID mode only changes payload). Crash is **not** a unique “new SSID command bug”; it is **DMA death under SoftAP + configure**, which Create New often hits after SSID list already failed (502).

---

## Phone “Redirect Setup” Next (symptom C)

### What serial shows

Phone AP client mostly:

1. Captive detection (`/generate_204`)
2. Failed root (`/` → notFound)
3. Successful `/admin/setup` loads

There is **no** serial evidence of a stuck POST from a wizard “Next” on the phone in the pasted window.

### What the product does today

| Entry | Behavior |
|-------|----------|
| SoftAP `GET /` | Plane notFound / `SETUP_PLANE_RESTRICTED` — **not** a redirect to `/admin/setup` |
| Captive detection paths | Handled separately (`CaptivePortalDetection`) |
| Correct setup URL | `http://192.168.4.1/admin/setup` |
| Unlock (laptop) | Session is device-wide; phone can use unlocked `/admin/setup` without re-unlock |

### Wizard “Next” that truly no-ops

Owner panel button `#createBtn` label **“Next”**:

```javascript
if (submitting || (setupStatus && setupStatus.ownerCreated)) return;
```

If status fetch fails under SoftAP pressure, `loadSetupStatus` catch falls back to `showPanel('panelOwner')`. With `ownerCreated: true`, **Next does nothing** — no error UI.

Android’s own captive “Continue / Next” after a redirect interstitial is **outside** this JS; broken `GET /` makes that chrome look dead.

---

## Relation to prior work (what was already done)

These are **related but separate** and **do not fix** A/B/C:

| Prior work | Doc / change | Effect on this incident |
|------------|--------------|-------------------------|
| Factory-reset HTTP/SSE quiesce | `FACTORY_RESET_COMMUNICATION_QUIESCE.md` | N/A (not resetting) |
| SSE `onConnect` never `client->close()` | `FACTORY_RESET_SSE_ONCONNECT_CLOSE_FORENSIC.md` | N/A |
| Unlock must call `unlockSetup()` mid-install | `SETUP_LOCKED_PASSWORD_FAILURE_FORENSIC.md` | Unlock on laptop **worked** here |
| Step 4 `0x80c` WiFi/coex provenance | `STEP4_1624_DMA_ALLOC_PROVENANCE.md` | **Same class** as this Guru; still no SoftAP product fix |

This forensic **does not** move WiFi DMA to PSRAM, change `ETH_DMA_LOW` / 1536, patch AsyncTCP/IDF SPI, or add wizard steps.

---

## What was done in this forensic pass

1. Correlated installer serial + browser console with current Waveshare ELF `6ec2953e5`.
2. Traced Step 4 UI: `loadWifiNetworks` → 202/502 handling; Finish → `saveWifiSelection` → `executeAdoption` → configure job.
3. Confirmed list-wifi and configure paths emit **502** / heavy RouterOS + INTERNAL use on SoftAP.
4. Symbolicated Guru neighborhood via `objdump` → SPI `uninstall_priv_desc` after failed priv RX (secondary to DMA).
5. Traced phone SoftAP entry: `/` notFound vs `/admin/setup`; Owner Next early-return when `ownerCreated`.
6. Wrote this document. **No firmware/UI code change in this pass.**

---

## Classification summary

| Claim | Level |
|-------|--------|
| Caps `0x80c` fails are WiFi/coex INTERNAL DMA, not W5500 bounce (`0x808`) | **PROVEN** (monitor class + prior provenance doc) |
| Captive SoftAP traffic correlates with `0x80c` alloc fails | **PROVEN** (same log window) |
| Existing scan can succeed while SSID list returns 502 / is slow | **PROVEN** (this session) |
| Configure under SoftAP collapses `dma_largest` to tens of bytes | **PROVEN** |
| Guru is secondary NULL deref after SPI RX buffer alloc failure | **PROVEN** (serial + objdump) |
| Create New uniquely crashes vs Existing | **NOT PROVEN** — both share configure job |
| Phone OS Redirect Next dead because `GET /` has no setup redirect | **STRONGLY INDICATED** |
| Phone Next is Owner `#createBtn` with `ownerCreated` | **POSSIBLE** if status fail → Owner panel |

---

## Minimal fix candidates (not implemented; product approval for SoftAP UX)

Ordered smallest → larger; do **not** expand the frozen wizard:

1. SoftAP `GET /` → **302/200 redirect or HTML meta refresh to `/admin/setup`** (preserves plane gates).
2. Owner `#createBtn`: if `ownerCreated`, **advance via `resumeWizardFromStatus`** instead of silent return; disable/hide when inappropriate.
3. SoftAP-safe Step 4: prefer **WifiDiscoveryCache** / avoid overlapping configure + heavy SoftAP storm; defer SD/`sd-readJson` churn during configure; optional wait-for-DMA-headroom before RouterOS wireless print (behavior change — measure first).
4. Persist / JSON off INTERNAL during SoftAP (see `STEP4_1624_DMA_ALLOC_PROVENANCE.md`) so SoftAP 1624-byte WiFi DMA retains a contiguous block.

**Do not:** move SoftAP WiFi DMA to PSRAM; lower ETH DMA gate; rewrite wizard; merge External AP drivers.

---

## Repro checklist (for a future fix)

1. SoftAP + laptop unlock → Step 4; note time to SSID list / 502.
2. SoftAP + phone captive only → open setup; try OS Next vs open `http://192.168.4.1/admin/setup` explicitly.
3. SoftAP + Finish with Create New **and** Existing after successful scan; capture `[dma-alloc-fail]` and whether Guru returns.
4. Optional control: Ethernet-only Admin path for Step 4 (if available) — expect less SoftAP `0x80c` pressure.

---

## End state

Installer issues A/B/C share **SoftAP INTERNAL/DMA pressure** and **weak SoftAP entry (`/`)**. Unlock and scan can succeed; **SSID discovery and configure** under SoftAP are the failure modes that produce slow UI, 502, and Guru. 

**Fix implemented:** see `docs/SETUP_STEP4_SOFTAP_DMA_FIX.md` (SoftAP entry redirects, captive 204 under load, SoftAP-safe DMA waits, PSRAM JSON on list/configure, Owner Next resume, SSID 503 retry).
