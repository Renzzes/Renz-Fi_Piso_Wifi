#!/usr/bin/env python3
"""Regression guards for SD FSPI boot order and SPI bus isolation."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SDSPI = ROOT.parent / "src" / "SdSpi.cpp"
SDSPI_H = ROOT.parent / "src" / "SdSpi.h"
STORAGE = ROOT.parent / "src" / "StorageManager.cpp"
FIRMWARE = ROOT.parent / "src" / "FirmwareApp.cpp"
ETH = ROOT.parent / "src" / "EthernetManager.cpp"
CONFIG = ROOT.parent / "src" / "Config.h"

_LINE_COMMENT_RE = re.compile(r"//.*")
_BLOCK_COMMENT_RE = re.compile(r"/\*.*?\*/", re.DOTALL)


def strip_comments(text: str) -> str:
    """Strips comments so checks only match executable code, not the prose
    root-cause explanations that intentionally reference forbidden calls."""
    text = _BLOCK_COMMENT_RE.sub(" ", text)
    return _LINE_COMMENT_RE.sub("", text)


def begin_body(text: str) -> str:
    start = text.find("bool StorageManager::begin()")
    end = text.find("void StorageManager::handleSdRemoved", start)
    return text[start:end if end > start else start + 1200]


def main() -> int:
    errors: list[str] = []

    sdspi = strip_comments(SDSPI.read_text(encoding="utf-8"))
    storage = strip_comments(STORAGE.read_text(encoding="utf-8"))
    firmware = strip_comments(FIRMWARE.read_text(encoding="utf-8"))
    eth = strip_comments(ETH.read_text(encoding="utf-8"))
    config = CONFIG.read_text(encoding="utf-8")
    begin = begin_body(storage)

    if "static SPIClass g_sdSpi(FSPI)" not in sdspi:
        errors.append("SD must use dedicated FSPI SPIClass instance")

    if "SPI3_HOST" not in eth:
        errors.append("W5500 must use SPI3_HOST via ETH.begin")

    if "g_sdSpi.end()" in sdspi:
        if "if (reinitBus)" not in sdspi:
            errors.append("g_sdSpi.end() must only run on explicit SD bus reinit")

    if "SD.end();" in storage:
        mount_body = storage[storage.find("bool StorageManager::mountSdCard") :
                             storage.find("bool StorageManager::mountSpiffs")]
        if "if (reinitBus)" not in mount_body:
            errors.append("SD.end() must only run on explicit SD remount")
        if "mountSdCard(\"SD mount\", false)" not in storage and \
           "mountSdCard(\"SD mount\")" in storage and "reinitBus" in storage:
            pass
        if "mountSdCard(\"SD mount\"" in storage and "false)" not in storage.split(
                "mountSdCard(\"SD mount\"")[1][:40]:
            if "mountSdCard(\"SD mount\", false)" not in storage:
                errors.append("Cold boot must call mountSdCard without SD.end reinit")

    if "mountSpiffs()" not in begin or "mountSdCard(" not in begin:
        errors.append("StorageManager::begin must mount SPIFFS fallback before SD")
    spiffs_pos = begin.find("mountSpiffs()")
    sd_pos = begin.find("mountSdCard(")
    if spiffs_pos >= 0 and sd_pos >= 0 and spiffs_pos > sd_pos:
        errors.append("StorageManager::begin must call mountSpiffs before mountSdCard")

    # Regression guard: SD init must NEVER touch the W5500's CS pin. SD
    # (FSPI/SPI2_HOST) and the W5500 (SPI3_HOST) are independent SPI
    # peripherals with separate CS lines, so there is no bus contention to
    # arbitrate. A prior revision called pinMode()/digitalWrite() on
    # W5500Config::PIN_CS here "to deassert it" — on arduino-esp32 3.x,
    # pinMode() resets the pin's GPIO-matrix function and silently detached
    # GPIO10 from the SPI3_HOST driver that ETH.begin() had already
    # configured as the W5500's hardware CS, breaking all further SPI
    # transactions to the PHY and causing ETH DISCONNECTED/STOP shortly
    # after SD init. See docs/ROUTEROS_STACK_OVERFLOW_INVESTIGATION.md-style
    # postmortems — do not reintroduce this.
    if re.search(r"pinMode\(\s*W5500Config::PIN_CS", sdspi):
        errors.append(
            "SD init (SdSpi.cpp) must never call pinMode() on "
            "W5500Config::PIN_CS — this detaches GPIO10 from SPI3_HOST and "
            "stops the Ethernet driver")
    if re.search(r"digitalWrite\(\s*W5500Config::PIN_CS", sdspi):
        errors.append(
            "SD init (SdSpi.cpp) must never call digitalWrite() on "
            "W5500Config::PIN_CS")
    if "PIN_SD_CS" not in sdspi or "digitalWrite(RenzFiConfig::PIN_SD_CS, HIGH)" not in sdspi:
        errors.append("SD CS must be driven HIGH before SPI.begin")

    if "g_sdSpi.begin(RenzFiConfig::PIN_SD_SCK" not in sdspi:
        errors.append("SD SPI must call explicit SPI.begin(sck,miso,mosi,-1)")

    if "Phase 2: SPIFFS" not in firmware or "Phase 3: SD card" not in firmware:
        errors.append("FirmwareApp must initialize SPIFFS before SD card phase")

    if "cardType=" not in storage or "cardSize=" not in storage:
        errors.append("StorageManager must log cardType/cardSize after successful mount")

    if "host=FSPI" not in sdspi:
        errors.append("SD diagnostics must log FSPI host identity")

    if not re.search(r"PIN_SD_MOSI\s*=\s*6", config):
        errors.append("Config.h must keep SD MOSI=6")
    if not re.search(r"PIN_SD_MISO\s*=\s*5", config):
        errors.append("Config.h must keep SD MISO=5")
    if not re.search(r"PIN_SD_SCK\s*=\s*7", config):
        errors.append("Config.h must keep SD SCK=7")
    if not re.search(r"PIN_SD_CS\s*=\s*18", config):
        errors.append("Config.h must keep SD CS=18")

    if errors:
        for err in errors:
            print(f"sd-spi-boot-order-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("sd-spi-boot-order-check: OK (SD FSPI boot order guards passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
