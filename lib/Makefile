PROGRAM = libambxlight.so.1.0
SRCS = libambxlight.c
OBJS = $(subst .c,.o,$(SRCS))

CC = gcc
CFLAGS = -I../include -Wall -O2

$(PROGRAM): $(OBJS)
	$(CC) -shared -Wl,-soname=libambxlight.so.1 -o $(PROGRAM) $(OBJS)

.c.o:
	$(CC) $(CFLAGS) -g -c -fPIC $< 

.PHONY: clean

clean:
	rm $(OBJS) $(PROGRAM)
