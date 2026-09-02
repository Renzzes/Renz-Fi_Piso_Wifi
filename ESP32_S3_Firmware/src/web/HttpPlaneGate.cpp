#include "HttpPlaneGate.h"

#include "AuthManager.h"
#include "DmaMemoryMonitor.h"
#include "EthernetManager.h"
#include "FactoryResetWorker.h"
#include "ManagementApConfig.h"
#include "WebRequestDiagnostics.h"
#include "WebResponse.h"

namespace HttpPlaneGate {

namespace {

EthernetManager *g_eth = nullptr;
AuthManager *g_auth = nullptr;
FactoryResetWorker *g_factoryReset = nullptr;

Plane classifyLocalIp(const IPAddress &local) {
  if (local == ManagementApConfig::IP) return Plane::Setup;
  if (local[0] == 0) return Plane::Unknown;
  if (g_eth && g_eth->hasIp()) {
    IPAddress ethIp;
    if (ethIp.fromString(g_eth->ip()) && local == ethIp) {
      return Plane::Production;
    }
  }
  return Plane::Unknown;
}

bool sameSubnet(const IPAddress &a, const IPAddress &b, const IPAddress &mask) {
  const uint32_t am = static_cast<uint32_t>(a) & static_cast<uint32_t>(mask);
  const uint32_t bm = static_cast<uint32_t>(b) & static_cast<uint32_t>(mask);
  return am == bm;
}

bool remoteOnApplianceLan(const IPAddress &remote) {
  if (!g_eth || !g_eth->hasIp()) return false;
  if (remote[0] == 0) return false;
  IPAddress ethIp;
  IPAddress ethMask;
  if (!ethIp.fromString(g_eth->ip())) return false;
  if (!ethMask.fromString(g_eth->subnet())) {
    // Safe default when mask string is unavailable: /24.
    ethMask = IPAddress(255, 255, 255, 0);
  }
  return sameSubnet(remote, ethIp, ethMask);
}

void rejectPlane(AsyncWebServerRequest *req, Plane required) {
  if (!req) return;
  if (required == Plane::Setup) {
    WebResponse::serveErrorJson(
        req, 403, "Available only on Renz-Fi Setup (192.168.4.1)",
        "SETUP_PLANE_REQUIRED");
    return;
  }
  if (WebRequestDiagnostics::isManagementApRequest(req)) {
    WebResponse::serveErrorJson(
        req, 403,
        "This route is available only through the Ethernet dashboard",
        "SETUP_PLANE_RESTRICTED");
    return;
  }
  WebResponse::serveErrorJson(
      req, 403,
      "Production service requires Ethernet — use the appliance LAN address",
      "PRODUCTION_PLANE_REQUIRED");
}

void rejectCustomerPortal(AsyncWebServerRequest *req) {
  if (!req) return;
  Serial.printf(
      "[http] deny customer-portal remote=%s path=%s access=management-required\n",
      WebRequestDiagnostics::requestRemoteIp(req).toString().c_str(),
      req->url().c_str());
  WebResponse::serveErrorJson(
      req, 403,
      "This service is not available on the customer Hotspot network",
      "CUSTOMER_PORTAL_RESTRICTED");
}

void rejectFactoryResetBusy(AsyncWebServerRequest *req) {
  if (!req) return;
  const DmaMemoryMonitor::Snapshot snap = DmaMemoryMonitor::readSnapshot();
  Serial.printf(
      "[http-quiesce] rejected method=%s path=%s dma_free=%u dma_largest=%u\n",
      req->methodToString(), req->url().c_str(),
      static_cast<unsigned>(snap.freeDma),
      static_cast<unsigned>(snap.largestDma));
  // Match existing FACTORY_RESET_IN_PROGRESS contract (409) used by enqueue.
  WebResponse::serveErrorJson(req, 409, "Factory reset is in progress",
                              "FACTORY_RESET_IN_PROGRESS");
}

}  // namespace

void bindEthernet(EthernetManager *eth) {
  g_eth = eth;
}

void bindAuth(AuthManager *auth) {
  g_auth = auth;
}

void bindFactoryReset(FactoryResetWorker *factoryReset) {
  g_factoryReset = factoryReset;
}

bool isFactoryResetBusy() {
  return g_factoryReset && g_factoryReset->busy();
}

bool isFactoryResetAllowListed(AsyncWebServerRequest *req) {
  if (!req) return false;
  const String &url = req->url();
  if (url == "/api/system/factory-reset") return true;
  if (url.startsWith("/api/system/factory-reset?")) return true;
  if (url == "/api/system/factory-reset/status") return true;
  if (url.startsWith("/api/system/factory-reset/status?")) return true;
  return false;
}

bool ensureNotFactoryResetting(AsyncWebServerRequest *req) {
  if (!isFactoryResetBusy()) return true;
  if (isFactoryResetAllowListed(req)) return true;
  rejectFactoryResetBusy(req);
  return false;
}

Plane classify(AsyncWebServerRequest *req) {
  if (!req) return Plane::Unknown;
  if (WebRequestDiagnostics::isManagementApRequest(req)) return Plane::Setup;
  return classifyLocalIp(WebRequestDiagnostics::requestLocalIp(req));
}

AccessClass classifyAccess(AsyncWebServerRequest *req) {
  if (!req) return AccessClass::Unknown;
  const Plane plane = classify(req);
  if (plane == Plane::Setup) return AccessClass::Setup;
  if (plane != Plane::Production) return AccessClass::Unknown;

  const IPAddress remote = WebRequestDiagnostics::requestRemoteIp(req);
  if (remoteOnApplianceLan(remote)) return AccessClass::Management;

  // Reached ESP32 Ethernet from outside the appliance LAN (typical: MikroTik
  // Hotspot guest via walled-garden). Treat as customer portal client.
  if (remote[0] != 0) return AccessClass::CustomerPortal;
  return AccessClass::Unknown;
}

bool isSetupPlane(AsyncWebServerRequest *req) {
  return classify(req) == Plane::Setup;
}

bool isProductionPlane(AsyncWebServerRequest *req) {
  return classify(req) == Plane::Production;
}

bool isCustomerPortalClient(AsyncWebServerRequest *req) {
  return classifyAccess(req) == AccessClass::CustomerPortal;
}

bool isManagementClient(AsyncWebServerRequest *req) {
  return classifyAccess(req) == AccessClass::Management;
}

bool ensureSetupPlane(AsyncWebServerRequest *req) {
  if (!ensureNotFactoryResetting(req)) return false;
  if (isSetupPlane(req)) return true;
  rejectPlane(req, Plane::Setup);
  return false;
}

bool ensureProductionPlane(AsyncWebServerRequest *req) {
  if (!ensureNotFactoryResetting(req)) return false;
  if (isProductionPlane(req)) return true;
  rejectPlane(req, Plane::Production);
  return false;
}

bool hasAdminSession(AsyncWebServerRequest *req) {
  if (!g_auth || !req) return false;
  const String cookie = req->hasHeader("Cookie")
                            ? req->getHeader("Cookie")->value()
                            : String("");
  return g_auth->isAuthenticated(cookie);
}

bool ensureManagementClient(AsyncWebServerRequest *req) {
  if (!ensureProductionPlane(req)) return false;
  if (isManagementClient(req)) return true;
  if (isCustomerPortalClient(req)) {
    rejectCustomerPortal(req);
    return false;
  }
  // Unknown remote on production plane — fail closed for privileged routes.
  rejectCustomerPortal(req);
  return false;
}

bool ensureAdminSpaClient(AsyncWebServerRequest *req) {
  if (!ensureProductionPlane(req)) {
    if (req) {
      Serial.printf(
          "[admin-spa] path=%s access=%s decision=deny reason=production-plane\n",
          req->url().c_str(), accessClassLabel(req));
    }
    return false;
  }
  if (isManagementClient(req) || isCustomerPortalClient(req)) {
    if (isCustomerPortalClient(req)) {
      Serial.printf("[admin-spa] path=%s access=%s decision=allow\n",
                    req->url().c_str(), accessClassLabel(req));
    }
    return true;
  }
  Serial.printf(
      "[admin-spa] path=%s access=%s decision=deny reason=not-spa-client\n",
      req->url().c_str(), accessClassLabel(req));
  rejectCustomerPortal(req);
  return false;
}

bool ensureAdminAccess(AsyncWebServerRequest *req) {
  if (!ensureProductionPlane(req)) return false;
  if (isManagementClient(req)) return true;
  if (isCustomerPortalClient(req)) {
    if (hasAdminSession(req)) return true;
    WebResponse::serveErrorJson(req, 401, "Authentication required",
                                "UNAUTHENTICATED");
    return false;
  }
  rejectCustomerPortal(req);
  return false;
}

bool ensureAppliancePlane(AsyncWebServerRequest *req) {
  if (!ensureNotFactoryResetting(req)) return false;
  const Plane plane = classify(req);
  if (plane == Plane::Setup || plane == Plane::Production) return true;
  WebResponse::serveErrorJson(req, 403, "Unknown network interface",
                              "PLANE_UNKNOWN");
  return false;
}

const char *planeLabel(Plane plane) {
  switch (plane) {
    case Plane::Setup:
      return "setup";
    case Plane::Production:
      return "production";
    default:
      return "unknown";
  }
}

const char *planeLabel(AsyncWebServerRequest *req) {
  return planeLabel(classify(req));
}

const char *accessClassLabel(AccessClass access) {
  switch (access) {
    case AccessClass::Setup:
      return "setup";
    case AccessClass::Management:
      return "management";
    case AccessClass::CustomerPortal:
      return "customer-portal";
    default:
      return "unknown";
  }
}

const char *accessClassLabel(AsyncWebServerRequest *req) {
  return accessClassLabel(classifyAccess(req));
}

}  // namespace HttpPlaneGate
