// Simple wrapper file that diverts boot from main program to bootloader
// when building the bootloader image by build_bootloader.py.

#ifdef ZULUIDE_BOOTLOADER_MAIN

extern "C" int bootloader_main(void);

#ifdef USE_ARDUINO
extern "C" void setup(void)
{
    bootloader_main();
}
extern "C" void loop(void)
{
}
#else
int main(void)
{
    return bootloader_main();
}
#endif

#else

void zuluide_setup(void);
void zuluide_main_loop(void);

#ifdef USE_ARDUINO
extern "C" void setup(void)
{
    zuluide_setup();
}

extern "C" void loop(void)
{
    zuluide_main_loop();
}
#else
int main(void)
{
    zuluide_setup();
    while (1)
    {
        zuluide_main_loop();
    }
}
#endif

#endif
