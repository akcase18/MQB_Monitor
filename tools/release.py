#!/usr/bin/env python3
"""Builds a firmware release into releases/<version>/ for the release
hardware -- the ESP32-S3 dash PCB (env:dash_s3), and the Teensy dash: the
Teensy MicroMod (env:dash_teensy) and its ESP32-C3 Wi-Fi relay
(env:wifi_c3). Out come the S3 dash's OTA file and everything needed to
install it over USB instead, the Teensy's .hex, the C3's USB install, and
a manifest saying what it is and whether the S3 dash can take it over the
air at all. Every other env (the S3 dev kit, stress builds, the
simulator) is never released.

The Teensy and the C3 go out together because there is no protocol
version between them (platform/esp32/README.md): both have to come from
the same tree.

    git tag v1.2.0 && python3 tools/release.py
    python3 tools/release.py --dev      # untagged or uncommitted: a trial run

A real release has to be built from a clean tree at a tag, so the version
the dashes show (tools/fw_version.py stamps `git describe`) is that tag.
--dev drops the requirement and files the build under releases/dev-<describe>/.

What comes out:

    dash_s3/mqb-dash-s3-<version>.bin           send this from the phone
    dash_s3/bootloader.bin, partitions.bin, boot_app0.bin
    dash_s3/flash-usb.sh                        USB install, same files
    dash_teensy/mqb-dash-teensy-<version>.hex   open in Teensy Loader
    wifi_c3/mqb-wifi-c3-<version>.bin           the Teensy dash's C3 relay
    wifi_c3/bootloader.bin, partitions.bin,     boot_app0.bin
    wifi_c3/flash-usb.sh                        USB install, the only way in

plus manifest.json (sizes, SHA-256s, partition layout, config version) and
RELEASE.md, a summary to paste into the GitHub release.

The USB installs write each file at its own offset, as `pio run -t upload`
does, rather than one merged image: esptool's merge_bin pads the gaps with
0xFF, and on the S3 dash one of those gaps is the NVS partition holding its
settings. (The C3 keeps nothing, but is installed the same way.)

Compared with the previous tag, the dash's build is marked as needing USB
when its partition table differs (OTA only ever writes an app slot -- the dash
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

# The only release hardware. The dev kit and the stress builds are never
# distributed (the dash refuses a stress build anyway --
# DASH_S3_BUILD_VARIANT).t
DASH_ENV = "dash_s3"
DASH_HW_VARIANT = "pcb"  # dash_s3_firmware.hpp's DASH_S3_HW_VARIANT default
TEENSY_ENV = "dash_teensy"
C3_ENV = "wifi_c3"

# What `pio run -e dash_s3 -t upload` passes esptool (Arduino turns the
# board's qio into dio for the bootloader header).
ESPTOOL_FLASH_ARGS = "--flash_mode dio --flash_freq 80m --flash_size 16MB"
BOOTLOADER_OFFSET = 0x0
PARTITION_TABLE_OFFSET = 0x8000

# The same for `pio run -e wifi_c3 -t upload`: the XIAO's 4MB flash and the
# Arduino default partition table, so the offsets are fixed.
C3_ESPTOOL_FLASH_ARGS = "--flash_mode dio --flash_freq 80m --flash_size 4MB"
C3_OTADATA_OFFSET = 0xE000
C3_APP_OFFSET = 0x10000
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


def stamped_version(build, image):
    """FW_VERSION a Teensy or C3 image was built with, or None. Neither
    carries an update descriptor to read it back from, so this goes by
    the header fw_version.py generated. It is only rewritten when the
    version changes, so a header newer than the image means the build
    that stamped it failed."""
    header = os.path.join(build, "fw_gen", "fw_version.h")
    image_path = os.path.join(build, image)
    if not (os.path.exists(header) and os.path.exists(image_path)):
        return None
    if os.path.getmtime(header) > os.path.getmtime(image_path):
        return None
    with open(header) as f:
        m = re.search(r'#define FW_VERSION "([^"]*)"', f.read())
    return m.group(1) if m else None


def offsets(rows):
    """(otadata offset, first app slot offset) from the partition rows."""
    ota = next(o for _, t, st, o, _, _ in rows if t == "data" and st == "ota")
    app = min(o for _, t, _, o, _, _ in rows if t == "app")
    return ota, app


DASH_FLASH_INTRO = """\
# USB install of this dash firmware. Keeps the dash's settings (NVS) and,
# unless the partition table changed, its logs (LittleFS): each file goes
# to its own offset and nothing else is touched."""

