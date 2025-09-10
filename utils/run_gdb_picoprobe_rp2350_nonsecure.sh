#!/bin/bash

# This OpenOCD script can be used for debugging the non-secure GPLv3 part of
# the firmware running on ZuluIDE-RP2350.

DIR="$( dirname -- "${BASH_SOURCE[0]}"; )";
ELF="$DIR/../.pio/build/ZuluIDE_V2/firmware.elf"

[ -e $ELF ] || pio run

# Stop background processes when script exits
trap "trap - SIGTERM && kill -- -$$" SIGINT SIGTERM EXIT

# Start openocd and serial cat to background
# For now this needs the openocd fork https://github.com/raspberrypi/openocd
openocd_rp2350 -f interface/cmsis-dap.cfg -c "adapter speed 5000" -f utils/rp2350_openocd_nonsecure.cfg -l openocd.log &
python3 $DIR/picoprobe_log.py &

arm-none-eabi-gdb \
       -iex 'target extended :3333' \
       -iex 'set mem inaccessible-by-default off' \
       -iex 'source utils/rp2040_gdb_macros' \
       $ELF
