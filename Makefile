KDIR := /usr/src/linux-headers-4.15.0-142-generic
CC = gcc
ccflags-y := -std=gnu99 -Wno-error
obj-m=arcofs.o
PWD=$(shell pwd)

all:
	make -C $(KDIR) M=$(PWD) modules
	$(CC) mkarcofs.c -o mkarcofs
	md5sum mkarcofs arcofs.ko
	cp mkarcofs arcofs.ko build

clean:
	make -C $(KDIR) M=$(PWD) clean