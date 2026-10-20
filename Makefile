CFLAGS_uniwill-acpi.o := -DDEBUG
CFLAGS_uniwill-wmi.o := -DDEBUG
obj-m += uniwill-laptop.o
uniwill-laptop-y := uniwill-acpi.o uniwill-wmi.o

all:
	make -C /lib/modules/`uname -r`/build M=`pwd` modules

install: all
	make -C /lib/modules/`uname -r`/build M=`pwd` modules_install
	depmod -a `uname -r`

clean:
	make -C /lib/modules/`uname -r`/build M=`pwd` clean
