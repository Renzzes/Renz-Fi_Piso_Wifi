#pragma once

// SSE event names for installation lifecycle (setup wizard / recovery).
namespace InstallationEvents {
constexpr const char *StateChanged = "installation.state_changed";
constexpr const char *Progress     = "installation.progress";
constexpr const char *Completed    = "installation.completed";
constexpr const char *Aborted      = "installation.aborted";
}  // namespace InstallationEvents
