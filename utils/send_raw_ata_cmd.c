// Small utility for Linux to send raw ATA commands using HDIO_DRIVE_CMD ioctl.
// Refer to https://www.kernel.org/doc/Documentation/ioctl/hdio.txt
// Build with: gcc -Wall -o send_raw_ata_cmd send_raw_ata_cmd.c

#define _GNU_SOURCE
#define __USE_GNU 1
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <linux/hdreg.h>

int main(int argc, const char **argv)
{
    if (argc < 5)
    {
        fprintf(stderr, "Usage: %s /dev/sr0 command nsector feature\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDONLY | O_DIRECT);
    if (fd < 0)
    {
        perror(argv[1]);
        return 2;
    }

    uint8_t buffer[1024];
    buffer[0] = strtoul(argv[2], NULL, 0);
    buffer[1] = strtoul(argv[3], NULL, 0);
    buffer[2] = strtoul(argv[4], NULL, 0);
    
    printf("CMD: 0x%02x, NSECTOR: 0x%02x, FEATURE: 0x%02x\n",
        buffer[0], buffer[1], buffer[2]);

    int status = ioctl(fd, HDIO_DRIVE_CMD, buffer);

    if (status < 0)
    {
        perror("ioctl");
        return 3;
    }
    else
    {
        printf("Success, IDE STATUS: 0x%02x, ERROR: 0x%02x, NSECTOR: 0x%02x\n", buffer[0], buffer[1], buffer[2]);
        return 0;
    }
}
