#pragma once

#include <Arduino.h>

enum class LogLevel {
  Info,
  Warn,
  Error
};

enum class LedMode {
  Off,
  Waiting,
  Active,
  Error
};

enum class CoinState {
  Disabled,
  WaitingForActivity,
  Responding,
  NoRecentActivity,
  Fault
};

enum class CoinFaultReason {
  None,
  IsrAttachFailed,
  IsrServiceMissing,
  InvalidGpio
};

enum class RgbSignal {
  Off,
  Booting,
  Recovery,
  OtaUpdate,
  Error,
  Warning,
  CoinAccepted,
  Idle
};

enum class RgbMode {
  Off,
  Solid,
  Breathing,
  Rainbow,
  SystemStatus
};

enum class SystemHealthLevel {
  Healthy,
  ActiveSession,
  Warning,
  Error
};

struct SaleRecord {
  uint8_t schemaVersion = 2;
  String id;
  String timestamp;
  String recordedAt;
  int amount = 0;
  String sessionId;
  String paymentType;
  int durationMinutes = 0;
  String macAddress;
  String ipAddress;
  String voucherCode;
  int promoId = 0;
  String promoName;
  int credits = 0;
  String profile;
  String speed;
  String expiresAt;
  String operatorName;
  String connectedAt;
  String endedAt;
  uint32_t actualConnectedSeconds = 0;
  String status;
  String terminationReason;
};

struct HotspotUser {
  String mac;
  String ip;
  String username;
  String profile;
  uint32_t timeoutSeconds = 0;
  // Portal session generation — stale Activate/Deauth outcomes must not
  // mutate a newer purchase for the same MAC.
  uint32_t sessionGeneration = 0;
};

// Filled by MikroTikDriver on the last Activate job. Copied into the
// HotspotOutcome so PortalSessionManager can stamp one expiry timeline.
struct ActivateAuthTrace {
  uint32_t authorizedAtMs = 0;
  uint32_t grantedSeconds = 0;
  uint32_t existingUserUptime = 0;
  uint32_t existingUserLimit = 0;
  uint32_t newUserLimit = 0;
  uint32_t activeUptime = 0;
  uint32_t activeSessionTimeLeft = 0;
  bool activeLoginSuccess = false;
  bool activeVerifySuccess = false;
  bool usedActiveSet = false;
};

// One entry in the pulse-count → PHP denomination table.
struct CoinDenomination {
  uint8_t pulses = 0;
  uint8_t pesos = 0;
};

struct CoinSettings {
  static constexpr size_t kMaxDenominations = 8;
  static constexpr uint8_t kPulseGroupHardMax = 20;

  // Explicit pulse-count → PHP mapping (authoritative for coin credit).
  CoinDenomination denominations[kMaxDenominations]{};
  uint8_t denominationCount = 0;

  // pulsesPerPeso: legacy/API field kept for backward-compat with existing
  // settings and admin contracts. Not used for credit when denominationMap is active.
  int pulsesPerPeso = 1;
  int pesoPerPulse  = 1;          // legacy alias; only used when loading old settings files
  int defaultMinutesPerPeso = 5;
  // TEMP calibration defaults (100ms / 200ms) for hardware testing without the
  // production external 10k pull-up on the coin GPIO — see Config.h.
  uint32_t debounceMs = 100;
  uint32_t settleMs = 200;
  uint32_t timeoutSeconds = 60;
  uint32_t noActivityTimeoutSec = 5UL * 60UL;
  // Legacy ceiling field kept in settings JSON; physical safety uses kPulseGroupHardMax.
  uint32_t maxPulsesPerGroup = 20;
  bool enabled = true;

  // Diagnostics — populated by CoinManager::processCoin
  uint32_t lastPulseCount      = 0;
  int      lastCoinValue       = 0;
  int      lastEffectivePesos  = 0;
};

struct CoinStats {
  uint32_t totalPulseCount = 0;
  uint32_t totalCoinCount = 0;
  uint32_t lastPulseMs = 0;
  uint32_t lastCoinMs = 0;
  uint32_t uptimePulseCount = 0;
  uint32_t uptimeCoinCount = 0;
};

// Phase 5 reusable coin status payload (GET /api/system/coin).
struct CoinStatus {
  bool enabled = false;
  CoinState state = CoinState::Disabled;
  CoinFaultReason faultReason = CoinFaultReason::None;
  uint32_t totalPulseCount = 0;
  uint32_t totalCoinCount = 0;
  uint32_t uptimePulseCount = 0;
  uint32_t uptimeCoinCount = 0;
  String lastPulseTimestamp;
  String lastCoinTimestamp;
};

struct RgbSettings {
  bool enabled = true;
  RgbMode mode = RgbMode::SystemStatus;
  uint8_t red = 0;
  uint8_t green = 0;
  uint8_t blue = 255;
  uint8_t brightness = 80;
};

// Phase 5 reusable RGB status payload (GET /api/system/rgb).
struct RgbStatus {
  bool enabled = true;
  uint8_t brightness = 80;
  RgbSignal signal = RgbSignal::Idle;
  const char *colorName = "BLUE";
  uint8_t red = 0;
  uint8_t green = 0;
  uint8_t blue = 255;
};

struct RouterSettings {
  String host;
  String username;
  String password;
  String profile;
  String ssid;
};

// Ethernet IP assignment mode. DHCP is the recommended/default mode — the
// MikroTik hAP lite is expected to hand out a DHCP reservation for the
// ESP32's W5500 MAC address. Static is an advanced/optional fallback for
// installations that require a fixed address without router-side reservation.
enum class EthernetAddressMode : uint8_t {
  Dhcp = 0,
  Static = 1,
};

const char *ethernetAddressModeLabel(EthernetAddressMode mode);
EthernetAddressMode parseEthernetAddressMode(const char *label);

struct NetworkSettings {
  EthernetAddressMode addressMode = EthernetAddressMode::Dhcp;
  // Static-mode fields only — ignored/unused while addressMode == Dhcp.
  // Defaults mirror the historical W5500Config compile-time values so a
  // unit that opts into static mode without filling every field still
  // gets a sane, previously-working configuration.
  String staticIp             = "10.40.0.2";
  String staticGateway        = "10.40.0.1";
  String staticSubnetMask     = "255.255.255.0";
  String staticDnsPrimary     = "10.40.0.1";
  String staticDnsSecondary   = "";
  // True only once an installer has explicitly saved a network configuration
  // (via the setup wizard / admin network settings). Until then the
  // appliance always behaves as DHCP + Management AP fallback, regardless of
  // whatever addressMode value happens to be stored.
  bool provisioned = false;
  // Phase 7C.2 — post-setup Management AP preference (default: disable).
  bool managementApKeepEnabledAfterSetup = false;
};

enum class RecoveryLevel {
  None,
  Level1,
  Level2
};
