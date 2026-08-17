#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

class AuthManager;
class EthernetManager;
class InstallationStateManager;
class RouterPlatform;
class RouterProvisioningManager;
class SetupProvisioningManager;
class StorageManager;
class WebServerManager;

namespace ProductionHandoff {

struct Context {
  AuthManager              *auth               = nullptr;
  SetupProvisioningManager *setupProvisioning  = nullptr;
  InstallationStateManager *installation       = nullptr;
  RouterPlatform           *router             = nullptr;
  RouterProvisioningManager *routerProvisioning = nullptr;
  EthernetManager          *eth                = nullptr;
  StorageManager           *storage              = nullptr;
  WebServerManager         *web                  = nullptr;
};

struct Checks {
  bool owner         = false;
  bool router        = false;
  bool adoption      = false;
  bool verification  = false;
  bool production    = false;
  bool adminApi      = false;
  bool dashboard     = false;
};

struct Status {
  bool   ready              = false;
  bool   dashboardReady     = false;
  bool   dashboardAssetsOk  = false;
  bool   ethernetIpOk       = false;
  String adminUrl;
  const char *phase         = "setup";
  Checks checks;
};

String buildAdminUrl(EthernetManager *eth);

Status evaluate(const Context &ctx);

void fillHealthFields(JsonObject data, const Status &status);

}  // namespace ProductionHandoff
