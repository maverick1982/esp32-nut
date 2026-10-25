# Writes the chip revision range of the precompiled ESP-IDF libraries into the app
# image header (esptool elf2image --min-rev-full / --max-rev-full).
#
# The ESP32-P4 has two incompatible library sets: silicon v1.x ("esp32p4_es") and
# v3.x ("esp32p4"). PlatformIO leaves the range open (0..65535), so an OTA upload of
# the wrong P4 image would pass the image check and leave a board that no longer
# boots. With the range in the header, esp_ota_end() rejects it and the running
# firmware stays.
#
# Post script: ELF2BINFLAGS is defined by the platform builder, after pre scripts.
import os
import re

Import("env")  # noqa: F821

board = env.BoardConfig()  # noqa: F821
variant = board.get("build.chip_variant", board.get("build.mcu"))
libs_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32-libs")  # noqa: F821
sdkconfig = os.path.join(libs_dir or "", variant, "sdkconfig")

if not os.path.isfile(sdkconfig):
    print("chip_rev.py: %s not found, revision range not set" % sdkconfig)
else:
    with open(sdkconfig, encoding="utf-8") as f:
        text = f.read()
    rev_min = re.search(r"^CONFIG_ESP_REV_MIN_FULL=(\d+)$", text, re.M)
    rev_max = re.search(r"^CONFIG_ESP_REV_MAX_FULL=(\d+)$", text, re.M)
    if rev_min and rev_max:
        env.Append(  # noqa: F821
            ELF2BINFLAGS=["--min-rev-full", rev_min.group(1), "--max-rev-full", rev_max.group(1)]
        )
        print("chip_rev.py: %s image for chip revision %s..%s" % (variant, rev_min.group(1), rev_max.group(1)))
