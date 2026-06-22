#include "SdSpi.h"

#include "Config.h"

static SPIClass g_sdSpi(FSPI);

SPIClass &renzFiSdSpi() { return g_sdSpi; }

void renzFiSdSpiBegin() {
  pinMode(RenzFiConfig::PIN_SD_CS, OUTPUT);
  digitalWrite(RenzFiConfig::PIN_SD_CS, HIGH);

  g_sdSpi.begin(RenzFiConfig::PIN_SD_SCK, RenzFiConfig::PIN_SD_MISO,
                RenzFiConfig::PIN_SD_MOSI, -1);
}
