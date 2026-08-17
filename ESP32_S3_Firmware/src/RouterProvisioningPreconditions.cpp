#include "RouterProvisioningPreconditions.h"

#include "EthernetManager.h"
#include "InstallationState.h"
#include "InstallationStateManager.h"
#include "SetupRouterConnectionManager.h"

namespace RouterProvisioningPreconditions {

Result check(InstallationStateManager *installation,
             SetupRouterConnectionManager *routerConnection,
             EthernetManager *eth) {
  Result result;
  if (!installation ||
      installation->current() != InstallationState::RouterConfigured) {
    result.httpStatus = 403;
    result.errorCode = "ROUTER_CONFIGURE_REQUIRED";
    result.errorMessage =
        "Installation must be router_configured before router provisioning";
    return result;
  }
  if (!routerConnection || !routerConnection->hasVerifiedConnection()) {
    result.httpStatus = 409;
    result.errorCode = "ROUTER_CONNECTION_REQUIRED";
    result.errorMessage = "Saved MikroTik connection is unavailable";
    return result;
  }
  if (!eth || !eth->linkUp() || !eth->hasIp()) {
    result.httpStatus = 400;
    result.errorCode = "ETHERNET_NOT_READY";
    result.errorMessage = "Ethernet link and DHCP IP are required";
    return result;
  }
  result.success = true;
  return result;
}

}  // namespace RouterProvisioningPreconditions
