#!/usr/bin/env python3
"""Builds a dash firmware release into releases/<version>/: the OTA file
for each dash hardware variant, everything needed to install it over USB
instead, and a manifest saying what it is and whether it can go over the
air at all.

    git tag v1.2.0 && python3 tools/release.py
    python3 tools/release.py --dev      # untagged or uncommitted: a trial run

A real release has to be built from a clean tree at a tag, so the version
the dash shows (tools/fw_version.py stamps `git describe`) is that tag.
--dev drops the requirement and files the build under releases/dev-<describe>/.

What comes out, per variant (pcb, devkit):

    <variant>/mqb-dash-s3-<variant>-<version>.bin   send this from the phone
    <variant>/bootloader.bin, partitions.bin, boot_app0.bin
    <variant>/flash-usb.sh                          USB install, same files

plus manifest.json (sizes, SHA-256s, partition layout, config version) and
RELEASE.md, a summary to paste into the GitHub release.

The USB install writes each file at its own offset, as `pio run -t upload`
does, rather than one merged image: esptool's merge_bin pads the gaps with
0xFF, and one of those gaps is the NVS partition holding the dash's
settings.

Compared with the previous tag, a release is marked as needing USB when its
partition table differs (OTA only ever writes an app slot -- the dash
refuses such an image, see dash_s3_firmware.hpp), and flagged when the
stored settings layout (core::ConfigStore::CURRENT_VERSION) or the
bootloader changed.

releases/ is git-ignored: upload a release's folder to the GitHub release
for its tag.
"""

import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from partition_table import layout_crc, parse_rows, partition_rows  # noqa: E402

PARTITIONS = "platform/dash_s3/partitions_dash.csv"
CONFIG_STORE = "core/config/inc/config_store.hpp"

# Release builds only: the stress builds fake their CAN data and are never
# distributed (the dash refuses them anyway -- DASH_S3_BUILD_VARIANT).
VARIANTS = {"pcb": "dash_s3", "devkit": "dash_s3_devkit"}

# What `pio run -e dash_s3 -t upload` passes esptool (Arduino turns the
# board's qio into dio for the bootloader header).
ESPTOOL_FLASH_ARGS = "--flash_mode dio --flash_freq 80m --flash_size 16MB"
BOOTLOADER_OFFSET = 0x0
PARTITION_TABLE_OFFSET = 0x8000
BOOT_APP0 = os.path.expanduser(
    "~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
)

# dash_s3_firmware.hpp's DashS3FirmwareId, at FIRMWARE_ID_OFFSET; the
# version field holds 31 characters (tools/fw_version.py truncates).
FW_ID_OFFSET = 288
FW_MAGIC = b"MQB-DASH-S3"
FW_VERSION_MAX = 31


def git(*args):
    return subprocess.run(
        ["git", *args], cwd=ROOT, capture_output=True, text=True, check=True
    ).stdout.strip()


def git_ok(*args):
    try:
        return git(*args)
    except subprocess.CalledProcessError:
        return None


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def config_version(text):
    m = re.search(r"CURRENT_VERSION\s*=\s*(\d+)", text)
    return int(m.group(1)) if m else None


def layout_at(rev):
    """layout_crc() and rows of the partition table as of `rev`, or of the
    working tree when rev is None."""
    if rev is None:
        rows = partition_rows(os.path.join(ROOT, PARTITIONS))
    else:
        text = git_ok("show", "%s:%s" % (rev, PARTITIONS))
        if text is None:
            return None, None
        rows = parse_rows(text)
    return layout_crc(rows), rows


def firmware_id(path):
    """(variant, version) from an image's update descriptor."""
    with open(path, "rb") as f:
        f.seek(FW_ID_OFFSET)
        raw = f.read(16 + 16 + 32)
    if not raw.startswith(FW_MAGIC):
        return None, None
    text = lambda b: b.split(b"\0", 1)[0].decode()  # noqa: E731
    return text(raw[16:32]), text(raw[32:64])


