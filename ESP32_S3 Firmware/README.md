# Renz-Fi ESP32-S3 Firmware

Arduino IDE compatible firmware for the Renz-Fi Piso WiFi appliance. This folder is intentionally standalone and contains only ESP32-S3 firmware/backend code.

## Target

- ESP32-S3 N16R8, 16MB flash, 8MB PSRAM
- Micro SD card over SPI
- Universal coin acceptor pulse input
- Insert Coin LED output
- MikroTik hEX Router integration boundary
- Captive portal and embedded REST/SSE server at `http://10.10.10.1`

## Required Libraries

Install these through Arduino IDE Library Manager or from their upstream repositories:

- ESP32 Arduino core
- ESPAsyncWebServer
- AsyncTCP
- ArduinoJson

The firmware also uses ESP32 core libraries: `WiFi.h`, `SPI.h`, `SD.h`, `ESPmDNS.h`, `Preferences.h`, and `DNSServer.h`.

## Arduino IDE Setup

1. Open `ESP32_S3_Firmware.ino` from this folder.
2. Select an ESP32-S3 board profile with PSRAM enabled.
3. Recommended partition scheme: 16MB flash with large app and filesystem.
4. Edit `src/config.h` for your pins and SD chip-select pin.
5. Build and upload.

## Static Frontend Files

Copy the React PWA build output onto the SD card:

```text
/www/index.html
/www/assets/*
/www/manifest.webmanifest
/www/sw.js
```

The firmware serves `/api/*` with `Cache-Control: no-store` and serves static assets with cache headers.

## SD Card Layout

The firmware creates these folders and JSON files automatically:

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

## Default Admin

- Password: `admin`
- First login requires changing the password.
- Auth is cookie/session based with HTTP-only local sessions.

## API Surface

The sketch implements `/api/health`, auth endpoints, status, promos, vouchers, users, sales, settings, logs, coin settings/diagnostics, router settings/test, system reboot/factory reset, and `/api/events` SSE.

MikroTik RouterOS operations are isolated behind `MikroTikManager`. Fill in production RouterOS API transport there without changing route handlers or coin/session logic.
