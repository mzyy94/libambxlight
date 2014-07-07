PROGRAM = libambx.so.1.0
SRCS = libambx.c
OBJS = $(subst .c,.o,$(SRCS))

CC = gcc
CFLAGS = -I../include -Wall -O2

$(PROGRAM): $(OBJS)
	$(CC) -shared -Wl,-soname=libambx.so.1 -o libambx.so.1.0 $(OBJS)

.c.o:
	$(CC) $(CFLAGS) -g -c -fPIC $< 

.PHONY: clean

clean:
	rm $(OBJS) $(PROGRAM)
