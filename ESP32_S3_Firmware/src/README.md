# Renz-Fi ESP32-S3 Firmware

Arduino IDE compatible firmware for the Renz-Fi Piso WiFi appliance. The ESP32-S3 is the embedded backend, storage owner, API host, fallback captive portal, SSE/event source, and React PWA frontend host. In production it joins the MikroTik/TP-Link LAN as a WiFi station while keeping a setup AP for recovery.

## Target

- ESP32-S3 N16R8, 16 MB flash, 8 MB PSRAM
- AP+STA appliance mode on the MikroTik/TP-Link LAN
- Production STA SSID: `RenzFi_PesoWifi` (open network, no WPA password)
- Default STA address: `http://192.168.30.252/admin`
- Fallback setup AP: `Renz-Fi-Setup`
- React PWA served from SPIFFS
- Runtime appliance data stored on microSD over SPI
- Universal coin acceptor pulse input
- Common-negative RGB status LED
- MikroTik hEX Router integration boundary
- Captive portal and embedded REST/SSE server

## Required Arduino IDE Setup

Install the **ESP32 by Espressif Systems** board package in Arduino IDE Board Manager.

Required libraries:

- ESPAsyncWebServer
- AsyncTCP
- ArduinoJson

ESP32 core libraries used by the firmware:

- `SPIFFS`
- `DNSServer`
- `WiFi`
- `ESPmDNS`
- `Preferences`
- `SPI`
- `SD`

Recommended board selection:

- Board: `ESP32S3 Dev Module`
- USB CDC On Boot: `Enabled`
- PSRAM: `OPI PSRAM` / enabled for your N16R8 board
- Flash Size: `16MB`
- Flash Mode: board default or `QIO` when supported
- Upload Speed: `921600` or lower if uploads are unstable
- Partition Scheme: choose a 16 MB scheme with app + filesystem space, for example a large app / SPIFFS capable scheme

## Arduino IDE Project Layout

Arduino IDE expects the sketch folder name to match the `.ino` file:

```text
ESP32_S3_Firmware/
├── ESP32_S3_Firmware.ino
├── ApiServer.cpp
├── ApiServer.h
├── AuthManager.cpp
├── AuthManager.h
├── CaptivePortal.cpp
├── CaptivePortal.h
├── CoinManager.cpp
├── CoinManager.h
├── Config.h
├── EventBus.cpp
├── EventBus.h
├── FirmwareApp.cpp
├── FirmwareApp.h
├── Logger.cpp
├── Logger.h
├── MikroTikManager.cpp
├── MikroTikManager.h
├── Models.h
├── PromoManager.cpp
├── PromoManager.h
├── SessionManager.cpp
├── SessionManager.h
├── StorageManager.cpp
├── StorageManager.h
├── VoucherManager.cpp
├── VoucherManager.h
├── WiFiBootstrap.cpp
├── WiFiBootstrap.h
├── data/
└── README.md
```

If Arduino IDE asks to move the sketch, allow it to create/use `ESP32_S3_Firmware`.

## React PWA Frontend Hosting

The ESP32-S3 serves the built React PWA directly from **SPIFFS**. `/api/*` remains same-origin with the embedded REST API on the ESP32 STA address or fallback AP address.

Final production workflow (from repo root):

```text
npm run deploy:esp32
```

Or step by step:

```text
npm run build:esp32    # stages admin (dist/) + portal (portal/) → data/
pio run -t upload
pio run -t uploadfs
open http://192.168.30.252/admin
```

Do not edit `ESP32_S3_Firmware/data/` manually. Portal sources live in `portal/`. See `docs/ESP32_STAGING.md`.

Expected filesystem contents (Vite must emit bundles under `assets/`):

```text
data/
  index.html
  assets/index-xxxxx.js
  assets/index-xxxxx.css
  manifest.webmanifest
  sw.js
```

Build and stage for SPIFFS:

```text
npm run build:esp32
```

Upload the SPIFFS image using an Arduino IDE ESP32 filesystem upload tool compatible with Arduino IDE 2.x, such as **ESP32 Sketch Data Upload** with SPIFFS support. The uploaded SPIFFS image becomes the frontend filesystem.

Static routes served from SPIFFS:

- `/`
- `/assets/*`
- `/manifest.webmanifest`
- `/sw.js`
- Unknown non-API routes fall back to `/index.html` for SPA routing

API routes keep using `/api/*` and return `Cache-Control: no-store`.

## Boot Architecture

The firmware uses AP+STA appliance mode and remains non-blocking. STA connects to the production WiFi first, while the fallback setup AP remains available even if SD, MikroTik, internet, or SPIFFS frontend assets are unavailable.

Boot order:

1. `Serial.begin(115200)`
2. `delay(1000)`
3. Startup banner and diagnostics
4. WiFi AP+STA startup
5. Captive portal DNS startup
6. SPIFFS frontend filesystem mount
7. Event bus and manager initialization
8. Optional SD runtime storage initialization
9. MikroTik boundary initialization
10. API/static route registration
11. AsyncWebServer startup
12. `RENZ-FI BOOT COMPLETE`

Errors such as `[ERROR] SD card mount failed` or `[ERROR] SPIFFS mount failed` are degraded-mode warnings. They should not prevent the AP/admin dashboard endpoint from being reachable.

## SD Card Runtime Data

The SD card remains the runtime appliance data owner for JSON state and logs. The firmware creates these folders and files automatically when the SD card is present:

```text
/config/settings.json
/config/promos.json
/config/router.json
/vouchers/vouchers.json
/sales/sales.json
/logs/logs.json
/sessions/users.json
/sessions/admin.json
```

## Hardware Pins

Configured in `Config.h`:

- W5500 CS: GPIO 10
- W5500 MOSI: GPIO 11
- W5500 SCK: GPIO 12
- W5500 MISO: GPIO 13
- W5500 RST: GPIO 14
- SD CS: GPIO 18
- SD MOSI: GPIO 15
- SD SCK: GPIO 17
- SD MISO: GPIO 16
- Coin pulse input: GPIO 4
- RGB red: GPIO 5
- RGB green: GPIO 6
- RGB blue: GPIO 7
- Reset button: GPIO 21

RGB behavior:

- Blue blinking: ready / waiting for coin
- Green for 1.2 seconds: coin accepted
- Red blinking: error mode
- Off: coin acceptor disabled

## Default Admin

- Password: `admin`
- First login requires changing the password.
- Auth is cookie/session based with HTTP-only local sessions.

## API Surface

The sketch implements `/api/health`, auth endpoints, status, promos, vouchers, users, sales, settings, logs, coin settings/diagnostics, router settings/test, system reboot/factory reset, and `/api/events` SSE.

MikroTik RouterOS operations remain isolated behind `MikroTikManager`. Fill in production RouterOS API transport there without changing route handlers or coin/session logic.
