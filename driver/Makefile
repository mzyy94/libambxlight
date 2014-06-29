TARGET := driver.ko
obj-m := driver.o

driver-objs := ambxlight.o proc_light_color.o

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
