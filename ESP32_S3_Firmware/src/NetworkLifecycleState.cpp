#include "NetworkLifecycleState.h"

const char *networkLifecycleStateLabel(NetworkLifecycleState state) {
  switch (state) {
    case NetworkLifecycleState::Booting:
      return "Booting";
    case NetworkLifecycleState::SetupApReady:
      return "SetupApReady";
    case NetworkLifecycleState::EthernetWaiting:
      return "EthernetWaiting";
    case NetworkLifecycleState::EthernetReady:
      return "EthernetReady";
    case NetworkLifecycleState::FactoryProvisioning:
      return "FactoryProvisioning";
    case NetworkLifecycleState::ProductionReady:
      return "ProductionReady";
    case NetworkLifecycleState::DegradedEthernetUnavailable:
      return "DegradedEthernetUnavailable";
  }
  return "Unknown";
}
