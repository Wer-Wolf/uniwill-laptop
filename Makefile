CFLAGS_uniwill-laptop.o := -DDEBUG
obj-m += uniwill-laptop.o
uniwill-laptop-y := uniwill-acpi.o uniwill-wmi.o

all:
	make -C /lib/modules/`uname -r`/build M=`pwd` modules

clean:
	make -C /lib/modules/`uname -r`/build M=`pwd` clean
