#########################################################
#             Makefile for MOS7703   LINUX 2.6          #      
#########################################################

KDIR:=/lib/modules/$(shell uname -r)/build

EXTRA_CFLAGS += -I$(KDIR)/drivers/usb/serial


obj-m:=mos7703.o

default:
	$(MAKE) -C $(KDIR) $(EXTRA_CFLAGS) SUBDIRS=$(PWD) modules
clean:
	$(RM) *.mod.c *.o *.ko .*.cmd 
load:
	-modprobe usbserial
	insmod mos7703.ko
unload:
	rmmod mos7703

install:
	sh mosinstall 

uninstall:
	sh mosuninstall




