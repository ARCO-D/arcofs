KDIR := /home/kirin7/kernel/linux-6.6.57
CC = aarch64-kirin7-linux-gnu-gcc
ccflags-y := -std=gnu99 -Wno-error
obj-m=arcofs.o
PWD=$(shell pwd)

all:
	make -C $(KDIR) M=$(PWD) modules
	$(CC) mkarcofs.c -o mkarcofs
	md5sum mkarcofs arcofs.ko

clean:
	make -C $(KDIR) M=$(PWD) clean
