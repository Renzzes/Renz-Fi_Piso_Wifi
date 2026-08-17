#include "ProductionHandoff.h"

#include <SPIFFS.h>

#include "AuthManager.h"
#include "Config.h"
#include "EthernetManager.h"
#include "InstallationState.h"
#include "InstallationStateManager.h"
#include "RouterProvisioningManager.h"
#include "SetupProvisioningManager.h"
#include "StorageManager.h"
#include "router/RouterPlatform.h"
#include "web/WebServerManager.h"

namespace ProductionHandoff {

namespace {

bool routerCredentialsSaved(RouterPlatform *router,
                            InstallationStateManager *installation) {
  if (installation &&
      installationStateAtLeast(installation->current(),
                               InstallationState::RouterConfigured)) {
    return true;
  }
  if (!router) return false;
  DynamicJsonDocument doc(RenzFiConfig::JSON_DOC_SMALL);
  if (!router->load(doc)) return false;
  return String(doc["host"] | "").length() > 0 &&
         String(doc["username"] | "").length() > 0;
}

bool dashboardAssetsReady(StorageManager *storage) {
  return storage && storage->isSpiffsMounted() && SPIFFS.exists("/index.html");
}

}  // namespace

String buildAdminUrl(EthernetManager *eth) {
  if (!eth || !eth->hasIp()) return "";
  const String ip = eth->ip();
  if (ip.isEmpty()) return "";
  return "http://" + ip + "/login";
}

Status evaluate(const Context &ctx) {
  Status out;
  out.adminUrl = buildAdminUrl(ctx.eth);

  out.checks.owner =
      (ctx.setupProvisioning && ctx.setupProvisioning->ownerCreated()) ||
      (ctx.auth && ctx.auth->firstBootCompleted());
  out.checks.router =
      routerCredentialsSaved(ctx.router, ctx.installation);
  out.checks.adoption =
      ctx.routerProvisioning &&
      ctx.routerProvisioning->isExistingNetworkAdopted();
  out.checks.verification =
      ctx.installation &&
      installationStateAtLeast(ctx.installation->current(),
                               InstallationState::Provisioned);
  out.checks.production =
      ctx.installation && ctx.installation->isReady();
  out.checks.adminApi =
      ctx.web && ctx.web->isProductionRegistered();
  out.dashboardAssetsOk = dashboardAssetsReady(ctx.storage);
  out.ethernetIpOk      = ctx.eth && ctx.eth->hasIp();
  out.checks.dashboard  = out.dashboardAssetsOk && out.ethernetIpOk;

  out.dashboardReady =
      out.checks.adminApi && out.checks.dashboard;

  if (!ctx.installation ||
      ctx.installation->current() == InstallationState::Factory) {
    out.phase = out.checks.owner ? "setup" : "factory";
    return out;
  }
  if (!out.checks.owner || !out.checks.router || !out.checks.adoption ||
      !out.checks.verification || !out.checks.production) {
    out.phase = "setup";
    return out;
  }

  out.phase = "completing";
  if (!out.checks.adminApi || !out.checks.dashboard) {
    return out;
  }

  out.phase = "production_ready";
  out.ready = true;
  return out;
}

void fillHealthFields(JsonObject data, const Status &status) {
  data["ready"] = status.ready;
  if (status.adminUrl.isEmpty()) {
    data["adminUrl"] = nullptr;
  } else {
    data["adminUrl"] = status.adminUrl;
  }
  data["dashboardReady"] = status.dashboardReady;
  data["handoffPhase"]   = status.phase;

  JsonObject checks = data["checks"].to<JsonObject>();
  checks["owner"]        = status.checks.owner;
  checks["router"]       = status.checks.router;
  checks["adoption"]     = status.checks.adoption;
  checks["verification"] = status.checks.verification;
  checks["production"]   = status.checks.production;
  checks["adminApi"]     = status.checks.adminApi;
  checks["dashboard"]    = status.checks.dashboard;

  if (!status.ready) {
    JsonArray pending = data["pending"].to<JsonArray>();
    if (!status.checks.owner) pending.add("owner");
    if (!status.checks.router) pending.add("router");
    if (!status.checks.adoption) pending.add("adoption");
    if (!status.checks.verification) pending.add("verification");
    if (!status.checks.production) pending.add("production");
    if (!status.checks.adminApi) pending.add("admin_api");
    if (!status.dashboardAssetsOk) pending.add("dashboard_assets");
    if (!status.ethernetIpOk) pending.add("ethernet_ip");
  }
}

}  // namespace ProductionHandoff
