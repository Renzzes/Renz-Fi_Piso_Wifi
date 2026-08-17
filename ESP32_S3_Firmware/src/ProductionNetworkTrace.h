#pragma once

#include <Arduino.h>

#include "RouterOsClient.h"

// Scoped tracing for finish-pipeline production-network stage only.
namespace ProductionNetworkTrace {

void enter();
void exit();
bool active();

void logSessionState(RouterOsClient &client);
void logCmdBegin(const char *command);
void logCmdEnd(RouterOsClient &client, const char *command, uint32_t elapsedMs,
               const RouterOsClient::CommandResult &result, bool executeOk);

void logReturnFalse(const char *statement, const char *reason = nullptr,
                    const char *errorCode = nullptr, const char *error = nullptr,
                    const char *replySummary = nullptr);

void logStageFailureStatement(const char *statement);

void logActivationSummary(bool sessionReused, bool reconnected,
                          bool wirelessQueryRequired, bool success);

void logWirelessEnableOutcome(bool disabledAfterConfig, bool enableCommandSent,
                              bool enableVerified, const char *runningLabel);

}  // namespace ProductionNetworkTrace
