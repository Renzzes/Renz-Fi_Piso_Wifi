# Setup SoftAP / cross-client / Step 4 DMA — Fix Record

**Date:** 2026-08-22 (updated same day)  
**Status:** Implemented (firmware). Flash Waveshare (or Freenove) to validate on hardware.  
**Related forensic:** `docs/SETUP_STEP4_SOFTAP_WIFI_DMA_GURU_FORENSIC.md`

## Intent

Setup is **one system** with multiple access paths (phone SoftAP captive redirect, laptop SoftAP browser, unlock on either device). Unlock session is device-wide. SoftAP entry and Step 4 (**Use Existing SSID** / **Create New SSID**) must not collapse INTERNAL DMA into a Guru Meditation, and Step 4 **Finish** must reach Step 5 when MikroTik work can run safely.

## Root causes addressed

| Symptom | Cause | Fix |
|---------|--------|-----|
| Phone “Redirect / Next” does nothing | SoftAP `GET /` → `SETUP_PLANE_RESTRICTED` JSON, not wizard | SoftAP **`/` serves wizard HTML** (same as `/admin/setup`); captive probes 302 → relative `/admin/setup` |
| Laptop Step 1 **duplicate chrome** | Absolute 302 `/` → `http://192.168.4.1/admin/setup` stacking + `FPSTR` HTML copy into RAM | Serve wizard on `/` (no 302); **`send_P`** streams PROGMEM HTML; Windows `/redirect` captive path registered |
| Cross-client Owner **Next** no-op | `#createBtn` returned early when `ownerCreated` | Next **resumes** wizard via `/api/setup/status` |
| Slow / empty SSIDs, 502 | SoftAP DMA + list-wifi INTERNAL JSON; fail-fast ETH_DMA_LOW | Prefer SoftAP margin then **ETH TX 1536**; **PSRAM** JSON; **503 ETH_DMA_LOW** + UI retry |
| Step 4 **Finish** stuck (no Step 5) | Hard wait for **4096** DMA while SoftAP steadies at **~4084** → 10s timeout → `configure-existing-network` **503** | `waitForRouterOsConnectHeadroom`: prefer 4096 briefly, **proceed if ≥1536**; UI retries ETH_DMA_LOW ×3 |
| Create/Existing SSID → Guru | SoftAP captive storms → SPI priv RX NULL | Captive **204** when busy/DMA-low; soft fail below 1536 instead of crash |

**Not changed:** wizard step count/order; `ETH_DMA_LOW` threshold (still **1536**); WiFi DMA not moved to PSRAM; External AP / Admin isolation rules.

## Code changes

1. **`WebServerManager::registerAdminEntryRoute`** — SoftAP entry URLs call `SetupServer::servePage` (wizard HTML). Ethernet still serves Admin SPA.
2. **`CaptivePortalDetectionServer`** — relative `SETUP_PATH` (`/admin/setup`); `/redirect` probe; **204** when busy/DMA-low.
3. **`DmaMemoryMonitor::waitForRouterOsConnectHeadroom`** — prefer SoftAP-safe 4096 briefly; hard gate remains **1536** only.
4. **`RouterProvisioningWorker::openPersistedRouterClient`** — uses prefer-then-1536 (no 10s fail at 4096); list/configure **PsramJsonDocument**.
5. **`RouterProvisioningManager` / `RouterWirelessAdapter`** — same prefer-then-1536 waits.
6. **`SetupServer::servePage` + `send_P`** — PROGMEM HTML without large RAM copy.
7. **`SetupWizardPageHtml.h`** — Owner Next resume; SSID 503 retry; Finish/adoption **ETH_DMA_LOW** retry.

## Safety properties

- Fail **soft** (503 / ETH_DMA_LOW) only when largest DMA is below **1536**, not when SoftAP is at a healthy ~4 KB.
- Captive probes stay cheap during heavy jobs so phone/laptop unlock+continue do not mutually starve DMA.
- Unlock remains shared RAM session — either client can continue after the other unlocks.

## Validation checklist (physical)

1. Laptop SoftAP `http://192.168.4.1/` → **one** Step 1 (no duplicated header/progress).
2. Phone captive redirect → wizard; Owner Next advances if already created.
3. Step 4 Existing SSID → **Finish** → configure job **200** → Step 5 Administrator & Operator (serial: no `open-persisted-dma-wait-failed` at largest≈4084).
4. Create New SSID → Finish without Guru.
5. Ethernet `http://10.10.10.2/` Admin SPA still loads.

## Build

```text
pio run -e waveshare_esp32_s3_eth
pio run -e freenove_esp32_s3_wroom
```
