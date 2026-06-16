CFLAGS_core.o := -DDEBUG
CFLAGS_dmi.o := -DDEBUG
CFLAGS_acpi-ec.o := -DDEBUG
CFLAGS_wmi-ec.o := -DDEBUG
CFLAGS_wmi-event.o := -DDEBUG
obj-m += uniwill-laptop.o
uniwill-laptop-y := core.o dmi.o acpi-ec.o wmi-ec.o wmi-event.o

all:
	make -C /lib/modules/`uname -r`/build M=`pwd` modules

clean:
	make -C /lib/modules/`uname -r`/build M=`pwd` clean
