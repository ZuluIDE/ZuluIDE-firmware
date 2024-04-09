#!/bin/bash -ex

# ZuluIDE™ - Copyright (c) 2022 Rabbit Hole Computing™
#
# ZuluIDE™ file is licensed under the GPL version 3 or any later version. 
#
# https://www.gnu.org/licenses/gpl-3.0.html
# ----
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version. 
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details. 
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

# This script converts .bin file from Icecube2 design tool
# to a .h file that can be included in code.

# INFILE="fpga/icecube2_ice5lp1k/ZuluIDE/ZuluIDE_Implmnt/sbt/outputs/bitmap/ice5lp1k_top_bitmap.bin"
INFILE=$1
OUTFILE="lib/ZuluIDE_platform_RP2040/fpga_bitstream.h"
DATE=$(date +%Y-%m-%d)

echo "/* ZuluIDE™ - Copyright (c) 2022 Rabbit Hole Computing™ */" > $OUTFILE
echo "/* All rights reserved - distribution and use permitted on hardware produced by Rabbit Hole Computing */" >> $OUTFILE
echo "/* FPGA bitstream, converted from $INFILE on $DATE */" >> $OUTFILE
echo '/* Use utils/convert_fpga_bitstream.sh to regenerate this file */' >> $OUTFILE
echo 'static const uint8_t fpga_bitstream[] = {' >> $OUTFILE
xxd -i < $INFILE >> $OUTFILE
echo '};' >> $OUTFILE

