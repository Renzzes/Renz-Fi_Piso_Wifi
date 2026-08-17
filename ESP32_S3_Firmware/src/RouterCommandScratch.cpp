#include "RouterCommandScratch.h"

namespace {

RouterOsClient::CommandResult *g_scratch = nullptr;

RouterOsClient::CommandResult &fallbackScratch() {
  static RouterOsClient::CommandResult scratch;
  return scratch;
}

}  // namespace

namespace RouterCommandScratchContext {

void bind(RouterOsClient::CommandResult *scratch) { g_scratch = scratch; }

void unbind() { g_scratch = nullptr; }

bool isBound() { return g_scratch != nullptr; }

RouterOsClient::CommandResult &acquire() {
  RouterOsClient::CommandResult &target = g_scratch ? *g_scratch : fallbackScratch();
  RouterOsClient::initializeCommandResult(target);
  return target;
}

RouterOsClient::CommandResult &get() { return g_scratch ? *g_scratch : fallbackScratch(); }

}  // namespace RouterCommandScratchContext
