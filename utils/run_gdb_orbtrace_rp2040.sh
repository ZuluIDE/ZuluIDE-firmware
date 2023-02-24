#!/bin/bash

#killall orbuculum
killall pyocd
killall nc
sleep 0.2

#orbtrace -T u -a 1000000
#sleep 0.2
pyocd gdbserver --target rp2040_core0 --persist &
#sleep 0.2
#orbuculum &

pidof orbuculum || orbuculum -O "-T u -a 7812500" -v3 > orbuculum.log 2>&1 &

sleep 0.2
nc -v localhost 3443 &

arm-none-eabi-gdb \
       -iex 'target extended :3333' \
       -iex 'set mem inaccessible-by-default off' \
       -iex 'source utils/rp2040_gdb_macros' \
       .pio/build/ZuluIDE_RP2040/firmware.elf

killall pyocd
