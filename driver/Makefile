obj-m := ambxlight.o

ROOTDIR  := /lib/modules/`uname -r`/build
PWD   := $(shell pwd)

all: ambxlight.c proc_light_color.c detect_ambxlight.c
		$(MAKE) -C $(ROOTDIR) M=$(PWD) modules

clean:
		$(MAKE) -C $(ROOTDIR) M=$(PWD) clean

install:
		insmod $(TARGET)

