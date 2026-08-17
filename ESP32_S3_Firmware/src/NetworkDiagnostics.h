#pragma once

#include <Arduino.h>

#include <ETH.h>

class AsyncWebServerRequest;
class EthernetManager;
class ManagementApManager;

// Exhaustive network-interface diagnostics for ESP32-S3 + W5500 field debugging.
// Controlled by RENZFI_NETWORK_DIAG (see RenzFiDebug.h). Additive logging only —
// does not alter DHCP/static selection or HTTP routing behavior.
namespace NetworkDiagnostics {

void install();
void noteEthBeginFinished(bool success, uint32_t finishedAtMs);
void printStartupReport(EthernetManager *eth, ManagementApManager *mgmtAp,
                        bool ethBeginOk);
void printRegisteredInterfaces(EthernetManager *eth,
                               ManagementApManager *mgmtAp);
void loop();

// Called from EthernetManager event handler (instrumentation hook).
void onEthEvent(arduino_event_id_t event);

// HTTP / portal API instrumentation (WebRequestDiagnostics delegates here).
const char *interfaceLabel(AsyncWebServerRequest *req);
void logHttpIncoming(AsyncWebServerRequest *req);
void logPortalApiDebug(AsyncWebServerRequest *req, const char *apiPath);

}  // namespace NetworkDiagnostics
