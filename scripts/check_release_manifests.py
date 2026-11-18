"""Validate the Web Installer manifests generated for a release (US-102).

Checks every board of release_package.BOARDS: exactly one board owns the root
manifest.json (binaries next to it), every other board has manifest-<env>.json
pointing into its <version>/<env>/ folder, with the board's chip family.
"""
import argparse
import json
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from release_package import BOARDS  # noqa: E402


def fail(message):
    sys.exit("check_release_manifests.py: %s" % message)


def load_manifest(out_dir, name):
    path = os.path.join(out_dir, name)
    try:
        with open(path, encoding="utf-8") as manifest_file:
            return json.load(manifest_file)
    except FileNotFoundError:
        fail("missing %s" % path)
    except json.JSONDecodeError as error:
        fail("invalid JSON in %s: %s" % (path, error))


def first_build(manifest, name):
    try:
        build = manifest["builds"][0]
        build["parts"][-1]["path"]
        return build
    except (IndexError, KeyError, TypeError):
        fail("%s has no firmware part" % name)


def main():
    parser = argparse.ArgumentParser(description="validate ESP32-NUT Web Installer manifests")
    parser.add_argument("--out", required=True, help="directory written by release_package.py manifests")
    parser.add_argument("--version", required=True, help="release version used to create the manifests")
    args = parser.parse_args()

    roots = [env for env in BOARDS if BOARDS[env].get("root_manifest")]
    if len(roots) != 1:
        fail("exactly one board needs root_manifest, found %s" % (roots or "none"))

    for env, board in BOARDS.items():
        if board.get("root_manifest"):
            name, expected = "manifest.json", "firmware.bin"
        else:
            name, expected = "manifest-%s.json" % env, "%s/%s/firmware.bin" % (args.version, env)
        build = first_build(load_manifest(args.out, name), name)
        if build.get("chipFamily") != board["chip_family"]:
            fail("%s has chipFamily %r, expected %r" % (name, build.get("chipFamily"), board["chip_family"]))
        if build["parts"][-1]["path"] != expected:
            fail("%s must use %s" % (name, expected))

    print("check_release_manifests.py: %d manifests are valid" % len(BOARDS))


if __name__ == "__main__":
    main()
