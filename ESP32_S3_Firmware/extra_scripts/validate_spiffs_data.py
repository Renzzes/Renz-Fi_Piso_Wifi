Import("env")

import json
import os

# Fail uploadfs/buildfs if npm run build:esp32 was not run (data/ incomplete).
# Matches scripts/esp32-staging-manifest.mjs PORTAL_REQUIRED + admin index.

REQUIRED_REL_PATHS = [
    "index.html",
    "build-info.json",
    "portal/login.html",
    "portal/renzfi-app.js",
    "portal/renzfi-style.css",
    "portal/md5.js",
    "portal/favicon.ico",
    "assets",
    "DO_NOT_EDIT.txt",
]

SPIFFS_MAX_OBJECT_NAME = 32


def _validate_spiffs_data(source, target, env):
    project_dir = env.subst("$PROJECT_DIR")
    data_dir = os.path.join(project_dir, "data")

    if not os.path.isdir(data_dir):
        env.FatalError(
            "SPIFFS data/ missing. Run from repo root: npm run build:esp32"
        )

    missing = []
    for rel in REQUIRED_REL_PATHS:
        path = os.path.join(data_dir, rel)
        if rel == "assets":
            if not os.path.isdir(path):
                missing.append("assets/")
            else:
                entries = [
                    f for f in os.listdir(path)
                    if os.path.isfile(os.path.join(path, f))
                ]
                has_js = any(f.endswith(".js") for f in entries)
                has_css = any(f.endswith(".css") for f in entries)
                if not has_js or not has_css:
                    missing.append("assets/ (.js + .css bundles)")
        elif not os.path.exists(path):
            missing.append(rel)

    if missing:
        env.FatalError(
            "SPIFFS data/ incomplete — run: npm run build:esp32\n"
            + "\n".join(f"  missing: data/{m}" for m in missing)
        )

    build_info_path = os.path.join(data_dir, "build-info.json")
    try:
        with open(build_info_path, encoding="utf-8") as fh:
            build_info = json.load(fh)
    except (OSError, json.JSONDecodeError) as exc:
        env.FatalError(f"Invalid data/build-info.json: {exc}")

    required_build_keys = (
        "firmwareVersion",
        "adminBuild",
        "portalRevision",
        "gitCommit",
        "buildNumber",
        "deviceProfileVersion",
        "storageContractVersion",
        "httpContractVersion",
    )
    missing_keys = [k for k in required_build_keys if k not in build_info]
    if missing_keys:
        env.FatalError(
            "build-info.json missing keys — re-run: npm run build:esp32\n"
            + "\n".join(f"  missing: {k}" for k in missing_keys)
        )

    violations = []
    for dirpath, _, filenames in os.walk(data_dir):
        for name in filenames:
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, data_dir).replace("\\", "/")
            object_path = "/" + rel
            if len(object_path) > SPIFFS_MAX_OBJECT_NAME:
                violations.append(f"{len(object_path)} chars: {object_path}")

    if violations:
        env.FatalError(
            "SPIFFS path length violations (max %d bytes):\n"
            % SPIFFS_MAX_OBJECT_NAME
            + "\n".join(f"  {v}" for v in violations)
        )

    print("[validate] SPIFFS data/ OK — portal + admin assets present")


env.AddPreAction("buildfs", _validate_spiffs_data)
env.AddPreAction("uploadfs", _validate_spiffs_data)
