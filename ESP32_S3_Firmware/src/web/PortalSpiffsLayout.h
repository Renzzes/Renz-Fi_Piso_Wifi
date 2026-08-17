#pragma once

// Web-layer SPIFFS layout for the captive portal (Phase 4B).
// These are HTTP document roots — not StoragePaths / SD contract paths.

namespace PortalSpiffsLayout {

static constexpr const char *kRoot       = "/portal";
static constexpr const char *kLoginHtml  = "/portal/login.html";
static constexpr const char *kDefaults   = "/defaults";

struct FlatAlias {
  const char *urlPath;
  const char *spiffsPath;
};

// Relative URLs in login.html resolve against /portal/ when served as the
// recovery fallback. Flat aliases remain for legacy absolute paths used by
// SPIFFS-hosted portal assets during development.
static constexpr FlatAlias kFlatAliases[] = {
    {"/renzfi-app.js",    "/portal/renzfi-app.js"},
    {"/renzfi-style.css", "/portal/renzfi-style.css"},
    {"/md5.js",           "/portal/md5.js"},
    {"/Default-Banner.png", "/portal/Default-Banner.png"},
    {"/bg_music.mp3",     "/portal/bg_music.mp3"},
    {"/favicon.ico",      "/portal/favicon.ico"},
    {nullptr,             nullptr},
};

static constexpr size_t kFlatAliasCount =
    sizeof(kFlatAliases) / sizeof(kFlatAliases[0]) - 1;

// Required captive-portal SPIFFS objects (boot validation + PortalServer runtime).
struct RequiredPortalAsset {
  const char *spiffsPath;
  const char *label;
};

static constexpr RequiredPortalAsset kRequiredPortalAssets[] = {
    {kLoginHtml,            "login.html"},
    {"/portal/renzfi-app.js",    "renzfi-app.js"},
    {"/portal/renzfi-style.css", "renzfi-style.css"},
    {"/portal/md5.js",           "md5.js"},
    {nullptr,                 nullptr},
};

static constexpr size_t kRequiredPortalAssetCount =
    sizeof(kRequiredPortalAssets) / sizeof(kRequiredPortalAssets[0]) - 1;

}  // namespace PortalSpiffsLayout
