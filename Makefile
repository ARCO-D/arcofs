KDIR := /home/kirin7/kernel/linux-imx
obj-m=arcofs.o
PWD=$(shell pwd)

all:
	make -C $(KDIR) M=$(PWD) modules
	arm-linux-gnueabihf-gcc mkarcofs.c -o mkarcofs

clean:
	make -C $(KDIR) M=$(PWD) clean