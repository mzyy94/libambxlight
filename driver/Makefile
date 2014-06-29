TARGET := ambxlight.ko
obj-m := ambxlight.o

ambxlight-objs := core.o proc.o usb-skeleton.o

ROOTDIR  := /lib/modules/`uname -r`/build
PWD   := $(shell pwd)

all:
		$(MAKE) -C $(ROOTDIR) M=$(PWD) modules

clean:
		$(MAKE) -C $(ROOTDIR) M=$(PWD) clean

install:
		insmod $(TARGET)

uninstall:
		rmmod $(TARGET)
