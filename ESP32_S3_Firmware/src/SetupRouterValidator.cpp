#include "SetupRouterValidator.h"

#include "Config.h"
#include "EthernetManager.h"
#include "RouterOsClient.h"
#include "RouterCommandScratch.h"
#include "RouterWorkerDiagnostics.h"

namespace {

bool isValidHost(const String &host) {
  IPAddress addr;
  if (!addr.fromString(host)) return false;
  return addr != IPAddress(0, 0, 0, 0);
}

SetupRouterValidator::Result fail(const char *code, const char *message,
                                  const char *stage = "") {
  SetupRouterValidator::Result result;
  result.code    = code;
  result.message = message;
  result.stage   = stage;
  return result;
}

const char *stageForConnectCode(const String &code) {
  if (code == "TCP_CONNECT_FAILED") return "connect";
  return "connect";
}

const char *stageForLoginCode(const String &code) {
  if (code == "ROUTEROS_API_READ_TIMEOUT") return "login";
  if (code == "ROUTEROS_API_FATAL") return "login";
  if (code == "ROUTEROS_API_AUTH_TRAP") return "login";
  if (code == "ROUTEROS_API_AUTH_FATAL") return "login";
  if (code == "ROUTEROS_LOGIN_FAILED") return "login";
  if (code == "ROUTEROS_API_PROTOCOL_ERROR") return "login";
  if (code == "API_PROTOCOL_ERROR") return "login";
  return "login";
}

String identityFromResult(const RouterOsClient::CommandResult &result) {
  const String name = RouterOsClient::findReplyAttr(result, "name");
  return name.isEmpty() ? String("RouterOS") : name;
}

void populateRouterMetadata(RouterOsClient *client,
                              SetupRouterValidator::Result &out) {
  if (!client) return;

  // CommandResult is ~6 KB (32 reply records x 12 String attrs). Three
  // stack locals here overflowed router_worker (48 KB) after login succeeded.
  // Reuse one heap scratch buffer for all metadata commands, same pattern as
  // ExistingNetworkScanner::_scratch.
  RouterOsClient::CommandResult &result = RouterCommandScratchContext::acquire();
  if (client->executeCommand("/system/identity/print", result) &&
      !result.trapReceived) {
    out.identity = identityFromResult(result);
  } else {
    out.identity = "RouterOS";
  }

  RouterOsClient::initializeCommandResult(result);
  if (client->executeCommand("/system/routerboard/print", result) &&
      !result.trapReceived) {
    out.board = RouterOsClient::findReplyAttr(result, "board-name");
    if (out.board.isEmpty()) {
      out.board = RouterOsClient::findReplyAttr(result, "model");
    }
  }

  RouterOsClient::initializeCommandResult(result);
  if (client->executeCommand("/system/resource/print", result) &&
      !result.trapReceived) {
    out.routerOsVersion = RouterOsClient::findReplyAttr(result, "version");
  }
}

}  // namespace

SetupRouterValidator::Result SetupRouterValidator::validate(const Input &input,
                                                            EthernetManager *eth) {
  if (input.host.isEmpty() || !isValidHost(input.host)) {
    return fail("INVALID_HOST", "Router IP address is invalid");
  }

  if (input.username.isEmpty()) {
    return fail("INVALID_USERNAME", "Router username is required");
  }

  if (input.password.isEmpty()) {
    return fail("INVALID_PASSWORD", "Router password is required");
  }

  if (input.apiPort == 0) {
    return fail("INVALID_HOST", "API port is invalid");
  }

  const bool linkUp = eth && eth->linkUp();
  const bool hasIp  = eth && eth->hasIp();
  if (!linkUp || !hasIp) {
    return fail("ETHERNET_NOT_READY",
                "Ethernet link or DHCP is not ready on Renz-Fi");
  }

  // Stack-allocated: lifetime is this validate() call only. Avoids
  // `new (std::nothrow)` which host IntelliSense cannot resolve against the
  // Arduino/xtensa <new> overloads (real GCC build is fine either way).
  // CommandResult scratch stays on the heap via RouterCommandScratchContext.
  RouterOsClient client;
  RouterOsClient *const clientPtr = &client;

  client.setTimeouts(RenzFiConfig::SETUP_ROUTER_CONNECT_TIMEOUT_MS,
                     RenzFiConfig::SETUP_ROUTER_IO_TIMEOUT_MS);
  client.setCredentials(input.host, input.username, input.password, input.apiPort);
  client.setCredentialSource("setup-test-save");

  RouterWorkerDiagnostics::logStackHighWaterMark("validate-before-connect");
  Serial.printf("[router-api] sizeof(CommandResult)=%u bytes (replyRecords=%u)\n",
                static_cast<unsigned>(sizeof(RouterOsClient::CommandResult)),
                static_cast<unsigned>(RouterOsClient::MAX_REPLY_RECORDS));

  if (!client.connect()) {
    const String code = client.lastErrorCode().isEmpty() ? "TCP_CONNECT_FAILED"
                                                         : client.lastErrorCode();
    const String msg  = client.lastError();
    return fail(code.c_str(), msg.c_str(), stageForConnectCode(code));
  }

  if (!client.login()) {
    const String code = client.lastErrorCode().isEmpty() ? "API_LOGIN_FAILED"
                                                         : client.lastErrorCode();
    const String msg  = client.lastError();
    return fail(code.c_str(), msg.c_str(), stageForLoginCode(code));
  }

  RouterWorkerDiagnostics::logStackHighWaterMark("validate-after-login");

  Result ok;
  ok.success = true;
  ok.code    = "ROUTER_VALIDATED";
  ok.message = "RouterOS API login validated";
  populateRouterMetadata(clientPtr, ok);

  RouterWorkerDiagnostics::logStackHighWaterMark("validate-after-metadata");

  client.disconnect("success");
  return ok;
}
