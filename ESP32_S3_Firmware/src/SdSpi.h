#pragma once

#include <SPI.h>

// Dedicated FSPI bus for the SD card (isolated from W5500 on SPI3_HOST/HSPI).
SPIClass &renzFiSdSpi();
void renzFiSdSpiBegin(bool reinitBus = false);
