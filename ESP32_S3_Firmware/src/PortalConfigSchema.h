#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  PortalConfigSchema — frozen portal.json layout (Phase 3B prep)
//
//  portal.json is shared by two managers:
//    PortalConfigManager — portal behaviour / copy / theme (configuration)
//    AssetManager        — media metadata (branding, audio, ads, videos)
//
//  See docs/PORTAL_CONFIG_ARCHITECTURE.md
// ─────────────────────────────────────────────────────────────────────────────

namespace PortalConfigSchema {

static constexpr const char *Branding = "branding";
static constexpr const char *Audio    = "audio";
static constexpr const char *Ads      = "ads";
static constexpr const char *Videos = "videos";
static constexpr const char *LegacyVideosSection = "media";  // read-only migration

static constexpr const char *KeyBanner     = "banner";
static constexpr const char *KeyLogo       = "logo";
static constexpr const char *KeyBackground = "background";

static constexpr const char *KeyMusic = "music";
static constexpr const char *KeyCoin  = "coin";

static constexpr const char *LegacyHasBanner = "hasBanner";
static constexpr const char *LegacyHasMusic  = "hasMusic";
static constexpr const char *LegacyBannerPath = "bannerPath";
static constexpr const char *LegacyMusicPath  = "musicPath";
static constexpr const char *LegacyAssetsMap  = "assets";

}  // namespace PortalConfigSchema
