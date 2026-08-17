# Renz-Fi

Renz-Fi is a local-first Piso WiFi appliance. The production runtime is an ESP32-S3 controller with W5500 Ethernet, SD storage, SPIFFS fallback, a captive portal, voucher and coin session management, and an Admin PWA. MikroTik RouterOS remains the network and Hotspot authority.

This repository is the source for:

- ESP32-S3 firmware (`ESP32_S3_Firmware/`)
- Admin React PWA (`src/`)
- Captive portal sources (`portal/`)
- Owner Android app (`RenzFi-Owner-App/`)
- A Node.js development simulator (`server/`) — not the production runtime

## Current Status — Fully Operational

**This tagged baseline represents the current fully functional and operational Renz-Fi system.**

The current validated build is fully functional and operational. No known active operational blockers were observed during the current physical validation.

Firmware: **0.5.0-w5500**  
Git release: **v0.5.0-fully-operational**

This tag is the stable rollback point immediately **before** the optional External Access Point feature. That feature is **not included** in this baseline.

### Physical validation / operational observations

During the current validation:

- ESP32 firmware operated normally and remained responsive.
- W5500 Ethernet remained stable; no observed persistent connection-loss behavior.
- MikroTik connectivity and RouterOS API communication worked, including credentials, synchronization, refresh, and recovery gating.
- Voucher generation, deletion, bulk deletion, View/Print, and the HTTP 202 job lifecycle worked.
- SD storage, hot-unplug/reinsert recovery, and SPIFFS emergency fallback worked.
- Captive portal, coin, and sales reporting worked. The 180-day sales chart was hardened against the previous DMA fragmentation failure.
- Storage health monitoring distinguished media health from SPIFFS/SD reconciliation conflicts.
- SD recovery did not permanently block RouterOS recovery. Admin polling did not create runaway RouterOS jobs.
- RouterWorker architecture remained in use.
- No Guru Meditation, task watchdog reset, or W5500 DMA allocation crash was observed during normal operation.
- The MikroTik was no longer exhibiting the previous 100% CPU spike / runaway workload behavior.

These are current physical validation observations. They are not a claim that future hardware or media faults are impossible.

See [docs/RELEASE_v0.5.0_FULLY_OPERATIONAL.md](docs/RELEASE_v0.5.0_FULLY_OPERATIONAL.md) for the full release record.

## Architecture

```
Internet
   ↓
MikroTik RouterOS          ← network / Hotspot / DHCP / gateway authority
   ↓
Renz-Fi ESP32-S3 + W5500   ← appliance controller / application layer
   ├── Admin API + PWA
   ├── Captive Portal
   ├── Voucher + coin sessions
   ├── Sales reporting
   ├── SD storage (authoritative when healthy)
   ├── SPIFFS (emergency fallback)
   └── RouterOS API client (RouterWorker)
```

MikroTik remains the network authority. The ESP32 does not replace the router. It owns coin/voucher/session/sales application state, serves Admin and portal APIs, and issues Hotspot user operations through RouterOS.

SPI buses (do not swap):

| Bus | Role | Pins |
|-----|------|------|
| W5500 SPI | Ethernet | MOSI=11 MISO=13 SCK=12 CS=10 RST=14 |
| SD SPI | Storage | MOSI=6 MISO=5 SCK=7 CS=18 |

## What this baseline supports

- Setup wizard (frozen six-step installer flow)
- Production Ethernet Admin dashboard (PWA)
- Captive portal coin and voucher sessions
- Voucher generate (count 1–20, default 3), delete, bulk-delete via worker jobs
- Sales today / week / month / 7 / 28 / 180-day charts, history, records
- SD fail-fast hot-unplug → degraded → remount → ready
- SPIFFS emergency storage with **no automatic conflict merge**
- RouterOS cache refresh/sync gated during storage recovery (`503 ROUTER_RECOVERY_IN_PROGRESS` means rejected, not queued)

## Known design boundaries

- MikroTik remains the gateway/Hotspot authority.
- SD remains authoritative for normal persistent storage.
- SPIFFS fallback is used where designed. Unresolved SPIFFS/SD copy differences require owner review — they are **not** auto-merged and are **not** the same as media failure.
- RouterOS health is separate from SD lifecycle. `SD_READY` does not automatically mean RouterOS HEALTHY.
- W5500 and SD remain on their existing SPI buses.
- Task Watchdog configuration is unchanged. The watchdog is not disabled as a workaround.
- Admin is an optional management client. Coin, session, sales, and internet grant do not depend on the dashboard being open.

## Release baseline

| Item | Value |
|------|--------|
| Firmware | `0.5.0-w5500` |
| Git tag | `v0.5.0-fully-operational` |
| Purpose | Fully operational freeze before optional External Access Point work |

Forensic reports that explain earlier failures are retained under `docs/` and `ESP32_S3_Firmware/docs/`. Do not delete them.

## Future roadmap

Upcoming optional feature: **External Access Point Management**.

**Not included in `v0.5.0-fully-operational`.** That work must start from this tag as a separate change so this known-good baseline can always be restored.

## Development

```bash
npm install
npm run dev
```

- UI: http://127.0.0.1:5173 (proxies `/api` to the simulator)
- Simulator API: http://127.0.0.1:3001

The Node.js/Express/SQLite tree is a development simulator only.

### Embedded Admin image

```bash
npm run build:esp32
```

This builds the PWA and stages it into `ESP32_S3_Firmware/data/` (gitignored). Upload with PlatformIO `uploadfs`.

### Firmware

```bash
cd ESP32_S3_Firmware
pio run -e freenove_esp32_s3_wroom
```

### Host checks

```bash
node scripts/test-sales-chart-buckets.mjs
node scripts/test-storage-health-semantics.mjs
node scripts/test-sales-uptime-aggregation.mjs
npm run test:portal
```

## Embedded API boundary

Admin talks to Core through existing REST + SSE (`/api/health`, `/api/status`, `/api/events`, job APIs). Do not open RouterOS from the browser. Do not copy MikroTik passwords into browser storage.

RouterOS credentials live on the appliance:

- Production telemetry: `/config/router.json`
- Setup store: `/config/router-connection.json`

Documentation uses placeholders `<ROUTER_USERNAME>` and `<ROUTER_PASSWORD>` — never publish real RouterOS credentials.
