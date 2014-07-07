TARGET := ambxlight.ko
obj-m := ambxlight.o

ROOTDIR  := /lib/modules/`uname -r`/build
PWD   := $(shell pwd)

all:
		$(MAKE) -C $(ROOTDIR) M=$(PWD) modules

clean:
		$(MAKE) -C $(ROOTDIR) M=$(PWD) clean

install:
		cp $(TARGET) /lib/modules/`uname -r`/kernel/drivers/usb/misc
		/sbin/depmod -a `uname -r`
		cp $(PWD)/udev/90-ambxlight.rules /etc/udev/rules.d/
		service udev restart
