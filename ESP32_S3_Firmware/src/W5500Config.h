#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  W5500Config.h  —  Centralised W5500 SPI pin map + VLAN40 static network
//
//  Board selection (PlatformIO build flag):
//    RENZFI_BOARD_WAVESHARE_ESP32_S3_ETH  → Waveshare ESP32-S3-ETH onboard W5500
//    (default / Freenove env)             → external W5500 on Freenove N8R8
//
//  W5500 ALWAYS uses SPI3_HOST (see EthernetManager). SD uses FSPI separately.
// ─────────────────────────────────────────────────────────────────────────────

#include <IPAddress.h>

namespace W5500Config {

// ── SPI Pin Mapping ───────────────────────────────────────────────────────────
#if defined(RENZFI_BOARD_WAVESHARE_ESP32_S3_ETH)
// Waveshare ESP32-S3-ETH onboard W5500 (Stage 1).
static constexpr int PIN_MOSI = 11;
static constexpr int PIN_MISO = 12;
static constexpr int PIN_SCK  = 13;
static constexpr int PIN_CS   = 14;
static constexpr int PIN_RST  = 9;
static constexpr int PIN_INT  = 10;  // board-wired INT; ETH.begin accepts GPIO
#else
// Freenove ESP32-S3 WROOM N8R8 + external W5500 breakout (rollback baseline).
static constexpr int PIN_MOSI = 11;
static constexpr int PIN_MISO = 13;
static constexpr int PIN_SCK  = 12;
static constexpr int PIN_CS   = 10;
static constexpr int PIN_RST  = 14;
static constexpr int PIN_INT  = -1;  // poll mode
#endif

// ── Static Network Configuration  (VLAN40 Backend) ───────────────────────────
static const IPAddress IP     (10, 40, 0,   2);
static const IPAddress GATEWAY(10, 40, 0,   1);
static const IPAddress SUBNET (255, 255, 255, 0);
static const IPAddress DNS    (10, 40, 0,   1);

// ── W5500 MAC Address ─────────────────────────────────────────────────────────
//  Locally-administered (bit 1 of first octet = 1), unicast.
//  Change the last 3 bytes to make each unit unique when deploying multiple
//  nodes on the same VLAN40 segment.
static constexpr uint8_t MAC[6] = { 0x02, 0xAA, 0xBB, 0x10, 0x40, 0x02 };

// ── SPI / timing ──────────────────────────────────────────────────────────────
// Stage 1 keeps 8 MHz on both boards for bring-up stability (do not raise yet).
static constexpr uint8_t  SPI_FREQ_MHZ         = 8;
static constexpr uint32_t LINK_TIMEOUT_MS      = 10000;  // boot link-up wait
static constexpr uint32_t LINK_POLL_INTERVAL_MS =  2000;  // loop() poll cadence

}  // namespace W5500Config
