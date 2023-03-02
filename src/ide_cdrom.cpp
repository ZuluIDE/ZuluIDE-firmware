#include "ide_cdrom.h"
#include "atapi_constants.h"

IDECDROMDevice::IDECDROMDevice()
{
    m_devinfo.devtype = ATAPI_DEVTYPE_CDROM;
    m_devinfo.removable = true;
    m_devinfo.bytes_per_sector = 2048;
}

