#!/usr/bin/env python3
"""Regression guards for W5500/SD SPI isolation and MAC-address integrity.

Background: SD card init (FSPI/SPI2_HOST) and the W5500 (SPI3_HOST) are
independent SPI peripherals with separate SCK/MISO/MOSI/CS pins. A prior
revision of SdSpi.cpp called pinMode()/digitalWrite() on the W5500's CS pin
(GPIO10) from inside the SD init path "to avoid bus contention". On
arduino-esp32 3.x, pinMode() resets a pin's GPIO-matrix/IOMUX function,
which silently detached GPIO10 from the SPI3_HOST peripheral that
ETH.begin() had already wired up as the W5500's hardware CS line. Every
subsequent SPI transaction to the W5500 PHY then failed, esp_eth's internal
watchdog gave up, and the driver posted DISCONNECTED/STOP — which is also
why ETH.macAddress() started returning all zeros afterward (the device
identity "MAC becomes 00:00:00:00:00:00" bug).

These checks make sure that regression can never silently come back.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SRC = ROOT.parent / "src"
SDSPI = SRC / "SdSpi.cpp"
STORAGE = SRC / "StorageManager.cpp"
FIRMWARE = SRC / "FirmwareApp.cpp"
ETH_CPP = SRC / "EthernetManager.cpp"
ETH_H = SRC / "EthernetManager.h"
ASSET_MANAGER = SRC / "AssetManager.cpp"
DEVICE_IDENTITY = SRC / "DeviceIdentity.cpp"
CONFIG = SRC / "Config.h"

# Files that participate in SD/storage/asset boot-phase logic and must never
# disturb the W5500's dedicated pins or the esp_eth driver lifecycle.
BOOT_PHASE_FILES = [SDSPI, STORAGE, ASSET_MANAGER]

FORBIDDEN_ETH_CALLS = [
    r"ETH\.end\s*\(",
    r"ETH\.stop\s*\(",
    r"esp_eth_stop\s*\(",
    r"esp_eth_driver_uninstall\s*\(",
]

# W5500 pins: MOSI=11, MISO=13, SCK=12, CS=10, RST=14. Any pinMode/
# digitalWrite/gpio_reset_pin/gpio_set_direction touching these numeric
# literals or the named W5500Config constants outside of EthernetManager.cpp
# itself is a regression.
FORBIDDEN_PIN_TOUCHES = [
    r"pinMode\(\s*(W5500Config::PIN_(CS|RST|SCK|MISO|MOSI)|1[0-4])\b",
    r"digitalWrite\(\s*(W5500Config::PIN_(CS|RST|SCK|MISO|MOSI)|1[0-4])\b",
    r"gpio_reset_pin\(\s*(?:\(gpio_num_t\)\s*)?(W5500Config::PIN_(CS|RST|SCK|MISO|MOSI)|1[0-4])\b",
    r"gpio_set_direction\(\s*(?:\(gpio_num_t\)\s*)?(W5500Config::PIN_(CS|RST|SCK|MISO|MOSI)|1[0-4])\b",
]

FORBIDDEN_GLOBAL_SPI = [
    r"(?<!g_sd)SPI\.end\s*\(",
    r"(?<!g_sd)SPI\.begin\s*\(",
    r"\bSPI3_HOST\b",
]


_LINE_COMMENT_RE = re.compile(r"//.*")
_BLOCK_COMMENT_RE = re.compile(r"/\*.*?\*/", re.DOTALL)


def read(path: Path) -> str:
    """Reads a source file with comments stripped.

    Comments are stripped so this checker only matches *executable* code —
    postmortem/root-cause comments (like the one in SdSpi.cpp documenting
    the exact regression this script guards against) are expected to
    mention the forbidden calls in prose and must not trip these checks.
    """
    text = path.read_text(encoding="utf-8")
    text = _BLOCK_COMMENT_RE.sub(" ", text)
    text = _LINE_COMMENT_RE.sub("", text)
    return text


def main() -> int:
    errors: list[str] = []

    sdspi = read(SDSPI)
    storage = read(STORAGE)
    firmware = read(FIRMWARE)
    eth_cpp = read(ETH_CPP)
    eth_h = read(ETH_H)
    config = read(CONFIG)

    # ── 1. W5500 pin/host contract (SPI3_HOST, 11/13/12, CS10, RST14) ──────
    if "SPI3_HOST" not in eth_cpp:
        errors.append("EthernetManager must drive the W5500 via SPI3_HOST")
    if not re.search(r"PIN_MOSI\s*=\s*11", read(SRC / "W5500Config.h")):
        errors.append("W5500Config must keep MOSI=11")
    if not re.search(r"PIN_MISO\s*=\s*13", read(SRC / "W5500Config.h")):
        errors.append("W5500Config must keep MISO=13")
    if not re.search(r"PIN_SCK\s*=\s*12", read(SRC / "W5500Config.h")):
        errors.append("W5500Config must keep SCK=12")
    if not re.search(r"PIN_CS\s*=\s*10", read(SRC / "W5500Config.h")):
        errors.append("W5500Config must keep CS=10")
    if not re.search(r"PIN_RST\s*=\s*14", read(SRC / "W5500Config.h")):
        errors.append("W5500Config must keep RST=14")

    # ── 2. SD dedicated FSPI contract (6/5/7, CS18) ─────────────────────────
    if "static SPIClass g_sdSpi(FSPI)" not in sdspi:
        errors.append("SD must use a dedicated FSPI SPIClass instance, not global SPI")
    if not re.search(r"PIN_SD_MOSI\s*=\s*6", config):
        errors.append("Config.h must keep SD MOSI=6")
    if not re.search(r"PIN_SD_MISO\s*=\s*5", config):
        errors.append("Config.h must keep SD MISO=5")
    if not re.search(r"PIN_SD_SCK\s*=\s*7", config):
        errors.append("Config.h must keep SD SCK=7")
    if not re.search(r"PIN_SD_CS\s*=\s*18", config):
        errors.append("Config.h must keep SD CS=18")

    # ── 3. SD/storage/asset boot-phase code must never call ETH.end()/stop ─
    for f in BOOT_PHASE_FILES:
        if not f.exists():
            continue
        text = read(f)
        for pattern in FORBIDDEN_ETH_CALLS:
            if re.search(pattern, text):
                errors.append(
                    f"{f.name} must never call an Ethernet stop primitive "
                    f"({pattern}) — SD/asset init must not disrupt ETH")

    # ── 4. SD/storage/asset boot-phase code must never touch W5500 pins ────
    for f in BOOT_PHASE_FILES:
        if not f.exists():
            continue
        text = read(f)
        for pattern in FORBIDDEN_PIN_TOUCHES:
            match = re.search(pattern, text)
            if match:
                errors.append(
                    f"{f.name} must never call pinMode/digitalWrite/"
                    f"gpio_reset_pin/gpio_set_direction on a W5500 pin "
                    f"(GPIO10/11/12/13/14) — matched: {match.group(0)!r}. "
                    f"This detaches the pin from SPI3_HOST and stops ETH.")

    # ── 5. SD/storage/asset boot-phase code must never use global SPI or
    #      SPI3_HOST (the W5500's dedicated host) ───────────────────────────
    for f in [STORAGE, ASSET_MANAGER]:
        if not f.exists():
            continue
        text = read(f)
        if re.search(r"\bSPI3_HOST\b", text):
            errors.append(f"{f.name} must never reference SPI3_HOST (W5500-only)")
        if re.search(r"(?<!renzFiSdSpi\(\)\.)(?<!g_sd)\bSPI\.begin\s*\(", text):
            errors.append(f"{f.name} must never call the global SPI.begin()")
        if re.search(r"(?<!g_sd)\bSPI\.end\s*\(", text):
            errors.append(f"{f.name} must never call the global SPI.end()")

    # ── 6. Explicit guard against the exact prior regression ───────────────
    if re.search(r"pinMode\(\s*W5500Config::PIN_CS", sdspi):
        errors.append(
            "SdSpi.cpp must never call pinMode(W5500Config::PIN_CS, ...) — "
            "this is the exact regression that detached GPIO10 from "
            "SPI3_HOST and stopped the Ethernet driver after SD init")
    if re.search(r"digitalWrite\(\s*W5500Config::PIN_CS", sdspi):
        errors.append(
            "SdSpi.cpp must never call digitalWrite(W5500Config::PIN_CS, ...)")

    # ── 7. Staged ETH diagnostics must exist at every required boot phase ──
    if "static void logDiagnosticStage" not in eth_h:
        errors.append("EthernetManager must expose logDiagnosticStage() for boot diagnostics")
    if "logDiagnosticStage(\"before_sd_init\")" not in firmware:
        errors.append("FirmwareApp must log ETH diagnostics before SD init")
    if "logDiagnosticStage(\"after_sd_spi_begin\")" not in sdspi:
        errors.append("SdSpi.cpp must log ETH diagnostics after SD SPI begin")
    if "logDiagnosticStage(\"after_sd_begin\")" not in storage:
        errors.append("StorageManager must log ETH diagnostics after SD.begin")
    if "logDiagnosticStage(\"after_fallback_sync\")" not in firmware:
        errors.append("FirmwareApp must log ETH diagnostics after fallback sync")
    if "logDiagnosticStage(\"before_appliance_summary\")" not in firmware:
        errors.append("FirmwareApp must log ETH diagnostics before the appliance summary")

    # ── 8. MAC-address integrity: cache + never trust a live all-zero read ─
    if "g_lastKnownGoodMac" not in eth_cpp:
        errors.append("EthernetManager must cache the last known-good MAC address")
    if "isMacValid" not in eth_cpp:
        errors.append("EthernetManager must validate MAC reads against the all-zero sentinel")
    if "String macAddress() const;" not in eth_h:
        errors.append("EthernetManager::macAddress() must be implemented (not a raw ETH.macAddress() passthrough)")

    # ── 9. Driver-active tracking must be wired to START/STOP events ───────
    if "g_driverStopped = true;" not in eth_cpp or "g_driverStopped = false;" not in eth_cpp:
        errors.append("EthernetManager must track driver start/stop transitions for isDriverActive()")
    if "static bool isDriverActive();" not in eth_h:
        errors.append("EthernetManager must expose isDriverActive() for regression diagnostics")

    if errors:
        for err in errors:
            print(f"eth-sd-isolation-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("eth-sd-isolation-check: OK (W5500/SD SPI isolation + MAC integrity guards passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
