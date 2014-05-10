#
# Makefile for mos7703
#

KERNELDIR ?= /lib/modules/$(shell uname -r)/build

ifneq ($(KERNELRELEASE),)
# called from kernel build system:

obj-m := mos7703.o

else

all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

load:
	-modprobe usbserial
	insmod mos7703.ko

unload:
	rmmod mos7703

.PHONY: all clean load unload

endif