# uniwill-laptop
Linux kernel driver for various Uniwill laptops. The driver itself should be shipped with the mainline kernel
starting with kernel version 6.19. This repository mainly contains experimental features for testers to test.

## Installation
You can build the kernel modules by simply executing `make`. Keep in mind that you need a recent enough linux kernel (>= 6.17.0)
and the linux kernel headers installed.

You can then load the kernel modules by executing `insmod uniwill-laptop.ko` with superuser privileges.

## Development

This driver is based on [qc71_laptop](https://github.com/pobrn/qc71_laptop) and [tuxedo-driver](https://github.com/tuxedocomputers/tuxedo-drivers).
All knowledge was retrieved using reverse engineering, so be careful when testing this driver!
