#!/bin/bash

DIR="$( dirname -- "${BASH_SOURCE[0]}"; )";
ELF="$DIR/../.pio/build/ZuluIDE_RP2350_core1_debug/firmware.elf"

[ -e $ELF ] || pio run

# Stop background processes when script exits
trap "trap - SIGTERM && kill -- -$$" SIGINT SIGTERM EXIT

# Start openocd and serial cat to background
# For now this needs the openocd fork https://github.com/raspberrypi/openocd
openocd_rp2350 -f interface/cmsis-dap.cfg -c "adapter speed 5000" -f target/rp2350.cfg -l openocd.log &
python3 $DIR/picoprobe_log.py &

arm-none-eabi-gdb \
       -iex 'target extended :3333' \
       -iex 'set mem inaccessible-by-default off' \
       -iex 'source utils/rp2040_gdb_macros' \
       $ELF
