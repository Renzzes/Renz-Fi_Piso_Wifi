#include "ProductionNetworkTrace.h"

#include "Config.h"
#include "RenzFiDebug.h"
#include "RouterOsClient.h"

namespace ProductionNetworkTrace {
namespace {

bool g_active = false;

#if RENZFI_DEBUG_ROUTER

const char *readFailCategory(RouterOsClient &client) {
  const String &code = client.lastErrorCode();
  const String &msg  = client.lastError();
  if (code == "ROUTEROS_API_READ_TIMEOUT") return "timeout";
  if (msg.indexOf("connection closed") >= 0) return "connection closed";
  if (msg.indexOf("protocol") >= 0 || code.indexOf("PROTOCOL") >= 0) {
    return "parser error";
  }
  if (msg.indexOf("EOF") >= 0 || msg.indexOf("closed") >= 0) return "EOF";
  if (!code.isEmpty()) return code.c_str();
  if (!msg.isEmpty()) return "read failed";
  return "unknown";
}

void logBoolLine(const char *key, bool value) {
  Serial.printf("[production-network] %s=%s\n", key, value ? "yes" : "no");
}

#endif  // RENZFI_DEBUG_ROUTER

}  // namespace

void enter() { g_active = true; }

void exit() { g_active = false; }

bool active() { return g_active; }

#if RENZFI_DEBUG_ROUTER

void logSessionState(RouterOsClient &client) {
  if (!g_active) return;
  logBoolLine("RouterSession connected", client.isConnected());
  logBoolLine("socket connected", client.socketConnected());
  logBoolLine("loggedIn", client.isLoggedIn());
  Serial.printf("[production-network] connectTimeoutMs=%u\n",
                static_cast<unsigned>(RenzFiConfig::ROUTEROS_CONNECT_TIMEOUT_MS));
  Serial.printf("[production-network] ioTimeoutMs=%u\n",
                static_cast<unsigned>(RenzFiConfig::ROUTEROS_IO_TIMEOUT_MS));
  if (!client.lastError().isEmpty()) {
    Serial.printf("[production-network] clientLastError=%s\n",
                  client.lastError().c_str());
  }
  if (!client.lastErrorCode().isEmpty()) {
    Serial.printf("[production-network] clientLastErrorCode=%s\n",
                  client.lastErrorCode().c_str());
  }
}

void logCmdBegin(const char *command) {
  if (!g_active) return;
  Serial.printf("[production-network] BEGIN %s\n", command ? command : "?");
}

void logCmdEnd(RouterOsClient &client, const char *command, uint32_t elapsedMs,
               const RouterOsClient::CommandResult &result, bool executeOk) {
  if (!g_active) return;
  Serial.printf("[production-network] END %s\n", command ? command : "?");
  logBoolLine("RouterSession connected", client.isConnected());
  logBoolLine("socket connected", client.socketConnected());
  logBoolLine("loggedIn", client.isLoggedIn());
  Serial.printf("[production-network] elapsed=%ums\n",
                static_cast<unsigned>(elapsedMs));
  Serial.printf("[production-network] replyCount=%u\n",
                static_cast<unsigned>(result.replyCount));
  logBoolLine("!trap", result.trapReceived);
  logBoolLine("!fatal", result.fatalReceived);
  logBoolLine("!done", result.doneReceived);
  if (!executeOk) {
    Serial.printf("[production-network] readFailedReason=%s\n",
                  readFailCategory(client));
    if (!client.lastError().isEmpty()) {
      Serial.printf("[production-network] lastError=%s\n", client.lastError().c_str());
    }
    if (!client.lastErrorCode().isEmpty()) {
      Serial.printf("[production-network] lastErrorCode=%s\n",
                    client.lastErrorCode().c_str());
    }
  }
  if (result.trapReceived && !result.trapMessage.isEmpty()) {
    Serial.printf("[production-network] trapMessage=%s\n",
                  result.trapMessage.c_str());
  }
  if (result.fatalReceived && !result.fatalMessage.isEmpty()) {
    Serial.printf("[production-network] fatalMessage=%s\n",
                  result.fatalMessage.c_str());
  }
}

void logReturnFalse(const char *statement, const char *reason,
                    const char *errorCode, const char *error,
                    const char *replySummary) {
  if (!g_active) return;
  Serial.println(F("[production-network] return false"));
  Serial.printf("[production-network] statement=%s\n", statement ? statement : "?");
  if (reason && reason[0]) {
    Serial.printf("[production-network] reason=%s\n", reason);
  }
  if (errorCode && errorCode[0]) {
    Serial.printf("[production-network] errorCode=%s\n", errorCode);
  }
  if (error && error[0]) {
    Serial.printf("[production-network] error=%s\n", error);
  }
  if (replySummary && replySummary[0]) {
    Serial.printf("[production-network] routerOsReplySummary=%s\n", replySummary);
  }
}

void logStageFailureStatement(const char *statement) {
  if (!g_active) return;
  Serial.printf("[production-network] STAGE_FAIL_CAUSE statement=%s\n",
                statement ? statement : "?");
}

void logActivationSummary(bool sessionReused, bool reconnected,
                          bool wirelessQueryRequired, bool success) {
  if (!g_active) return;
  logBoolLine("SESSION REUSED", sessionReused);
  logBoolLine("RECONNECTED", reconnected);
  logBoolLine("WIRELESS QUERY REQUIRED", wirelessQueryRequired);
  Serial.printf("[production-network] PRODUCTION ACTIVATION=%s\n",
                success ? "SUCCESS" : "FAILURE");
  logBoolLine("INSTALLATION READY", success);
  if (success) {
    Serial.println(F("[production-network] Production activation complete"));
  }
}

void logWirelessEnableOutcome(bool disabledAfterConfig, bool enableCommandSent,
                              bool enableVerified, const char *runningLabel) {
  if (!g_active) return;
  Serial.printf("[production-network] WIRELESS DISABLED AFTER CONFIG=%s\n",
                disabledAfterConfig ? "yes" : "no");
  Serial.printf("[production-network] WIRELESS ENABLE COMMAND SENT=%s\n",
                enableCommandSent ? "yes" : "no");
  Serial.printf("[production-network] WIRELESS ENABLE VERIFIED=%s\n",
                enableVerified ? "yes" : "no");
  Serial.printf("[production-network] WIRELESS RUNNING=%s\n",
                runningLabel && runningLabel[0] ? runningLabel : "unknown");
}

#else  // RENZFI_DEBUG_ROUTER

void logSessionState(RouterOsClient &) {}
void logCmdBegin(const char *) {}
void logCmdEnd(RouterOsClient &, const char *, uint32_t,
               const RouterOsClient::CommandResult &, bool) {}
void logReturnFalse(const char *, const char *, const char *, const char *,
                    const char *) {}
void logStageFailureStatement(const char *) {}
void logActivationSummary(bool, bool, bool, bool) {}
void logWirelessEnableOutcome(bool, bool, bool, const char *) {}

#endif  // RENZFI_DEBUG_ROUTER

}  // namespace ProductionNetworkTrace
