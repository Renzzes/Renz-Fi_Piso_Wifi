#pragma once

// SSE event names emitted by RouterPlatform via EventBus.
namespace RouterEvents {
constexpr const char *Connected              = "router.connected";
constexpr const char *Disconnected           = "router.disconnected";
constexpr const char *AuthenticationFailed   = "router.auth_failed";
constexpr const char *Unavailable            = "router.unavailable";
constexpr const char *CapabilitiesChanged    = "router.capabilities_changed";
constexpr const char *ProfileUpdated         = "router.profile_updated";
}  // namespace RouterEvents
