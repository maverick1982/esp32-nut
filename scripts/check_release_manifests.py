"""Validate the Web Installer manifests generated for a release (US-102)."""
import argparse
import json
import os
import sys


def load_manifest(out_dir, name):
    path = os.path.join(out_dir, name)
    try:
        with open(path, encoding="utf-8") as manifest_file:
            return json.load(manifest_file)
    except FileNotFoundError:
        sys.exit("check_release_manifests.py: missing %s" % path)
    except json.JSONDecodeError as error:
        sys.exit("check_release_manifests.py: invalid JSON in %s: %s" % (path, error))


def firmware_path(manifest, name):
    try:
        return manifest["builds"][0]["parts"][-1]["path"]
    except (IndexError, KeyError, TypeError):
        sys.exit("check_release_manifests.py: %s has no firmware part" % name)


def main():
    parser = argparse.ArgumentParser(description="validate ESP32-NUT Web Installer manifests")
    parser.add_argument("--out", required=True, help="directory written by release_package.py manifests")
    parser.add_argument("--version", required=True, help="release version used to create the manifests")
    args = parser.parse_args()

    root_name = "manifest.json"
    nano_name = "manifest-arduino-nano-esp32.json"
    root = load_manifest(args.out, root_name)
    nano = load_manifest(args.out, nano_name)

    expected_nano = "%s/arduino-nano-esp32/firmware.bin" % args.version
    if firmware_path(root, root_name) != "firmware.bin":
        sys.exit("check_release_manifests.py: standard ESP32-S3 manifest must use root firmware.bin")
    if firmware_path(nano, nano_name) != expected_nano:
        sys.exit("check_release_manifests.py: Nano manifest must use %s" % expected_nano)

    print("check_release_manifests.py: manifests are valid")


if __name__ == "__main__":
    main()
