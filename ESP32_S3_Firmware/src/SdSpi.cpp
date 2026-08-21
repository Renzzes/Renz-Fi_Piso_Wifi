#include "SdSpi.h"

#include <Arduino.h>

#include "Config.h"
#include "EthernetManager.h"
#include "W5500Config.h"

static SPIClass g_sdSpi(FSPI);

SPIClass &renzFiSdSpi() { return g_sdSpi; }

// IMPORTANT: this function must ONLY touch the SD card's own FSPI/SPI2_HOST
// pins (from RenzFiConfig::PIN_SD_*). It must NEVER call pinMode()/
// digitalWrite() on the W5500's pins (from W5500Config::PIN_*) or on the
// global/default SPI object.
//
// Root-cause history: an earlier revision called
// `pinMode(W5500Config::PIN_CS, OUTPUT); digitalWrite(...)` here "to keep
// the other SPI device's CS deasserted". That was based on a false premise
// — SD (FSPI/SPI2_HOST) and the W5500 (SPI3_HOST) are fully independent SPI
// peripherals with separate, non-shared SCK/MISO/MOSI/CS lines, so there is
// no bus contention to arbitrate. Worse, on arduino-esp32 3.x, `pinMode()`
// calls `gpio_config()`, which force-resets the pin's IOMUX/GPIO-matrix
// function back to plain GPIO (PIN_FUNC_GPIO). The W5500 CS GPIO was already
// owned by the SPI3_HOST driver as the W5500's hardware CS line (assigned
// inside `ETH.begin()`), so re-running pinMode() on it silently detached CS
// from the SPI peripheral. All subsequent W5500 SPI transactions then failed,
// esp_eth's internal watchdog gave up, and the driver posted
// DISCONNECTED/STOP events (see EthernetManager.cpp onEthArduinoEvent) —
// which is also why ETH.macAddress() started returning all zeros afterward.
void renzFiSdSpiBegin(bool reinitBus) {
  pinMode(RenzFiConfig::PIN_SD_CS, OUTPUT);
  digitalWrite(RenzFiConfig::PIN_SD_CS, HIGH);

  Serial.printf(
      "[SD] renzFiSdSpiBegin host=FSPI reinit=%d sdCs=%d=%d "
      "pins sck=%d miso=%d mosi=%d (w5500Cs=%d untouched)\n",
      reinitBus ? 1 : 0, RenzFiConfig::PIN_SD_CS,
      digitalRead(RenzFiConfig::PIN_SD_CS), RenzFiConfig::PIN_SD_SCK,
      RenzFiConfig::PIN_SD_MISO, RenzFiConfig::PIN_SD_MOSI,
      W5500Config::PIN_CS);

  if (reinitBus) {
    g_sdSpi.end();
  }
  g_sdSpi.begin(RenzFiConfig::PIN_SD_SCK, RenzFiConfig::PIN_SD_MISO,
                RenzFiConfig::PIN_SD_MOSI, -1);
  Serial.println("[SD] renzFiSdSpiBegin: SPI.begin complete (FSPI)");
  EthernetManager::logDiagnosticStage("after_sd_spi_begin");
}
