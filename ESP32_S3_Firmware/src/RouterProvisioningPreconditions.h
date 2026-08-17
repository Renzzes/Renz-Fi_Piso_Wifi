#pragma once

#include <Arduino.h>

class InstallationStateManager;
class SetupRouterConnectionManager;
class EthernetManager;

// The single shared precondition gate for every operation that touches
// RouterOS: existing-network scan, apply configuration, and adopt existing
// network. There must be exactly ONE implementation of these checks — add a
// caller here instead of re-deriving the same three conditions elsewhere.
namespace RouterProvisioningPreconditions {

struct Result {
  bool   success = false;
  int    httpStatus = 400;
  String errorCode;
  String errorMessage;
};

Result check(InstallationStateManager *installation,
             SetupRouterConnectionManager *routerConnection,
             EthernetManager *eth);

}  // namespace RouterProvisioningPreconditions
