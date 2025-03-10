# ZuluIDE Hardware Support Library Exception

This *ZuluIDE Hardware Support Library Exception* is an additional permission under section 7 of the GNU General Public License, version 3.
It applies to the complete source code and compiled object files of the ZuluIDE project.

Some hardware platforms targeted by ZuluIDE use a proprietary Hardware Support Library for low level hardware control.
The purpose of this Exception is to allow distribution of compiled object files that combine the ZuluIDE project with these proprietary libraries.

## Definitions

"ZuluIDE" means a version of the ZuluIDE drive emulator firmware, with or without modifications, governed by version 3 of the GNU GPL, or (at the licensee’s option) any later version published by the Free Software Foundation.

"Hardware Support Library" refers to any library distributed by Rabbit Hole Computing that is required for proper functionality of the hardware platform and is limited to following functionalities:

* Initialization of the hardware
* Verification of code signatures and license keys
* Communication with IDE hardware bus

## Grant of Additional Permission

You have permission to propagate the derivative work formed by combining the ZuluIDE firmware with a Hardware Support Library, without applying the GPLv3’s copyleft requirements to the Hardware Support Library itself. However, all other parts of the work remain governed by the GPLv3’s copyleft provisions.
