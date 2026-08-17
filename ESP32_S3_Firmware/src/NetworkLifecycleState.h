#pragma once

#include <Arduino.h>

// Explicit appliance networking lifecycle — see docs/NETWORK_PLANE_ARCHITECTURE.md
enum class NetworkLifecycleState : uint8_t {
  Booting = 0,
  SetupApReady,
  EthernetWaiting,
  EthernetReady,
  FactoryProvisioning,
  ProductionReady,
  DegradedEthernetUnavailable,
};

const char *networkLifecycleStateLabel(NetworkLifecycleState state);
