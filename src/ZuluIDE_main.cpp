// Simple wrapper file that diverts boot from main program to bootloader
// when building the bootloader image by build_bootloader.py.

#ifdef ZULUIDE_BOOTLOADER_MAIN

extern "C" int bootloader_main(void);

#ifdef USE_ARDUINO
extern "C" void initVariant(void)
{
    bootloader_main();
}

extern "C" void setup(void)
{
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
void zuluide_init(void);
void zuluide_setup(void);
void zuluide_main_loop(void);
void zuluide_setup1(void);
void zuluide_main_loop1(void);

#ifdef USE_ARDUINO
extern "C" 
{
    void initVariant(void)
    {
        zuluide_init();
    }
    void setup(void)
    {
        zuluide_setup();
    }

    void loop(void)
    {
        zuluide_main_loop();
    }

    void setup1(void)
    {
        zuluide_setup1();
    }

    void loop1(void)
    {
        zuluide_main_loop1();
    }
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
