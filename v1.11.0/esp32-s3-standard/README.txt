ESP32-NUT v1.11.0 for ESP32-S3
Hardware test status: tested.

Files
  firmware.factory.bin  complete image (bootloader, partitions, firmware): write it at 0x0
  firmware.bin          application only, for OTA updates
  bootloader.bin        0x0
  partitions.bin        0x8000
  boot_app0.bin         0xe000
  manifest.json         ESP Web Tools manifest of this image

Install from the browser (Chrome, Edge, Opera)
  Open https://maverick1982.github.io/esp32-nut/ and use the main install button.

Install with esptool
  esptool --chip esp32s3 write-flash 0x0 firmware.factory.bin
  (esptool v4: esptool.py --chip esp32s3 write_flash 0x0 firmware.factory.bin)

Update over the air
  Web UI > Firmware OTA: upload this zip or firmware.bin.
  An image for another chip or another chip revision is refused and the
  board keeps the firmware it is running.
