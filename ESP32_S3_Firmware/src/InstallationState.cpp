#include "InstallationState.h"

const char *installationStateLabel(InstallationState state) {
  switch (state) {
    case InstallationState::Factory:
      return "factory";
    case InstallationState::OwnerCreated:
      return "owner_created";
    case InstallationState::RouterConfigured:
      return "router_configured";
    case InstallationState::Provisioned:
      return "provisioned";
    case InstallationState::RouterSelected:
      return "router_selected";
    case InstallationState::RouterConnected:
      return "router_connected";
    case InstallationState::PortalConfigured:
      return "portal_configured";
    case InstallationState::CoinConfigured:
      return "coin_configured";
    case InstallationState::ValidationPassed:
      return "validation_passed";
    case InstallationState::Ready:
      return "ready";
    default:
      return "factory";
  }
}

InstallationState parseInstallationState(const char *label) {
  if (!label) return InstallationState::Factory;
  if (strcmp(label, "owner_created") == 0) return InstallationState::OwnerCreated;
  if (strcmp(label, "router_configured") == 0) return InstallationState::RouterConfigured;
  if (strcmp(label, "provisioned") == 0) return InstallationState::Provisioned;
  if (strcmp(label, "router_selected") == 0) return InstallationState::RouterSelected;
  if (strcmp(label, "router_connected") == 0) return InstallationState::RouterConnected;
  if (strcmp(label, "portal_configured") == 0) return InstallationState::PortalConfigured;
  if (strcmp(label, "coin_configured") == 0) return InstallationState::CoinConfigured;
  if (strcmp(label, "validation_passed") == 0) return InstallationState::ValidationPassed;
  if (strcmp(label, "ready") == 0) return InstallationState::Ready;
  return InstallationState::Factory;
}

uint8_t installationStateIndex(InstallationState state) {
  return static_cast<uint8_t>(state);
}

uint8_t installationStateCount() {
  return static_cast<uint8_t>(InstallationState::Ready) + 1;
}

uint8_t installationStateProgress(InstallationState state) {
  if (installationStateCount() <= 1) return 0;
  const uint8_t index = installationStateIndex(state);
  return static_cast<uint8_t>((index * 100U) / (installationStateCount() - 1U));
}

bool installationStateAtLeast(InstallationState current, InstallationState required) {
  return installationStateIndex(current) >= installationStateIndex(required);
}

InstallationState installationNextState(InstallationState state) {
  if (state >= InstallationState::Ready) return InstallationState::Ready;
  return static_cast<InstallationState>(installationStateIndex(state) + 1U);
}

InstallationState installationPreviousState(InstallationState state) {
  if (state <= InstallationState::Factory) return InstallationState::Factory;
  return static_cast<InstallationState>(installationStateIndex(state) - 1U);
}

const char *installationStepKeyForState(InstallationState state) {
  switch (state) {
    case InstallationState::OwnerCreated:
    case InstallationState::RouterConfigured:
    case InstallationState::Provisioned:
    case InstallationState::RouterSelected:
    case InstallationState::RouterConnected:
      return InstallationSteps::Router;
    case InstallationState::PortalConfigured:
      return InstallationSteps::Portal;
    case InstallationState::CoinConfigured:
      return InstallationSteps::Coin;
    case InstallationState::ValidationPassed:
      return InstallationSteps::Validation;
    default:
      return nullptr;
  }
}
