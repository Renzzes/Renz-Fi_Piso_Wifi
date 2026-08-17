#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "AuthManager.h"
#include "AssetManager.h"
#include "BackupManager.h"
#include "BuildMetadata.h"
#include "CoinManager.h"
#include "EthernetManager.h"
#include "ExternalAccessPointManager.h"
#include "EventBus.h"
#include "FactoryResetWorker.h"
#include "InstallationStateManager.h"
#include "Logger.h"
#include "ManagementApManager.h"
#include "ManagementApLifecycle.h"
#include "NetworkSettingsManager.h"
#include "router/RouterPlatform.h"
#include "PortalConfigManager.h"
#include "PortalSessionManager.h"
#include "PromoManager.h"
#include "RgbController.h"
#include "RouterProvisioningWorker.h"
#include "SessionManager.h"
#include "StorageManager.h"
#include "SystemHealthService.h"
#include "VoucherManager.h"
#include "web/IWebRouteProvider.h"

class WebServerManager;
class SetupProvisioningManager;
class RouterProvisioningManager;

class ApiServer : public IWebRouteProvider {
 public:
  void begin(StorageManager       *storage,
             AuthManager          *auth,
             SessionManager       *sessions,
             PromoManager         *promos,
             VoucherManager       *vouchers,
             CoinManager          *coin,
             RouterPlatform       *router,
             Logger               *logger,
             EventBus             *events,
             EthernetManager      *eth,
             PortalSessionManager *portalSessions,
             PortalConfigManager  *portalConfig,
             AssetManager         *assets,
             RgbController        *rgb,
             SystemHealthService  *health,
             BuildMetadata        *build,
             InstallationStateManager *installation,
             ManagementApManager  *mgmtAp,
             ManagementApLifecycle *mgmtApLifecycle,
             NetworkSettingsManager *networkSettings = nullptr,
             RouterProvisioningWorker *routerWorker = nullptr,
             FactoryResetWorker *factoryReset = nullptr,
             ExternalAccessPointManager *accessPoints = nullptr);

  void registerSetupRoutes(WebServerManager &web,
                           SetupProvisioningManager *setupProvisioning = nullptr,
                           RouterProvisioningManager *routerProvisioning = nullptr);
  void registerProductionRoutes(WebServerManager &web);
  void registerRoutes(WebServerManager &web) override;
  const char *providerName() const override;
  int notFoundPriority() const override;
  bool handleNotFound(AsyncWebServerRequest *req) override;

 private:
  AsyncWebServer       *_server         = nullptr;
  StorageManager       *_storage        = nullptr;
  AuthManager          *_auth           = nullptr;
  SessionManager       *_sessions       = nullptr;
  PromoManager         *_promos         = nullptr;
  VoucherManager       *_vouchers       = nullptr;
  CoinManager          *_coin           = nullptr;
  RouterPlatform       *_router         = nullptr;
  Logger               *_logger         = nullptr;
  EventBus             *_events         = nullptr;
  EthernetManager      *_eth            = nullptr;
  PortalSessionManager *_portalSessions = nullptr;
  PortalConfigManager  *_portalConfig   = nullptr;
  AssetManager         *_assets         = nullptr;
  RgbController        *_rgb            = nullptr;
  SystemHealthService  *_health         = nullptr;
  BuildMetadata        *_build          = nullptr;
  InstallationStateManager *_installation = nullptr;
  ManagementApManager  *_mgmtAp         = nullptr;
  ManagementApLifecycle *_mgmtApLifecycle = nullptr;
  NetworkSettingsManager *_networkSettings = nullptr;
  RouterProvisioningWorker *_routerWorker  = nullptr;
  FactoryResetWorker       *_factoryReset  = nullptr;
  ExternalAccessPointManager *_accessPoints = nullptr;
  WebServerManager         *_web                = nullptr;
  SetupProvisioningManager *_setupProvisioning  = nullptr;
  RouterProvisioningManager *_routerProvisioning = nullptr;
  BackupManager        _backup;

  // Returns false and sends 401/403 when auth requirements are not met.
  enum class AuthRequirement { Session, FullAccess, OwnerOnly };
  bool requireAuth(AsyncWebServerRequest *req,
                   AuthRequirement requirement = AuthRequirement::FullAccess);
  bool requireOwnerAuth(AsyncWebServerRequest *req);

  // Adds CORS + security headers to a response object.
  static void addCorsHeaders(AsyncWebServerResponse *res);

  // Send JSON success envelope.
  void sendOk(AsyncWebServerRequest *req, JsonDocument &data,
               const String &message = "OK");
  void sendOk(AsyncWebServerRequest *req, JsonDocument &data, int httpStatus,
               const String &message);
  void sendOk(AsyncWebServerRequest *req, const String &message = "OK");

  // Send JSON error envelope.
  void sendError(AsyncWebServerRequest *req, int status,
                 const String &error, const String &code);

  // Forwards a RouterProvisioningWorker::Result (already a fully-formed
  // JSON envelope) as the raw HTTP response — used by legacy/debug sync
  // worker paths only. Admin RouterOS mutations use enqueue + 202.
  void sendWorkerResult(AsyncWebServerRequest *req,
                       const RouterProvisioningWorker::Result &result);

  // Accept a non-blocking Admin router job (HTTP 202 + jobId).
  void sendAdminJobAccepted(AsyncWebServerRequest *req, uint32_t jobId,
                            const char *typeLabel);

  // Stream a file from SD card as an attachment download.
  void sendSdFile(AsyncWebServerRequest *req, const char *sdPath,
                  const char *filename);

  // Returns the raw POST/PUT body (empty string if none).
  static String getBody(AsyncWebServerRequest *req);

  // Log the current request to Serial with handler tag.
  static void logRequest(AsyncWebServerRequest *req, const char *handler);

  void appendAssetInfoJson(JsonObject obj, const AssetInfo &info) const;
  void appendUploadResultJson(JsonObject root,
                              const AssetOperationResult &result) const;
};
