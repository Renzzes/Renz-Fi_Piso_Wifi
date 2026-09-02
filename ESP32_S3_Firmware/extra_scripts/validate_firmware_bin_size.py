Import("env")

import os
import sys


def _parse_size(value):
    if isinstance(value, int):
        return value
    value = str(value).strip()
    if value.isdigit():
        return int(value)
    if value.startswith("0x") or value.startswith("0X"):
        return int(value, 16)
    if value and value[-1].upper() in ("K", "M"):
        base = 1024 if value[-1].upper() == "K" else 1024 * 1024
        return int(value[:-1]) * base
    raise ValueError("invalid partition size: %r" % value)


def _fatal(env, message):
    print("")
    print("*** %s" % message)
    print("")
    if hasattr(env, "Exit"):
        env.Exit(1)
    sys.exit(1)


def _app_partition_size(project_dir, partition_csv, env):
    path = os.path.join(project_dir, partition_csv)
    if not os.path.isfile(path):
        _fatal(env, "Partition table not found: %s" % path)

    next_offset = 0
    app_size = 0
    with open(path, encoding="utf-8") as fp:
        for raw in fp:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            tokens = [t.strip() for t in line.split(",")]
            if len(tokens) < 5:
                continue
            name, ptype, subtype, offset, size = tokens[:5]
            bound = 0x10000 if ptype in ("0", "app") else 4
            parsed_offset = _parse_size(offset) if offset else (
                (next_offset + bound - 1) & ~(bound - 1)
            )
            parsed_size = _parse_size(size)
            if subtype == "ota_0" or name == "app0":
                app_size = parsed_size
            next_offset = parsed_offset + parsed_size
    if app_size <= 0:
        _fatal(env, "No OTA app partition (ota_0/app0) in %s" % path)
    return app_size


def validate_firmware_bin_size(source, target, env):
    bin_path = str(target[0])
    if not os.path.isfile(bin_path):
        _fatal(env, "firmware.bin missing: %s" % bin_path)

    project_dir = env.subst("$PROJECT_DIR")
    partition_csv = env.subst("$PARTITIONS_TABLE_CSV")
    app_size = _app_partition_size(project_dir, partition_csv, env)
    bin_size = os.path.getsize(bin_path)
    headroom = app_size - bin_size

    print("")
    print("[firmware-bin] Actual firmware.bin: %d bytes" % bin_size)
    print("[firmware-bin] App partition:       %d bytes" % app_size)
    print("[firmware-bin] Headroom:            %d bytes" % headroom)

    if bin_size > app_size:
        _fatal(
            env,
            "firmware.bin exceeds OTA application partition by %d bytes"
            % (bin_size - app_size),
        )

    print("[firmware-bin] PASS: firmware.bin fits in OTA application partition")
    print("")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", validate_firmware_bin_size)
