# sonnetd

I heard you want a PowerPC in your PC?

## Building

    make NOLIBCDIR=/path/to/linux/tools/include/nolibc

## Running

    sonnetd -d /dev/sonnet0 -k dtbImage.sonnet.elf [-b disk.img]...
