# Waveshare ESP32-S3-ETH — Stage 1 Hardware Migration Checkpoint

## Status

- **CODE VALIDATION:** PASS
  - Host tests: pass
  - `npm run build:esp32`: pass
  - `pio run -e waveshare_esp32_s3_eth`: SUCCESS (~190s)
  - `pio run -e freenove_esp32_s3_wroom`: SUCCESS (~161s)
- **PHYSICAL VALIDATION:** NOT YET PERFORMED

Do not claim hardware migration complete until physical soak tests pass.

## Baseline / Rollback

| Item | Value |
|------|--------|
| Branch | `feature/waveshare-esp32-s3-eth` |
| Pre-migration checkpoint | `d370b86` (`checkpoint: external-ap-detect-ui-fix`) |
| Production tag (untouched) | `v0.5.0-fully-operational` → `55a33ac289896a20c8687a5da0b623699eef19a7` |
| Freenove software rollback | keep building `env:freenove_esp32_s3_wroom` (old pin map) |

## Boards

| | Old | New (Stage 1) |
|--|-----|----------------|
| Board | Freenove ESP32-S3 WROOM N8R8 + external W5500/SD | Waveshare ESP32-S3-ETH |
| Flash | 8 MB | 16 MB physical (partition table still 8 MB layout) |
| PSRAM | 8 MB OPI | 8 MB OPI |
| Hardware revision string | `ESP32-S3-W5500-N8R8` | `ESP32-S3-ETH-WAVESHARE` |

## Exact pin map (Waveshare / `RENZFI_BOARD_WAVESHARE_ESP32_S3_ETH`)

| Function | GPIO |
|----------|------|
| W5500 MOSI | 11 |
| W5500 MISO | 12 |
| W5500 SCK | 13 |
| W5500 CS | 14 |
| W5500 INT | 10 |
| W5500 RST | 9 |
| SD MOSI | 6 |
| SD MISO | 5 |
| SD SCK | 7 |
| SD CS | **4** |
| COIN_PULSE | **18** |
| RECOVERY | 2 |
| RGB R/G/B | 38 / 39 / 40 |
| WS2812 (GPIO21) | **unused in Stage 1** |

### Critical GPIO4 / GPIO18 swap

| Role | Freenove | Waveshare |
|------|----------|-----------|
| SD CS | 18 | **4** |
| Coin | 4 | **18** |

## SPI architecture (unchanged)

- W5500 → **SPI3_HOST** @ **8 MHz**
- SD → **FSPI / SPI2**
- Buses remain independent (no shared SPIClass / CS / init)

## RGB Phase 1 decision

- Keep discrete RGB on GPIO38/39/40
- Waveshare onboard WS2812 on GPIO21 is **deferred** (no NeoPixel driver)

## PlatformIO

- **New:** `[env:waveshare_esp32_s3_eth]`
  - board: `esp32s3_120_16_8-qio_opi` (16 MB flash / 8 MB OPI PSRAM)
  - flag: `-DRENZFI_BOARD_WAVESHARE_ESP32_S3_ETH`
  - keeps `partitions_custom.csv`
- **Preserved:** `[env:freenove_esp32_s3_wroom]` (default_envs unchanged)
- Pin maps selected at compile time in `W5500Config.h` / `Config.h`

## Files changed (Stage 1)

- `ESP32_S3_Firmware/platformio.ini`
- `ESP32_S3_Firmware/src/W5500Config.h`
- `ESP32_S3_Firmware/src/Config.h`
- `ESP32_S3_Firmware/src/FirmwareApp.cpp` (one-time boot hardware log)
- `ESP32_S3_Firmware/src/EthernetManager.cpp` / `.h` (pin-agnostic diagnostics)
- `ESP32_S3_Firmware/src/SdSpi.cpp` (comment hygiene)
- `ESP32_S3_Firmware/src/NetworkDiagnostics.cpp` (INT poll/irq label)
- `ESP32_S3_Firmware/src/CoinManager.cpp` (comment only)
- `docs/WAVESHARE_ESP32_S3_ETH_STAGE_1_CHECKPOINT.md` (this file)

## Intentionally NOT changed

RouterProvisioningWorker, RouterOS stack, External AP Check/Detect, StorageManager recovery, STORAGE_LOCK, DMA strategy, TWDT, CoinManager behavior, RgbController, RecoveryManager semantics, partition table, production tag.

## Known risks (physical)

1. W5500 INT=GPIO10 ISR coexistence with coin ISR
2. DMA internal fragmentation still possible on new PCB
3. Coin wiring must move to GPIO18
4. Discrete RGB 38/39/40 may be unused on Waveshare unless externally wired
5. 8 MB partition layout on 16 MB flash (intentional Stage 1)

## Physical validation checklist

See Stage 1 prompt TEST 1–12. Required before claiming hardware success: boot, ETH, DMA metrics, MikroTik, SD, hot-unplug, coin, portal, RouterOS, External AP C24, Admin, soak.
