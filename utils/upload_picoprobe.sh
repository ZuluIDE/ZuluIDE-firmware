#!/bin/bash

DIR="$( dirname -- "${BASH_SOURCE[0]}"; )";
ELF="$DIR/../.pio/build/ZuluIDE_RP2040/firmware.elf"

openocd -f interface/cmsis-dap.cfg -c "adapter speed 5000" -f target/rp2040.cfg \
    -c "init" -c "targets" -c "reset halt" \
    -c "flash write_image erase $ELF" -c "verify_image $ELF" \
    -c "reset run" -c "shutdown"
