# Adds a platformio/Scons target to build the bootloader image.
# It is basically a copy of the main firmware but using ZuluIDE_bootloader.cpp
# as the main() function.

import os
Import("env")

# Build a version of ZuluIDE_main.cpp that calls bootloader instead
env2 = env.Clone()
env2.Append(CPPFLAGS = "-DZULUIDE_BOOTLOADER_MAIN")
bootloader_main = env2.Object(
    os.path.join("$BUILD_DIR", "bootloader_main.o"),
    ["ZuluIDE_main.cpp"]
)

# Include all other dependencies except ZuluIDE_main.cpp and ZuluIDE.cpp
dep_objs = []
for nodelist in env["PIOBUILDFILES"]:
    for node in nodelist:
        filename = str(node.rfile())
        if ('ZuluIDE_main'  not in filename and
            'ZuluIDE.'      not in filename and
            'ide_'          not in filename and
            'ZuluIDE_msc.'  not in filename):
            dep_objs.append(node)
# print("Bootloader dependencies: ", type(dep_objs), str([str(f.rfile()) for f in dep_objs]))

# Use different linker script for bootloader
if env2.GetProjectOption("ldscript_bootloader"):
    env2.Replace(LDSCRIPT_PATH = env2.GetProjectOption("ldscript_bootloader"))
    env2['LINKFLAGS'] = [a for a in env2['LINKFLAGS'] if not a.startswith('-T') and not a.endswith('.ld')]
    env2.Append(LINKFLAGS="-T" + env2.GetProjectOption("ldscript_bootloader"))

# Build bootloader.elf
bootloader_elf = env2.Program(
    os.path.join("$BUILD_DIR", "bootloader.elf"),
    [bootloader_main] + dep_objs
)

# Strip bootloader symbols so that it can be combined with main program
bootloader_bin = env.ElfToBin(
    os.path.join("$BUILD_DIR", "bootloader.bin"),
    bootloader_elf
)

# Convert back to .o to facilitate linking
def escape_cpp(path):
    '''Apply double-escaping for path name to include in generated C++ file'''
    result = ''
    for c in str(path).encode('utf-8'):
        if 32 <= c <= 127 and c not in (ord('\\'), ord('"')):
            result += chr(c)
        else:
            result += '\\\\%03o' % c
    return result

temp_src = env.subst(os.path.join("$BUILD_DIR", "bootloader_bin.cpp"))
open(temp_src, 'w').write(
'''
__asm__(
"   .section .text.btldr\\n"
"btldr:\\n"
"   .incbin \\"%s\\"\\n"
);
''' % escape_cpp(bootloader_bin[0])
)

bootloader_obj = env.Object(
    os.path.join("$BUILD_DIR", "bootloader_bin.o"),
    temp_src
)

# Add bootloader binary as dependency to the main firmware
main_fw = os.path.join("$BUILD_DIR", env.subst("$PROGNAME$PROGSUFFIX"))
env.Depends(bootloader_obj, bootloader_bin)
env.Depends(main_fw, bootloader_obj)
env.Append(LINKFLAGS = [bootloader_obj])