def offsets(rows):
    """(otadata offset, first app slot offset) from the partition rows."""
    ota = next(o for _, t, st, o, _, _ in rows if t == "data" and st == "ota")
    app = min(o for _, t, _, o, _, _ in rows if t == "app")
    return ota, app


def flash_script(app_name, ota_offset, app_offset):
    return """#!/bin/sh
# USB install of this dash firmware. Keeps the dash's settings (NVS) and,
# unless the partition table changed, its logs (LittleFS): each file goes
# to its own offset and nothing else is touched.
#
#   ./flash-usb.sh /dev/cu.usbmodemXXXX
#
# Needs esptool (pip install esptool). Hold BOOT while plugging in if the
# board doesn't enter download mode by itself.
set -e
[ -n "$1" ] || { echo "usage: $0 <serial port>"; exit 1; }
cd "$(dirname "$0")"
python3 -m esptool --chip esp32s3 --port "$1" --baud 460800 \\
    --before default_reset --after hard_reset \\
    write_flash -z %s \\
    0x%x bootloader.bin \\
    0x%x partitions.bin \\
    0x%x boot_app0.bin \\
    0x%x %s
""" % (ESPTOOL_FLASH_ARGS, BOOTLOADER_OFFSET, PARTITION_TABLE_OFFSET, ota_offset, app_offset, app_name)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--dev", action="store_true", help="allow an untagged or uncommitted tree")
    ap.add_argument("--no-build", action="store_true", help="package the existing build output")
    args = ap.parse_args()

    describe = git("describe", "--tags", "--always", "--dirty")
    tag = git_ok("describe", "--tags", "--exact-match", "HEAD")
    dirty = describe.endswith("-dirty")
    if not args.dev and (tag is None or dirty):
        sys.exit(
            "A release is built from a clean tree at a tag (HEAD is %s).\n"
            "Commit, then `git tag vX.Y.Z`, or pass --dev for a trial run." % describe
        )
    version = describe if (args.dev or tag is None) else tag
    out_name = ("dev-" + describe) if args.dev else version
    out = os.path.join(ROOT, "releases", out_name)
    if os.path.exists(out):
        if not args.dev:
            sys.exit("%s already exists -- a released version is never rebuilt." % os.path.relpath(out, ROOT))
        shutil.rmtree(out)

    if not args.no_build:
        cmd = ["pio", "run"]
        for env in VARIANTS.values():
            cmd += ["-e", env]
        subprocess.run(cmd, cwd=ROOT, check=True)

    layout, rows = layout_at(None)
    ota_offset, app_offset = offsets(rows)
    with open(os.path.join(ROOT, CONFIG_STORE)) as f:
        cfg_version = config_version(f.read())

    # The release this one replaces: the nearest tag before HEAD.
    prev = git_ok("describe", "--tags", "--abbrev=0", "HEAD^" if tag else "HEAD")
    prev_layout, prev_rows = layout_at(prev) if prev else (None, None)
    prev_cfg = config_version(git_ok("show", "%s:%s" % (prev, CONFIG_STORE)) or "") if prev else None
    prev_manifest = None
    if prev:
        path = os.path.join(ROOT, "releases", prev, "manifest.json")
        if os.path.exists(path):
            with open(path) as f:
                prev_manifest = json.load(f)

    manifest = {
        "version": version,
        "commit": git("rev-parse", "HEAD"),
        "dev": bool(args.dev),
        "built": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
        "partition_layout": "0x%08x" % layout,
        "config_version": cfg_version,
        "previous": prev,
        "needs_usb": False,
        "notes": [],
        "variants": {},
    }

    if prev_layout is not None and prev_layout != layout:
        manifest["needs_usb"] = True
        changed = sorted(
            {r[0] for r in rows}.symmetric_difference({r[0] for r in prev_rows})
            | {r[0] for r in rows if r not in prev_rows}
        )
        manifest["notes"].append(
            "Partition table changed since %s (%s): USB only. A dash running firmware "
            "with the layout check refuses it over the air; an older one would take it "
            "and misbehave. Logs are lost if the LittleFS partition moved." % (prev, ", ".join(changed))
        )
    if prev_cfg is not None and cfg_version is not None and prev_cfg != cfg_version:
        manifest["notes"].append(
            "Stored settings layout changed (v%d -> v%d): settings are kept only as far as "
            "ConfigStore::migrate() carries them. A phone-page backup first is cheap." % (prev_cfg, cfg_version)
        )

    for variant, env in VARIANTS.items():
        build = os.path.join(ROOT, ".pio", "build", env)
        firmware = os.path.join(build, "firmware.bin")
        built_variant, built_version = firmware_id(firmware)
        if built_variant != variant or built_version != version[:FW_VERSION_MAX]:
            sys.exit(
                "%s holds %s/%s, not %s/%s -- rebuild (drop --no-build)."
                % (os.path.relpath(firmware, ROOT), built_variant, built_version, variant, version)
            )

        dest = os.path.join(out, variant)
        os.makedirs(dest)
        app_name = "mqb-dash-s3-%s-%s.bin" % (variant, version)
        files = {
            app_name: firmware,
            "bootloader.bin": os.path.join(build, "bootloader.bin"),
            "partitions.bin": os.path.join(build, "partitions.bin"),
            "boot_app0.bin": BOOT_APP0,
        }
        for name, src in files.items():
            shutil.copyfile(src, os.path.join(dest, name))
        script = os.path.join(dest, "flash-usb.sh")
        with open(script, "w") as f:
            f.write(flash_script(app_name, ota_offset, app_offset))
        os.chmod(script, 0o755)

        info = {
            "env": env,
            "ota": app_name,
            "files": {
                name: {"size": os.path.getsize(os.path.join(dest, name)), "sha256": sha256(os.path.join(dest, name))}
                for name in files
            },
            "usb_offsets": {
                "bootloader.bin": "0x%x" % BOOTLOADER_OFFSET,
                "partitions.bin": "0x%x" % PARTITION_TABLE_OFFSET,
                "boot_app0.bin": "0x%x" % ota_offset,
                app_name: "0x%x" % app_offset,
            },
        }
        manifest["variants"][variant] = info

        old = (prev_manifest or {}).get("variants", {}).get(variant)
        if old and old["files"]["bootloader.bin"]["sha256"] != info["files"]["bootloader.bin"]["sha256"]:
            manifest["notes"].append(
                "%s: the bootloader changed since %s. OTA never replaces it; the new app runs "
                "on the old one, and a USB install brings it up to date." % (variant, prev)
            )

    with open(os.path.join(out, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")

    lines = ["# Dash firmware %s" % version, ""]
    lines.append(
        "**Install over USB only** (see notes)."
        if manifest["needs_usb"]
        else "Installs over the air: Settings → Wi-Fi on the dash, then the **Update** page on the phone."
    )
    lines += [
        "",
        "| Hardware | %s | SHA-256 |" % ("Firmware" if manifest["needs_usb"] else "Send from the phone"),
        "| --- | --- | --- |",
    ]
    for variant, info in manifest["variants"].items():
        lines.append("| %s | `%s` | `%s` |" % (variant, info["ota"], info["files"][info["ota"]]["sha256"][:16]))
    lines += [
        "",
        "%s: `<variant>/flash-usb.sh <port>`. It keeps the dash's settings."
        % ("Install" if manifest["needs_usb"] else "Over USB instead"),
    ]
    if manifest["notes"]:
        lines += ["", "## Notes", ""] + ["- " + n for n in manifest["notes"]]
    lines += ["", "Commit `%s`%s." % (manifest["commit"][:12], ", replacing %s" % prev if prev else "")]
    with open(os.path.join(out, "RELEASE.md"), "w") as f:
        f.write("\n".join(lines) + "\n")

    print("\n".join(lines))
    print("\n-> %s" % os.path.relpath(out, ROOT))


if __name__ == "__main__":
    main()
