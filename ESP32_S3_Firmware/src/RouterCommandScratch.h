#pragma once

#include "RouterOsClient.h"

// RouterProvisioningWorker binds one reusable CommandResult for the duration
// of each job. RouterOS helper code borrows it instead of heap-allocating
// per command.
namespace RouterCommandScratchContext {

void bind(RouterOsClient::CommandResult *scratch);
void unbind();
bool isBound();

// Returns the bound scratch after initializeCommandResult(), or a process-
// local fallback when called outside the worker (e.g. unit-style paths).
RouterOsClient::CommandResult &acquire();
RouterOsClient::CommandResult &get();

}  // namespace RouterCommandScratchContext
