# Renz-Fi Release — v0.5.0 Fully Operational Baseline

| Field | Value |
|-------|--------|
| Release | `v0.5.0-fully-operational` |
| Firmware | `0.5.0-w5500` |
| Status | **FULLY FUNCTIONAL / OPERATIONAL** |
| Branch | `main` |
| Remote | `https://github.com/Renzzes/Renz-Fi_Piso_Wifi.git` |

The current validated build is fully functional and operational. No known active operational blockers were observed during the current validation.

This document does **not** claim that future hardware, media, or network faults are impossible.

**External Access Point Management is not included in this release.** That optional feature must be implemented after this tag, as a separate change.

---

## 1. Release purpose

Preserve the current successful Renz-Fi ESP32-S3 + W5500 + MikroTik Piso WiFi system as a reproducible Git rollback point.

This is the stable baseline immediately before the optional External Access Point feature.

An earlier tag `v0.5.1-stable-baseline` exists on the remote as a prior snapshot. It is **not** this operational freeze. The firmware string for this baseline is `0.5.0-w5500`; the Git tag name requested for this freeze is `v0.5.0-fully-operational`.

---

## 2. Architecture

```
Internet
   ↓
MikroTik RouterOS     ← Hotspot / DHCP / gateway authority
   ↓
ESP32-S3 + W5500      ← appliance controller
   ├── Admin REST + SSE + PWA
   ├── Captive portal
   ├── Coin + voucher sessions
   ├── Sales
   ├── SD (authoritative when healthy)
   ├── SPIFFS emergency fallback
   └── RouterWorker → RouterOS API
```

MikroTik remains the network authority. The ESP32 is the application layer. Admin is an optional client of Core; coin/session/sales/internet grant do not require the dashboard to be open.

SPI (unchanged):

- W5500: MOSI=11 MISO=13 SCK=12 CS=10 RST=14
- SD: MOSI=6 MISO=5 SCK=7 CS=18

---

## 3. Major completed features

- Frozen six-step setup wizard
- Production Ethernet Admin PWA
- Captive portal coin and voucher flows
- Voucher generate (count 1–20, default 3), delete, bulk-delete
- Sales summaries and 7 / 28 / 180-day charts
- SD fail-fast lifecycle and remount recovery
- SPIFFS fallback with owner-review conflicts
- RouterOS cache refresh/sync on RouterWorker
- Storage health UI (media vs reconciliation)

---

## 4. Stability fixes included in this baseline

These already exist in the tree. They are not re-implemented here. Forensic reports remain in `docs/` and `ESP32_S3_Firmware/docs/`.

### 4.1 Voucher job / TWDT

History is no longer appended one voucher at a time.

```
one Generate job
  → one voucher collection transaction
  → StorageManager::appendHistoryPreparedLines
  → NdjsonLedger::appendSdPreparedLines
```

SD remains authoritative. Vouchers are not continuously mirrored to SPIFFS.

### 4.2 Voucher Admin lifecycle

Terminal job states, explicit ok/count/result, one poller per job (~400 ms while running), polling stops when terminal, success toast, list refresh, modal close. Amount and minutes must be explicit (no `count ?? 0` / `amount ?? 0` / `minutes ?? 0` payloads).

### 4.3 Voucher delete routing

Exact URI match for `POST /api/vouchers` so `POST /api/vouchers/bulk-delete` cannot hit Generate. Bulk-delete is HTTP 202 + worker job + one collection persist + one history batch. Not synchronous DELETE processing.

### 4.4 SD hot-unplug recovery

Lifecycle: `SD_DISABLED` / `SD_MOUNTING` / `SD_READY` / `SD_DEGRADED` / `SD_REMOUNTING` / `SD_FAILED`.

```
failed SD I/O → tripSdMediaMissing → SD_DEGRADED → emergency storage
  → remount when media returns → verify → synchronize/replay → SD_READY
```

Recovery marks in-progress, publishes `SD_REMOUNTING`, marks SD unreadable, **releases STORAGE_LOCK**, remounts, verifies, refreshes the RAM snapshot. This prevents AsyncTCP Task Watchdog failures from holding the lock across `SD.begin`. The system does not continuously poll a missing card.

### 4.5 Router recovery gating

SD/storage recovery is separate from RouterOS health. `SD_READY` does not mean RouterOS HEALTHY.

Admin jobs are blocked only while storage is Mounting / Remounting / Syncing / Degraded:

- `503 ROUTER_RECOVERY_IN_PROGRESS` — rejected, not queued
- `503 ROUTER_WORKER_BUSY` — a worker is already running

Deferred means **REJECTED**, not queued.

### 4.6 RouterOS credentials

Production telemetry reads `/config/router.json`. Setup credentials are in `/config/router-connection.json`. The implementation re-reads `router.json` and reconciles when required. It does not keep an unconditional `_productionCredentialsOk` shortcut after SD recovery. Username is not hardcoded to `admin`. Authentication is not bypassed. Documentation uses `<ROUTER_USERNAME>` / `<ROUTER_PASSWORD>`.

