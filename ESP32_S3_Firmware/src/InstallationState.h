#pragma once

#include <Arduino.h>

// Tracks first-time installer progress across reboots.
enum class InstallationState : uint8_t {
  Factory = 0,
  OwnerCreated,
  RouterConfigured,
  Provisioned,
  RouterSelected,
  RouterConnected,
  PortalConfigured,
  CoinConfigured,
  ValidationPassed,
  Ready,
};

// Persisted completed-step keys (wizard sections).
namespace InstallationSteps {
constexpr const char *Router     = "router";
constexpr const char *Portal     = "portal";
constexpr const char *Coin       = "coin";
constexpr const char *Validation = "validation";
}  // namespace InstallationSteps

static constexpr uint16_t INSTALLATION_SCHEMA_VERSION = 2;

const char *installationStateLabel(InstallationState state);
InstallationState parseInstallationState(const char *label);
uint8_t installationStateIndex(InstallationState state);
uint8_t installationStateCount();
uint8_t installationStateProgress(InstallationState state);
bool installationStateAtLeast(InstallationState current, InstallationState required);

InstallationState installationNextState(InstallationState state);
InstallationState installationPreviousState(InstallationState state);
const char *installationStepKeyForState(InstallationState state);
