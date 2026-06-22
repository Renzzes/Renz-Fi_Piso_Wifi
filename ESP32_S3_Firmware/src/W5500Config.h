#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  W5500Config.h  —  Centralised W5500 SPI pin map + VLAN40 static network
//
//  Hardware target : ESP32-S3 N16R8  +  WIZnet W5500 breakout
//  Network role    : VLAN40 wired backend (10.40.0.0/24)
//
//  W5500 uses a dedicated HSPI bus (see Config.h for the separate SD SPI bus).
// ─────────────────────────────────────────────────────────────────────────────

#include <IPAddress.h>

namespace W5500Config {

// ── SPI Pin Mapping ───────────────────────────────────────────────────────────
//  Dedicated W5500 SPI pin mapping.
static constexpr int PIN_MOSI = 11;
static constexpr int PIN_MISO = 13;
static constexpr int PIN_SCK  = 12;
static constexpr int PIN_CS   = 10;  // W5500 chip-select
static constexpr int PIN_RST  = 14;  // W5500 hardware reset
static constexpr int PIN_INT  = -1;  // Interrupt — unused; set to -1 to skip

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
static constexpr uint8_t  SPI_FREQ_MHZ         = 8;      // ETH.begin SPI clock (8 MHz robust for prototype wiring)
static constexpr uint32_t LINK_TIMEOUT_MS      = 10000;  // boot link-up wait
static constexpr uint32_t LINK_POLL_INTERVAL_MS =  2000;  // loop() poll cadence

}  // namespace W5500Config
