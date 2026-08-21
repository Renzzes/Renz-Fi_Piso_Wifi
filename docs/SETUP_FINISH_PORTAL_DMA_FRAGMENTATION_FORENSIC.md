# Setup Step 4 Finish + Portal Activation + DMA Fragmentation

**Board:** Waveshare ESP32-S3-ETH  
**HEAD before this fix:** `37f47a7`  
**Physical validation:** NOT DONE in this session

## References (authority, not rewritten)

| Document | What it already proved |
|----------|------------------------|
| `TWDT_WIFI_SELECTION_ROOT_CAUSE.md` | Step 4 **Finish** is Wi-Fi Continue, **not** `POST /api/setup/finish` |
| `WIFI_SELECTION_TWDT_IMPLEMENTATION_REPORT.md` | Selection → 202 + poll `/api/setup/status` until `PERSISTED` → adoption |
| `ESP32_S3_Firmware/docs/SETUP_ARCHITECTURE.md` | Frozen 6-step wizard; finish job is later |
| `ESP32_S3_Firmware/docs/REMAINING_ISSUES_FORENSIC_IMPLEMENTATION.md` | `JSON_DOC_MEDIUM` (8192) on default heap = INTERNAL/DMA shredder |
| `docs/ADMIN_POLL_INTERNAL_JSON_DMA_FORENSIC.md` | Same class on Admin poll (`37f47a7`) |
| `docs/RENZFI_GURU_MEDITATION_PREVENTION_BASELINE.md` | Frozen TWDT/Guru protections |
| `DONE_PAYING_INTERNET_GRANT_IMPLEMENTATION_REPORT.md` | Activation: worker → RouterOS → outcome; portal JS `noteApplianceFailure` |
| `portal/renzfi-app.js` | Two background HTTP failures → “Payment Service is temporarily unavailable” |

## 1. Setup Step 4 → Step 5 call graph (current)

```
wifiNextBtn "Finish"  (SetupWizardPageHtml.h — NOT React Summary Finish)
  → POST /api/setup/router/wifi/selection
       SetupServer: RAM save + deferred persist → HTTP 202
  → poll GET /api/setup/status every 250 ms (≤15 s)
       HeapJsonDocument(JSON_DOC_MEDIUM=8192) INTERNAL  ← shredder
       fillSetupStatus + serveJsonEnvelope → Arduino String INTERNAL
  → POST /api/setup/router/existing-network/configure  (202 + jobId)
       RouterProvisioningWorker adoption
  → poll job → showSetupCompletePanel / Step 5 Admin

POST /api/setup/finish is a LATER wizard step (Summary). It is not Step 4 Finish.
```

### Setup A–O (this log)

| Q | Answer | Label |
|---|--------|--------|
| A Browser POST `/api/setup/finish`? | **No — that is not Step 4 Finish.** Step 4 sends wifi/selection then configure. | **PROVEN** (HTML + TWDT wifi doc) |
| B–E finish pipeline? | Not the Step 4 path | **RULED OUT** as Step 4 stall |
| G–I RouterOS on Step 4? | After persist: **adoption** configure job. DMA gate can skip connect. | **STRONGLY INDICATED** |
| J–O installation provisioned / Step 5? | Not reached if DMA kills HTTP or ETH_DMA_LOW blocks worker | **STRONGLY INDICATED** |

Exact stall boundary: **`GET /api/setup/status` 8192 INTERNAL poll + W5500 TX**, then **RouterOS connect deferred (`ETH_DMA_LOW`)**. Not a missing Finish handler.

## 2. Captive portal activation call graph

```
Insert coin → credits OK
POST /api/portal/done-paying
  → PortalSessionManager::donePaying
  → onSessionActivated → tryEnqueueActivateHotspotUser
  → RouterWorker: credentials → RouterOS login → user → active/login
  → HTTP 200 "Session activating" + GET /session + heartbeat
portal JS: 2 failed background fetches → noteApplianceFailure()
```

MikroTik **active session** means RouterOS grant **did run**. “Payment Service temporarily unavailable” is **appliance HTTP failure after grant**, not “activation never ran”.

## 3. DMA sequence (this physical log)

**PROVEN fragmentation, not OOM:** `dma_free=7424` `dma_largest=1268` while TX needs ~1394.

```
sd-readJson:before  largest=1268   ← already fragmented BEFORE SD
sd-readJson delta   dmaFree=-436 / -1208  ← amplifier (Arduino String payload)
dma-alloc-fail size=1394  largest=1268  → TX fail
dma-alloc-fail size=636   largest=1076  → RX fail
spi_master setup_dma_priv_buffer fail
Guru Meditation LoadProhibited EXCVADDR=0  ← SECONDARY IDF NULL
```

**RULED OUT:** SD as origin (largest already 1268 before read).  
**RULED OUT:** “ESP32 out of RAM” (PSRAM/total heap not the allocator).  
**PROVEN:** `[ros-health] probe login_failed reason=Ethernet DMA memory low — defer RouterOS connect` is the **DMA gate** (`RouterOsClient::connect`), **not** MikroTik TCP proof of down. Then `UNAVAILABLE` / `verify skipped health=COOLDOWN` is **downstream**.

## 4. Combined classification

| Symptom | Classification | Boundary |
|---------|----------------|----------|
| Step 4 Finish appears stuck | **STRONGLY INDICATED** DMA shred from status poll; **POSSIBLE** Guru reboot | wifi/selection + `/api/setup/status` + adoption job |
| Portal unavailable + MikroTik active | **STRONGLY INDICATED** HTTP/heartbeat lost after grant | `noteApplianceFailure`; ETH TX fail |
| ros-health UNAVAILABLE | **PROVEN** ETH_DMA_LOW gate | `RouterOsClient.cpp` connect precheck |
| Guru Meditation | **PROVEN** secondary | IDF `setup_dma_priv_buffer` NULL |
| SD readJson | **PROVEN** amplifier | deltas; origin is prior INTERNAL JSON |

## 5. Why `37f47a7` (Admin poll PSRAM) was not enough

That commit moved Admin `/api/system/health`. Setup `/api/setup/status` (8192 × 4/s) and `serveJsonEnvelope` String copies were unchanged. Portal session `HeapJsonDocument(JSON_DOC_SMALL)` still INTERNAL.

## 6. Minimal fix (this change)

1. `GET /api/setup/status` → `PsramJsonDocument`
2. `POST .../wifi/selection` response → `PsramJsonDocument` + `serveJsonEnvelope`
3. `WebResponse::serveJsonEnvelope` / `serveErrorJson` → PSRAM body callback (no INTERNAL String envelope)
4. Portal `GET /session`, coin open, done-paying response docs → `PsramJsonDocument`

Unchanged: W5500 pins, DMA gates, RouterOS command count, wizard steps, portal JSON schema, HTTP contracts, SD recovery, WDT.

## 7. Physical tests still required

Step 4 Finish → Step 5; coin + Done Paying → Connected without service notice; `dma_largest` stays ≥ ~8 KB after polls; no `dma-alloc-fail`; no Guru.
