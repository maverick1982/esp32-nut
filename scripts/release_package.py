"""Release packages and web installer manifests for every board image (US-102).

BOARDS is the single list of release images: one entry per PlatformIO
environment, whose name is also the board folder on GitHub Pages
(vX.Y.Z/<env>/) and the suffix of the P4 zips. The ESP32-S3 keeps the zip
name and the manifest of the releases before the P4 (esp32-nut-vX.Y.Z.zip).

Usage:
  python scripts/release_package.py package --env esp32-s3-standard --version v1.11.0 \
      --build-dir .pio/build/esp32-s3-standard --out pkg/esp32-s3-standard --zip-dir dist
  python scripts/release_package.py manifests --version v1.11.0 --out site
"""
import argparse
import glob
import json
import os
import shutil
import sys
import zipfile

INSTALLER_URL = "https://maverick1982.github.io/esp32-nut/"
MANIFEST_NAME = "ESP32 NUT Server"

# Flash layout shared by every image: only the bootloader offset depends on the
# chip (0x2000 on the ESP32-P4, pioarduino-build.py)
PARTITIONS_OFFSET = 0x8000
BOOT_APP0_OFFSET = 0xE000
APP_OFFSET = 0x10000

BOARDS = {
    "esp32-s3-standard": {
        "chip_family": "ESP32-S3",
        "esptool_chip": "esp32s3",
        "bootloader_offset": 0x0,
        "zip_suffix": "",
        "label": "ESP32-S3",
        "button": "the main install button",
        "tested": "tested",
    },
    "esp32-p4-eth": {
        "chip_family": "ESP32-P4",
        "esptool_chip": "esp32p4",
        "bootloader_offset": 0x2000,
        "zip_suffix": "-esp32-p4-eth",
        "label": "ESP32-P4 v3.1 or later (Ethernet)",
        "button": "the \"ESP32-P4 v3.1 or later\" button",
        "tested": "not yet tested on hardware",
    },
    "esp32-p4-eth-v1": {
        "chip_family": "ESP32-P4",
        "esptool_chip": "esp32p4",
        "bootloader_offset": 0x2000,
        "zip_suffix": "-esp32-p4-eth-v1",
        "label": "ESP32-P4 before v3 (v1.x, Ethernet)",
        "button": "the \"ESP32-P4 before v3 (v1.x)\" button",
        "tested": "tested on a v1.3 board",
    },
}

# Copied from the PlatformIO build directory; boot_app0.bin comes from the
# Arduino core (empty otadata at 0xe000, see release.yml)
BUILD_FILES = ["firmware.bin", "bootloader.bin", "partitions.bin", "firmware.factory.bin"]


def manifest(env, version, prefix=""):
    board = BOARDS[env]
    return {
        "name": MANIFEST_NAME,
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [
            {
                "chipFamily": board["chip_family"],
                "parts": [
                    {"path": prefix + "bootloader.bin", "offset": board["bootloader_offset"]},
                    {"path": prefix + "partitions.bin", "offset": PARTITIONS_OFFSET},
                    {"path": prefix + "boot_app0.bin", "offset": BOOT_APP0_OFFSET},
                    {"path": prefix + "firmware.bin", "offset": APP_OFFSET},
                ],
            }
        ],
    }


def readme(env, version):
    board = BOARDS[env]
    return "\n".join([
        "ESP32-NUT %s for %s" % (version, board["label"]),
        "Hardware test status: %s." % board["tested"],
        "",
        "Files",
        "  firmware.factory.bin  complete image (bootloader, partitions, firmware): write it at 0x0",
        "  firmware.bin          application only, for OTA updates",
        "  bootloader.bin        0x%x" % board["bootloader_offset"],
        "  partitions.bin        0x%x" % PARTITIONS_OFFSET,
        "  boot_app0.bin         0x%x" % BOOT_APP0_OFFSET,
        "  manifest.json         ESP Web Tools manifest of this image",
        "",
        "Install from the browser (Chrome, Edge, Opera)",
        "  Open %s and use %s." % (INSTALLER_URL, board["button"]),
        "",
        "Install with esptool",
        "  esptool --chip %s write-flash 0x0 firmware.factory.bin" % board["esptool_chip"],
        "  (esptool v4: esptool.py --chip %s write_flash 0x0 firmware.factory.bin)" % board["esptool_chip"],
        "",
        "Update over the air",
        "  Web UI > Firmware OTA: upload this zip or firmware.bin.",
        "  An image for another chip or another chip revision is refused and the",
        "  board keeps the firmware it is running.",
        "",
    ])


def write_json(path, data):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, indent=2)
        f.write("\n")


def find_boot_app0():
    pattern = os.path.join(os.path.expanduser("~"), ".platformio", "packages", "*",
                           "tools", "partitions", "boot_app0.bin")
    found = sorted(glob.glob(pattern))
    return found[0] if found else None


def package(args):
    if args.env not in BOARDS:
        sys.exit("release_package.py: unknown environment %s" % args.env)
    # The version is part of the zip file name
    if "/" in args.version or "\\" in args.version:
        sys.exit("release_package.py: the version %s contains a path separator" % args.version)
    boot_app0 = args.boot_app0 or find_boot_app0()
    if not boot_app0 or not os.path.isfile(boot_app0):
        sys.exit("release_package.py: boot_app0.bin not found")
    missing = [f for f in BUILD_FILES if not os.path.isfile(os.path.join(args.build_dir, f))]
    if missing:
        sys.exit("release_package.py: missing in %s: %s" % (args.build_dir, ", ".join(missing)))

    os.makedirs(args.out, exist_ok=True)
    for name in BUILD_FILES:
        shutil.copyfile(os.path.join(args.build_dir, name), os.path.join(args.out, name))
    shutil.copyfile(boot_app0, os.path.join(args.out, "boot_app0.bin"))
    write_json(os.path.join(args.out, "manifest.json"), manifest(args.env, args.version))
    with open(os.path.join(args.out, "README.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write(readme(args.env, args.version))

    os.makedirs(args.zip_dir, exist_ok=True)
    zip_path = os.path.join(args.zip_dir, "esp32-nut-%s%s.zip" % (args.version, BOARDS[args.env]["zip_suffix"]))
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for name in sorted(os.listdir(args.out)):
            z.write(os.path.join(args.out, name), name)
    print("release_package.py: %s" % zip_path)


def manifests(args):
    # Root of the web installer: manifest.json stays ESP32-S3 only with the
    # binaries next to it, as before the P4; the P4 manifests point into the
    # board folders of this version
    os.makedirs(args.out, exist_ok=True)
    for env in BOARDS:
        if BOARDS[env]["chip_family"] == "ESP32-S3":
            write_json(os.path.join(args.out, "manifest.json"), manifest(env, args.version))
        else:
            write_json(os.path.join(args.out, "manifest-%s.json" % env),
                       manifest(env, args.version, "%s/%s/" % (args.version, env)))


def main():
    parser = argparse.ArgumentParser(description="ESP32-NUT release packages and installer manifests")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("package", help="package one board image: files, manifest, README and zip")
    p.add_argument("--env", required=True, choices=list(BOARDS))
    p.add_argument("--version", required=True)
    p.add_argument("--build-dir", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--zip-dir", required=True)
    p.add_argument("--boot-app0", help="path of boot_app0.bin (default: search ~/.platformio/packages)")
    p.set_defaults(func=package)

    m = sub.add_parser("manifests", help="write the web installer manifests of every board")
    m.add_argument("--version", required=True)
    m.add_argument("--out", required=True)
    m.set_defaults(func=manifests)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
