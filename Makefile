CFLAGS_uniwill-laptop.o := -DDEBUG
obj-m += uniwill-laptop.o

all:
	make -C /lib/modules/`uname -r`/build M=`pwd` modules

clean:
	make -C /lib/modules/`uname -r`/build M=`pwd` clean