### 4.7 Sales / W5500 DMA

The 180-day chart failure was **not** general heap exhaustion. Proven chain:

```
180-day sales chart
  → INTERNAL SRAM JSON + SD recover-on-read allocations + retained cache docs
  → DMA-capable INTERNAL SRAM fragmentation
  → dma_largest ≈ 16 bytes
  → W5500 SPI DMA private buffer alloc fail (size=54, caps=0x00000808)
  → setup_dma_priv_buffer / W5500 TX failure
```

The W5500 was the victim. Hardening: PSRAM-backed CPU-side JSON, bounded `int32_t[180]` buckets, no eager recover-on-healthy-read, DMA headroom check before HTTP send. **180-day charts remain.** They are not clamped.

### 4.8 Storage health / conflict semantics

Media health is not the same as reconciliation. SD can be HEALTHY while an unresolved SPIFFS/SD copy difference needs owner review. No auto-merge. Do not silently delete `/sessions/portal_sessions.json`.

### 4.9 SPA / Admin serving

StaticFileServer prefers `/index.html.gz` when present, else `/index.html`. SPA fallback routing is unchanged. Deployment remains Admin build → `ESP32_S3_Firmware/data/` → `uploadfs`.

---

## 5. Voucher system

- Count 1–20, default 3
- One Generate job, one collection write, one history batch
- HTTP 202 job lifecycle, single-flight
- Bulk-delete via worker job

---

## 6. SD recovery

Fail-fast media missing, emergency SPIFFS, remount owner off async_tcp, write verification, return to `SD_READY`. Ethernet remains up during the validated remount sequence.

---

## 7. RouterOS integration

All RouterOS I/O stays on `router_worker`. HTTP enqueues; the worker runs login/commands; Admin polls job results. No RouterOS inside AsyncWebServer callbacks.

---

## 8. Sales / DMA hardening

See §4.7. Host regression: `scripts/test-sales-chart-buckets.mjs`.

---

## 9. Storage conflict semantics

See §4.8. Host regression: `scripts/test-storage-health-semantics.mjs`.

---

## 10. Admin frontend

React PWA served from SPIFFS. Connect flow: existing auth → Core state (`GET /api/status`, optional stale `POST /api/router/cache/sync` worker job) → Dashboard. Router sync is skipped while storage recovery is in progress.

---

## 11. Physical validation observations

Observed on the current validated hardware build:

- ESP32 operational; not continuously rebooting
- W5500 Ethernet operational
- MikroTik RouterOS communication operational
- Voucher generate/delete/bulk-delete/View-Print operational
- Captive portal and coin operational
- SD storage and remount recovery operational
- Router synchronization and recovery gating operational
- Sales reporting operational, including DMA-hardened chart path
- No observed Guru Meditation during normal operation
- No observed Task Watchdog resets during normal operation
- No observed persistent Ethernet connection loss
- No observed W5500 DMA allocation crash during normal operation
- No observed MikroTik 100% CPU runaway workload

---

## 12. Build validation

Commands used for this freeze (results recorded in the GitHub baseline report):

```bash
node scripts/test-sales-chart-buckets.mjs
node scripts/test-storage-health-semantics.mjs
node scripts/test-sales-uptime-aggregation.mjs
npm run build:esp32
pio run -e freenove_esp32_s3_wroom
```

`ESP32_S3_Firmware/data/` is generated and gitignored. Staging it is expected; it is not committed.

---

## 13. Git commit / tag

| Item | Value |
|------|--------|
| Commit message | `release: v0.5.0 fully operational baseline` |
| Tag | `v0.5.0-fully-operational` (annotated) |
| Tag target | the release commit on `main` |

Resolve SHAs after the tag exists:

```bash
git rev-parse HEAD
git rev-parse v0.5.0-fully-operational^{}
```

---

## 14. Future feature boundary

Upcoming optional feature: **External Access Point Management**.

**NOT INCLUDED IN `v0.5.0-fully-operational`.**

Do not start that work by modifying this tag. Branch from it.

---

## Forensic history (retained)

Do not delete:

- `ESP32_S3_Firmware/docs/SD_HOTUNPLUG_*`
- `ESP32_S3_Firmware/docs/SD_HOTUNPLUG_ASYNCTCP_WDT_RECOVERY_FIX_REPORT.md`
- `ESP32_S3_Firmware/docs/ROUTER_SYNC_CREDENTIAL_RECOVERY_FIX_REPORT.md`
- `ESP32_S3_Firmware/docs/SALES_CHART_SPI_DMA_FORENSIC.md`
- `ESP32_S3_Firmware/docs/REMAINING_ISSUES_FORENSIC_IMPLEMENTATION.md`
- Related `docs/RENZFI_*` incident and remediation reports
