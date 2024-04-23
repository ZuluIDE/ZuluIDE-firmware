ZuluIDE™ Firmware
=================

ZuluIDE™ is a hardware interface between IDE bus and SD cards.
Currently it supports emulating ATAPI CD-ROM drives, Zip Drive 100, and a generic removable drive by providing access to image files stored on SD card.

Drive Types
-----------
There are three ways to specify drive type
1) In the zuluide.ini file under `[IDE]` set `Device = "[Type]"`
    - `CDROM` - CD-ROM drive
    - `Zip100` - Iomega Zip Drive 100
    - `Removable` - Generic removable device
2) Use a image filename prefix of:
    - cdrm - for a CD-ROM drive
    - zipd - for a Zip Drive 100
    - remv - for a generic removable file
    - hddr - for a hard drive images
3) If no prefix or `Device = [type]` used the drive will default to CD-ROM

Image files
-----------
- Currently `.iso` image files, as well as `.bin/.cue` files, are supported for the CD-ROM drive. The images are  used alphabetically. 
- For Zip drives and removable drives the extension is optional but also any extension is valid, except for `.iso`, `.bin/.cue`, and any extension on the [ignored list](#ignored-list). The images are used in alphabetic order.
- If a prefix to specify the drive is used, all other files that wish to be inserted and ejected into the drive must have the same prefix. The files are used alphabetically.
 - If ZuluIDE has defaulted to a CD-ROM drive, the first image that it finds on the SD card will be used a CD. 

Any file on the [ignored list](#ignored-list) will not be used as an device image.

Cycling Images
--------------
Currently the only way to cycle to the next image for a removable device is the eject the medium via the operating system. This will load the next valid file in alphabetic order.

Log files and error indications
-------------------------------
Log messages are stored in `zululog.txt`, which is cleared on every boot.
Normally only basic initialization information is stored, but switching the `DEBUG` DIP switch on will cause every IDE command to be logged, once the board is power cycled.

The indicator LED will normally report disk access.
It also reports following status conditions:

- 1 fast blink on boot: Image file loaded successfully
- 3 fast blinks: No images found on SD card
- 5 fast blinks: SD card not detected
- Continuous morse pattern: firmware crashed, morse code indicates crash location

In crashes the firmware will also attempt to save information into `zuluerr.txt`.

Hotplugging
-----------
The firmware supports hot-plug removal and reinsertion of SD card.
The status led will blink continuously when card is not present, then blink once when card is reinserted successfully.

Programming & bootloader
------------------------
For RP2040-based boards, the USB programming uses `.uf2` format file that can be copied to the virtual USB drive that shows up in USB bootloader mode, which is enabled with DIP switch.

- There is a custom SD-card bootloader that loads new firmware from SD card on boot.
- The firmware file name must be e.g. `ZuluIDE.bin` or `ZuluIDEv1_0_2023-xxxxx.bin`.
- Firmware update takes about 1 second, during which the LED will flash rapidly.
- When successful, the bootloader removes the update file and continues to main firmware.

DIP switches
------------
There are 3 DIP switches on board:

1. **CABLE SEL**: Enable cable selection of IDE drive id
2. **PRI/SEC**: When CABLE SEL is off, this DIP switch determins IDE drive id. Set to OFF for primary drive and ON for secondary.
3. **DEBUG**: Enable debug log messages

In addition to the DIP switches, there is a momentary-contact button,
labeled BOOTLDR. If the bootloader button is held down at power-on,
the board will not load its firmware, and will instead boot the in-ROM
bootloader, and will wait for a .uf2 file to be copied to it via USB.

In hardware revisions before PCB Rev 2023e there is a fourth DIP
switch for BOOTLDR instead of a button.

Project structure
-----------------
- **src/ZuluIDE.cpp**: Main program, initialization.
- **src/ZuluIDE_log.cpp**: Simple logging functionality, uses memory buffering.
- **src/ZuluIDE_config.h**: Some compile-time options, usually no need to change.
- **src/ide_xxxx.cpp**: High-level implementation of IDE and ATAPI protocols.
- **lib/ZuluIDE_platform_RP2040**: Platform-specific code for RP2040. Includes low level IDE bus access code.
- **lib/minIni**: Ini config file access library

Building
--------
This codebase uses [PlatformIO](https://platformio.org/).
To build run the command:

    pio run

Debugging with picoprobe
------------------------
There are helper scripts in `utils` folder for debugging the code using [picoprobe](https://github.com/raspberrypi/picoprobe):

* `utils/picoprobe_log.py`: Read picoprobe serial port at 1 Mbps baudrate to show the log from ZuluIDE.
* `utils/upload_picoprobe.sh`: Upload firmware through SWD (requires openocd >= 0.12 installed on system)
* `utils/run_gdb_picoprobe.sh`: Start log reader, openocd and command line gdb for debugging.

In gdb, use `load` to upload the firmware and `run` to restart from beginning.

Theoretically you can also use the platformio `pio debug` command, but currently it doesn't seem to work with picoprobe.

Origins and License
-------------------

This firmware is original work developed by Rabbit Hole Computing™.
It is closely related to [ZuluSCSI](https://github.com/ZuluSCSI/ZuluSCSI-firmware), but starts from fresh beginnings.

The firmware code is licensed under the GPL version 3 or any later version.

The RP2040 platform utilizes a separate ICE5LP1K FPGA for IDE bus communication.
Bitstream for the FPGA is provided in binary format and is licensed for free use and distribution on hardware produced by Rabbit Hole Computing™.
Communication between the FPGA and the CPU uses QSPI interface documented in [rp2040_fpga.h](lib/ZuluIDE_platform_RP2040/rp2040_fpga.h).

Ignored List
------------
Any file that starts with `zulu` is ignored

The following common compression extensions are ignored:
 ".tar", ".tgz", ".gz", ".bz2", ".tbz2", ".xz", ".zst", ".z", ".zip", ".zipx", ".rar", ".lzh", ".lha", ".lzo", ".lz4", ".arj", ".dmg", ".hqx", ".cpt", ".7z", ".s7z"

The following document extensions are ignored:
".cue", ".txt", ".rtf", ".md", ".nfo", ".pdf", ".doc"