C3_FLASH_INTRO = """\
# USB install of the Teensy dash's C3 Wi-Fi relay. Flash the Teensy from
# the same release: the two share no protocol version. The C3 keeps no
# settings of its own (the Teensy holds them)."""


def flash_script(chip, flash_args, intro, app_name, ota_offset, app_offset):
    return """#!/bin/sh
%s
#
#   ./flash-usb.sh /dev/cu.usbmodemXXXX
#
# Needs esptool (pip install esptool). Hold BOOT while plugging in if the
# board doesn't enter download mode by itself.
set -e
[ -n "$1" ] || { echo "usage: $0 <serial port>"; exit 1; }
cd "$(dirname "$0")"
python3 -m esptool --chip %s --port "$1" --baud 460800 \\
    --before default_reset --after hard_reset \\
    write_flash -z %s \\
    0x%x bootloader.bin \\
    0x%x partitions.bin \\
    0x%x boot_app0.bin \\
    0x%x %s
""" % (intro, chip, flash_args, BOOTLOADER_OFFSET, PARTITION_TABLE_OFFSET, ota_offset, app_offset, app_name)


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
        subprocess.run(["pio", "run", "-e", DASH_ENV, "-e", TEENSY_ENV, "-e", C3_ENV], cwd=ROOT, check=True)

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
        "builds": {},
    }

    if prev_layout is not None and prev_layout != layout:
        manifest["needs_usb"] = True
        changed = sorted(
            {r[0] for r in rows}.symmetric_difference({r[0] for r in prev_rows})
            | {r[0] for r in rows if r not in prev_rows}
        )
        manifest["notes"].append(
            "Dash partition table changed since %s (%s): USB only. A dash running firmware "
            "with the layout check refuses it over the air; an older one would take it "
            "and misbehave. Logs are lost if the LittleFS partition moved." % (prev, ", ".join(changed))
        )
    if prev_cfg is not None and cfg_version is not None and prev_cfg != cfg_version:
        manifest["notes"].append(
            "Stored settings layout changed (v%d -> v%d): settings are kept only as far as "
            "ConfigStore::migrate() carries them. A phone-page backup first is cheap." % (prev_cfg, cfg_version)
        )

    # Check every build before writing anything, so a stale one never
    # leaves a half-packaged release behind.
    build = os.path.join(ROOT, ".pio", "build", DASH_ENV)
    firmware = os.path.join(build, "firmware.bin")
    built_variant, built_version = firmware_id(firmware)
    if built_variant != DASH_HW_VARIANT or built_version != version[:FW_VERSION_MAX]:
        sys.exit(
            "%s holds %s/%s, not %s/%s -- rebuild (drop --no-build)."
            % (os.path.relpath(firmware, ROOT), built_variant, built_version, DASH_HW_VARIANT, version)
        )
    teensy_build = os.path.join(ROOT, ".pio", "build", TEENSY_ENV)
    c3_build = os.path.join(ROOT, ".pio", "build", C3_ENV)
    for stamped_build, image in ((teensy_build, "firmware.hex"), (c3_build, "firmware.bin")):
        built_version = stamped_version(stamped_build, image)
        if built_version != version[:FW_VERSION_MAX]:
            sys.exit(
                "%s was built as %s, not %s -- rebuild (drop --no-build)."
                % (os.path.relpath(stamped_build, ROOT), built_version, version)
            )

    # The dash: its OTA file, plus the same release over USB.
    dest = os.path.join(out, DASH_ENV)
    os.makedirs(dest)
    app_name = "mqb-dash-s3-%s.bin" % version
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
        f.write(flash_script("esp32s3", ESPTOOL_FLASH_ARGS, DASH_FLASH_INTRO, app_name, ota_offset, app_offset))
    os.chmod(script, 0o755)

    info = {
        "env": DASH_ENV,
        "install": app_name,
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
    manifest["builds"][DASH_ENV] = info

    old = (prev_manifest or {}).get("builds", {}).get(DASH_ENV)
    if old and old["files"]["bootloader.bin"]["sha256"] != info["files"]["bootloader.bin"]["sha256"]:
        manifest["notes"].append(
            "Dash: the bootloader changed since %s. OTA never replaces it; the new app runs "
            "on the old one, and a USB install brings it up to date." % prev
        )

    # The Teensy: one .hex, installed over USB with PJRC's Teensy Loader.
    dest = os.path.join(out, TEENSY_ENV)
    os.makedirs(dest)
    hex_name = "mqb-dash-teensy-%s.hex" % version
    shutil.copyfile(os.path.join(teensy_build, "firmware.hex"), os.path.join(dest, hex_name))
    manifest["builds"][TEENSY_ENV] = {
        "env": TEENSY_ENV,
        "install": hex_name,
        "files": {
            hex_name: {"size": os.path.getsize(os.path.join(dest, hex_name)), "sha256": sha256(os.path.join(dest, hex_name))}
        },
    }

    # The Teensy's C3 relay: USB only, laid out like the dash's USB install.
    dest = os.path.join(out, C3_ENV)
    os.makedirs(dest)
    c3_app = "mqb-wifi-c3-%s.bin" % version
    files = {
        c3_app: os.path.join(c3_build, "firmware.bin"),
        "bootloader.bin": os.path.join(c3_build, "bootloader.bin"),
        "partitions.bin": os.path.join(c3_build, "partitions.bin"),
        "boot_app0.bin": BOOT_APP0,
    }
    for name, src in files.items():
        shutil.copyfile(src, os.path.join(dest, name))
    script = os.path.join(dest, "flash-usb.sh")
    with open(script, "w") as f:
        f.write(flash_script("wifi_c3", C3_ESPTOOL_FLASH_ARGS, C3_FLASH_INTRO, c3_app, C3_OTADATA_OFFSET, C3_APP_OFFSET))
    os.chmod(script, 0o755)
    manifest["builds"][C3_ENV] = {
        "env": C3_ENV,
        "install": c3_app,
        "files": {
            name: {"size": os.path.getsize(os.path.join(dest, name)), "sha256": sha256(os.path.join(dest, name))}
            for name in files
        },
        "usb_offsets": {
            "bootloader.bin": "0x%x" % BOOTLOADER_OFFSET,
            "partitions.bin": "0x%x" % PARTITION_TABLE_OFFSET,
            "boot_app0.bin": "0x%x" % C3_OTADATA_OFFSET,
            c3_app: "0x%x" % C3_APP_OFFSET,
        },
    }

    with open(os.path.join(out, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")

    lines = ["# Firmware %s" % version, "", "| Hardware | Firmware | SHA-256 |", "| --- | --- | --- |"]
    for name, env in (("ESP32-S3 dash", DASH_ENV), ("Teensy dash: Teensy", TEENSY_ENV), ("Teensy dash: C3 relay", C3_ENV)):
        info = manifest["builds"][env]
        lines.append("| %s | `%s/%s` | `%s` |" % (name, env, info["install"], info["files"][info["install"]]["sha256"][:16]))
    lines.append("")
    if manifest["needs_usb"]:
        lines.append(
            "**ESP32-S3 dash:** install over USB only (see notes): `%s/flash-usb.sh <port>`. "
            "It keeps the dash's settings." % DASH_ENV
        )
    else:
        lines.append(
            "**ESP32-S3 dash:** installs over the air: Settings → Wi-Fi on the dash, then the "
            "**Update** page on the phone. Over USB instead: `%s/flash-usb.sh <port>`. "
            "It keeps the dash's settings." % DASH_ENV
        )
    lines += [
        "",
        "**Teensy dash:** over USB only, and flash both boards from this release (they share no "
        "protocol version). Teensy: open the `.hex` in PJRC's Teensy Loader and press the program "
        "button. C3 relay: `%s/flash-usb.sh <port>`." % C3_ENV,
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
